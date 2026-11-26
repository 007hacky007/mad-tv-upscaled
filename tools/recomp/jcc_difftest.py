"""Every conditional branch condition used by the translator must decide like the CPU (Unicorn) for all flag states."""
import subprocess
import sys
import tempfile
from pathlib import Path

from unicorn import UC_ARCH_X86, UC_MODE_16, Uc
from unicorn.x86_const import UC_X86_REG_CS, UC_X86_REG_FLAGS, UC_X86_REG_IP, UC_X86_REG_CX

sys.path.insert(0, str(Path(__file__).resolve().parent))
import x86recomp as X  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OPC = {"jo": 0x70, "jno": 0x71, "jb": 0x72, "jae": 0x73, "je": 0x74, "jne": 0x75, "jbe": 0x76, "ja": 0x77, "js": 0x78, "jns": 0x79,
       "jp": 0x7A, "jnp": 0x7B, "jl": 0x7C, "jge": 0x7D, "jle": 0x7E, "jg": 0x7F}
names = sorted(X.JCC)
src = '#include "rt/cpu.h"\n#include <cstdio>\nusing namespace mtv::rt;\nint main(){unsigned f; while(std::scanf("%u",&f)==1){Cpu c; c.set_flags(uint16_t(f));'
src += "".join(f'std::printf("%d ", ({X.JCC[n]}) ? 1 : 0);' for n in names) + 'std::printf("\\n");}}\n'
work = Path(tempfile.mkdtemp())
(work / "j.cpp").write_text(src)
subprocess.run(["clang++", "-std=c++20", f"-I{ROOT / 'src'}", str(work / "j.cpp"), "-o", str(work / "j")], check=True)
flags_list = [f | 2 for f in range(0, 0x1000) if (f & ~0x8D5) == 0]
out = subprocess.run([str(work / "j")], input="\n".join(map(str, flags_list)), capture_output=True, text=True).stdout.split("\n")
bad = 0
for f, line in zip(flags_list, out):
    got = list(map(int, line.split()))
    for n, g in zip(names, got):
        uc = Uc(UC_ARCH_X86, UC_MODE_16); uc.mem_map(0, 0x100000)
        uc.mem_write(0x10100, bytes([OPC[n], 0x10]))
        uc.reg_write(UC_X86_REG_CS, 0x1000); uc.reg_write(UC_X86_REG_FLAGS, f)
        uc.emu_start(0x100, 0x10112, count=1)
        taken = uc.reg_read(UC_X86_REG_IP) == 0x112
        if taken != bool(g):
            bad += 1
            print("FAIL", n, hex(f), "cpu", taken, "translated", g)
print(f"conditions: {len(names)}, flag states: {len(flags_list)}, mismatches: {bad}")
sys.exit(1 if bad else 0)
