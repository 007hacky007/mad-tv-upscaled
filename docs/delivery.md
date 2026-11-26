# Delivery

Native port of Mad TV (Rainbow Arts, 1991) for macOS arm64 and Windows x64, with the game's art upscaled 6x.

## What it is

- The game logic is the original program: `MTV.EXE` and the Palladix AdLib driver are translated instruction by instruction into C++ at build time (`tools/recomp`) and run on a small model of the PC and DOS (`src/rt`). The intro (`INTRO.EXE` with `MCV.BIN`) is a hand port.
- Graphics: the game's own 320x200 picture, drawn 6x. Each pixel that came from an image is drawn from the upscaled image (nearest x2, then Real-ESRGAN AnimeVideo v3 x3, chosen in the art pilot); fills and lines drawn by the code stay flat. F12 > Pictures (or Pause) switches to plain 6x pixels.
- Sound: the driver's AdLib register writes played by ymfm (YM3812).
- Saves: the game's own save slots, kept in a copy of `SOURCE1.RSC` in the user folder.
- Release form: the translated logic builds as a library (`mtv-logic`) that the package carries only encrypted (`logic.bin`, XChaCha20-Poly1305, key = BLAKE2b over the original `MTV.EXE` and `SOURCE1.RSC`). The first start unlocks it into the user folder and builds the upscaled art with the bundled Real-ESRGAN runner. Without the original files a package does nothing.

## Building and packaging

```
cmake --preset macos-release && cmake --build --preset macos-release
python3 packaging/package.py --platform macos --build build/macos-release --out dist
cmake --preset windows-release && cmake --build --preset windows-release
python3 packaging/package.py --platform windows --build build/windows-release --out dist
```

The build needs the original files in `mad-tv/` (`MTV_GAME_DIR`): the translated program is generated from them and is never committed. `package.py` encrypts the logic library with the key from those files (`mtv-tool encrypt-logic`) and adds the upscaler and the x3 model. A package contains no original file, no upscaled art and no plaintext game logic; `packaging/tests/package_test.py` checks that, plus translated function names, any run of 48 bytes shared with the original files, and build host paths.

First start: the program asks for the folder with the six original files, checks them against the known release (SHA-256) and copies them into the user folder (`~/Library/Application Support/MadTV`, `%APPDATA%\MadTV`); `mtv --import <folder>` does the same without a window. It then unlocks the logic into `logic/<build id>/` (again after an update, from the copies in the user folder) and builds the upscaled art with a progress screen; Esc plays with plain 6x pixels, F12 offers the build later. A pack found next to the original files is still taken over by the import.

Keys: F12 options (language, aspect 4:3 or square pixels, fullscreen, upscaled or original pictures, or building the upscaled pictures when they are missing), F11 or Alt+Enter fullscreen, Pause upscaled or original pictures. Each also has a Ctrl (or Cmd) letter: Ctrl+O or Ctrl+, options, Ctrl+F fullscreen, Ctrl+P pictures; plain letters stay with the game, which takes typed save names. F1-F10 stay the game's own hotkeys.

Signing is ad-hoc only (no Apple ID, no notarization, no Authenticode); the README explains the first-open steps.

## Acceptance

1. Resources: every image, frame, animation, music track and text decodes with bounds checks; `corpus.coverage` lists them (Task 10). Passes on both platforms.
2. Reference runs: the translated program is checked per instruction against Unicorn (756 instruction forms, all branch conditions), per function from game snapshots, and against DOSBox: language prompt, title and player select are pixel-identical (`corpus.game`), and the title music writes the same 1800 AdLib registers with note times within 6 ms. Day-by-day DOSBox state dumps are not compared: the game's random numbers are seeded from its 50 Hz timer, and DOSBox's cycle timing cannot be reproduced instruction for instruction. Idle play reaches the dismissal ending on day 6 (`corpus.idle_to_dismissal`); every room is visited without a fault (`corpus.room_tour`). A full in-game month and the bankruptcy and winning endings were not run.
3. Upscaled art: all 1185 image frames are in the pack, and the decoder recognises every one of the 164 image slots (`every_image_slot_is_recognised_at_the_decoder`); every image the game showed along the test routes is in the pack (`every_image_on_screen_is_in_the_pack`). Text comes from the upscaled font glyphs. Sprite edges use the upscaled masks. Not upscaled yet: the 20 small delta animations (e.g. the eyes of the face after the title) and the intro; they are drawn 6x. Mouse mapping in letterboxed, high-DPI and fullscreen windows is covered by `render.viewport`; clicks were driven by scripts, not tried by hand.
4. Music: register writes and timing match DOSBox (item 2). Listening side by side has not been done: `work/listen/title-native.wav` holds 30 s of the title music for that.
5. Packages (installable release, 2026-11-26): the macOS `MadTV.app`, run from outside the source tree with a fresh user folder, imported the six files, unlocked the logic (byte-identical to the built library), built all 1185 upscaled pictures itself (pixel-identical to the developer pack), played to a save in the office safe (17:44) and after a restart loaded it from the title screen (office, 17:44). A modified file is refused with its name, a missing one is listed, and nothing is written. Windows 11: same import, unlock and save/load (below). The folder dialog of the first start was not clicked through by hand; `--import` runs the same import.
6. Languages: both are the original's; the language argument selects them, and switching in the options menu (F12) loads the other message table into the running game without changing game state (`language_switch_preserves_state_dump`); each language reads only its own text slots.
7. Tests: macOS `ctest` all pass (55 suites, including the package check and the comparison of the in-app pack builder with `build_pack.py`). Windows 11 VM: `mtv-tests.exe` 167/167 cases, `mtv-game-tests.exe` 27/27 cases, with the corpus; the Windows package check passes. Windows 10 is built for but not verified.

## Windows 11 results

Windows 11 VM (x64, no GPU, no Vulkan driver; SDL used Direct3D 11), package `MadTV-win64` unpacked to `C:\bench\madtv\rel`, outside any build tree, fresh user folder (`MTV_USER_DIR`):

- `mtv.exe --import <folder>`: the six files checked and copied.
- First start: `logic.bin` unlocked into `logic\b5f598fc6f2fe320\mtv-logic.dll` and loaded; an error raised by the app unwinds through the library (`platform.logic`).
- Art build: the runner cannot start a Vulkan device in this VM ("vkCreateInstance failed"), so no pictures came out; the app reported it, kept no partial pack and ran the game with plain 6x pixels. The in-app build was therefore verified on macOS only.
- Scripted save in the office safe and load from the title screen after a restart: the office restored. At frame 1300 the Windows load run still showed player select (macOS was in the office by then); at frame 2500 the office was shown. Not investigated.
- Defender: disabled in this VM by policy, so the antivirus question was not answered here.
- Frame time: without a GPU the VM shows about 35 frames per second on the upscaled picture (earlier measurement); the game keeps its speed.

## Known gaps

- Animation deltas and the intro are not upscaled.
- No side-by-side listening comparison yet. Smoke tests are now silent; `--smoke-test --realtime` runs by the real clock, muted, and counts audio gaps: 0 and 1 in two 900-frame runs, the one at the first upscaled screen.
- Windows: the antivirus reaction to the unlocked DLL (Defender is off in the VM) and the art build on a machine with Vulkan are unverified.
- No month-long, bankruptcy or winning-ending run.
- The language prompt is never shown (the language comes from the settings). If it is shown (running the original without an argument), keys pressed there can be taken by the game's hotkey poller; this race is the original's.
