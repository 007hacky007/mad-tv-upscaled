# Mad TV findings

Evidence for the native port. Addresses are file offsets in the unpacked `MTV.EXE` (`work/unpacked/MTV.EXE`, SHA-256 `656e4419...41ca`) unless stated otherwise.

## Executables

- `MTV.EXE` and `INTRO.EXE` are LZEXE 0.91 packed Turbo C programs. `mtv-tool unpack` restores them.
- The unpacked files behave like the originals in the patched DOSBox-X reference: `MTV.EXE` produced byte-identical frames every 2.5 s for 30 s from boot. `INTRO.EXE` matched from 5 s on. Its first two frames differ because the packed file spends time decompressing before it starts (evidence: `work/m0/{intro,mtv}-{orig,unpacked}/`).
- `INTRO.EXE` shows the Rainbow Arts logo with a burning fuse and returns to DOS after about 17 s. It is a separate program from the game.

## Startup and language

- `MTV.EXE` starts with a prompt `(D)eutsch / (E)nglish` and then uses the chosen language for everything, including the copyright screen and menus. The original selects the language at runtime; there is no separate English build.
- After the prompt: copyright screen, the "Select a player" menu (Mad-TV/Archie, Fun-TV/Brian, Sun-TV/Churchill; Player or Computer per station; Easy/Normal/Difficult; New game / Load game / End), then the building.
- A key press at 2.5 s after start was accepted by the prompt, one at 3.0 s was not, and the run that ignored it later crashed with "Divide error" when Space was pressed. Keyboard handling must be understood before input is recorded (Task 3/5).

## Copy protection

- None seen between start and the first in-game building screen on a new game. Keep checking during longer runs.

## Saves

- Saving and loading happen in the player's office: the safe on the bookshelf opens the Load/Save overlay (left icon Save, right icon Load), then a list of 10 slots (2 columns x 5 rows). Loading is also offered on the player-select screen ("Load game").
- A save is the game state serialised by FUN_329e_139a (0x33D7A) into a buffer of up to 40000 bytes and written as RSC slot 210-219 inside SOURCE1.RSC itself (FUN_485d_04f3 / FUN_485d_0336); a save of a new size is appended to the file and the slot table entry updated. Slot 209 (400 bytes) holds the 10 names. Slot 43 is the new-game state, loaded the same way.
- The game closes the file after writing a save, so a finished save is complete on disk. The port writes all of this into its copy of SOURCE1.RSC in the user directory (copied on first write), which then shadows the original.
- A load restores the game state blocks; it does not restore the walking figures (DS:7B42) or the cells of the floor map (DS:7C82) that mark where they stand, and the clock keeps running.

## Mouse

- INT 33h calls at 0x3b9e9-0x3ba6d: reset, horizontal and vertical range 0..3200 (AX=7/8, DX=0x0C80), mickey ratio 3/3 (AX=0x0F), set position 1600,1600 (AX=4), read position (AX=3).
- Logical position = INT 33h x / 10, y / 16 (verified: setting 1600,1600 shows the cursor at 160,100).

## Video

- 320x200, 16 colours (BIOS mode 0Dh with VGA DAC). DOSBox-X renders it as 320x400 (double scan).

## Reference determinism

- The DOS date and time are pinned (`date 02-10-1994`, `time 12:00:00`) in the reference autoexec. With that, two runs a minute apart produced byte-identical frames at every checkpoint. Without it, the same input gave different results between runs.

## Static analysis (REA, Ghidra 12.2, 16-bit real mode)

`MTV.EXE`: 691 procedures (668 with decompilable bodies). `INTRO.EXE`: 99. Data segment DS = 0x49D4 (linear 0x49D40), so DS offset = linear - 0x49D40.

