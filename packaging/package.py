"""Package the built port: macOS -> <out>/MadTV.app (ad-hoc signed), Windows -> <out>/MadTV-win64/.

The packages hold no original game files and no upscaled art. The translated game logic ships only encrypted
(logic.bin), with the key taken from the original MTV.EXE and SOURCE1.RSC: the first start asks for the folder with
the player's own files, copies them, unlocks the logic and builds the upscaled art with the bundled upscaler.

CLI: python3 packaging/package.py --platform macos|windows --build <build dir> --out <dist dir>
       [--data <original files>] [--tool <host mtv-tool>]
"""
import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
README = """Mad TV (native port)

This package does not contain the game. You need your own copy of Mad TV (1991, English/German release) with the
files MTV.EXE, SOURCE1.RSC, SOURCE2.RSC, INTRO.EXE, MCV.BIN and DATAV.PCK.

Start: {start}
{first_open}
On the first start, choose the folder that holds those files. They are checked and copied once into your user
folder, the game logic is unlocked with them, and the pictures are upscaled on your computer (a few minutes with a
graphics card; Esc plays without them, F12 builds them later).

Keys: F12 options (language, aspect, fullscreen), F11 or Alt+Enter fullscreen. F1-F10 are the game's own keys.
"""
FIRST_OPEN = {
    "macos": "The app is not notarized: on the first start, right-click MadTV.app and choose Open (or allow it in\n"
             "System Settings, Privacy & Security).",
    "windows": "The program is not signed: if Windows SmartScreen warns on the first start, choose More info, then Run anyway.",
}


LICENSES = {"SDL3.txt": "tools/vendor/SDL3-3.4.20/LICENSE.txt", "json.txt": "tools/vendor/json/LICENSE.MIT",
            "stb.txt": "tools/vendor/stb/LICENSE", "ymfm.txt": "tools/vendor/ymfm/LICENSE",
            "monocypher.txt": "tools/vendor/monocypher/LICENCE.md"}
WINDOWS_LICENSES = {"llvm-mingw.txt": "tools/vendor/llvm-mingw-20261104-ucrt-macos-universal/LICENSE.TXT"}
UPSCALER_LICENSES = {n: f"tools/upscalers/licenses/{n}" for n in ("runner-MIT.txt", "Real-ESRGAN-BSD-3-Clause.txt", "ncnn-BSD-3-Clause.txt",
                                                                  "libwebp-BSD-3-Clause.txt")}
MACOS_UPSCALER_LICENSES = {"MoltenVK-Apache-2.0.txt": "tools/upscalers/licenses/MoltenVK-Apache-2.0.txt"}
MODEL = ("realesr-animevideov3-x3.param", "realesr-animevideov3-x3.bin")


def upscaler(dest, runner_dir, runner_files):
    """The Real-ESRGAN runner and the one model the pack uses, in <dest>/upscaler."""
    (dest / "upscaler/models").mkdir(parents=True)
    for f in runner_files:
        shutil.copy2(runner_dir / f, dest / "upscaler" / f)
    for m in MODEL:
        shutil.copy2(runner_dir / "models" / m, dest / "upscaler/models" / m)


def logic_bin(library, build, dest, data, tool):
    """The game logic library, encrypted with the key from the original files."""
    build_id = (build / "logic-build-id.txt").read_text().strip()
    subprocess.run([str(tool), "encrypt-logic", "--data", str(data), "--in", str(library), "--out", str(dest / "logic.bin"),
                    "--build-id", build_id], check=True, capture_output=True)


def notices(dest, extra=None):
    """The port's licence, the third-party notices and each shipped component's licence text."""
    shutil.copy2(ROOT / "LICENSE", dest / "LICENSE")
    shutil.copy2(ROOT / "THIRD_PARTY_NOTICES.md", dest / "THIRD_PARTY_NOTICES.md")
    (dest / "licenses").mkdir(exist_ok=True)
    for name, src in {**LICENSES, **(extra or {})}.items():
        shutil.copy2(ROOT / src, dest / "licenses" / name)


