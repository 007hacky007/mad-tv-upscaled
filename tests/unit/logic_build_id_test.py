"""The logic build id must follow the generated code: any change to the translated sources gives a new id.

Environment: MTV_BUILD (build dir with gen/ and logic-build-id.txt).
"""
import hashlib
import os
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ["src/rt/cpu.h", "src/rt/cpu.cpp", "src/rt/machine.h", "src/rt/blobs.h", "src/rt/logic.h", "src/rt/logic_export.cpp"]


def expected_id(build):
    files = sorted((build / "gen").glob("*.cpp")) + sorted((build / "gen").glob("*.h"))
    files = [f for f in files if f.name not in ("state_layout.cpp", "logic_build_id.h")]
    parts = [hashlib.sha256(f.read_bytes()).hexdigest() for f in files]
    parts += [hashlib.sha256((ROOT / r).read_bytes()).hexdigest() for r in RUNTIME]
    return hashlib.sha256(":".join(parts).encode()).hexdigest()[:16]


class LogicBuildId(unittest.TestCase):
    def test_id_covers_the_generated_code(self):
        build = Path(os.environ["MTV_BUILD"])
        self.assertEqual((build / "logic-build-id.txt").read_text().strip(), expected_id(build))


if __name__ == "__main__":
    unittest.main()
