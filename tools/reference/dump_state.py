"""Read named game-state fields from a guest memory image using analysis/evidence/state-layout.json."""
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SIGNATURE = b"Turbo-C - Copyright (c) 1988"
TYPES = {"u8": "<B", "i8": "<b", "u16": "<H", "i16": "<h", "u32": "<I", "i32": "<i"}


def find_ds(memory: bytes) -> int:
    """DS segment of MTV.EXE: its data segment starts 4 bytes before the Turbo C copyright string."""
    i = memory.find(SIGNATURE)
    if i < 4 or (i - 4) % 16:
        raise ValueError("MTV.EXE data segment not found")
    return (i - 4) >> 4


def dump(ds_bytes: bytes, layout: dict, tick: int) -> dict:
    if layout.get("schema_version") != 1:
        raise ValueError("unsupported layout schema")
    out = {"tick": tick}
    for f in layout["fields"]:
        off = int(f["offset"], 16)
        if f["type"] == "bytes":
            out[f["name"]] = ds_bytes[off:off + f["size"]].hex()
        else:
            fmt = TYPES[f["type"]]
            count = f.get("count", 1)
            size = struct.calcsize(fmt)
            vals = [struct.unpack_from(fmt, ds_bytes, off + i * size)[0] for i in range(count)]
            out[f["name"]] = vals if "count" in f else vals[0]
    return out


def load_layout(path=ROOT / "analysis/evidence/state-layout.json"):
    return json.loads(Path(path).read_text())


def dump_session(session, work: Path, tick: int, layout=None) -> dict:
    """Dump state from a live reference session (paused)."""
    mem = session.memory(work / "conv.bin", 0, 0xA0000)
    ds = find_ds(mem) * 16
    return dump(mem[ds:ds + 0x10000], layout or load_layout(), tick)
