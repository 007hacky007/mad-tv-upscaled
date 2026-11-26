# Reference environment

The original game runs in a patched DOSBox-X 2026.10.01 (`tools/dosbox-mac/`, built into ignored `tools/cache/dosbox-mac/` by `python3 tools/dosbox-mac/build.py`). The patch adds a Unix-socket control channel. The guest starts paused and only advances on `step`, so runs are driven by emulated time, not wall time. Video and audio use SDL dummy drivers: no windows, no macOS capture or input APIs.

`tools/reference/launch.py` builds the config. It uses machine `vgaonly`, the normal core, cycles fixed at 8000, a 386 CPU type, SB16 with OPL2, and the PC speaker. It mounts the game directory read-only as D:, copies it to a private C: under the work directory, pins the DOS clock, and starts the program. `launch.session(work, data, program, replace={...})` returns a context manager. Replacement files (for example the unpacked executables) are copied over C: before the program starts.

Commands (one per line, JSON reply each), as in Horke Leto plus Mad TV changes:

| Command | Meaning |
| --- | --- |
| `status` | Guest ms tick, frame count, current output size and depth |
| `step ms N` / `step frames N` | Advance emulated time, then pause |
| `key e down` / `key e up` | Keyboard make/break |
| `mouse X Y` | Set the INT 33h position directly in driver units (Mad TV: logical x*10, y*16) |
| `button 0 down` / `up` | Mouse buttons |
| `frame "file.png"` | Last completed frame at its real size: indexed PNG for 8-bit output, RGB PNG for 32-bit output |
| `vram "file.bin"` | Raw VGA memory (all planes, `vga.mem.linear`) |
| `dac "file.bin"` | 256 DAC entries (6-bit RGB) followed by the 16 attribute palette registers |
| `memory START LEN "file.bin"` | Linear guest memory |
| `save` / `load "file"` | DOSBox-X save states |
| `iosig HEX` | Log a probe-port write only if the far-call return address on the guest stack starts with these bytes |
| `iolog "file"` | Write and clear the probe log: one `ms port value` line per guest write to port 0xEE |
| `quit` | Exit |

Interactive use: `python3 tools/reference/launch.py --program MTV.EXE`, then type commands.

## Recording

`python3 tools/reference/record.py <script.json> <out-dir>` patches a private copy of the unpacked `MTV.EXE` (`tools/reference/probe_patch.py`: the timer getter writes its value to port 0xEE). It plays the tick-addressed script and writes `recording.json` (inputs per 100 Hz tick plus every timer value `random()` read), `dumps.json` (fields from `analysis/evidence/state-layout.json` at ticks marked `"dump": true`) and a frame PNG per dump. `tools/reference/compare.py a/dumps.json b/dumps.json` lists differing fields.
