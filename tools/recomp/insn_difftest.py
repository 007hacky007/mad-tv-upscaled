"""Differential test of the translator: each distinct instruction form used by the game, run from random states
in Unicorn and in the translated C++, must produce identical registers, defined flags and memory.

Usage: tools/cache/pyenv/bin/python tools/recomp/insn_difftest.py --exe work/unpacked/MTV.EXE [--states 40]
"""
import argparse
import json
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from capstone.x86 import X86_OP_IMM, X86_OP_MEM
from unicorn import UC_ARCH_X86, UC_MODE_16, Uc
from unicorn.x86_const import *

sys.path.insert(0, str(Path(__file__).resolve().parent))
import x86recomp as X  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SKIP = {"jmp", "ljmp", "call", "lcall", "ret", "retf", "iret", "int", "in", "out", "hlt", "loop", "loope", "loopne", "jcxz",
        "cli", "sti", "enter", "leave", "nop", "wait", "pushf", "popf", "pushfw", "popfw"} | set(X.JCC)
UNDEF = {  # flags left undefined by the instruction (bit mask over CF PF AF ZF SF OF = 1 4 0x10 0x40 0x80 0x800)
    "and": 0x10, "or": 0x10, "xor": 0x10, "test": 0x10,
    "mul": 0xD4, "imul": 0xD4, "div": 0x8D5, "idiv": 0x8D5,
    "shl": 0x10, "sal": 0x10, "shr": 0x10, "sar": 0x10, "rol": 0, "ror": 0, "rcl": 0, "rcr": 0,
    "aam": 0x811, "aad": 0x811, "daa": 0x800, "das": 0x800, "aaa": 0x8C4, "aas": 0x8C4,
}
REGS = [UC_X86_REG_AX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_BX, UC_X86_REG_SP, UC_X86_REG_BP, UC_X86_REG_SI, UC_X86_REG_DI]
SREGS = [UC_X86_REG_ES, UC_X86_REG_CS, UC_X86_REG_SS, UC_X86_REG_DS]
CODE_SEG = 0x8000


def pattern(ins):
    return ins.mnemonic + " " + re.sub(r"0x[0-9a-f]+|\b\d+\b", "N", ins.op_str)


