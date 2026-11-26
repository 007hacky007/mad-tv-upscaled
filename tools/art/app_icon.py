"""Draw the app icon: original 32x32 pixel art (a 1990s TV set with rabbit ears), no game pixels.

Writes packaging/icons/app-icon.png (1024x1024), app-icon.icns and app-icon.ico.
CLI: python3 tools/art/app_icon.py
"""
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "packaging/icons"

BG = [(52, 36, 96), (64, 44, 116), (78, 52, 136), (92, 60, 152)]
WOOD, WOOD2, WOOD3 = (150, 84, 40), (184, 110, 56), (104, 56, 24)
SCREEN = [(40, 140, 220), (56, 168, 236), (88, 196, 248)]
GLASS, RED, RED2, YELLOW = (200, 236, 255), (228, 40, 40), (255, 96, 72), (255, 216, 64)
METAL, METAL2, OUTLINE = (208, 208, 216), (140, 140, 156), (16, 12, 24)


def pixels():
    im = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, 31, 31), radius=6, fill=BG[0])
    for i, c in enumerate(BG):
        d.rectangle((1, 1 + i * 8, 30, 8 + i * 8), fill=c)
    d.rounded_rectangle((0, 0, 31, 31), radius=6, outline=OUTLINE)
    # rabbit ears
    d.line((15, 10, 9, 3), fill=METAL, width=1)
    d.line((16, 10, 23, 2), fill=METAL, width=1)
    d.point([(9, 3), (23, 2)], fill=YELLOW)
    d.ellipse((13, 8, 18, 11), fill=METAL2)
    # cabinet
    d.rounded_rectangle((3, 10, 28, 27), radius=2, fill=WOOD, outline=OUTLINE)
    d.line((4, 11, 27, 11), fill=WOOD2)
    d.line((4, 26, 27, 26), fill=WOOD3)
    # screen with a rounded CRT edge
    d.rounded_rectangle((5, 12, 20, 25), radius=3, fill=OUTLINE)
    d.rounded_rectangle((6, 13, 19, 24), radius=3, fill=SCREEN[0])
    d.rectangle((7, 14, 18, 17), fill=SCREEN[1])
    d.rectangle((7, 14, 9, 15), fill=GLASS)
    # a big red M on the screen
    for x0 in (8, 16):
        d.line((x0, 22, x0, 16), fill=RED)
        d.line((x0 + 1, 22, x0 + 1, 16), fill=RED)
    d.line((10, 17, 12, 19), fill=RED)
    d.line((15, 17, 13, 19), fill=RED)
    d.point([(12, 20), (13, 20)], fill=RED)
    d.point([(8, 16), (16, 16)], fill=RED2)
    # side panel: two knobs and a speaker grille
    d.rectangle((21, 13, 26, 25), fill=WOOD3)
    d.ellipse((22, 14, 25, 17), fill=METAL, outline=OUTLINE)
    d.ellipse((22, 18, 25, 21), fill=METAL, outline=OUTLINE)
    d.point([(23, 15), (24, 20)], fill=OUTLINE)
    for y in (23, 25):
        d.line((22, y, 25, y), fill=WOOD2)
    # legs
    d.line((6, 28, 5, 29), fill=OUTLINE)
    d.line((25, 28, 26, 29), fill=OUTLINE)
    return im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    big = pixels().resize((1024, 1024), Image.NEAREST)
    big.save(OUT / "app-icon.png")
    big.save(OUT / "app-icon.icns", format="ICNS", sizes=[(s, s) for s in (16, 32, 64, 128, 256, 512, 1024)])
    big.save(OUT / "app-icon.ico", format="ICO", sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    print(OUT / "app-icon.png")


if __name__ == "__main__":
    main()
