"""Translate 16-bit x86 real-mode code into C++ for src/rt (one C++ function per original function).

Usage (MTV.EXE):
  python3 tools/recomp/x86recomp.py --exe work/unpacked/MTV.EXE --load-seg 0x824 --functions analysis/function-map.json --out src/gen --prefix mtv

The generated code keeps original stack layout, flags and memory effects. Every instruction increments the
instruction counter; backward branches and calls poll for pending timer events.
"""
import argparse
import json
import re
from pathlib import Path

from capstone import CS_ARCH_X86, CS_MODE_16, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG

R16 = {"ax": "AX", "cx": "CX", "dx": "DX", "bx": "BX", "sp": "SP", "bp": "BP", "si": "SI", "di": "DI"}
R8 = {"al": "AL", "cl": "CL", "dl": "DL", "bl": "BL", "ah": "AH", "ch": "CH", "dh": "DH", "bh": "BH"}
SEGS = {"es": "ES", "cs": "CS", "ss": "SS", "ds": "DS"}
JCC = {
    "je": "c.zf", "jne": "!c.zf", "jb": "c.cf", "jae": "!c.cf", "jbe": "(c.cf || c.zf)", "ja": "(!c.cf && !c.zf)",
    "jl": "(c.sf != c.of)", "jge": "(c.sf == c.of)", "jle": "(c.zf || c.sf != c.of)", "jg": "(!c.zf && c.sf == c.of)",
    "js": "c.sf", "jns": "!c.sf", "jo": "c.of", "jno": "!c.of", "jp": "c.pf", "jnp": "!c.pf",
}


class Unsupported(Exception):
    pass


class RawImage:
    """A position-independent code blob loaded at a paragraph boundary (the Palladix drivers)."""
    def __init__(self, data):
        self.raw = bytearray(data)
        self.load_seg = 0
        self.relocs = set()
        self.entry_cs = self.entry_ip = 0


class Image:
    def __init__(self, exe_path, load_seg):
        d = Path(exe_path).read_bytes()
        hdr = int.from_bytes(d[8:10], "little") * 16
        self.raw = bytearray(d[hdr:])
        self.load_seg = load_seg
        nrel = int.from_bytes(d[6:8], "little")
        roff = int.from_bytes(d[0x18:0x1A], "little")
        self.relocs = set()
        for i in range(nrel):
            o, s = int.from_bytes(d[roff + 4 * i:roff + 4 * i + 2], "little"), int.from_bytes(d[roff + 4 * i + 2:roff + 4 * i + 4], "little")
            self.relocs.add(s * 16 + o)
        self.entry_cs = int.from_bytes(d[0x16:0x18], "little")
        self.entry_ip = int.from_bytes(d[0x14:0x16], "little")

    def reloc_word(self, off):
        """True if the word at image offset `off` is a segment fixup."""
        return off in self.relocs


