"""Build and check analysis/function-map.json from the REA procedure inventory.

Modules follow Turbo C's large model: one code segment per original source file.
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# segment -> (class, subsystem, port target, note)
MODULES = {
    "1000": ("crt", "crt", "-", "Turbo C startup and runtime helpers (long arithmetic, exit)"),
    "1054": ("game", "startup", "src/game/startup", "main, language prompt, new game initialisation from slot 208, message table"),
    "148c": ("game", "state", "src/game/state", "game state helpers, resource-backed tables"),
    "15ff": ("ui", "rooms", "src/ui/rooms", "studio rental, building encounters and greetings"),
    "1aa4": ("ui", "boss", "src/ui/boss", "boss office: daily review, credit, Sammy"),
    "1d5e": ("data_load", "resources", "src/data", "resource loaders, memory, random()"),
    "1ddd": ("ui", "text", "src/ui/text", "word-wrapped text layout"),
    "1e35": ("game", "util", "src/game/util", "string formatting (#s/#d/#l codes), memory copy"),
    "1ed7": ("ui", "main_loop", "src/game/loop", "frame loop, cursor, input dispatch"),
    "205e": ("ui", "graphics", "src/render", "sprites, blits, text drawing on the back buffer"),
    "24e1": ("ui", "planner", "src/ui/planner", "programme planner, money and rating displays"),
    "293d": ("ui", "broadcast", "src/ui/broadcast", "studio production and broadcast screens"),
    "2c18": ("data_load", "database", "src/game/database", "films, series, sponsors, people, countries from text and table slots"),
    "2da8": ("game", "market", "src/game/market", "buying, selling, auctions, library"),
    "322a": ("game", "finance", "src/game/finance", "money and credit"),
    "329e": ("game", "schedule", "src/game/schedule", "programme schedule"),
    "34b8": ("game", "ratings", "src/game/ratings", "viewer and rating calculation"),
    "36a7": ("ui", "dialogs", "src/ui/dialogs", "info board, messages, buy/sell dialogs, presents catalogue"),
    "3b74": ("ui", "screens", "src/ui/screens", "room screens, genre lists, news"),
    "3dd4": ("game", "simulation", "src/game/simulation", "small helpers of the simulation"),
    "3dd9": ("game", "simulation", "src/game/simulation", "daily simulation and rival stations"),
    "42ba": ("game", "rivals", "src/game/rivals", "computer players (10 very large functions)"),
    "448b": ("game", "events", "src/game/events", "world events and news generation"),
    "45f1": ("driver_sound", "sound", "src/audio", "sound glue"),
    "45f8": ("driver_sound", "sound", "src/audio", "Palladix driver setup, music event table"),
    "465d": ("driver_sound", "timer", "src/game/loop", "timer counter accessors"),
    "465f": ("driver_sound", "animation", "src/ui/animation", "animated sequences player"),
    "46f4": ("data_load", "lzw", "src/data/rsc_codec", "LZW decoder"),
    "471e": ("driver_sound", "sound", "src/audio", "driver detection and calls"),
    "4736": ("driver_input", "mouse", "src/platform/input", "mouse"), "4737": ("driver_input", "mouse", "src/platform/input", "mouse"),
    "473b": ("driver_input", "mouse", "src/platform/input", "mouse"),
    "473f": ("driver_video", "video", "src/render", "mode 13h"), "4742": ("driver_video", "video", "src/render", "vertical retrace wait"),
    "4743": ("driver_video", "video", "src/render", "set DAC block"), "4745": ("driver_video", "video", "src/render", "video"),
    "4747": ("driver_video", "blit", "src/render", "grab"), "474e": ("driver_video", "blit", "src/render", "opaque blit"),
    "4755": ("driver_video", "blit", "src/render", "blit"), "475b": ("driver_video", "blit", "src/render", "blit"),
    "4762": ("driver_video", "blit", "src/render", "blit"), "476d": ("driver_video", "blit", "src/render", "blit"),
    "477a": ("driver_video", "blit", "src/render", "masked blit"), "4786": ("driver_video", "blit", "src/render", "masked blit"),
    "4795": ("driver_video", "blit", "src/render", "masked blit"), "47a2": ("driver_video", "blit", "src/render", "masked blit"),
    "47b4": ("data_load", "image_codec", "src/data/rsc_codec", "packed image decoder"),
    "482c": ("driver_video", "image", "src/render", "image allocation"), "4833": ("driver_video", "image", "src/render", "image allocation"),
    "4839": ("driver_video", "image", "src/render", "image size"), "483e": ("driver_video", "blit", "src/render", "blit"),
    "4848": ("data_load", "resources", "src/data", "resource size"),
    "4858": ("driver_input", "keyboard", "src/platform/input", "flush keyboard"), "4859": ("driver_input", "keyboard", "src/platform/input", "read key"),
    "485d": ("data_load", "resources", "src/data/rsc", "RSC index and I/O"),
    "5456": ("driver_video", "animation", "src/data/animation", "animation delta codec"),
}
CRT_LIB = ("48bc", "48c0", "48c3", "48c6", "48f7", "490c", "490e", "4928", "492d", "4930", "4933", "4935", "4942", "4944",
           "495a", "495f", "4962", "4971", "4973", "4976", "497a", "497c", "4985", "49b8", "49b9", "49c1", "49c8", "49cc", "49d0")
KNOWN = {
    "0x10000": "entry", "0x10545": "main", "0x10f4b": "load_new_game_state_and_messages", "0x1d809": "random",
    "0x1d5e1": "resources_open", "0x1d680": "load_raw_resource", "0x1d6fb": "load_image_resource", "0x1d781": "load_image_resource_1",
    "0x1da6d": "resource_size_or_disk_prompt", "0x1e0a9": "draw_wrapped_text", "0x2c18c": "load_database_texts",
    "0x2078e": "update_cursor", "0x1f628": "frame_present", "0x20846": "draw_text_glyphs",
    "0x45f83": "sound_init", "0x461cc": "play_song_slot", "0x462fc": "play_music_event", "0x465d6": "timer_reset", "0x465de": "timer_get",
    "0x465e4": "timer_set", "0x465fb": "load_animation_slot", "0x46695": "animation_open", "0x46819": "animation_frame",
    "0x46834": "animation_tick", "0x469e2": "attract_sequence", "0x46f49": "lzw_decode", "0x47421": "wait_vretrace",
    "0x47431": "set_dac_block", "0x473f9": "set_mode_13h", "0x47476": "grab_image", "0x474e9": "blit_opaque", "0x477ac": "blit_masked",
    "0x47fec": "unpack_image", "0x47d3e": "unpack_image_stream", "0x48482": "resource_unpacked_size", "0x48584": "keyboard_flush",
    "0x48592": "keyboard_read", "0x48640": "rsc_build_index", "0x4886a": "rsc_read_slot", "0x48906": "rsc_write_slot",
    "0x48ac3": "rsc_resize_slot", "0x49ccd": "srand", "0x49cde": "rand", "0x54560": "animation_apply_delta",
}


def rea_procedures():
    raw = json.loads((ROOT / "analysis/evidence/rea/mtv-procedures.json").read_text())
    res = raw["result"].get("structuredContent") or json.loads(raw["result"]["content"][0]["text"])
    return {p["address"]: p["value"] for p in res["normalized_result"]}


def segment_of(name, address):
    m = re.match(r"(?:thunk_)?FUN_([0-9a-f]{4})_", name)
    if m:
        return m.group(1)
    return "1000" if int(address, 16) < 0x10545 else "????"


def build():
    out = []
    for addr, name in sorted(rea_procedures().items(), key=lambda kv: int(kv[0], 16)):
        seg = segment_of(name, addr)
        if seg == "????":  # functions Ghidra left unnamed by segment: assign by address range
            seg = "1054"
        if seg in CRT_LIB:
            cls, sub, target, note = "crt", "crt", "-", "Turbo C library (malloc, file I/O, strings, findfirst)"
        else:
            cls, sub, target, note = MODULES[seg]
        nm = KNOWN.get(addr, f"{sub}_{addr[2:]}")
        out.append({"address": addr, "ghidra_name": name, "segment": seg, "name": nm, "class": cls, "subsystem": sub,
                    "port_target": target, "notes": note if addr not in KNOWN else ""})
    return {"schema_version": 1, "binary": "MTV.EXE (unpacked) sha256 656e44191a6fa1bec77ee7ad26d94d05ff07c7c54980914564aa2d8803f841ca",
            "copy_protection": "none found: no protection prompt, no manual or code-wheel strings, no check before play", "functions": out}


if __name__ == "__main__":
    m = build()
    (ROOT / "analysis/function-map.json").write_text(json.dumps(m, indent=1) + "\n")
    counts = {}
    for f in m["functions"]:
        counts[f["subsystem"]] = counts.get(f["subsystem"], 0) + 1
    print(len(m["functions"]), json.dumps(counts))
