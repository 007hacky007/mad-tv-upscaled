"""Visit every room of the building in the translated game and fail on any program fault.

New game (English), leave the boss with a right click, open the info board next to the lift, then click each of its
entries in a separate run; the figure walks to that room and enters it. Writes the last frame of each run and a
contact sheet to --out. Exit status 1 when any run stops with a fault or exits.

With --pack, each run also saves the upscaled picture (half size) and a second contact sheet of those.

Usage: room_tour.py --tool <mtv-tool> --data <game dir> --out <dir> [--jobs N] [--pack <dir>]
"""
import argparse
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ENTER, SPACE = 0x1C0D, 0x3920
BASE = [
    {"frame": 100, "keys": [0x1265]},         # "e" at the language prompt
    {"frame": 400, "keys": [ENTER]},          # title
    {"frame": 700, "keys": [SPACE]},          # player select
    {"frame": 900, "mouse": [63, 100]},       # "New game"
    {"frame": 920, "buttons": 1}, {"frame": 925, "buttons": 0},
    {"frame": 1900, "mouse": [160, 60]},      # next boss message
    {"frame": 1920, "buttons": 1}, {"frame": 1925, "buttons": 0},
    {"frame": 2100, "buttons": 2}, {"frame": 2105, "buttons": 0},  # leave the boss
    {"frame": 2600, "buttons": 2}, {"frame": 2605, "buttons": 0},
    {"frame": 2700, "mouse": [150, 5]},       # info board
    {"frame": 2720, "buttons": 1}, {"frame": 2725, "buttons": 0},
]
BOARD = [(x, y) for y in range(9, 113, 8) for x in (30, 110, 190, 270)]
FRAMES = 8000


def run(tool, data, out, point, pack=None):
    x, y = point
    d = out / f"{x:03d}_{y:03d}"
    d.mkdir(parents=True, exist_ok=True)
    script = BASE + [{"frame": 4800, "mouse": [x, y]}, {"frame": 4820, "buttons": 1}, {"frame": 4825, "buttons": 0}]
    (d / "script.json").write_text(json.dumps(script))
    r = subprocess.run([tool, "run-game", "--data", data, "--out", str(d), "--frames", str(FRAMES),
                        "--every", str(FRAMES - 1), "--script", str(d / "script.json")], capture_output=True, text=True,
                       env={**os.environ, **({"MTV_PACK": pack} if pack else {})})
    faults = [l for l in r.stderr.splitlines() if "stopped" in l or "exited" in l]
    if r.returncode:
        faults.append(f"mtv-tool exit status {r.returncode}")
    return point, d / f"f{FRAMES - 1:05d}.png", faults


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--pack")
    a = ap.parse_args()
    out = Path(a.out)
    with ThreadPoolExecutor(a.jobs) as ex:
        results = list(ex.map(lambda p: run(a.tool, a.data, out, p, a.pack), BOARD))
    bad = [(p, f) for p, _, f in results if f]
    for p, f in bad:
        print(f"{p}: {'; '.join(f)}")
    try:
        from PIL import Image
        shots = [Image.open(s).convert("RGB") for _, s, _ in results if s.exists()]
        sheet = Image.new("RGB", (320 * 8, 200 * ((len(shots) + 7) // 8)))
        for i, im in enumerate(shots):
            sheet.paste(im, ((i % 8) * 320, (i // 8) * 200))
        sheet.save(out / "sheet.png")
        ups = [Image.open(s.parent / f"u{FRAMES - 1:05d}.png").convert("RGB") for _, s, _ in results if (s.parent / f"u{FRAMES - 1:05d}.png").exists()]
        if ups:
            usheet = Image.new("RGB", (960 * 4, 600 * ((len(ups) + 3) // 4)))
            for i, im in enumerate(ups):
                usheet.paste(im, ((i % 4) * 960, (i // 4) * 600))
            usheet.save(out / "sheet-upscaled.png")
    except ImportError:
        pass
    print(f"{len(results) - len(bad)}/{len(results)} runs without faults")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
