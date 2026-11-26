"""Run one function from a snapshot in Unicorn (original code) and compare with the translated run (mtv-tool call-fn).

python tools/recomp/func_difftest.py <snapshot> <image offset hex>
The function must return with RET/RETF to the return address already on the snapshot stack.
"""
import struct
import subprocess
import sys
from pathlib import Path

from unicorn import UC_ARCH_X86, UC_MODE_16, Uc, UC_HOOK_INTR
from unicorn.x86_const import *

ROOT = Path(__file__).resolve().parents[2]
REGS = [UC_X86_REG_AX, UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_BX, UC_X86_REG_SP, UC_X86_REG_BP, UC_X86_REG_SI, UC_X86_REG_DI]
SREGS = [UC_X86_REG_ES, UC_X86_REG_CS, UC_X86_REG_SS, UC_X86_REG_DS]
NAMES = ["AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI", "ES", "CS", "SS", "DS", "FL"]


def main(snap, off_hex):
    data = Path(snap).read_bytes()
    regs = list(struct.unpack_from("<13H", data, 0)); mem = bytearray(data[26:26 + 0x100000])
    # BIOS handlers are native in mtv-tool; give Unicorn an IRET at each BIOS vector target (the runtime's BIOS
    # INT 8 also advances the tick count at 0040:006C, so that word may differ).
    for n in range(256):  # DosMachine's BIOS vectors are F000:(n * 4); programs keep copies of them
        mem[0xF0000 + n * 4] = 0xCF
    uc = Uc(UC_ARCH_X86, UC_MODE_16); uc.mem_map(0, 0x110000); uc.mem_write(0, bytes(mem))
    for i, r in enumerate(REGS): uc.reg_write(r, regs[i])
    for i, r in enumerate(SREGS): uc.reg_write(r, regs[8 + i])
    uc.reg_write(UC_X86_REG_FLAGS, regs[12])
    sp_lin = regs[10] * 16 + regs[4]
    ret_ip, ret_cs = struct.unpack_from("<HH", mem, sp_lin)
    cs = regs[9]
    ip = int(off_hex[5:], 16) if off_hex.startswith("blob:") else int(off_hex, 16) + 0x824 * 16 - cs * 16
    def intr(uc, n, _):
        # minimal DOS shared with mtv-tool call-fn: memory block resize always succeeds
        if n == 0x21 and (uc.reg_read(UC_X86_REG_AX) >> 8) == 0x4A:
            uc.reg_write(UC_X86_REG_FLAGS, uc.reg_read(UC_X86_REG_FLAGS) & ~1)
            return
        raise RuntimeError(f"interrupt {n} AX={uc.reg_read(UC_X86_REG_AX):04X}")
    uc.hook_add(UC_HOOK_INTR, intr)
    from unicorn import UC_HOOK_INSN
    from unicorn.x86_const import UC_X86_INS_IN, UC_X86_INS_OUT
    outs = []
    def port_in(uc, port, size, _):
        return 0
    def port_out(uc, port, size, value, _):
        outs.append((port, value & 0xFF))
    uc.hook_add(UC_HOOK_INSN, port_in, None, 1, 0, UC_X86_INS_IN)
    uc.hook_add(UC_HOOK_INSN, port_out, None, 1, 0, UC_X86_INS_OUT)
    # stop on return: near (ret_ip in CS) or far (ret_cs:ret_ip) - run until either linear address
    targets = {cs * 16 + ret_ip, ret_cs * 16 + ret_ip}
    uc.reg_write(UC_X86_REG_IP, ip)
    from unicorn import UC_HOOK_CODE
    start_sp = regs[4]
    def code(uc, address, size, _):
        # returned when execution reaches the return address with the return address popped
        if address in targets and uc.reg_read(UC_X86_REG_SP) > start_sp:
            uc.emu_stop()
    for t in targets:
        uc.hook_add(UC_HOOK_CODE, code, None, t, t)
    try:
        uc.emu_start(cs * 16 + ip, 0xFFFFFFFF, count=50_000_000)
    except Exception as e:
        print("unicorn:", e); return 2
    want = [uc.reg_read(r) for r in REGS + SREGS] + [uc.reg_read(UC_X86_REG_FLAGS)]
    want_mem = uc.mem_read(0, 0x100000)
    r = subprocess.run([str(ROOT / "build/macos-debug/mtv-tool"), "call-fn", snap, off_hex], capture_output=True, text=True)
    if r.returncode:
        print("translated run failed:", r.stderr); return 1
    out = Path(snap + ".out").read_bytes()
    got = list(struct.unpack_from("<13H", out, 0)); got_mem = out[26:26 + 0x100000]
    bad = [f"{NAMES[i]} {got[i]:04X} vs {want[i]:04X}" for i in range(12) if got[i] != want[i]]
    if (got[12] ^ want[12]) & 0x8D5:
        bad.append(f"FL {got[12]:04X} vs {want[12]:04X}")
    diffs = [i for i in range(0x100000) if got_mem[i] != want_mem[i] and not (0xF0000 <= i or 0x46C <= i < 0x470)]
    got_ports = [tuple(int(x, 16) for x in l.split()) for l in Path(snap + ".ports").read_text().split("\n") if l]
    if got_ports != outs:
        bad.append(f"port writes differ: {len(got_ports)} vs {len(outs)}, first {next(((i, g, w) for i, (g, w) in enumerate(zip(got_ports, outs)) if g != w), None)}")
    print(f"registers: {'ok' if not bad else bad}; memory bytes different: {len(diffs)} {[hex(d) for d in diffs[:10]]}")
    return 0 if not bad and not diffs else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