class Translator:
    def __init__(self, img, prefix):
        self.img = img
        self.prefix = prefix
        self.md = Cs(CS_ARCH_X86, CS_MODE_16)
        self.md.detail = True
        self.functions = {}  # entry imgoff -> cs paragraph (image-relative)
        self.queue = []
        self.unresolved = []
        self.table_counts = {}   # jmp image offset -> entry count (manual overrides)
        self.indirect_words = {}  # jmp image offset -> (cs, memory offset) for `jmp word ptr [X]`
        self.dispatch_unresolved = False
        self.ds = None             # data segment paragraph (image-relative) for near pointer roots
        self.hooks = set()         # function entries that call Machine::hook first
        self.tag_stores = set()    # byte stores whose tag comes from Machine::store_tag
        self.near_ptr_words = set()  # (cs, DS offset) of `call/jmp word ptr [X]`

    def fname(self, off):
        return f"{self.prefix}_{off:05x}"

    def add_function(self, off, cs):
        if off not in self.functions:
            self.functions[off] = cs
            self.queue.append(off)

    def decode(self, off, cs):
        """Decode at image offset `off`; capstone sees the real IP (offset within CS) so branches wrap at 64 KiB."""
        ip = off - cs * 16
        if not 0 <= ip < 0x10000:
            raise Unsupported(f"{off:05x} is outside code segment {cs:04x}")
        it = self.md.disasm(bytes(self.img.raw[off:off + 16]), ip)
        ins = next(it, None)
        if ins is None:
            raise Unsupported(f"cannot decode at {off:05x}")
        return ins

    # ---------------- discovery ----------------
    def explore(self, entry, cs):
        """Recursive descent over one function. Returns dict addr -> insn and extra info."""
        seen, work, ins_at, tables = set(), [entry], {}, {}
        while work:
            a = work.pop()
            while a not in seen:
                if a < 0 or a >= len(self.img.raw):
                    raise Unsupported(f"flow leaves the image at {a:05x}")
                ins = self.decode(a, cs)
                seen.add(a)
                ins_at[a] = ins
                m = ins.mnemonic
                nxt = a + ins.size
                if m in ("ret", "retf", "iret", "hlt"):
                    break
                if m in JCC or m in ("jcxz", "loop", "loope", "loopne"):
                    work.append(self.near_target(ins, cs))
                    a = nxt
                    continue
                if m == "jmp":
                    op = ins.operands[0]
                    if op.type == X86_OP_IMM:
                        a = self.near_target(ins, cs)
                        continue
                    targets = self.jump_table(ins, ins_at, cs)
                    tables[a] = targets
                    work.extend(targets)
                    break
                if m == "ljmp":
                    op = ins.operands
                    if len(op) == 2 and op[0].type == X86_OP_IMM:
                        seg, off = self.far_imm(ins)
                        self.add_function(seg * 16 + off, seg)
                    break
                if m == "call":
                    op = ins.operands[0]
                    if op.type == X86_OP_IMM:
                        self.add_function(self.near_target(ins, cs), cs)
                    elif op.type == X86_OP_MEM and not op.mem.base and not op.mem.index and op.size == 2:
                        self.near_ptr_words.add((cs, op.mem.disp & 0xFFFF))
                elif m == "lcall":
                    op = ins.operands
                    if len(op) == 2 and op[0].type == X86_OP_IMM:
                        seg, off = self.far_imm(ins)
                        self.add_function(seg * 16 + off, seg)
                a = nxt
        return ins_at, tables

    def near_target(self, ins, cs):
        return cs * 16 + (ins.operands[0].imm & 0xFFFF)

    def far_imm(self, ins):
        # capstone: lcall seg, off  (operands[0]=seg, operands[1]=off); the segment word is relocated in the image
        return ins.operands[0].imm & 0xFFFF, ins.operands[1].imm & 0xFFFF

    def jump_table(self, ins, ins_at, cs):
        """Resolve `jmp word ptr cs:[reg + table]` using the bound from a preceding cmp/ja or mov cx,N."""
        op = ins.operands[0]
        here_off = cs * 16 + ins.address
        if op.type == X86_OP_MEM and not op.mem.base and not op.mem.index:
            # jump through a handler variable: targets are resolved from immediate stores to it (resolve_indirect)
            self.indirect_words[here_off] = (cs, op.mem.disp & 0xFFFF)
            return []
        if op.type != X86_OP_MEM or ins.reg_name(op.mem.segment) != "cs":
            if self.dispatch_unresolved:
                self.indirect_words[here_off] = (cs, None)  # dispatched at run time over all functions
                return []
            self.unresolved.append((cs * 16 + ins.address, f"{ins.mnemonic} {ins.op_str}"))
            return []
        if here_off in self.table_counts:
            table = cs * 16 + (op.mem.disp & 0xFFFF)
            return [cs * 16 + int.from_bytes(self.img.raw[table + 2 * i:table + 2 * i + 2], "little") for i in range(self.table_counts[here_off])]
        table = cs * 16 + (op.mem.disp & 0xFFFF)
        here = cs * 16 + ins.address
        prev = sorted(a for a in ins_at if a < here)[-8:]
        count = None
        search = any(ins_at[a].mnemonic == "cmp" and "cs:[bx]" in ins_at[a].op_str for a in prev)
        for a in reversed(prev):
            p = ins_at[a]
            if search:
                # value-search switch: mov cx, N / mov bx, VALUES / cmp ax, cs:[bx] / ... / jmp cs:[bx + 2N]
                if p.mnemonic == "mov" and p.op_str.startswith("bx,") and p.operands[1].type == X86_OP_IMM:
                    table = cs * 16 + ((p.operands[1].imm + op.mem.disp) & 0xFFFF)
                if p.mnemonic == "mov" and p.op_str.startswith("cx,") and p.operands[1].type == X86_OP_IMM and count is None:
                    count = p.operands[1].imm & 0xFFFF
                continue
            if p.mnemonic == "cmp" and p.operands[1].type == X86_OP_IMM and count is None:
                count = (p.operands[1].imm & 0xFFFF) + 1
        if count is None or count > 256:
            self.unresolved.append((cs * 16 + ins.address, "jump table bound"))
            return []
        return [cs * 16 + int.from_bytes(self.img.raw[table + 2 * i:table + 2 * i + 2], "little") for i in range(count)]

    def run(self):
        out = {}
        while True:
            while self.queue:
                f = self.queue.pop()
                try:
                    out[f] = self.explore(f, self.functions[f])
                except Unsupported as e:
                    out[f] = e
            # handler variables: every `mov word ptr [X], imm` in explored code names a target of `jmp [X]`
            added = False
            if self.ds is not None:
                for (cs, word) in set(self.near_ptr_words):
                    if self.ds * 16 + word + 2 > len(self.img.raw):
                        continue
                    at = self.ds * 16 + word
                    t = cs * 16 + int.from_bytes(self.img.raw[at:at + 2], "little")
                    if t not in self.functions and t < len(self.img.raw) and int.from_bytes(self.img.raw[at:at + 2], "little"):
                        self.add_function(t, cs)
                        added = True
            for (cs, word) in set(self.indirect_words.values()) | set(self.near_ptr_words):
                if word is None:
                    continue
                for r in out.values():
                    if isinstance(r, Exception):
                        continue
                    for a, ins in r[0].items():
                        ops = ins.operands
                        if ins.mnemonic == "mov" and len(ops) == 2 and ops[0].type == X86_OP_MEM and ops[1].type == X86_OP_IMM \
                                and not ops[0].mem.base and not ops[0].mem.index and (ops[0].mem.disp & 0xFFFF) == word and ops[0].size == 2:
                            t = cs * 16 + (ops[1].imm & 0xFFFF)
                            if t not in self.functions:
                                self.add_function(t, cs)
                                added = True
            if not added:
                return out

    # ---------------- emission ----------------
    def seg_of(self, ins, op):
        s = ins.reg_name(op.mem.segment) if op.mem.segment else None
        if s:
            return SEGS[s]
        base = ins.reg_name(op.mem.base) if op.mem.base else None
        return "SS" if base in ("bp", "sp") else "DS"

    def ea(self, ins, op):
        parts = []
        if op.mem.base:
            parts.append(f"c.r[{R16[ins.reg_name(op.mem.base)]}]")
        if op.mem.index:
            parts.append(f"c.r[{R16[ins.reg_name(op.mem.index)]}]")
        disp = op.mem.disp & 0xFFFF
        if disp or not parts:
            parts.append(hex(disp))
        return f"c.lin({self.seg_of(ins, op)}, uint16_t({' + '.join(parts)}))"

    def reloc_imm(self, a, ins):
        """True if a segment fixup covers this instruction's immediate (e.g. `mov dx, DGROUP`)."""
        return any(r in self.img.relocs for r in range(a, a + ins.size)) and ins.mnemonic not in ("lcall", "ljmp")

    def rd(self, ins, op, size):
        if op.type == X86_OP_REG:
            n = ins.reg_name(op.reg)
            if n in R16: return f"c.r[{R16[n]}]"
            if n in R8: return f"c.g8({R8[n]})"
            if n in SEGS: return f"c.s[{SEGS[n]}]"
            raise Unsupported(f"register {n}")
        if op.type == X86_OP_IMM:
            if getattr(self, "_cur_reloc", False) and size == 2:
                return f"uint16_t({hex(op.imm & 0xFFFF)} + c.mach->load_seg)"
            return hex(op.imm & (0xFF if size == 1 else 0xFFFF))
        if op.type == X86_OP_MEM:
            return f"c.m->r{8 * size}({self.ea(ins, op)})"
        raise Unsupported("operand")

    def wr(self, ins, op, size, value):
        if op.type == X86_OP_REG:
            n = ins.reg_name(op.reg)
            if n in R16: return f"c.s16({R16[n]}, uint16_t({value}));"
            if n in R8: return f"c.s8({R8[n]}, uint8_t({value}));"
            if n in SEGS: return f"c.s[{SEGS[n]}] = uint16_t({value});"
            raise Unsupported(f"register {n}")
        if op.type == X86_OP_MEM:
            return f"c.m->w{8 * size}({self.ea(ins, op)}, uint{8 * size}_t({value}));"
        raise Unsupported("destination")

    def size(self, ins):
        ops = ins.operands
        if ins.mnemonic in ("shl", "sal", "shr", "sar", "rol", "ror", "rcl", "rcr") and ops:
            ops = ops[:1]  # the CL count does not set the operand size
        if ins.mnemonic in ("in", "out"):
            ops = [o for o in ops if o.type == X86_OP_REG and ins.reg_name(o.reg) in ("al", "ax")]
        for op in ops:
            if op.type == X86_OP_REG:
                n = ins.reg_name(op.reg)
                if n in R8: return 1
                if n in R16 or n in SEGS: return 2
        for op in ops:
            if op.type == X86_OP_MEM:
                return op.size
        return 2

    def emit_insn(self, ins, a, cs, labels, tables, lines):
        self._cur_reloc = self.reloc_imm(a, ins)
        if self._cur_reloc and not any(o.type == X86_OP_IMM for o in ins.operands):
            raise Unsupported(f"relocation inside a non-immediate operand at {a:05x}")
        m, ops = ins.mnemonic, ins.operands
        ip_next = (ins.address + ins.size) & 0xFFFF
        B = lambda s: 8 if s == 1 else 16
        e = lines.append
        prefix = ins.prefix[0] if ins.prefix[0] else 0
        rep = {0xF3: "rep", 0xF2: "repne"}.get(prefix)
        if m.startswith("rep") and " " in m:
            m = m.split()[1]
        sz = self.size(ins) if ops else 2

        def poll():
            e("if (c.icount >= c.next_event) c.mach->events(c);")

        def jump(target):
            if target <= a:
                poll()
            if target not in labels:
                raise Unsupported(f"jump target {target:05x} not explored")
            e(f"goto L_{target:05x};")

        if m == "mov":
            d, s = ops
            if d.type == X86_OP_REG and ins.reg_name(d.reg) in R16 and s.type == X86_OP_MEM:
                e(f"c.ld16({R16[ins.reg_name(d.reg)]}, {self.ea(ins, s)});")
            elif d.type == X86_OP_REG and ins.reg_name(d.reg) in R8 and s.type == X86_OP_MEM:
                e(f"c.ld8({R8[ins.reg_name(d.reg)]}, {self.ea(ins, s)});")
            elif d.type == X86_OP_MEM and s.type == X86_OP_REG and ins.reg_name(s.reg) in R16:
                e(f"c.st16({self.ea(ins, d)}, {R16[ins.reg_name(s.reg)]});")
            elif d.type == X86_OP_MEM and s.type == X86_OP_REG and ins.reg_name(s.reg) in R8 and a in self.tag_stores:
                # a store that creates pixel provenance (the image decoder's output): the machine names the tag
                e(f"c.m->w8({self.ea(ins, d)}, c.g8({R8[ins.reg_name(s.reg)]}), c.mach->store_tag(c, 0x{a:x}));")
            elif d.type == X86_OP_MEM and s.type == X86_OP_REG and ins.reg_name(s.reg) in R8:
                e(f"c.st8({self.ea(ins, d)}, {R8[ins.reg_name(s.reg)]});")
            elif d.type == X86_OP_REG and s.type == X86_OP_REG and ins.reg_name(d.reg) in R16 and ins.reg_name(s.reg) in R16:
                dn, sn = R16[ins.reg_name(d.reg)], R16[ins.reg_name(s.reg)]
                e(f"c.r[{dn}] = c.r[{sn}]; c.t[{dn}*2] = c.t[{sn}*2]; c.t[{dn}*2+1] = c.t[{sn}*2+1];")
            elif d.type == X86_OP_REG and s.type == X86_OP_REG and ins.reg_name(d.reg) in R8 and ins.reg_name(s.reg) in R8:
                e(f"c.s8({R8[ins.reg_name(d.reg)]}, c.g8({R8[ins.reg_name(s.reg)]}), c.t8({R8[ins.reg_name(s.reg)]}));")
            else:
                e(self.wr(ins, d, sz, self.rd(ins, s, sz)))
        elif m in ("add", "adc", "sub", "sbb", "cmp", "and", "or", "xor", "test"):
            d, s = ops
            a1, a2 = self.rd(ins, d, sz), self.rd(ins, s, sz)
            if s.type == X86_OP_IMM and not self._cur_reloc:
                a2 = hex(s.imm & (0xFF if sz == 1 else 0xFFFF))
            b = B(sz)
            expr = {"add": f"add<{b}>(c, {a1}, {a2})", "adc": f"add<{b}>(c, {a1}, {a2}, c.cf)",
                    "sub": f"sub<{b}>(c, {a1}, {a2})", "sbb": f"sub<{b}>(c, {a1}, {a2}, c.cf)", "cmp": f"sub<{b}>(c, {a1}, {a2})",
                    "and": f"logic<{b}>(c, {a1} & {a2})", "test": f"logic<{b}>(c, {a1} & {a2})",
                    "or": f"logic<{b}>(c, {a1} | {a2})", "xor": f"logic<{b}>(c, {a1} ^ {a2})"}[m]
            if m in ("cmp", "test"):
                e(f"{expr};")
            elif m == "xor" and d.type == X86_OP_REG and s.type == X86_OP_REG and d.reg == s.reg:
                e(f"{{ auto v = {expr}; {self.wr(ins, d, sz, 'v')} }}")
            else:
                e(f"{{ auto v = {expr}; {self.wr(ins, d, sz, 'v')} }}")
        elif m in ("inc", "dec", "neg", "not"):
            d = ops[0]; b = B(sz); v = self.rd(ins, d, sz)
            expr = {"inc": f"inc<{b}>(c, {v})", "dec": f"dec<{b}>(c, {v})", "neg": f"neg<{b}>(c, {v})", "not": f"(~{v} & {0xFF if sz == 1 else 0xFFFF})"}[m]
            e(f"{{ auto v = {expr}; {self.wr(ins, d, sz, 'v')} }}")
        elif m in ("shl", "sal", "shr", "sar", "rol", "ror", "rcl", "rcr"):
            d = ops[0]; b = B(sz)
            cnt = "1" if len(ops) == 1 else (hex(ops[1].imm & 0xFF) if ops[1].type == X86_OP_IMM else "c.g8(CL)")
            fn = {"sal": "shl"}.get(m, m)
            e(f"{{ auto v = {fn}<{b}>(c, {self.rd(ins, d, sz)}, {cnt}); {self.wr(ins, d, sz, 'v')} }}")
        elif m in ("mul", "imul", "div", "idiv") and len(ops) == 1:
            v = self.rd(ins, ops[0], sz); b = B(sz)
            if m in ("mul", "imul"):
                e(f"{m}{b}(c, uint{b}_t({v}));")
            else:
                e(f"if (!{m}{b}(c, uint{b}_t({v}))) {{ c.mach->interrupt(c, 0, 0x{ins.address:x}); return; }}")
        elif m == "imul":
            d = ops[0]; s1 = self.rd(ins, ops[1], 2); s2 = self.rd(ins, ops[2], 2) if len(ops) == 3 else self.rd(ins, d, 2)
            e(self.wr(ins, d, 2, f"imul16_3(c, {s1}, {s2})"))
        elif m in ("cbw", "cwde"):
            e("c.s16(AX, uint16_t(int16_t(int8_t(c.r[AX]))));")
        elif m in ("cwd", "cdq"):
            e("c.s16(DX, (c.r[AX] & 0x8000) ? 0xFFFF : 0);")
        elif m == "lea":
            op = ops[1]
            parts = [f"c.r[{R16[ins.reg_name(op.mem.base)]}]"] if op.mem.base else []
            if op.mem.index: parts.append(f"c.r[{R16[ins.reg_name(op.mem.index)]}]")
            parts.append(hex(op.mem.disp & 0xFFFF))
            e(self.wr(ins, ops[0], 2, " + ".join(parts)))
        elif m in ("les", "lds"):
            reg = R16[ins.reg_name(ops[0].reg)]; ea = self.ea(ins, ops[1])
            e(f"{{ uint32_t p = {ea}; c.ld16({reg}, p); c.s[{'ES' if m == 'les' else 'DS'}] = c.m->r16((p + 2) & 0xFFFFF); }}")
        elif m == "xchg":
            d, s = ops
            if sz == 2 and d.type == X86_OP_REG and s.type == X86_OP_REG:
                x, y = R16[ins.reg_name(d.reg)], R16[ins.reg_name(s.reg)]
                e(f"std::swap(c.r[{x}], c.r[{y}]); std::swap(c.t[{x}*2], c.t[{y}*2]); std::swap(c.t[{x}*2+1], c.t[{y}*2+1]);")
            else:
                e(f"{{ auto x = {self.rd(ins, d, sz)}; auto y = {self.rd(ins, s, sz)}; {self.wr(ins, d, sz, 'y')} {self.wr(ins, s, sz, 'x')} }}")
        elif m == "push":
            op = ops[0]
            if op.type == X86_OP_REG and ins.reg_name(op.reg) in R16:
                n = ins.reg_name(op.reg)
                e("c.push16(c.r[SP]);" if n == "sp" else f"c.pushr({R16[n]});")
            elif op.type == X86_OP_MEM:
                e(f"{{ uint32_t p = {self.ea(ins, op)}; c.push16(c.m->r16(p), c.m->tag[p], c.m->tag[(p + 1) & 0xFFFFF]); }}")
            else:
                e(f"c.push16(uint16_t({self.rd(ins, op, 2)}));")
        elif m == "pop":
            op = ops[0]
            if op.type == X86_OP_REG and ins.reg_name(op.reg) in R16:
                e(f"c.popr({R16[ins.reg_name(op.reg)]});")
            else:
                e(f"{{ uint16_t v = c.pop16(); {self.wr(ins, op, 2, 'v')} }}")
        elif m in ("pushf", "pushfw"):
            e("c.push16(c.flags());")
        elif m in ("popf", "popfw"):
            e("c.set_flags(c.pop16());")
        elif m in ("pushaw", "pusha"):
            e("{ uint16_t sp = c.r[SP]; c.pushr(AX); c.pushr(CX); c.pushr(DX); c.pushr(BX); c.push16(sp); c.pushr(BP); c.pushr(SI); c.pushr(DI); }")
        elif m in ("popaw", "popa"):
            e("c.popr(DI); c.popr(SI); c.popr(BP); c.pop16(); c.popr(BX); c.popr(DX); c.popr(CX); c.popr(AX);")
        elif m == "lahf":
            e("c.s8(AH, uint8_t(c.flags()));")
        elif m == "sahf":
            e("c.set_flags(uint16_t((c.flags() & 0xFF00) | c.g8(AH)));")
        elif m in ("clc", "stc", "cmc", "cld", "std", "cli", "sti"):
            e({"clc": "c.cf = false;", "stc": "c.cf = true;", "cmc": "c.cf = !c.cf;", "cld": "c.df = false;", "std": "c.df = true;",
               "cli": "c.ifl = false;", "sti": "c.ifl = true;"}[m])
        elif m in ("nop", "wait"):
            pass
        elif m in ("daa", "das", "aaa", "aas"):
            e(f"{m}(c);")
        elif m in ("aam", "aad"):
            e(f"{m}(c, {hex(ops[0].imm & 0xFF) if ops else '10'});")
        elif m == "xlatb":
            e("c.ld8(AL, c.lin(DS, uint16_t(c.r[BX] + c.g8(AL))));")
        elif m in ("movsb", "movsw", "stosb", "stosw", "lodsb", "lodsw", "scasb", "scasw", "cmpsb", "cmpsw"):
            base, w = m[:4], m[4]
            seg = "DS"
            for op in ops:
                if op.type == X86_OP_MEM and op.mem.segment and ins.reg_name(op.mem.base) == "si":
                    seg = SEGS[ins.reg_name(op.mem.segment)]
            fn = f"{base}{8 if w == 'b' else 16}"
            call = f"{fn}(c, {seg});" if base in ("movs", "lods", "cmps") else f"{fn}(c);"
            if rep and base in ("scas", "cmps"):
                cond = "c.zf" if rep == "rep" else "!c.zf"
                e(f"while (c.r[CX]) {{ {call} c.r[CX]--; ++c.icount; if (!({cond})) break; }}")
            elif rep:
                e(f"while (c.r[CX]) {{ {call} c.r[CX]--; ++c.icount; }}")
            else:
                e(call)
        elif m in JCC:
            e(f"if ({JCC[m]}) {{")
            jump(self.near_target(ins, cs))
            e("}")
        elif m == "jcxz":
            e("if (c.r[CX] == 0) {"); jump(self.near_target(ins, cs)); e("}")
        elif m in ("loop", "loope", "loopne"):
            cond = {"loop": "", "loope": " && c.zf", "loopne": " && !c.zf"}[m]
            e(f"if (--c.r[CX] != 0{cond}) {{"); jump(self.near_target(ins, cs)); e("}")
        elif m == "jmp":
            op = ops[0]
            if op.type == X86_OP_IMM:
                jump(self.near_target(ins, cs))
            else:
                tgt = self.rd(ins, op, 2)
                if a in self.indirect_words:
                    e(f"c.mach->jump_near(c, uint16_t({tgt})); return;")
                    return
                e(f"{{ uint32_t t = {cs * 16} + {tgt};")
                if a in tables and tables[a]:
                    e("switch (t) {")
                    for t in sorted(set(tables[a])):
                        e(f"case 0x{t:x}: goto L_{t:05x};")
                    e("} }")
                else:
                    e("switch (t) {")
                    for t in sorted(labels):
                        e(f"case 0x{t:x}: goto L_{t:05x};")
                    e("} }")
                e(f"fatal(c, 0x{a:x}, \"indirect jump to an untranslated address\");")
        elif m == "ljmp":
            if len(ops) == 2 and ops[0].type == X86_OP_IMM:
                # The target is read from the instruction bytes at run time: MTV.EXE patches its driver
                # call stubs (371E:0083..) with the loaded driver's segment and entry points.
                e(f"{{ uint32_t p = c.lin(CS, 0x{(ins.address + 1) & 0xFFFF:x}); uint16_t o = c.m->r16(p), s = c.m->r16((p + 2) & 0xFFFFF);")
                e("c.s[CS] = s; c.mach->jump_far(c, s, o); return; }")
            else:
                op = ops[0]
                e(f"{{ uint32_t p = {self.ea(ins, op)}; uint16_t o = c.m->r16(p), s = c.m->r16((p + 2) & 0xFFFFF); c.s[CS] = s; c.mach->jump_far(c, s, o); return; }}")
        elif m == "call":
            op = ops[0]
            tgt = None if op.type == X86_OP_IMM else self.rd(ins, op, 2)
            if tgt:
                e(f"{{ uint16_t t = uint16_t({tgt});")
            e(f"c.push16(0x{ip_next:x}); c.enter(c.s[CS], 0x{ip_next:x});")
            poll()
            if op.type == X86_OP_IMM:
                e(f"{self.fname(self.near_target(ins, cs))}(c);")
            else:
                e("c.mach->call_near(c, t);")
            e("c.leave();" + (" }" if tgt else ""))
        elif m == "lcall":
            if len(ops) == 2 and ops[0].type == X86_OP_IMM:
                seg, off = self.far_imm(ins)
                e(f"c.push16(c.s[CS]); c.push16(0x{ip_next:x}); c.enter(c.s[CS], 0x{ip_next:x});")
                poll()
                e(f"c.s[CS] = uint16_t(0x{seg:x} + c.mach->load_seg);")
                e(f"{self.fname(seg * 16 + off)}(c);")
                e("c.leave();")
            else:
                op = ops[0]
                e(f"{{ uint32_t p = {self.ea(ins, op)}; uint16_t o = c.m->r16(p), s = c.m->r16((p + 2) & 0xFFFFF);")
                e(f"c.push16(c.s[CS]); c.push16(0x{ip_next:x}); c.enter(c.s[CS], 0x{ip_next:x});")
                poll()
                e("c.s[CS] = s; c.mach->call_far(c, s, o); c.leave(); }")
        elif m == "ret":
            n = ops[0].imm & 0xFFFF if ops else 0
            e("{ uint16_t ip = c.pop16();" + (f" c.r[SP] = uint16_t(c.r[SP] + {n});" if n else "") +
              " if (!c.shadow.empty() && Memory::lin(c.s[CS], ip) != c.shadow.back()) c.mach->jump_near(c, ip); return; }")
        elif m == "retf":
            n = ops[0].imm & 0xFFFF if ops else 0
            e("{ uint16_t ip = c.pop16(); c.s[CS] = c.pop16();" + (f" c.r[SP] = uint16_t(c.r[SP] + {n});" if n else "") +
              " if (!c.shadow.empty() && Memory::lin(c.s[CS], ip) != c.shadow.back()) c.mach->jump_far(c, c.s[CS], ip); return; }")
        elif m == "iret":
            e("{ uint16_t ip = c.pop16(); c.s[CS] = c.pop16(); c.set_flags(c.pop16());"
              " if (!c.shadow.empty() && Memory::lin(c.s[CS], ip) != c.shadow.back()) c.mach->jump_far(c, c.s[CS], ip); return; }")
        elif m == "int":
            e(f"c.mach->interrupt(c, 0x{ops[0].imm & 0xFF:x}, 0x{ip_next:x});")
        elif m == "in":
            d, s = ops
            port = hex(s.imm & 0xFF) if s.type == X86_OP_IMM else "c.r[DX]"
            e(f"c.s8(AL, c.mach->in8(c, {port}));" if sz == 1 else f"c.s16(AX, c.mach->in16(c, {port}));")
        elif m == "out":
            d, s = ops
            port = hex(d.imm & 0xFF) if d.type == X86_OP_IMM else "c.r[DX]"
            e(f"c.mach->out8(c, {port}, c.g8(AL));" if sz == 1 else f"c.mach->out16(c, {port}, c.r[AX]);")
        elif m == "enter":
            n, lvl = ops[0].imm & 0xFFFF, ops[1].imm & 0xFF
            if lvl:
                raise Unsupported("enter with nesting level")
            e(f"c.pushr(BP); c.r[BP] = c.r[SP]; c.r[SP] = uint16_t(c.r[SP] - {n});")
        elif m == "leave":
            e("c.r[SP] = c.r[BP]; c.popr(BP);")
        elif m == "hlt":
            e(f"fatal(c, 0x{a:x}, \"hlt\");")
        else:
            raise Unsupported(f"{m} {ins.op_str} at {a:05x}")

    def emit_function(self, entry, cs, ins_at, tables):
        lines = [f"void {self.fname(entry)}(Cpu& c) {{", f"if (g_trace_entries) trace_entry(c, 0x{entry:x});"]
        if entry in self.hooks:
            lines.append(f"c.mach->hook(c, 0x{entry:x});")
        labels = set(ins_at)
        targets = set()
        for a, ins in ins_at.items():
            if ins.mnemonic in JCC or ins.mnemonic in ("jcxz", "loop", "loope", "loopne") or (ins.mnemonic == "jmp" and ins.operands[0].type == X86_OP_IMM):
                targets.add(self.near_target(ins, cs))
        for t in tables.values():
            targets.update(t)
        indirect = any(ins.mnemonic == "jmp" and ins.operands[0].type != X86_OP_IMM and not tables.get(a) and a not in self.indirect_words
                       for a, ins in ins_at.items())
        order = sorted(ins_at)
        if order[0] != entry:
            # code shared with lower addresses (e.g. a RET just before the entry): start at the entry
            lines.append(f"goto L_{entry:05x};")
            targets.add(entry)
        for i, a in enumerate(order):
            ins = ins_at[a]
            if a in targets or indirect:
                lines.append(f"L_{a:05x}:")
            lines.append(f"// {a:05x}: {ins.mnemonic} {ins.op_str}")
            lines.append("++c.icount;")
            self.emit_insn(ins, a, cs, labels, tables, lines)
            # fall through into a non-contiguous instruction
            nxt = a + ins.size
            ends = ins.mnemonic in ("ret", "retf", "iret", "hlt", "jmp", "ljmp")
            if not ends and (i + 1 == len(order) or order[i + 1] != nxt):
                if nxt in ins_at:
                    lines.append(f"goto L_{nxt:05x};")
                    targets.add(nxt)
                else:
                    lines.append(f"fatal(c, 0x{nxt:x}, \"flow falls out of the function\");")
        lines.append("}")
        # labels needed for fall-through gotos added late
        text = "\n".join(lines)
        for t in targets:
            if f"L_{t:05x}:" not in text:
                text = text.replace(f"// {t:05x}: ", f"L_{t:05x}:\n// {t:05x}: ", 1)
        return text


