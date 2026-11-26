"""Play a new game without doing anything until the boss fires the player, and fail on any program fault.

New game (English), leave the boss, then no input. The station broadcasts nothing, the boss fires the player
on day 6 ("You're FIRED!"), the building is blown up and THE END stays on screen. Checks that no run faults,
that at least five days passed and that the last two sampled frames are the same still end screen.

Usage: idle_to_dismissal.py --tool <mtv-tool> --data <game dir> --out <dir>
"""
import argparse
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

ENTER, SPACE = 0x1C0D, 0x3920
SCRIPT = [
    {"frame": 100, "keys": [0x1265]}, {"frame": 400, "keys": [ENTER]}, {"frame": 700, "keys": [SPACE]},
    {"frame": 900, "mouse": [63, 100]}, {"frame": 920, "buttons": 1}, {"frame": 925, "buttons": 0},
    {"frame": 1900, "mouse": [160, 60]}, {"frame": 1920, "buttons": 1}, {"frame": 1925, "buttons": 0},
    {"frame": 2100, "buttons": 2}, {"frame": 2105, "buttons": 0}, {"frame": 2600, "buttons": 2}, {"frame": 2605, "buttons": 0},
]
FRAMES, EVERY = 480001, 20000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "script.json").write_text(json.dumps(SCRIPT))
    r = subprocess.run([a.tool, "run-game", "--data", a.data, "--out", str(out), "--frames", str(FRAMES), "--every", str(EVERY),
                        "--script", str(out / "script.json")], capture_output=True, text=True, env={"MTV_DSDUMP": str(EVERY)})
    faults = [l for l in r.stderr.splitlines() if "stopped" in l or "exited" in l]
    days = []
    for f in range(0, FRAMES, EVERY):
        p = out / f"ds{f:05d}.bin"
        if p.exists():
            days.append(struct.unpack_from("<h", p.read_bytes(), 0x71E4)[0])
    last = [hashlib.sha256((out / f"f{f:05d}.png").read_bytes()).hexdigest() for f in (FRAMES - 1 - EVERY, FRAMES - 1)]
    ok = not faults and r.returncode == 0 and max(days, default=0) >= 5 and last[0] == last[1]
    print(f"faults: {faults or 'none'}; days reached: {max(days, default=0)}; end screen still: {last[0] == last[1]}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
