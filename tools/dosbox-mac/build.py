#!/usr/bin/env python3
"""Fetch the hash-locked release and build arm64 SDL2 without global installs."""
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
CACHE = ROOT / "tools/cache/dosbox-mac"
SOURCE = CACHE / "dosbox-x-dosbox-x-v2026.10.01"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args, log, env):
    with (CACHE / log).open("wb") as output:
        result = subprocess.run(args, cwd=SOURCE, env=env, stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"{args[0]} failed; see tools/cache/dosbox-mac/{log}")


def main():
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        raise RuntimeError("This build requires macOS arm64")
    lock = json.loads((ROOT / "dependencies.lock.json").read_text())["dosbox_mac"]
    CACHE.mkdir(parents=True, exist_ok=True)
    archive = ROOT / lock["source"]["archive"]
    if not archive.exists():
        staging = archive.with_suffix(".part")
        with urllib.request.urlopen(lock["source"]["url"]) as source, staging.open("wb") as target:
            shutil.copyfileobj(source, target)
        if sha(staging) != lock["source"]["sha256"]:
            raise RuntimeError("Downloaded archive hash mismatch")
        staging.replace(archive)
    if sha(archive) != lock["source"]["sha256"]:
        raise RuntimeError("Cached archive hash mismatch")
    # Restore only patched upstream files on rebuild; preserve compiled objects.
    with tarfile.open(archive) as tar:
        if not SOURCE.exists():
            tar.extractall(CACHE, filter="data")
        else:
            paths = [line[6:] for line in (HERE / "dosbox-x.patch").read_text().splitlines() if line.startswith("--- a/")]
            for relative in paths:
                member = tar.getmember(SOURCE.name + "/" + relative)
                with tar.extractfile(member) as source, (SOURCE / relative).open("wb") as target:
                    shutil.copyfileobj(source, target)
    # Some upstream files use CRLF. The small reviewed patch uses LF contexts.
    for line in (HERE / "dosbox-x.patch").read_text().splitlines():
        if line.startswith("--- a/"):
            path=SOURCE/line[6:]
            path.write_text(path.read_text())
    if sha(ROOT / lock["license"]["path"]) != lock["license"]["sha256"]:
        raise RuntimeError("License hash mismatch")
    env = os.environ.copy()
    env.update(CFLAGS="-O2 -arch arm64", CXXFLAGS="-O2 -arch arm64",
               CPPFLAGS="-I/opt/homebrew/include", LDFLAGS="-L/opt/homebrew/lib -arch arm64",
               GIT_CEILING_DIRECTORIES=str(CACHE))
    run(["patch", "-p1", "--batch", "-i", str(HERE / "dosbox-x.patch")], "patch.log", env)
    for name in ("ref_control.cpp", "ref_control.h"):
        shutil.copyfile(HERE / name, SOURCE / "src" / name)
    run(["./autogen.sh"], "autogen.log", env)
    run(["./configure", "--enable-sdl2", "--disable-debug", "--disable-avcodec",
         "--disable-libfluidsynth", "--disable-opengl", "--disable-sdl2test"], "configure.log", env)
    # The release tar contains a stale build timestamp header; never inherit this
    # repository's HEAD through upstream's parent-directory Git discovery.
    header = SOURCE / "include/build_timestamp.h"
    content = header.read_text()
    import re
    content = re.sub(r'#define GIT_COMMIT_HASH .*', '#define GIT_COMMIT_HASH "b6abbd5-ref1"', content)
    header.write_text(content)
    run(["make", "-j8"], "build.log", env)
    binary = SOURCE / "src/dosbox-x"
    architecture = subprocess.check_output(["file", str(binary)], text=True).strip().split(": ", 1)[1]
    if "arm64" not in architecture:
        raise RuntimeError("Wrong binary architecture")
    metadata = {"release": lock["requested_ref"], "commit": lock["commit"], "architecture": architecture,
                "binary": str(binary.relative_to(ROOT)), "binary_sha256": sha(binary),
                "patch_sha256": sha(HERE / "dosbox-x.patch"),
                "controller_sha256": {n: sha(HERE / n) for n in ("ref_control.cpp", "ref_control.h")}}
    metadata["libraries"] = []
    for line in subprocess.check_output(["otool", "-L", str(binary)], text=True).splitlines()[1:]:
        path = Path(line.strip().split(" (", 1)[0])
        metadata["libraries"].append({"path": str(path), "sha256": sha(path) if path.is_file() else None})
    metadata["sdl_version"] = subprocess.check_output(["pkg-config", "--modversion", "sdl2"], text=True).strip()
    metadata["libpng_version"] = subprocess.check_output(["pkg-config", "--modversion", "libpng"], text=True).strip()
    (CACHE / "build.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