def raw_main(a):
    """Translate a driver blob. Entries come from the caller (its jump table / interrupt handler offsets)."""
    data = Path(a.exe).read_bytes()
    img = RawImage(data)
    t = Translator(img, a.prefix)
    t.dispatch_unresolved = True
    for spec in a.table:
        k, v = spec.split("=")
        t.table_counts[int(k, 16)] = int(v)
    for spec in a.targets:
        k, v = spec.split(":")
        for i in range(int(v)):
            t.add_function(int.from_bytes(img.raw[int(k, 16) + 2 * i:int(k, 16) + 2 * i + 2], "little"), 0)
    for e in a.raw_entries.split(","):
        t.add_function(int(e, 16), 0)
    results = t.run()
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    bodies, errors = [], []
    for f in sorted(results):
        r = results[f]
        if isinstance(r, Exception):
            errors.append(f"{f:05x}: {r}"); continue
        try:
            bodies.append(t.emit_function(f, 0, *r))
        except Unsupported as e:
            errors.append(f"{f:05x}: {e}")
    decl = "\n".join(f"void {t.fname(f)}(Cpu& c);" for f in sorted(t.functions))
    (out / f"{a.prefix}.h").write_text(f"#pragma once\n#include \"rt/cpu.h\"\n#include \"rt/machine.h\"\nnamespace mtv::rt {{\n{decl}\n}}\n")
    (out / f"{a.prefix}_00.cpp").write_text(
        f"// Generated by tools/recomp/x86recomp.py from {Path(a.exe).name}. Do not edit.\n#include \"{a.prefix}.h\"\n"
        "#if defined(__clang__)\n#pragma clang diagnostic ignored \"-Wunused-label\"\n#endif\nnamespace mtv::rt {\n" + "\n\n".join(bodies) + "\n}\n")
    table = ",\n".join(f"    {{0x{f:x}, {t.fname(f)}, 0}}" for f in sorted(t.functions))
    (out / f"{a.prefix}_table.cpp").write_text(
        f"#include \"{a.prefix}.h\"\nnamespace mtv::rt {{\nextern const FunctionEntry {a.prefix}_functions[];\nextern const size_t {a.prefix}_function_count;\n"
        f"const FunctionEntry {a.prefix}_functions[] = {{\n{table}\n}};\n"
        f"const size_t {a.prefix}_function_count = {len(t.functions)};\n}}\n")
    print(f"{a.prefix}: functions {len(t.functions)}, emitted {len(bodies)}, errors {len(errors)}, unresolved {len(t.unresolved)}")
    for e in errors: print("  ", e)
    for u in t.unresolved: print("   unresolved", hex(u[0]), u[1])


