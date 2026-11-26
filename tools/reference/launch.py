"""Run the original Mad TV in the patched DOSBox-X reference build.

The game directory is mounted read-only as D: and copied to a private writable C:
so the original files are never modified.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/dosbox-mac"))

DEFAULT_CYCLES = 8000


def make_config(data_dir, drive_dir, program, cycles=DEFAULT_CYCLES, capture_dir=None):
    if not re.fullmatch(r"[A-Za-z0-9_]{1,8}\.(EXE|COM|BAT|exe|com|bat)", program):
        raise ValueError("program must be a plain DOS file name")
    capture = f"captures={capture_dir}\n" if capture_dir else ""
    return f"""[sdl]
fullscreen=false
output=surface
waitonerror=false
showmenu=false
autolock=false
mouse_emulation=always
usesystemcursor=false
[dosbox]
machine=vgaonly
memsize=4
fastbioslogo=true
{capture}[render]
frameskip=0
aspect=false
scaler=none
[cpu]
core=normal
cputype=386
cycles=fixed {int(cycles)}
[mixer]
rate=44100
nosound=true
[sblaster]
sbtype=sb16
sbbase=220
irq=7
dma=1
hdma=5
oplmode=opl2
[speaker]
pcspeaker=true
[dos]
xms=true
ems=true
[autoexec]
@echo off
mount c "{drive_dir}"
mount d "{data_dir}" -ro
copy d:\\*.* c:\\
date 02-10-1994
time 12:00:00
c:
{program}
"""


def session(work_dir, data_dir, program="MTV.EXE", cycles=DEFAULT_CYCLES, replace=None):
    """Return a client.Session running `program`. `replace` maps DOS names to host files copied over C: first."""
    from client import Session
    work = Path(work_dir).resolve()
    drive = work / "drive-c"
    conf = make_config(Path(data_dir).resolve(), drive, program, cycles, capture_dir=work)
    if replace:
        stage = work / "replace"
        stage.mkdir(parents=True, exist_ok=True)
        copies = [f'mount e "{stage}" -ro']
        for dos_name, host in replace.items():
            (stage / dos_name).write_bytes(Path(host).read_bytes())
            copies.append(f"copy e:\\{dos_name} c:\\{dos_name}")
        head, tail = conf.rsplit("date 02-10-1994\n", 1)
        tail = "date 02-10-1994\n" + tail
        conf = head + "\n".join(copies) + "\n" + tail
    return Session(work, conf)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=ROOT / "mad-tv")
    parser.add_argument("--work", type=Path, default=ROOT / "work/reference/interactive")
    parser.add_argument("--program", default="MTV.EXE")
    parser.add_argument("--cycles", type=int, default=DEFAULT_CYCLES)
    args = parser.parse_args()
    with session(args.work, args.data, args.program, args.cycles) as s:
        print(json.dumps({"socket": str(s.socket_path), "status": s.command("status")}), flush=True)
        for line in sys.stdin:
            try:
                print(json.dumps(s.command(line.rstrip("\n"))), flush=True)
            except (RuntimeError, ValueError) as error:
                print(json.dumps({"ok": False, "error": str(error)}), flush=True)
            if s.process.poll() is not None or line.strip() == "quit":
                break


if __name__ == "__main__":
    main()