| Address | Ghidra name | Role |
| --- | --- | --- |
| 0x10000 | entry | Turbo C startup; calls main at 0x10545 then exit |
| 0x10545 | FUN_1054_0005 | `main(argc, argv)`. argv[1] starting `d`/`D` sets language 0 (German), `e`/`E` sets 1 (English); argv[2] counts leading `*` into DS:0580 (capped at 10, purpose unknown). Language variable DS:055C = -1 until chosen; if still -1, FUN_1054_4233 shows the D/E prompt |
| 0x49CCD | FUN_49cc_000d | `srand(seed)`: DS:1960 = seed, DS:1962 = 0 |
| 0x49CDE | FUN_49cc_001e | `rand()`: Turbo C LCG, `seed = seed * 0x015A4E35 + 1`, returns `(seed >> 16) & 0x7FFF` |
| 0x1D809 | FUN_1d5e_0229 | `random(n, salt)`: `srand(DS:9034 + salt + DS:8EAC)` (32-bit add, DS:8EAE high word), then `rand() % n + 1`. Reseeds on every call. `random` is the only caller of `srand` |
| 0x465DE / 0x465D6 / 0x465E4 | | get / clear / set the 16-bit counter DS:9034 used by `random`. No other code in the file writes it by DS offset; how it advances is open (Task 5) |
| 0x1B505 | | writes DS:8EAC from DS:6143 |
| 0x48640 | FUN_485d_0070 | Resource index: `findfirst("*.rsc")` (DS:17B2); for each file reads the 0xE10-byte table of 600 slots x 6 bytes; every slot with a non-zero offset goes into one global table at DS:A686 (8 bytes: file handle, size, offset low, offset high). Slots are global across files; SOURCE1 and SOURCE2 use disjoint slots |
| 0x1D5E segment | FUN_1d5e_01a1(n), _00a0(n), _011b(n), _06fb(a,b) | Resource loaders by slot number (called from main with 3, 2, 299, 1, 6, 0x0B, 0x0F, 4, 7, 0x0D, 0x10) |
| 0x48592 | FUN_4859_0002 | Keyboard: BIOS INT 16h AH=1 (key ready?) then AH=0; maps ASCII through DS:16AE or extended scan codes through DS:1730 (0xFF = ignored). Returns 0xFFFF if no key |
| 0x48584 | FUN_4858_0004 | Flush BIOS keyboard buffer |
| 0x47421 | FUN_4742_0001 | Wait for the start of vertical retrace (port 0x3DA bit 3). Called from 0x1F628 and 0x46819 |
| 0x47367-0x473B9 | | Mouse (INT 33h) |
| 0x47431, 0x473F9 | | INT 10h video mode set |
| 0x47476-0x47A21 | FUN_4747..FUN_47a2 | Blitters taking a parameter block (far pointer); built into MTV.EXE, `MCV.BIN` is not used by the game |
| 0x385F7, 0x38868 | | Use port 0x388 (AdLib / OPL) |

The game has no Turbo C `rand` call other than through `random`, so its whole randomness is `random(n, salt)` and depends on the DS:9034 counter, the DS:8EAC value and the per-call salt.

## Timing and randomness (Task 5)

- The sound driver loaded at runtime hooks INT 08 (4E9D:0432 in the reference run, outside the program image) and reprograms the PIT to divisor 5590 (213.45 Hz). Its handler subtracts 5590 from an accumulator and adds 11930 on borrow, giving an exact 100 Hz tick (1193182 / 11930 = 100.015 Hz), and chains to the BIOS at 18.2 Hz.
- On every 100 Hz tick it increments the four words at DS:902C, every 2nd tick DS:9034-903A (50 Hz), every 4th DS:903C-9042 (25 Hz), every 10th DS:9044-904A (10 Hz). The pointer to DS:902C is stored in the driver at CS:0427. The game resets these counters for delays.
- Native game tick: 100 Hz.
- `random()` reads DS:9034, a 50 Hz counter that the game resets, so the result depends on how long the original's own code took between reset and call. That is CPU speed dependent and cannot be derived from input alone. Reference runs therefore record every counter value read by `random()` (harness port 0xEE plus caller-signature filter on the return address 0x1D812). Native verification replays those values; normal native play uses its own tick count.
- The reference is deterministic: two recordings of `reference/scripts/smoke-newgame.json` (language, title, New game, 2 minutes of play) produced byte-identical inputs, 176,277 timer reads and 13 state dumps.
- Game clock: one in-game minute per 2 s (200 ticks) at the start of the first day (17:02 at tick 2000, 17:57 at tick 13000). A new game opens with the boss ("Welcome to MadTV!") and $200 on the display.
- The game clock, money and ratings are not plain counters in the data segment (searched DS for constant-rate and 2/7/12 minute values). They likely live in far-allocated memory; Task 12 maps them.