def is_code_reloc(img, r):
    """A relocation inside a far call/jump instruction (9A/EA seg:off) belongs to code, not to a data pointer."""
    return r >= 3 and img.raw[r - 3] in (0x9A, 0xEA)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe", required=True)
    p.add_argument("--load-seg", type=lambda s: int(s, 0), required=True)
    p.add_argument("--functions", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--prefix", default="mtv")
    p.add_argument("--chunks", type=int, default=0, help="write exactly this many body files (padding with empty ones)")
    p.add_argument("--ds", type=lambda s: int(s, 0), default=None, help="image-relative data segment for near function pointer roots")
    p.add_argument("--native", default="", help="comma-separated image offsets implemented natively (body not emitted)")
    p.add_argument("--hooks", default="", help="comma-separated function entries (image offsets, hex) that call Machine::hook")
    p.add_argument("--tag-stores", default="", help="comma-separated byte-store instruction addresses whose tag comes from Machine::store_tag")
    p.add_argument("--entries", default="", help="extra roots: comma-separated <segment>:<offset> image-relative hex (e.g. handlers installed with setvect)")
    p.add_argument("--raw-entries", default="", help="raw blob mode: comma-separated entry offsets (the file is a code blob)")
    p.add_argument("--table", action="append", default=[], help="jump table size override: <jmp offset hex>=<count>")
    p.add_argument("--targets", action="append", default=[], help="extra roots from a word table: <table offset hex>:<count>")
    a = p.parse_args()
    if a.raw_entries:
        return raw_main(a)
    img = Image(a.exe, a.load_seg)
    t = Translator(img, a.prefix)
    t.ds = a.ds
    t.hooks = {int(x, 16) for x in a.hooks.split(",") if x}
    t.tag_stores = {int(x, 16) for x in a.tag_stores.split(",") if x}
    # Roots: the entry point and every far pointer stored in data (relocated segment word + preceding offset word
    # that lands on a candidate function). Ghidra's list only names candidates; unreachable ones are skipped.
    fm = json.loads(Path(a.functions).read_text())
    candidates = {}
    for f in fm["functions"]:
        off = int(f["address"], 16) - 0x10000
        if re.fullmatch(r"[0-9a-f]{4}", f["segment"]):
            candidates[off] = int(f["segment"], 16) - 0x1000
    t.add_function(img.entry_cs * 16 + img.entry_ip, img.entry_cs)
    for e in filter(None, a.entries.split(",")):
        seg, off = (int(x, 16) for x in e.split(":"))
        t.add_function(seg * 16 + off, seg)
    for r in sorted(img.relocs):
        seg = int.from_bytes(img.raw[r:r + 2], "little")
        off = int.from_bytes(img.raw[r - 2:r], "little")
        target = seg * 16 + off
        if target in candidates and candidates[target] == seg and not is_code_reloc(img, r):
            t.add_function(target, seg)
    native = {int(x, 16) for x in a.native.split(",") if x}
    results = t.run()
    # Optional roots: remaining Ghidra candidates that translate cleanly (possible function-pointer targets).
    dropped = []
    for off, seg in sorted(candidates.items()):
        if off in t.functions:
            continue
        trial = Translator(img, a.prefix)
        trial.functions = dict(t.functions)
        trial.add_function(off, seg)
        r = trial.run()
        ok = all(not isinstance(v, Exception) for v in r.values())
        if ok:
            try:
                for f, v in r.items():
                    trial.emit_function(f, trial.functions[f], *v)
            except Unsupported:
                ok = False
        if ok:
            t.functions = trial.functions
            results.update(r)
        else:
            dropped.append(off)
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    bodies, errors = [], []
    for f in sorted(results):
        r = results[f]
        if f in native:
            continue
        if isinstance(r, Exception):
            errors.append(f"{f:05x}: {r}")
            continue
        try:
            bodies.append(t.emit_function(f, t.functions[f], *r))
        except Unsupported as e:
            errors.append(f"{f:05x}: {e}")
    decl = "\n".join(f"void {t.fname(f)}(Cpu& c);" for f in sorted(t.functions))
    hdr = f"#pragma once\n#include \"rt/cpu.h\"\n#include \"rt/machine.h\"\nnamespace mtv::rt {{\n{decl}\n}}\n"
    (out / f"{a.prefix}.h").write_text(hdr)
    n = a.chunks or max(1, (len(bodies) + 59) // 60)
    per = (len(bodies) + n - 1) // n
    for i in range(n):
        (out / f"{a.prefix}_{i:02d}.cpp").write_text(
            f"// Generated by tools/recomp/x86recomp.py from {Path(a.exe).name}. Do not edit.\n#include \"{a.prefix}.h\"\n"
            "#if defined(__clang__)\n#pragma clang diagnostic ignored \"-Wunused-label\"\n#endif\nnamespace mtv::rt {\n" + "\n\n".join(bodies[i * per:(i + 1) * per]) + "\n}\n")
    table = ",\n".join(f"    {{0x{f:x}, {t.fname(f)}, 0x{t.functions[f]:x}}}" for f in sorted(t.functions))
    (out / f"{a.prefix}_table.cpp").write_text(
        f"#include \"{a.prefix}.h\"\nnamespace mtv::rt {{\nextern const FunctionEntry {a.prefix}_functions[];\nextern const size_t {a.prefix}_function_count;\n"
        f"const FunctionEntry {a.prefix}_functions[] = {{\n{table}\n}};\n"
        f"const size_t {a.prefix}_function_count = {len(t.functions)};\n}}\n")
    skipped = sorted(set(candidates) - set(t.functions))
    assert set(skipped) == set(dropped), (skipped, dropped)
    (out / f"{a.prefix}_skipped.txt").write_text("".join(f"{x:05x}\n" for x in skipped))
    print(f"functions: {len(t.functions)}, emitted: {len(bodies)}, errors: {len(errors)}, unresolved indirect jumps: {len(t.unresolved)}, Ghidra candidates not reached: {len(skipped)}")
    for e in errors[:30]:
        print("  ", e)
    for u in t.unresolved[:30]:
        print("   unresolved", hex(u[0]), u[1])


if __name__ == "__main__":
    main()
