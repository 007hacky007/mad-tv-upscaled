# Local REA route

The globally registered REA MCP server starts Ghidra with `REA_GHIDRA_CHECKPOINT_DIR`, and Ghidra checkpoints support only ELF, PE and Mach-O. It therefore refuses DOS MZ targets. Global settings are not changed. This project runs its own REA 6.9.0 process (same version as the global one), cloned from `../horke-leto/tools/rea` into ignored `tools/rea/`.

`tools/reference/rea_client.py` starts `node tools/rea/node_modules/rea-agents/scripts/rea.mjs --mcp` with a scoped environment:

- every inherited `REA_*` variable removed (in particular the checkpoint directory)
- `GHIDRA_INSTALL_DIR=~/tools/ghidra_12.2_PUBLIC`
- `JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-27.jdk/Contents/Home`
- `TMPDIR=tools/cache/ghidra`

It speaks MCP over stdio (`initialize`, `tools/call`) and calls `close_binary` on exit. Evidence is written to `analysis/evidence/rea/` with the project root and home directory scrubbed from paths. `tools/reference/rea_probe.py <target> <tool> '<json args>' [--save name]` runs one call.

Ghidra loads the unpacked executables with `MzLoader`, language `x86:LE:16:Real Mode`, load segment 0x1000. Addresses are linear: Ghidra address = 0x10000 + load-image offset; file offset of `work/unpacked/MTV.EXE` = 0x4680 + load-image offset.

Full decompilations (`decompile_all`, address layout) are written to ignored `work/decomp/{MTV,INTRO}/functions/<address>.c`. They are derived from the copyrighted program and stay out of git.
