"""Check that every distinct reference frame appears, in order, among the native frames.

Reference frames come from DOSBox (`frame` PNGs, any size; 320x400 is row-doubled), native ones from mtv-tool.
Prints matched/total distinct reference frames and the first mismatch.
"""
import hashlib
import sys
from pathlib import Path

from PIL import Image


def norm(path):
    im = Image.open(path).convert("RGB")
    if im.size == (320, 400):
        im = im.resize((320, 200), Image.NEAREST)
    if im.size != (320, 200):
        return None
    return im.tobytes()


def distinct(paths):
    out, last = [], None
    for p in paths:
        b = norm(p)
        if b is None or b == last:
            continue
        out.append((p, b))
        last = b
    return out


def main(ref_dir, nat_dir):
    ref = distinct(sorted(Path(ref_dir).glob("*.png")))
    nat = distinct(sorted(Path(nat_dir).glob("*.png")))
    index = {}
    for i, (_, b) in enumerate(nat):
        index.setdefault(hashlib.sha256(b).digest(), []).append(i)
    pos, matched, first_bad = -1, 0, None
    for p, b in ref:
        cands = [i for i in index.get(hashlib.sha256(b).digest(), []) if i > pos]
        if cands:
            pos = cands[0]; matched += 1
        elif first_bad is None:
            first_bad = p
    print(f"distinct reference frames: {len(ref)}, matched in order: {matched}, native distinct: {len(nat)}")
    if first_bad:
        print("first unmatched:", first_bad)
    return matched == len(ref)


if __name__ == "__main__":
    sys.exit(0 if main(sys.argv[1], sys.argv[2]) else 1)