def collect(t, results, per=3):
    samples = {}
    for f, r in results.items():
        if isinstance(r, Exception):
            continue
        ins_at, _ = r
        for a, ins in ins_at.items():
            if ins.mnemonic.split()[-1] in SKIP or ins.mnemonic in SKIP:
                continue
            k = pattern(ins)
            lst = samples.setdefault(k, [])
            if len(lst) < per:
                lst.append((a, t.functions[f], bytes(t.img.raw[a:a + ins.size]), ins))
    return samples


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe", default=str(ROOT / "work/unpacked/MTV.EXE"))
    p.add_argument("--states", type=int, default=40)
    p.add_argument("--seed", type=int, default=1)
    a = p.parse_args()
    img = X.Image(a.exe, 0x824)
    t = X.Translator(img, "mtv")
    t.add_function(img.entry_cs * 16 + img.entry_ip, img.entry_cs)
    results = t.run()
    samples = collect(t, results)
    cases = [s for lst in samples.values() for s in lst]
    # One C++ function per case: the instruction translated as if it lived at CODE_SEG:0x100.
    bodies = []
    for i, (addr, cs, raw, ins) in enumerate(cases):
        lines = []
        t2 = X.Translator(img, "mtv")
        ins2 = next(t2.md.disasm(raw, 0x100))
        t2.emit_insn(ins2, CODE_SEG * 16 + 0x100, CODE_SEG, set(), {}, lines)
        bodies.append(f"void t{i}(Cpu& c) {{ {' '.join(lines)} }}")
    names = ",".join(f"t{i}" for i in range(len(cases)))
    harness = f'''#include "rt/cpu.h"
#include "rt/machine.h"
#include <cstdio>
#include <vector>
namespace mtv::rt {{
struct M : Machine {{ void interrupt(Cpu&, uint8_t, uint16_t) override {{ std::puts("INT"); }} uint8_t in8(Cpu&, uint16_t) override {{ return 0; }}
 void out8(Cpu&, uint16_t, uint8_t) override {{}} void events(Cpu& c) override {{ c.next_event = ~0ull; }}
 void call_near(Cpu&, uint16_t) override {{}} void call_far(Cpu&, uint16_t, uint16_t) override {{}} void jump_far(Cpu&, uint16_t, uint16_t) override {{}} void jump_near(Cpu&, uint16_t) override {{}} }};
{chr(10).join(bodies)}
}}
using namespace mtv::rt;
int main() {{
  static Fn fns[] = {{{names}}};
  Memory mem; M mach; unsigned idx;
  while (std::fread(&idx, 4, 1, stdin) == 1) {{
    Cpu c; c.m = &mem; c.mach = &mach; uint16_t regs[13];
    std::fread(regs, 2, 13, stdin);
    for (int i = 0; i < 8; ++i) c.r[i] = regs[i];
    for (int i = 0; i < 4; ++i) c.s[i] = regs[8 + i];
    c.set_flags(regs[12]);
    uint32_t n; std::fread(&n, 4, 1, stdin);
    std::vector<uint32_t> addrs(n); std::vector<uint8_t> vals(n);
    for (uint32_t i = 0; i < n; ++i) {{ std::fread(&addrs[i], 4, 1, stdin); std::fread(&vals[i], 1, 1, stdin); mem.b[addrs[i]] = vals[i]; }}
    fns[idx](c);
    for (int i = 0; i < 8; ++i) regs[i] = c.r[i];
    for (int i = 0; i < 4; ++i) regs[8 + i] = c.s[i];
    regs[12] = c.flags();
    std::fwrite(regs, 2, 13, stdout);
    for (uint32_t i = 0; i < n; ++i) std::fwrite(&mem.b[addrs[i]], 1, 1, stdout);
    for (uint32_t i = 0; i < n; ++i) mem.b[addrs[i]] = 0;
    std::fflush(stdout);
  }}
}}
'''
    work = Path(tempfile.mkdtemp(prefix="difftest-"))
    (work / "h.cpp").write_text(harness)
    exe = work / "h"
    subprocess.run(["clang++", "-std=c++20", "-O1", f"-I{ROOT / 'src'}", str(work / "h.cpp"), str(ROOT / "src/rt/cpu.cpp"), "-o", str(exe)], check=True)
    proc = subprocess.Popen([str(exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    rnd = random.Random(a.seed)
    fails = {}
    for idx, (addr, cs, raw, ins) in enumerate(cases):
        base = ins.mnemonic.split()[-1]
        undef = UNDEF.get(base, 0)
        if base in ("shl", "sal", "shr", "sar", "rol", "ror", "rcl", "rcr"):
            undef |= 0x800  # OF is only defined for 1-bit shifts; compare it only then
        for s in range(a.states):
            regs = [rnd.randrange(0x10000) for _ in range(8)]
            regs[4] = 0x8000 + rnd.randrange(0x100) * 2  # SP
            if base in ("movsb", "movsw", "stosb", "stosw", "lodsb", "lodsw", "scasb", "scasw", "cmpsb", "cmpsw") or "rep" in ins.mnemonic:
                regs[1] = rnd.randrange(0, 6)  # small CX for REP forms
            if base in ("div", "idiv") and rnd.random() < 0.8:
                regs[2] = rnd.randrange(0x10)  # keep most quotients in range
            sregs = [0x2000, CODE_SEG, 0x3000, 0x4000]
            flags = 0x0002 | (rnd.randrange(0x10000) & 0x0CD5)
            flags &= ~0x400 if rnd.random() < 0.5 else 0xFFFF
            uc = Uc(UC_ARCH_X86, UC_MODE_16)
            uc.mem_map(0, 0x110000)
            code_at = CODE_SEG * 16 + 0x100
            uc.mem_write(code_at, raw)
            # random memory around every possible effective address the instruction may touch
            touched = set()
            for segv in sregs:
                for regv in set(regs) | {0}:
                    for d in range(-4, 8):
                        for disp in ([op.mem.disp & 0xFFFF for op in ins.operands if op.type == X86_OP_MEM] or [0]):
                            for r2 in (0, regs[3], regs[5], regs[6], regs[7]):
                                pass
            eas = set()
            for op in ins.operands:
                if op.type == X86_OP_MEM:
                    seg = {"es": 0x2000, "cs": CODE_SEG, "ss": 0x3000, "ds": 0x4000}.get(ins.reg_name(op.mem.segment) if op.mem.segment else ("ss" if ins.reg_name(op.mem.base) in ("bp",) else "ds"))
                    off = (op.mem.disp + (regs[[3, 5, 6, 7][["bx", "bp", "si", "di"].index(ins.reg_name(op.mem.base))]] if op.mem.base else 0)
                           + (regs[[3, 5, 6, 7][["bx", "bp", "si", "di"].index(ins.reg_name(op.mem.index))]] if op.mem.index else 0)) & 0xFFFF
                    eas.update(seg * 16 + ((off + d) & 0xFFFF) for d in range(-2, 6))
            for r, segv in ((6, 0x4000), (7, 0x2000), (6, 0x2000), (6, 0x3000), (6, CODE_SEG)):
                eas.update(segv * 16 + ((regs[r] + d) & 0xFFFF) for d in range(-12, 12))
            eas.update(0x3000 * 16 + ((regs[4] + d) & 0xFFFF) for d in range(-24, 24))
            eas -= set(range(code_at, code_at + len(raw)))
            mem = {e: rnd.randrange(256) for e in sorted(eas)}
            for e, v in mem.items():
                uc.mem_write(e, bytes([v]))
            for i, r in enumerate(REGS):
                uc.reg_write(r, regs[i])
            for i, r in enumerate(SREGS):
                uc.reg_write(r, sregs[i])
            uc.reg_write(UC_X86_REG_IP, 0x100)
            uc.reg_write(UC_X86_REG_FLAGS, flags)
            try:
                uc.emu_start(0x100, code_at + len(raw), count=200)
            except Exception as ex:  # e.g. divide error: the translated code raises INT 0 instead
                continue
            want = [uc.reg_read(r) for r in REGS] + [uc.reg_read(r) for r in SREGS] + [uc.reg_read(UC_X86_REG_FLAGS)]
            want_mem = [uc.mem_read(e, 1)[0] for e in mem]
            payload = idx.to_bytes(4, "little") + b"".join(x.to_bytes(2, "little") for x in regs + sregs + [flags])
            payload += len(mem).to_bytes(4, "little") + b"".join(e.to_bytes(4, "little") + bytes([v]) for e, v in mem.items())
            proc.stdin.write(payload); proc.stdin.flush()
            got = proc.stdout.read(26)
            got_regs = [int.from_bytes(got[2 * i:2 * i + 2], "little") for i in range(13)]
            got_mem = list(proc.stdout.read(len(mem)))
            fm = 0x8D5 & ~undef
            if base in ("shl", "sal", "shr", "sar", "rol", "ror", "rcl", "rcr"):
                cnt = 1 if len(ins.operands) == 1 else (ins.operands[1].imm if ins.operands[1].type == X86_OP_IMM else regs[1] & 0xFF)
                if (cnt & 0x1F) == 1:
                    fm |= 0x800
                if (cnt & 0x1F) == 0:
                    fm = 0x8D5
            bad = [i for i in range(12) if got_regs[i] != want[i]]
            if (got_regs[12] & fm) != (want[12] & fm):
                bad.append("flags %04x vs %04x mask %04x" % (got_regs[12], want[12], fm))
            if got_mem != want_mem:
                bad.append("memory")
            if bad:
                fails.setdefault(pattern(ins), []).append((s, bad))
                if "memory" in bad and len(fails[pattern(ins)]) == 1:
                    diff = [(hex(e), w, g) for (e, w, g) in zip(mem, want_mem, got_mem) if w != g]
                    print("   detail", pattern(ins), ins.op_str, "regs", [hex(x) for x in regs], "flags", hex(flags), diff[:6])
    proc.stdin.close(); proc.wait()
    print(f"instruction forms: {len(samples)}, cases: {len(cases)}, states each: {a.states}, failing forms: {len(fails)}")
    for k, v in sorted(fails.items())[:40]:
        print("  FAIL", k, v[0])
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