## Resource formats (Task 7)

`analysis/rsc-map.json` lists every used slot (242) with kind, language and loader. Totals: 164 packed images, 44 LZW music entries, 17 text tables, 11 data tables, 3 sound drivers, 2 palettes, 1 unresolved (164, candidate font).

- Packed images (decoder FUN_47b4_04ac at 0x47FEC, called by FUN_1d5e_011b / FUN_1d5e_01a1): byte N = frame count, N (width, height) byte pairs, u16 big-endian node count, u32 big-endian pixel count, then an LSB-first bit stream. A root Huffman tree and 256 context trees (one per previous output byte) are serialised as bits: 0 = internal node (left, right), 1 = leaf; the first leaf of each tree is the escape symbol 0x100, later leaves carry 8 bits. Decoding starts with the root tree, then uses the previous byte's context tree; an escape retries with the root tree, and an escape there reads a raw 8-bit literal. Output is one byte per pixel per frame, rows padded to a multiple of 8 pixels; in memory each frame has an 8-byte header (width in bytes, height, far pointer) after a u16 frame count. The mode argument (0 or 1) is ignored by the decoder.
- Music (FUN_45f8_024c, decoder FUN_46f4_0009 at 0x46F49): u16 unpacked size, u16 packed size, `00 08 09 00`, then MSB-first LZW codes starting at 9 bits; 0x100 resets the dictionary (next code 0x102), 0x101 widens codes by one bit. Dictionary entries hold prefix, first and last byte; the KwKwK case is handled by writing the new entry's first byte before reading the code's. The loop stops when the packed byte counter reaches zero and discards the code read last; bytes past the entry only feed that discarded code. Decoded songs start `PLX`. All 44 entries decode to exactly their declared size. FUN_45f8_037c maps game events to song slots 0x4E-0x61, 0x93-0x95 and 500 -> 0x95.
- Sound drivers: slot 95 PALLADIX-PSM V1.1 PC-Beeper, slot 98 PALLADIX-PSM V1.3 AdLib, slot 99 PALLADIX-PSMDETECT (AdLib or PC-Beeper). FUN_45f8_0003 runs the detector and loads 98 or 95. There is no Sound Blaster digital driver; the port's sound target is the AdLib (OPL2) driver.
- Language: the table at DS:055E + language*16 lists 8 text slots per language. German 199, 42, 207, 47, 45, 46, 49, 44 pair with English 253, 246, 252, 251, 250, 249, 248, 247. Slot 41 holds the prompt text for both.
- Slot 209 (400 bytes) is rewritten inside SOURCE1.RSC by the game (FUN_485d_0336 / FUN_485d_04f3). The original modifies its own data file; the port keeps this data in the user directory instead.
- Slots 43 and 210 are tables of 18-byte records with identical layout and different values.
- FUN_1d5e_048d asks for a disk when a slot is missing (message 0x11F for slots below 0x12D, 0x1AD otherwise), a leftover of the floppy release.
- DATAV.PCK is PowerPacker 2.0 and unpacks to its declared size.

## Images and palette (Task 8)

