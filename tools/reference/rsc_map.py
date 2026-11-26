"""Build analysis/rsc-map.json: kind, language and loader for every used RSC slot.

Kinds for Image and Music come from successful decoding (mtv-tool classify); everything else is
annotated here from the code evidence in analysis/findings.md.
"""
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LANG_PAIRS = [(199, 253), (42, 246), (207, 252), (47, 251), (45, 250), (46, 249), (49, 248), (44, 247)]  # DS:055E table
NOTES = {
    1: ("Palette", "none", "FUN_1d5e_00a0", "256-entry VGA DAC table (2-byte header + 768); identical to 299"),
    299: ("Palette", "none", "FUN_1d5e_00a0 (main)", "same bytes as slot 1; set by FUN_4743_0001"),
    41: ("Text", "both", "FUN_1d5e_00fa(0x29)", "language prompt strings (D)eutsch / (E)nglish"),
    95: ("Driver", "none", "FUN_485d_029a(0x5f) in FUN_45f8_0003", "PALLADIX-PSM V1.1 PC-Beeper driver"),
    98: ("Driver", "none", "FUN_485d_029a(0x62) in FUN_45f8_0003", "PALLADIX-PSM V1.3 AdLib driver"),
    99: ("Driver", "none", "FUN_485d_029a(99) in FUN_45f8_0003", "PALLADIX-PSMDETECT: AdLib / PC-Beeper detection"),
    43: ("Table", "none", "unknown (Task 12)", "18-byte records; same layout as 210, different values"),
    210: ("Table", "none", "unknown (Task 12)", "18-byte records; same layout as 43, different values"),
    48: ("Table", "none", "unknown (Task 12)", "714 bytes, mostly zero"),
    164: ("Table", "none", "FUN_1d5e_00a0(0xa4) in main -> DS:7C7E", "1-bit mask, 21 words x 109 rows, indexed (y*21 + x/8); matches the 160x109 US map (slot 70): broadcast coverage"),
    3: ("Font", "none", "FUN_1d5e_01a1(3) in main -> DS:8386", "118 glyph frames, 8 px high; char -> frame via DS:012E, widths DS:022E"),
    201: ("Table", "none", "FUN_485d_029a(0xc9) in main", "loaded into DS buffer 24A9"),
    202: ("Table", "none", "FUN_485d_029a(0xca) in main", "loaded into DS buffer 2646"),
    203: ("Table", "none", "FUN_485d_029a(0xcb) in main", "loaded into DS buffer 715C"),
    204: ("Table", "none", "FUN_485d_029a(0xcc)", "loaded into DS:3F4F"),
    205: ("Table", "none", "FUN_485d_029a(0xcd)", "loaded into DS:2731"),
    206: ("Table", "none", "unknown (Task 12)", ""),
    208: ("Table", "none", "FUN_485d_029a(0xd0)", "loaded into DS:8FB0"),
    209: ("Table", "none", "FUN_485d_0336 / FUN_485d_04f3 (0xd1, 400)", "written back into SOURCE1.RSC by the game; the port keeps it in the user directory"),
}


def main():
    classes = json.loads(sys.stdin.read())  # from `mtv-tool classify --data <dir>`
    out = {"schema_version": 1, "slots": []}
    lang = {}
    for de, en in LANG_PAIRS:
        lang[de] = ("de", en)
        lang[en] = ("en", de)
    for c in classes:
        s = c["slot"]
        entry = {"slot": s, "archive": c["archive"], "size": c["size"], "kind": c["kind"], "language": "none", "loader": "", "note": ""}
        if c["kind"] == "Image":
            entry["loader"] = "FUN_1d5e_011b / FUN_1d5e_01a1 (packed image, decoder 0x47FEC)"
            entry["frames"] = c.get("frames")
        elif c["kind"] == "Music":
            entry["loader"] = "FUN_45f8_024c (LZW, decoder 0x46F49) via FUN_45f8_037c event table"
        if s in NOTES:
            k, l, loader, note = NOTES[s]
            entry.update(kind=k, language=l, loader=loader, note=note)
        if s in lang:
            entry.update(kind="Text", language=lang[s][0], pair=lang[s][1], loader="language table DS:055E")
        out["slots"].append(entry)
    (ROOT / "analysis/rsc-map.json").write_text(json.dumps(out, indent=1) + "\n")
    kinds = {}
    for e in out["slots"]:
        kinds[e["kind"]] = kinds.get(e["kind"], 0) + 1
    print(json.dumps(kinds))


if __name__ == "__main__":
    main()