def macos(build, out, data, tool):
    app = out / "MadTV.app"
    shutil.rmtree(app, ignore_errors=True)
    (app / "Contents/MacOS").mkdir(parents=True)
    (app / "Contents/Frameworks").mkdir()
    (app / "Contents/Resources").mkdir()
    shutil.copy2(ROOT / "packaging/macos/Info.plist", app / "Contents/Info.plist")
    exe = app / "Contents/MacOS/mtv"
    shutil.copy2(build / "mtv", exe)
    sdl = (build / "tools/vendor/SDL3-3.4.20/libSDL3.0.dylib").resolve()
    shutil.copy2(sdl, app / "Contents/Frameworks/libSDL3.0.dylib")
    # build-tree rpaths name the host's directories; the bundle only needs its own Frameworks folder
    rpaths = [l.split()[1] for l in subprocess.run(["otool", "-l", str(exe)], capture_output=True, text=True, check=True).stdout.splitlines()
              if l.strip().startswith("path ")]
    for r in rpaths:
        subprocess.run(["install_name_tool", "-delete_rpath", r, str(exe)], check=True)
    subprocess.run(["install_name_tool", "-add_rpath", "@executable_path/../Frameworks", str(exe)], check=True)
    res = app / "Contents/Resources"
    (res / "README.txt").write_text(README.format(start="open MadTV.app", first_open=FIRST_OPEN["macos"]))
    notices(res, {**UPSCALER_LICENSES, **MACOS_UPSCALER_LICENSES})
    upscaler(res, ROOT / "tools/upscalers/realesrgan-20220424", ["realesrgan-ncnn-vulkan"])
    subprocess.run(["codesign", "--force", "--sign", "-", "--timestamp=none", str(res / "upscaler/realesrgan-ncnn-vulkan")], check=True, capture_output=True)
    # the library is signed before it is encrypted, so the unlocked copy runs as it is (ad-hoc, no identity)
    with tempfile.TemporaryDirectory() as t:
        lib = Path(t) / "mtv-logic.dylib"
        shutil.copy2(build / "mtv-logic.dylib", lib)
        subprocess.run(["codesign", "--force", "--sign", "-", "--timestamp=none", str(lib)], check=True, capture_output=True)
        logic_bin(lib, build, res, data, tool)
    subprocess.run(["codesign", "--force", "--sign", "-", "--timestamp=none", str(app / "Contents/Frameworks/libSDL3.0.dylib")], check=True, capture_output=True)
    subprocess.run(["codesign", "--force", "--sign", "-", "--timestamp=none", str(app)], check=True, capture_output=True)
    return app


def windows(build, out, data, tool):
    d = out / "MadTV-win64"
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    shutil.copy2(build / "mtv.exe", d / "mtv.exe")
    for dll in ("SDL3.dll", "libc++.dll", "libunwind.dll"):
        shutil.copy2(build / dll, d / dll)
    (d / "README.txt").write_text(README.format(start="mtv.exe", first_open=FIRST_OPEN["windows"]).replace("\n", "\r\n"))
    notices(d, {**WINDOWS_LICENSES, **UPSCALER_LICENSES})
    upscaler(d, ROOT / "tools/upscalers/realesrgan-20220424-windows", ["realesrgan-ncnn-vulkan.exe", "vcomp140.dll"])
    logic_bin(build / "mtv-logic.dll", build, d, data, tool)
    return d


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--platform", choices=["macos", "windows"], required=True)
    p.add_argument("--build", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--data", type=Path, default=ROOT / "mad-tv", help="the original files (for the logic key)")
    p.add_argument("--tool", type=Path, default=ROOT / "build/macos-release/mtv-tool", help="an mtv-tool that runs on this host")
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    print((macos if a.platform == "macos" else windows)(a.build, a.out, a.data, a.tool))


if __name__ == "__main__":
    main()
