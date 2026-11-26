"""Upscaler pilot: run candidate methods on a fixed set of frames and build side-by-side comparisons.

Usage: python3 tools/art/pilot.py --images work/images --out work/pilot
Inputs come from `mtv-tool export-images` (RGBA PNGs; alpha 0 = colour index 0).
"""
import argparse
import hashlib
import html
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
PILOT = [  # (png name, masked?, label)
    ("slot051-f000.png", False, "room background"),
    ("slot062-f000.png", False, "room background"),
    ("slot305-f000.png", False, "street backdrop"),
    ("slot017-f000.png", True, "walking character"),
    ("slot181-f000.png", True, "large character"),
    ("slot012-f000.png", False, "UI panel"),
    ("slot009-f000.png", False, "UI panel"),
    ("slot002-f000.png", True, "cursor"),
    ("font", True, "font sheet (slot 3)"),
    ("screen", False, "whole screen from the original"),
]


def dilate_into_transparent(rgba):
    """Fill transparent pixels with the colour of an opaque neighbour so upscalers see no dark fringe."""
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


def run_runner(src, dst, model, scale):
    subprocess.run([str(RUNNER), "-i", str(src), "-o", str(dst), "-n", model, "-s", str(scale), "-m", str(MODELS)],
                   check=True, capture_output=True)


def ai(rgb, steps):
    with tempfile.TemporaryDirectory() as t:
        cur = Path(t) / "in.png"
        Image.fromarray(rgb).save(cur)
        for i, (model, s) in enumerate(steps):
            nxt = Path(t) / f"s{i}.png"
            run_runner(cur, nxt, model, s)
            cur = nxt
        return np.array(Image.open(cur).convert("RGB"))


def resize(rgb, size, method):
    return np.array(Image.fromarray(rgb).resize(size, method))


METHODS = {
    "nearest": lambda rgb, w, h: resize(rgb, (w * SCALE, h * SCALE), Image.NEAREST),
    "animevideov3 x3+x2": lambda rgb, w, h: ai(rgb, [("realesr-animevideov3", 3), ("realesr-animevideov3", 2)]),
    "nearest x2 + animevideov3 x3": lambda rgb, w, h: ai(resize(rgb, (w * 2, h * 2), Image.NEAREST), [("realesr-animevideov3", 3)]),
    "x4plus-anime + lanczos": lambda rgb, w, h: resize(ai(rgb, [("realesrgan-x4plus-anime", 4)]), (w * SCALE, h * SCALE), Image.LANCZOS),
    "x4plus + lanczos": lambda rgb, w, h: resize(ai(rgb, [("realesrgan-x4plus", 4)]), (w * SCALE, h * SCALE), Image.LANCZOS),
}


def load_inputs(images):
    out = []
    for name, masked, label in PILOT:
        if name == "font":
            frames = [Image.open(images / f"slot003-f{i:03d}.png").convert("RGBA") for i in range(118)]
            sheet = Image.new("RGBA", (5 * 40, 8 * 3), (0, 0, 0, 0))
            for i, f in enumerate(frames[:120]):
                sheet.paste(f, ((i % 40) * 5, (i // 40) * 8))
            out.append((name, masked, label, np.array(sheet)))
        elif name == "screen":
            shot = Image.open(ROOT / "work/runs/smoke-a/frame-0002000.png").convert("RGB").resize((320, 200), Image.NEAREST)
            out.append((name, False, label, np.array(shot.convert("RGBA"))))
        else:
            out.append((name, masked, label, np.array(Image.open(images / name).convert("RGBA"))))
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--images", type=Path, default=ROOT / "work/images")
    p.add_argument("--out", type=Path, default=ROOT / "work/pilot")
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    rows = []
    for name, masked, label, rgba in load_inputs(a.images):
        h, w = rgba.shape[:2]
        rgb = dilate_into_transparent(rgba) if masked else rgba[..., :3].copy()
        a_src = np.dstack([rgba[..., 3]] * 3)
        cells = []
        for method, fn in METHODS.items():
            up = fn(rgb, w, h)
            if up.shape[:2] != (h * SCALE, w * SCALE):
                up = resize(up, (w * SCALE, h * SCALE), Image.LANCZOS)
            if masked:
                # The mask goes through the same method, then is thresholded back to fully opaque / transparent.
                a_up = fn(a_src, w, h)
                if a_up.shape[:2] != (h * SCALE, w * SCALE):
                    a_up = resize(a_up, (w * SCALE, h * SCALE), Image.LANCZOS)
                alpha = np.where(a_up[..., 0] >= 128, 255, 0).astype(np.uint8)
            else:
                alpha = np.full((h * SCALE, w * SCALE), 255, np.uint8)
            out = np.dstack([up, alpha])
            fn_out = f"{Path(name).stem}-{method.replace(' ', '_').replace('+', 'p')}.png"
            Image.fromarray(out, "RGBA").save(a.out / fn_out)
            cells.append((method, fn_out))
        rows.append((label, name, w, h, cells))
    page = ["<!doctype html><meta charset=utf-8><title>Mad TV upscaler pilot</title>",
            "<style>body{background:#222;color:#ddd;font:14px sans-serif}img{image-rendering:auto;background:#f0f;display:block}td{vertical-align:top;padding:6px}</style>",
            "<h1>Mad TV upscaler pilot (6x)</h1><p>Each row: one source; columns: methods at 1:1 output pixels. Magenta = transparent.</p><table>"]
    for label, name, w, h, cells in rows:
        page.append(f"<tr><td colspan={len(cells)}><h2>{html.escape(label)} ({w}x{h})</h2></td></tr><tr>")
        for method, f in cells:
            page.append(f"<td><b>{html.escape(method)}</b><img src='{f}'></td>")
        page.append("</tr>")
    (a.out / "index.html").write_text("\n".join(page) + "</table>\n")
    lock = {"runner": {"path": str(RUNNER.relative_to(ROOT)), "sha256": hashlib.sha256(RUNNER.read_bytes()).hexdigest()},
            "models": {m.name: hashlib.sha256(m.read_bytes()).hexdigest() for m in sorted(MODELS.glob("*"))}}
    (a.out / "lock.json").write_text(json.dumps(lock, indent=1))
    print(a.out / "index.html")


if __name__ == "__main__":
    main()
