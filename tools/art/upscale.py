"""Production 6x upscale, as chosen by the user in the art pilot (2026-11-25):
nearest-neighbour 2x, then Real-ESRGAN AnimeVideo v3 x3. Transparent pixels are filled from opaque
neighbours first; the alpha mask goes through the same method and is thresholded at 128.

CLI: python3 tools/art/upscale.py --in <dir of RGBA PNGs> --out <dir> [--masked-list file]
"""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "tools/upscalers/realesrgan-20220424/realesrgan-ncnn-vulkan"
MODELS = ROOT / "tools/upscalers/realesrgan-20220424/models"
SCALE = 6
METHOD = {"pre": "nearest x2", "model": "realesr-animevideov3", "model_scale": 3, "mask": "same method, threshold 128"}


def dilate_into_transparent(rgba):
    rgb = rgba[..., :3].astype(np.int32).copy()
    known = rgba[..., 3] > 0
    if known.all() or not known.any():
        return rgb.astype(np.uint8)
    for _ in range(64):
        if known.all():
            break
        acc = np.zeros_like(rgb); cnt = np.zeros(known.shape, np.int32)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            k = np.roll(known, (dy, dx), (0, 1)); c = np.roll(rgb, (dy, dx), (0, 1))
            acc += c * k[..., None]; cnt += k
        new = (~known) & (cnt > 0)
        rgb[new] = acc[new] // cnt[new][:, None]
        known = known | new
    return rgb.astype(np.uint8)


def animevideov3_x3(rgb):
    with tempfile.TemporaryDirectory() as t:
        src, dst = Path(t) / "in.png", Path(t) / "out.png"
        Image.fromarray(rgb).save(src)
        subprocess.run([str(RUNNER), "-i", str(src), "-o", str(dst), "-n", METHOD["model"], "-s", "3", "-m", str(MODELS)],
                       check=True, capture_output=True)
        return np.array(Image.open(dst).convert("RGB"))


def _method(rgb, ai):
    pre = np.repeat(np.repeat(rgb, 2, 0), 2, 1)
    return ai(pre)


def upscale_rgba(rgba, masked, ai=animevideov3_x3):
    h, w = rgba.shape[:2]
    rgb = dilate_into_transparent(rgba) if masked else rgba[..., :3].copy()
    up = _method(rgb, ai)
    assert up.shape[:2] == (h * SCALE, w * SCALE), up.shape
    if masked:
        a = _method(np.dstack([rgba[..., 3]] * 3), ai)
        alpha = np.where(a[..., 0] >= 128, 255, 0).astype(np.uint8)
    else:
        alpha = np.full((h * SCALE, w * SCALE), 255, np.uint8)
    return np.dstack([up, alpha])


def lock():
    return {"method": METHOD, "scale": SCALE, "runner_sha256": hashlib.sha256(RUNNER.read_bytes()).hexdigest(),
            "model_sha256": hashlib.sha256((MODELS / "realesr-animevideov3-x3.bin").read_bytes()).hexdigest(),
            "param_sha256": hashlib.sha256((MODELS / "realesr-animevideov3-x3.param").read_bytes()).hexdigest()}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--in", dest="src", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--opaque", nargs="*", default=[], help="PNG names drawn opaque (alpha ignored)")
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    for f in sorted(a.src.glob("*.png")):
        rgba = np.array(Image.open(f).convert("RGBA"))
        Image.fromarray(upscale_rgba(rgba, masked=f.name not in a.opaque), "RGBA").save(a.out / f.name)
    (a.out / "upscale-lock.json").write_text(json.dumps(lock(), indent=1) + "\n")


if __name__ == "__main__":
    main()
