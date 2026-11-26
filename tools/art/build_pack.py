"""Build the upscale pack from the user's game files: every image frame at 6x, as chosen in the art pilot.

Same method as tools/art/upscale.py (nearest 2x, Real-ESRGAN AnimeVideo v3 x3, transparent pixels filled from
opaque neighbours first, the mask through the same method and thresholded at 128), but each runner pass handles
a whole directory so the model loads once.

Output: <out>/<slot>-<frame>.png (RGBA, 6x) and <out>/pack.json:
  {"schema_version": 1, "scale": 6, "lock": {...}, "images": [{"slot", "frame", "width", "height", "png"}]}

CLI: python3 tools/art/build_pack.py --tool <mtv-tool> --data <game dir> --out <pack dir> [--work <dir>] [--limit N]
"""
import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import upscale  # noqa: E402

SCALE = upscale.SCALE


def run_runner(src, dst):
    dst.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(upscale.RUNNER), "-i", str(src), "-o", str(dst), "-n", upscale.METHOD["model"],
                    "-s", str(upscale.METHOD["model_scale"]), "-m", str(upscale.MODELS), "-f", "png"],
                   check=True, capture_output=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--tool", required=True)
    p.add_argument("--data", required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--work", type=Path)
    p.add_argument("--limit", type=int, default=0, help="only the first N images (for tests)")
    a = p.parse_args()
    work = a.work or Path(tempfile.mkdtemp(prefix="mtv-pack-"))
    src, rgb, mask = work / "src", work / "rgb", work / "mask"
    for d in (src, rgb, mask):
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
    subprocess.run([a.tool, "export-images", "--data", a.data, "--out", str(src)], check=True, capture_output=True)
    names = sorted(f.name for f in src.glob("slot*.png"))
    if a.limit:
        names = names[:a.limit]
    sizes = {}
    for n in names:
        rgba = np.array(Image.open(src / n).convert("RGBA"))
        sizes[n] = rgba.shape[:2]
        filled = upscale.dilate_into_transparent(rgba)
        Image.fromarray(np.repeat(np.repeat(filled, 2, 0), 2, 1)).save(rgb / n)
        alpha = np.dstack([rgba[..., 3]] * 3)
        Image.fromarray(np.repeat(np.repeat(alpha, 2, 0), 2, 1)).save(mask / n)
    run_runner(rgb, work / "rgb-up")
    run_runner(mask, work / "mask-up")
    a.out.mkdir(parents=True, exist_ok=True)
    images = []
    for n in names:
        h, w = sizes[n]
        up = np.array(Image.open(work / "rgb-up" / n).convert("RGB"))
        m = np.array(Image.open(work / "mask-up" / n).convert("RGB"))[..., 0]
        if up.shape[:2] != (h * SCALE, w * SCALE) or m.shape != (h * SCALE, w * SCALE):
            raise RuntimeError(f"{n}: runner output {up.shape} for a {w}x{h} image")
        alpha = np.where(m >= 128, 255, 0).astype(np.uint8)
        slot, frame = int(n[4:7]), int(n[9:12])
        out_name = f"{slot:03d}-{frame:03d}.png"
        Image.fromarray(np.dstack([up, alpha]), "RGBA").save(a.out / out_name)
        images.append({"slot": slot, "frame": frame, "width": w, "height": h, "png": out_name})
    pack = {"schema_version": 1, "scale": SCALE, "lock": upscale.lock(), "images": images}
    (a.out / "pack.json").write_text(json.dumps(pack, indent=1) + "\n")
    print(f"{len(images)} images")


if __name__ == "__main__":
    main()
