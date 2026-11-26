"""Generate all translated code from the user's game files (run by CMake at build time).

python tools/recomp/generate.py --game <dir with MTV.EXE and SOURCE1.RSC> --out <build>/gen
The output is derived from the copyrighted program: it lives only in the build tree and is never committed.
"""
import argparse
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools/recomp"))
CHUNKS = 16
DRIVERS = {  # slot: (prefix, extra translator arguments)
    98: ("pal98", ["--raw-entries", "40,71,40c,7e,93,bf,f3,134,13c,141,1f4,1f8,3a8,432,516", "--table", "1492=3"]),
    # 516: the timer handler calls the BIOS INT 8 through a hand-built frame (pushf; push cs; push 516h; jmp far)
    # and continues there after the BIOS IRET.
    95: ("pal95", ["--raw-entries", "40,6a,2e5,77,92,ad,e1,122,12a,12f,153,157,28c,359", "--targets", "70c:10"]),
    99: ("pal99", ["--raw-entries", "24"]),
}


def unlzexe(data):
    sys.path.insert(0, str(ROOT / "tools/recomp"))
    from unlzexe import unpack
    return unpack(data)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--game", required=True, type=Path)
    p.add_argument("--out", required=True, type=Path)
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    tmp = Path(tempfile.mkdtemp(prefix="mtv-gen-"))
    exe = tmp / "MTV.EXE"
    exe.write_bytes(unlzexe((a.game / "MTV.EXE").read_bytes()))
    rsc = (a.game / "SOURCE1.RSC").read_bytes()
    tr = [sys.executable, str(ROOT / "tools/recomp/x86recomp.py")]
    subprocess.run(tr + ["--exe", str(exe), "--load-seg", "0x824", "--functions", str(ROOT / "analysis/function-map.json"),
                         "--out", str(a.out), "--prefix", "mtv", "--chunks", str(CHUNKS), "--ds", "0x39d4",
                         # Turbo C's divide-error handler, installed by the startup code with INT 21h AH=25h (CS:0125)
                         "--entries", "0:125",
                         # pixel provenance for the upscaled renderer: the image decoder (FUN_47b4_04ac) names the
                         # slot, its pixel store (FUN_47b4_016d) the frame, x and y
                         "--hooks", "37fec", "--tag-stores", "37c36"], check=True)
    for slot, (prefix, args) in DRIVERS.items():
        lo, hi, size = struct.unpack_from("<3H", rsc, slot * 6)
        blob = tmp / f"{prefix}.bin"
        blob.write_bytes(rsc[(hi << 16 | lo):(hi << 16 | lo) + size])
        subprocess.run(tr + ["--exe", str(blob), "--load-seg", "0", "--functions", "-", "--out", str(a.out), "--prefix", prefix] + args, check=True)
        (a.out / f"{prefix}.sig").write_bytes(blob.read_bytes()[:64])
    sigs = []
    for slot, (prefix, _) in DRIVERS.items():
        sig = (a.out / f"{prefix}.sig").read_bytes()
        sigs.append(f"    {{{slot}, {{{', '.join(str(b) for b in sig)}}}, {prefix}_functions, {prefix}_function_count}}")
    decl = "\n".join(f"extern const FunctionEntry {p}_functions[]; extern const size_t {p}_function_count;" for p, _ in DRIVERS.values())
    (a.out / "blobs.cpp").write_text("#include \"rt/blobs.h\"\nnamespace mtv::rt {\n" + decl + "\nconst BlobModule kBlobModules[] = {\n" + ",\n".join(sigs)
                                     + "\n};\nconst size_t kBlobModuleCount = " + str(len(DRIVERS)) + ";\n}\n")
    print("generated into", a.out)


if __name__ == "__main__":
    main()
