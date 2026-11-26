import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/reference"))
import launch  # noqa: E402


class ReferenceLaunchTest(unittest.TestCase):
    def test_config_mounts_data_read_only_copy(self):
        with tempfile.TemporaryDirectory() as tmp:
            data, drive = Path(tmp) / "data", Path(tmp) / "drive"
            conf = launch.make_config(data, drive, "MTV.EXE", cycles=8000)
            autoexec = conf.split("[autoexec]", 1)[1]
            self.assertIn(f'mount c "{drive}"', autoexec)
            self.assertIn(f'mount d "{data}" -ro', autoexec)
            self.assertIn("copy d:\\*.* c:\\", autoexec)
            self.assertTrue(autoexec.rstrip().endswith("MTV.EXE"))

    def test_config_pins_cycles(self):
        conf = launch.make_config(Path("/d"), Path("/c"), "INTRO.EXE", cycles=12345)
        self.assertIn("cycles=fixed 12345", conf)
        self.assertIn("core=normal", conf)

    def test_config_uses_vga_and_adlib_capable_sound(self):
        conf = launch.make_config(Path("/d"), Path("/c"), "MTV.EXE", cycles=8000)
        self.assertIn("machine=vgaonly", conf)
        self.assertIn("sbtype=sb16", conf)
        self.assertIn("oplmode=", conf)

    def test_config_pins_dos_clock_before_program(self):
        conf = launch.make_config(Path("/d"), Path("/c"), "MTV.EXE", cycles=8000)
        autoexec = conf.split("[autoexec]", 1)[1].splitlines()
        self.assertIn("date 02-10-1994", autoexec)
        self.assertIn("time 12:00:00", autoexec)
        self.assertLess(autoexec.index("time 12:00:00"), autoexec.index("MTV.EXE"))

    def test_program_must_be_dos_name(self):
        with self.assertRaises(ValueError):
            launch.make_config(Path("/d"), Path("/c"), "../evil.exe", cycles=8000)

    def test_replacement_files_copied_before_program(self):
        with tempfile.TemporaryDirectory() as tmp:
            host = Path(tmp) / "MTV.EXE"
            host.write_bytes(b"MZ")
            s = launch.session(Path(tmp) / "work", Path(tmp) / "data", "MTV.EXE", replace={"MTV.EXE": host})
            autoexec = s.conf.split("[autoexec]", 1)[1].splitlines()
            self.assertLess(autoexec.index("copy e:\\MTV.EXE c:\\MTV.EXE"), autoexec.index("MTV.EXE"))
            self.assertLess(autoexec.index("copy d:\\*.* c:\\"), autoexec.index("copy e:\\MTV.EXE c:\\MTV.EXE"))
            self.assertEqual((Path(tmp) / "work/replace/MTV.EXE").read_bytes(), b"MZ")


if __name__ == "__main__":
    unittest.main()
