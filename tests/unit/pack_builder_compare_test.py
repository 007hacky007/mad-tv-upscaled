"""The in-app pack builder (mtv-tool build-pack) against tools/art/build_pack.py on the first images.

Environment: MTV_TOOL (mtv-tool), MTV_DATA (the original files), MTV_OUT (scratch folder).
"""
import json
import os
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
LIMIT = 20


def pixels(p):
    return np.array(Image.open(p).convert("RGBA" if Image.open(p).mode == "RGBA" else "RGB"))


class PackBuilderCompare(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tool, cls.data, out = os.environ["MTV_TOOL"], os.environ["MTV_DATA"], Path(os.environ["MTV_OUT"])
        shutil.rmtree(out, ignore_errors=True)
        cls.cpp, cls.py = out / "cpp", out / "py"
        subprocess.run([cls.tool, "build-pack", "--data", cls.data, "--out", str(cls.cpp / "pack"), "--work", str(cls.cpp / "work"),
                        "--limit", str(LIMIT)], check=True)
        subprocess.run([sys.executable, str(ROOT / "tools/art/build_pack.py"), "--tool", cls.tool, "--data", cls.data,
                        "--out", str(cls.py / "pack"), "--work", str(cls.py / "work"), "--limit", str(LIMIT)], check=True)

    def test_preprocessing_matches_python(self):
        for sub in ("rgb", "mask"):
            names = sorted(f.name for f in (self.py / "work" / sub).glob("*.png"))
            self.assertEqual(len(names), LIMIT)
            self.assertEqual(names, sorted(f.name for f in (self.cpp / "work" / sub).glob("*.png")))
            for n in names:
                a, b = pixels(self.py / "work" / sub / n), pixels(self.cpp / "work" / sub / n)
                self.assertTrue(np.array_equal(a, b), f"{sub}/{n}")

    def test_pack_matches_python(self):
        a = json.loads((self.py / "pack" / "pack.json").read_text())
        b = json.loads((self.cpp / "pack" / "pack.json").read_text())
        self.assertEqual(a, b)
        for img in a["images"]:
            pa, pb = pixels(self.py / "pack" / img["png"]), pixels(self.cpp / "pack" / img["png"])
            self.assertEqual(pa.shape, pb.shape, img["png"])
            self.assertTrue(np.array_equal(pa, pb), img["png"])


if __name__ == "__main__":
    unittest.main()
