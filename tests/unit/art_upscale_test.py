import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/art"))
import upscale  # noqa: E402


def fake_ai(rgb):  # stands in for the GPU model: nearest x3
    return np.repeat(np.repeat(rgb, 3, 0), 3, 1)


class UpscaleTest(unittest.TestCase):
    def test_output_is_6x(self):
        rgba = np.zeros((2, 3, 4), np.uint8); rgba[..., 3] = 255
        out = upscale.upscale_rgba(rgba, masked=False, ai=fake_ai)
        self.assertEqual(out.shape, (12, 18, 4))

    def test_opaque_alpha_stays_opaque(self):
        rgba = np.zeros((2, 2, 4), np.uint8)  # alpha 0 = colour index 0, drawn opaque for backgrounds
        out = upscale.upscale_rgba(rgba, masked=False, ai=fake_ai)
        self.assertTrue((out[..., 3] == 255).all())

    def test_mask_is_binary_after_threshold(self):
        rgba = np.zeros((2, 2, 4), np.uint8)
        rgba[0, 0] = (200, 10, 10, 255)
        out = upscale.upscale_rgba(rgba, masked=True, ai=fake_ai)
        self.assertTrue(set(np.unique(out[..., 3])) <= {0, 255})
        self.assertEqual(out[0, 0, 3], 255)
        self.assertEqual(out[11, 11, 3], 0)

    def test_transparent_pixels_take_neighbour_colour(self):
        rgba = np.zeros((1, 3, 4), np.uint8)
        rgba[0, 0] = (0, 200, 0, 255)
        rgb = upscale.dilate_into_transparent(rgba)
        self.assertEqual(tuple(rgb[0, 2]), (0, 200, 0))

    def test_method_is_pinned(self):
        self.assertEqual(upscale.METHOD, {"pre": "nearest x2", "model": "realesr-animevideov3", "model_scale": 3, "mask": "same method, threshold 128"})


if __name__ == "__main__":
    unittest.main()