- The game runs in VGA mode 13h (320x200, 256 colours, one byte per pixel), not EGA mode 0Dh. Mode 0Dh is only used by the intro's video driver `MCV.BIN`. The design's "16 colours" is corrected to "a single 32-colour palette".
- Packed images unpack to chunky 8-bit frames; blitters copy rows of `width_bytes * 8` pixels to a 320-byte-wide back buffer (segment in DS:16A8). Opaque blitters use `REP MOVSW`; masked blitters skip index 0.
- One palette for the whole game: slot 1 (= slot 299), loaded with INT 10h AX=1012h. It defines entries 1-30 and 255; the art uses indices 0-31 plus 200 stray pixels in 5 frames. No fades or palette effects exist in MTV.EXE. Evidence: `analysis/evidence/palettes.json`.
- All 164 image slots (1,185 frames) decode and look correct when exported (`mtv-tool export-images --data mad-tv --out work/images`).

## Text and font (Task 9)

- Code page 437. Accented letters used: ä ö ü Ä Ö Ü ß é è ê à â ù á í ó ú ñ ¿ ¡. Bytes 0x01-0x07 and 0xC0-0xC5 have game-specific glyphs (icons); the port maps them to U+E000 + byte so they survive UTF-8 conversion. `~` is a line break and `#s`, `#d`, `#l`, `#_8l`, `#40f`, `#*13s` are the game's own format codes (printf-like, handled at runtime).
- Text layouts per slot (German / English): 199 / 253 Sequence (u16 count 293 + 4 bytes, then NUL-separated); 42 / 246 offset table + NUL strings (80); 207 / 252, 47 / 251, 45 / 250 offset table + records starting with a length-prefixed string (201, 51, 51; the last 207/252 record is empty); 49 / 248 offset table + raw strings (401, news); 46 / 249 two blocks of [51-entry u16 length table][strings] (sponsor names, then slogans; FUN_2c18_000c); 44 / 247 one 561-entry length table indexed `i*10 + j` then strings (FUN_2c18_000c). German and English tables have identical counts.
- Font: image slot 3, 118 frames, 8 pixels high. FUN_205e_0266 maps each byte through DS:012E (1-based frame; 0 = missing, drawn as '.'), advances glyph width (DS:022E) + 1, and 3 pixels for a space; with the digit flag, digits are right-aligned in a 5-pixel cell. Glyphs are drawn with the masked blitter at pixel positions. When DS:8BA6 = 1 the same routine reads glyphs from the image at DS:0108 (slot 29 at load). The glyph and width tables are read from the user's MTV.EXE at import (`read_exe_tables`), so the port contains no copy of them.
- Slot 164 is the broadcast coverage bitmask for the US map (slot 70), not a font.
- German-only strings in the executables: MTV.EXE "Datenfile nicht gefunden" (data file not found), "Fehler aufgetreten!" (an error occurred); INTRO.EXE "Nana, nen bischen mehr Speicher muss schon sein..." (not enough memory). They belong to error paths that do not exist in the native port (missing files are handled by the port's importer, memory is not limited), so no English text needs to be written for them. The Turbo C runtime messages ("Divide error", "Abnormal program termination") are English already and also have no native equivalent.

## Music, animation and coverage (Task 10)

- Sound devices: AdLib (PALLADIX-PSM V1.3, slot 98) and PC speaker (V1.1, slot 95), chosen by the detector (slot 99). No digitized sound effects exist; all game audio is PLX music through the driver.
- Driver module layout: text header `PALLADIX-PSM\r\n\x1a$`, a DOS stub that prints the header and exits, a word count (13) at offset 0x11 and 13 entry offsets (0x40, 0x71, 0x40C, 0x7E, 0x93, 0xBF, 0xF3, 0x134, 0x13C, 0x141, 0x1F4, 0x1F8, 0x3A8). Entry 6 (0xF3) loads a song through the parser at 0x1431. Disassembly: `tools/cache/pyenv` capstone over the slot bytes (`work/decomp/ADLIB/all.asm`, not committed).
- PLX songs (24 slots: 78-94, 96, 97, 147-149, 254, 255): `PLX`, mode byte (0 = 9 melodic tracks with handler 0x0EF5, 1 = OPL rhythm mode with 11 tracks and handler 0x1046, 2 = single stream with handler 0x1252, above 2 rejected), speed byte (driver CS:0B01), tempo word (CS:0AFF; converted to the timer rate with the 0x2E9A divisor), then u16 track offsets.
- Animations (20 LZW slots 300-337, not music): the table at DS:161E pairs a backdrop image slot with up to three delta slots; FUN_465f_0244 plays them, FUN_5456_0000 (0x54560) applies each frame. All 20 decode over their backdrops (261 frames) and look coherent.
- `mtv-tool coverage --data mad-tv --out work/coverage.json`: 242 slots, complete, 0 unknown. `mtv-tool viewer --data mad-tv` browses every slot.

## Function map (Task 12)

`analysis/function-map.json` lists all 691 procedures with segment, class, subsystem, port target and a name (known names, otherwise `<subsystem>_<address>`), generated by `tools/reference/function_map_check.py`. Turbo C's large model puts each source file in its own code segment, so segments are the original modules. Game modules and roles: 1054 startup, 148C state, 15FF rooms, 1AA4 boss, 1D5E resources, 1DDD text layout, 1E35 formatting/util, 1ED7 main loop, 205E graphics, 24E1 planner, 293D broadcast, 2C18 database, 2DA8 market, 322A finance, 329E schedule, 34B8 ratings, 36A7 dialogs, 3B74 screens, 3DD4/3DD9 simulation, 42BA rivals (10 functions, 17,500 decompiled lines), 448B events. Roles come from call structure and from which of the 430 messages each module shows.

- Messages: FUN_1054_0a0b reads 430 NUL-terminated strings from offset 0 of slot 199/253 into the far-pointer array at DS:8390 (string 0 is a filler). Code refers to message i as DS:8390 + 4*i.
- New game: the same function copies slot 208 into DS:1D00 (8), 6165 (0xD0), 8BDC (0x5C), 5013 (0x540), 6235 (0x180), 268E (0x60), 1D09 (0x738, player records of 0x1CE bytes indexed by DS:7A36), 724A (0x58), 7B42 (0xB4), 7C82 (0x6AC), 7160 (0x80). Game state is in the data segment; these areas are in the state layout as raw ranges until their fields are named.
- Copy protection: none. No check runs before play and no protection text exists in the executable or resources. Nothing to skip.

## Intro (Task 14)

- INTRO.EXE loads `MCx.BIN` (x = adapter letter from detection, V for VGA) and `DATAx.PCK`. The driver's first word is the paragraph offset of its data; its code starts at byte 2 with a jump table of 11 entries at multiples of 3. Entries: 0 init (mode 0Dh, palette, clear, sprite mask), 1 spark sprite with save-under, 2 restore save-under, 3 XOR fuse line (plane 0) towards (160, 200), 4 fire tile, 5 retrace wait, 6 page flip, 7 logo to off-screen memory, 8 logo band slide, 9 border pixels on the visible page, 10 fade out and text mode.
- DATAV.PCK (25,000 bytes unpacked): spark frames 0-0x117F (10 frames, 28 rows x 4 planes x 4 bytes, words big-endian), fire tiles from 0x1180 (7 layers x 53 path positions), logo at 0x5080 (4 planes x 18 x 61). Tables come from MCV.BIN data (palette, tile positions and sources, spark offsets) and INTRO.EXE data (spark path phases, layer delays, tile heights, logo steps).
- `src/intro` ports main and the driver onto an EGA memory model with per-pixel provenance. Compared with DOSBox captures of every retrace: all 382 distinct frames match pixel for pixel in order (evidence `analysis/evidence/intro-frames.json`, test `corpus.intro`). The intro lasts 1,110 retraces (15.9 s at 70 Hz); a key press jumps to the 32-step fade.

## Translated game (Tasks 15-17)

- Clock and calendar: hour at DS:71EA, minute at DS:71EC, day at DS:71E4 (1 on the first day). A new game starts at 17:00; after midnight the day advances and the clock restarts at 17:00.
- Language switch: DS:055C selects the language. Startup (0x10F4B) loads the main message table (slot 199/253) into a buffer at DS:8BAE, points the 430 entries at DS:8390 into it, copies 7/6/10 bytes of messages 63/67/69 into small buffers (pointers at DS:8BF3/8C0A/8C21), and stores pointers to messages 71/76/145 at DS:8BF7/8C0E/8C25 (these sit inside the state block DS:8BDC). Rooms load their own texts by DS:055C when they open. The port switches by loading the other table into a host area at 9800:0000 (kept from DOS; the game's peak use over every room is segment 9054) and repointing those entries; switching back repoints into the game's own buffer, so memory ends exactly as the game left it.
- Hotkeys: FUN_205e_16ee polls the keyboard every main-loop step. Keys map through DS:16AE (BIOS ASCII) / DS:1730 (scan codes); 0x1F is a left click (Enter maps to it), 0x14 a right click, 0x15-0x1C move the cursor, everything else goes to FUN_1054_085e (Space opens a screen through FUN_15ff_000a).
- Language prompt race in the original: the prompt loop reads keys itself right after the main-loop step has polled them. Which reader gets a key depends on when it arrives. In DOSBox, Space and Enter at the prompt were taken by the prompt and ignored. When the poller gets Space, the hotkey screen runs before any game data exists, random(0) divides by zero and the program ends with "Divide error" (exit code 3). The port passes the language as the game's own argument, so the prompt is never shown.
- The Palladix timer handler calls the BIOS INT 8 through a frame it builds itself (pushf; push cs; push 516h; jmp far [old]) and does the music work at 0x516 after the BIOS IRET. The runtime's BIOS chain now continues there; before this fix the music code never ran and the stack was left unbalanced when it was due.
- DOS memory blocks: real DOS keeps a 1-paragraph arena header before each block, the runtime does not, so blocks the game allocates sit 1 paragraph lower than in DOSBox (the sound driver at 4E9C instead of 4E9D). The game does not read the headers.

## Music (Task 19)

- All music and sound go through the Palladix AdLib driver (slot 98); the resources hold no sampled sound.
- The driver calibrates its register-write delays at start (0xD14): it times 256 reads of the AdLib port with PIT channel 2 (gate on port 61h bit 0, count read from port 42h) and stores 0x400/t+1 and 0x1C00/t+1 as the index and data delay loop counts (DS:AA9/AAB). DOSBox measures 3 and 15. Without a running channel 2 the counts become about 1000 and 7000, and each register write takes 9 ms.
- Title music: the native run writes the same 1800 register values as DOSBox from the first note on, with note times within 6 ms (reference/runs/title-music).

## Upscaled rendering (Task 20)

- Image decoding: FUN_47b4_04ac (0x37FEC) gets the packed entry (far pointer at [bp+6]) and the output buffer; every pixel goes through FUN_47b4_016d (0x37B59), which stores it with `mov es:[bx], al` at 0x37C36 while DS:A684 is the frame number + 1, DS:A672 the column and DS:A64C the row. The port tags each such store with (slot, frame, x, y); the slot is found from the first 512 bytes of the packed entry.
- What the screen shows without a source image: rectangles and lines the code fills in one colour (building walls, dialog boxes, frame borders) and the animation deltas (FUN_5456_0000 at 0x54560 writes them with stosb/rep stosb). Over the save route about 12% of the non-zero pixels are of this kind.
