import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/reference"))
import function_map_check  # noqa: E402

VALID = {"crt", "driver_video", "driver_input", "driver_sound", "game", "ui", "data_load", "copy_protection"}


class FunctionMapTest(unittest.TestCase):
    def setUp(self):
        self.map = json.loads((ROOT / "analysis/function-map.json").read_text())
        self.procs = function_map_check.rea_procedures()

    def test_every_procedure_from_rea_is_listed(self):
        listed = {f["address"] for f in self.map["functions"]}
        self.assertEqual(set(self.procs), listed)

    def test_class_values_valid(self):
        for f in self.map["functions"]:
            self.assertIn(f["class"], VALID, f["address"])
            self.assertTrue(f["subsystem"], f["address"])

    def test_no_duplicate_names(self):
        names = [f["name"] for f in self.map["functions"]]
        self.assertEqual(len(names), len(set(names)))

    def test_known_functions_named(self):
        by = {f["address"]: f["name"] for f in self.map["functions"]}
        self.assertEqual(by["0x10545"], "main")
        self.assertEqual(by["0x49cde"], "rand")
        self.assertEqual(by["0x1d809"], "random")


if __name__ == "__main__":
    unittest.main()
