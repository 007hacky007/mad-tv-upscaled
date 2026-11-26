# Building

Dependencies live under `tools/vendor/` (ignored by git) and are pinned in `dependencies.lock.json`. They were cloned from `../horke-leto/tools/vendor/` with the same versions and hashes. Configuration never downloads anything.

macOS arm64:

```
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --test-dir build/macos-debug --output-on-failure
```

Windows x64 is cross-compiled on macOS with llvm-mingw (UCRT):

```
cmake --preset windows-debug
cmake --build --preset windows-debug
```

`mtv.exe` needs `SDL3.dll`, `libc++.dll` and `libunwind.dll` next to it: the app and the game logic library share one C++ runtime, so errors raised by the app unwind through the library. Run `mtv-tests.exe` inside the Windows 11 VM; never on the macOS host.

The translated game logic builds as a separate library (`mtv-logic.dylib` / `mtv-logic.dll`); the app in the build tree loads it from next to the executable. A package carries it only encrypted:

```
python3 packaging/package.py --platform macos --build build/macos-release --out dist
python3 packaging/package.py --platform windows --build build/windows-release --out dist
```

`--dmg` also makes `MadTV-macOS.dmg` (the app, an Applications shortcut and a background that explains the first open) with dmgbuild, installed once with `python3 -m venv tools/cache/dmgenv && tools/cache/dmgenv/bin/pip install dmgbuild==2.0.0`. The background is `packaging/macos/dmg/background.html`, rendered to PNG by `render_background.sh` (headless Chrome). The app icon is original pixel art drawn by `tools/art/app_icon.py` into `packaging/icons/`.

`package.py` encrypts the library with `mtv-tool encrypt-logic` (key from `mad-tv/MTV.EXE` and `mad-tv/SOURCE1.RSC`), and adds the Real-ESRGAN runner and model from `tools/upscalers/` (the Windows runner is the pinned release zip in `dependencies.lock.json`, unpacked to `tools/upscalers/realesrgan-20220424-windows`). The release check (`packaging/tests/package_test.py`, ctest `packaging.contents`; for Windows run it with `MTV_BUILD=build/windows-release MTV_PLATFORM=windows`) fails if a package holds an original file, a picture, the plaintext library, translated function names or any run of 48 bytes shared with the original files.

`MTV_USER_DIR` replaces the per-user folder (tests); `MTV_RUNNER` points the app at another upscaler. `mtv-tool build-pack --data mad-tv --out <dir>` builds the upscale pack the way the app does.

Smoke test: `mtv --smoke-test --frames 3 --report <file.json>` opens a window, presents three frames and writes the platform, architecture and render backend.
