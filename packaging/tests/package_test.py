"""The release check: build the package from a build directory and make sure it holds nothing of the game.

A package may carry the game logic only encrypted (logic.bin). Every other file must share no run of 48 bytes or
more with the original files or the unpacked MTV.EXE image, must not be the plaintext logic library, and must not
contain translated function symbols.

Environment: MTV_BUILD (build dir), MTV_PLATFORM (macos|windows), MTV_DATA (original files, default mad-tv/),
MTV_TOOL (a host mtv-tool, default build/macos-release/mtv-tool).
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WINDOW, STRIDE = 32, 16  # any shared run of WINDOW + STRIDE = 48 bytes contains an indexed window
SYMBOL = re.compile(rb"mtv_[0-9a-f]{5}[^0-9a-f]|pal9[589]_[0-9a-f]{5}")
# Every file a package may hold (paths relative to MadTV.app or MadTV-win64); anything else is refused.
ALLOWED = [re.compile(p) for p in (
    r"Contents/Info\.plist", r"Contents/MacOS/mtv", r"Contents/Frameworks/libSDL3\.0\.dylib", r"Contents/_CodeSignature/CodeResources",
    r"(Contents/Resources/)?(LICENSE|README\.txt|THIRD_PARTY_NOTICES\.md|logic\.bin)",
    r"(Contents/Resources/)?licenses/[A-Za-z0-9.+-]+\.txt",
    r"(Contents/Resources/)?upscaler/(realesrgan-ncnn-vulkan(\.exe)?|vcomp140\.dll)",
    r"(Contents/Resources/)?upscaler/models/realesr-animevideov3-x3\.(bin|param)",
    r"(mtv\.exe|SDL3\.dll|libc\+\+\.dll|libunwind\.dll)")]
LOGIC_PATHS = {"Contents/Resources/logic.bin", "logic.bin"}


def original_bytes(data, tool):
    """The six original files and the LZEXE-unpacked MTV.EXE."""
    blobs = [p.read_bytes() for p in sorted(Path(data).iterdir()) if p.is_file()]
    with tempfile.TemporaryDirectory() as t:
        out = Path(t) / "MTV.unpacked"
        subprocess.run([tool, "unpack", str(Path(data) / "MTV.EXE"), str(out)], check=True, capture_output=True)
        blobs.append(out.read_bytes())
    return blobs


def window_index(blobs):
    index = set()
    for b in blobs:
        for i in range(0, len(b) - WINDOW + 1, STRIDE):
            w = b[i:i + WINDOW]
            if len(set(w)) >= 8:  # runs of padding or a few repeated values say nothing
                index.add(w)
    return index


def shared_run(data, index):
    for i in range(len(data) - WINDOW + 1):
        w = data[i:i + WINDOW]
        if w in index and len(set(w)) >= 8:
            return i
    return None


def logic_problems(data, build_id):
    if not data.startswith(b"MTVLOGIC") or len(data) < 13:
        return ["logic.bin is not an encrypted logic file"]
    n = data[12]
    head = 13 + n + 24 + 8
    if len(data) < head + 16:
        return ["logic.bin is truncated"]
    found = []
    if data[13:13 + n].decode(errors="replace") != build_id:
        found.append(f"logic.bin is for build {data[13:13 + n].decode(errors='replace')}, the app for {build_id}")
    size = int.from_bytes(data[13 + n + 24:head], "little")
    if len(data) != head + size + 16:
        found.append("logic.bin size does not match its header")
    return found


def problems(package, build_id, index, plain_logic_sha):
    """Everything in `package` that would ship part of the game. Empty when the package is clean."""
    found = []
    known = {v["sha256"] for v in json.loads((ROOT / "analysis/evidence/source-files.json").read_text())["files"].values()}
    files = [p for p in Path(package).rglob("*") if p.is_file()]
    logic = [p for p in files if p.name == "logic.bin"]
    if len(logic) != 1 or logic[0].relative_to(package).as_posix() not in LOGIC_PATHS:
        found.append(f"expected one logic.bin at the package root or Contents/Resources, found {[p.relative_to(package).as_posix() for p in logic]}")
    for p in files:
        data = p.read_bytes()
        rel = p.relative_to(package).as_posix()
        if not any(a.fullmatch(rel) for a in ALLOWED):
            found.append(f"{rel}: not a file a package may hold")
        sha = hashlib.sha256(data).hexdigest()
        if sha in known:
            found.append(f"{rel} is an original game file")
        if sha == plain_logic_sha:
            found.append(f"{rel} is the plaintext game logic")
        if p.name == "logic.bin":
            found += logic_problems(data, build_id)
        # ciphertext never matches these, so logic.bin gets them too
        if SYMBOL.search(data):
            found.append(f"{rel} contains translated function symbols")
        at = shared_run(data, index)
        if at is not None:
            found.append(f"{rel} shares a run of bytes with the original files at offset {at}")
    return found


class PackageTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = Path(os.environ["MTV_BUILD"])
        cls.platform = os.environ.get("MTV_PLATFORM", "macos")
        data = os.environ.get("MTV_DATA", str(ROOT / "mad-tv"))
        tool = os.environ.get("MTV_TOOL", str(ROOT / "build/macos-release/mtv-tool"))
        cls.tmp = tempfile.TemporaryDirectory()
        subprocess.run([sys.executable, str(ROOT / "packaging/package.py"), "--platform", cls.platform, "--build", str(cls.build),
                        "--out", cls.tmp.name, "--data", data, "--tool", tool], check=True, capture_output=True)
        cls.package = Path(cls.tmp.name) / ("MadTV.app" if cls.platform == "macos" else "MadTV-win64")
        cls.build_id = (cls.build / "logic-build-id.txt").read_text().strip()
        cls.library = cls.build / ("mtv-logic.dylib" if cls.platform == "macos" else "mtv-logic.dll")
        cls.plain_sha = hashlib.sha256(cls.library.read_bytes()).hexdigest()
        cls.index = window_index(original_bytes(data, tool))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_package_contains_no_game_data(self):
        self.assertEqual(problems(self.package, self.build_id, self.index, self.plain_sha), [])

    def test_package_has_notices_and_the_upscaler(self):
        names = {p.name for p in self.package.rglob("*") if p.is_file()}
        for required in ("LICENSE", "THIRD_PARTY_NOTICES.md", "SDL3.txt", "json.txt", "stb.txt", "ymfm.txt", "monocypher.txt",
                         "runner-MIT.txt", "Real-ESRGAN-BSD-3-Clause.txt", "ncnn-BSD-3-Clause.txt",
                         "realesr-animevideov3-x3.bin", "realesr-animevideov3-x3.param"):
            self.assertIn(required, names)
        if self.platform == "windows":
            for required in ("llvm-mingw.txt", "libc++.dll", "libunwind.dll", "realesrgan-ncnn-vulkan.exe", "vcomp140.dll"):
                self.assertIn(required, names)
        else:
            self.assertIn("realesrgan-ncnn-vulkan", names)
            self.assertIn("MoltenVK-Apache-2.0.txt", names)

    def test_no_build_host_paths(self):
        home = str(Path.home()).encode()
        for p in (p for p in self.package.rglob("*") if p.is_file()):
            data = p.read_bytes()
            self.assertFalse(home in data, f"{p.name} names the build host's home directory")
            self.assertFalse(str(ROOT).encode() in data, f"{p.name} names the source tree")

    def test_the_check_catches_a_plaintext_library(self):
        with tempfile.TemporaryDirectory() as t:
            copy = Path(t) / "pkg"
            shutil.copytree(self.package, copy, symlinks=True)
            shutil.copy2(self.library, copy / self.library.name)
            found = problems(copy, self.build_id, self.index, self.plain_sha)
            self.assertTrue(any("plaintext game logic" in f for f in found), found)
            self.assertTrue(any("translated function symbols" in f or "shares a run" in f for f in found), found)


class ReleaseCheckSeeds(PackageTest):
    """Bad packages the check must refuse."""

    def bad_copy(self, t):
        copy = Path(t) / "pkg"
        shutil.copytree(self.package, copy, symlinks=True)
        return copy

    def test_plaintext_behind_a_logic_header_is_caught(self):
        with tempfile.TemporaryDirectory() as t:
            copy = self.bad_copy(t)
            box = next(copy.rglob("logic.bin"))
            head = box.read_bytes()[:13 + box.read_bytes()[12] + 24 + 8]
            box.write_bytes(head + self.library.read_bytes() + bytes(16))
            found = problems(copy, self.build_id, self.index, self.plain_sha)
            self.assertTrue(found, "plaintext behind a valid header passed")

    def test_a_second_logic_file_is_caught(self):
        with tempfile.TemporaryDirectory() as t:
            copy = self.bad_copy(t)
            box = next(copy.rglob("logic.bin"))
            (box.parent / "licenses" / "logic.bin").write_bytes(box.read_bytes())
            self.assertTrue(problems(copy, self.build_id, self.index, self.plain_sha))

    def test_an_unlisted_file_is_caught(self):
        with tempfile.TemporaryDirectory() as t:
            copy = self.bad_copy(t)
            box = next(copy.rglob("logic.bin"))
            (box.parent / "pack.zip").write_bytes(b"PK\x03\x04 any packed art")
            found = problems(copy, self.build_id, self.index, self.plain_sha)
            self.assertTrue(any("pack.zip" in f for f in found), found)


if __name__ == "__main__":
    unittest.main()
