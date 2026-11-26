"""Play a tick-addressed input script in the reference and record what the game saw.

Script: JSON list of {"tick": t, "keys": [...], "mouse": [x, y], "buttons": b, "dump": true}
in 100 Hz ticks since boot, strictly increasing. Keys are pressed at `tick` and released
KEY_HOLD_TICKS later. Mouse position and button state persist until changed.
Output: a tick recording (schemas/tick-input.schema.json) plus state dumps.
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

TICK_MS = 10
KEY_HOLD_TICKS = 5
COUNTER_PORTS = {0xEE: 0x9034}
# Bytes after the counter-getter call inside random() (return address 0x1D812): add ax,[bp+8]; xor dx,dx
RANDOM_CALLER_SIGNATURE = "03460833d2"


def play(session, script, on_tick=None):
    ticks = [s["tick"] for s in script]
    if any(b <= a for a, b in zip(ticks, ticks[1:])):
        raise ValueError("script ticks must strictly increase")
    timeline = []  # (tick, order, kind, payload)
    for i, s in enumerate(script):
        timeline.append((s["tick"], 1, i, "event", s))
        for k in s.get("keys", []):
            timeline.append((s["tick"] + KEY_HOLD_TICKS, 0, i, "release", k))
    timeline.sort(key=lambda t: (t[0], t[1], t[2]))
    now, mx, my, buttons = 0, 0, 0, 0
    events = []
    for tick, _, _, kind, payload in timeline:
        if tick > now:
            session.command(f"step ms {(tick - now) * TICK_MS}")
            now = tick
        if kind == "release":
            session.command(f"key {payload} up")
            continue
        s = payload
        if "mouse" in s:
            mx, my = s["mouse"]
            if not (0 <= mx < 320 and 0 <= my < 200):
                raise ValueError("mouse outside 320x200")
            session.command(f"mouse {mx * 10} {my * 16}")
        if "buttons" in s and s["buttons"] != buttons:
            for bit in (0, 1):
                if (s["buttons"] ^ buttons) & (1 << bit):
                    session.command(f"button {bit} {'down' if s['buttons'] & (1 << bit) else 'up'}")
            buttons = s["buttons"]
        for k in s.get("keys", []):
            session.command(f"key {k} down")
        events.append({"tick": tick, "mouse_x": mx, "mouse_y": my, "buttons": buttons, "keys": list(s.get("keys", []))})
        if on_tick and s.get("dump"):
            on_tick(tick)
    return events


def parse_iolog(text):
    reads = []
    for line in text.splitlines():
        if not line.strip():
            continue
        ms, port, value = (int(x) for x in line.split())
        if port in COUNTER_PORTS:
            reads.append({"tick": ms // TICK_MS, "address": COUNTER_PORTS[port], "value": value})
    return reads


def main():
    import launch
    import dump_state
    import probe_patch
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("script", type=Path)
    p.add_argument("out", type=Path)
    p.add_argument("--data", type=Path, default=ROOT / "mad-tv")
    p.add_argument("--exe", type=Path, default=ROOT / "work/unpacked/MTV.EXE")
    a = p.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    probe = out / "MTV-probe.EXE"
    probe.write_bytes(probe_patch.patch(a.exe.read_bytes()))
    script = json.loads(a.script.read_text())
    dumps = []
    with launch.session(out / "dosbox", a.data, "MTV.EXE", replace={"MTV.EXE": probe}) as s:
        s.command(f"iosig {RANDOM_CALLER_SIGNATURE}")
        def on_tick(t):
            dumps.append(dump_state.dump_session(s, out / "dosbox", t))
            s.frame(str(out / f"frame-{t:07d}.png"))
        events = play(s, script, on_tick)
        s.command(f'iolog "{out / "iolog.txt"}"')
    rec = {"schema_version": 1, "tick_hz": 100, "events": events, "timer_reads": parse_iolog((out / "iolog.txt").read_text())}
    (out / "recording.json").write_text(json.dumps(rec, indent=1) + "\n")
    (out / "dumps.json").write_text(json.dumps(dumps, indent=1) + "\n")
    print(json.dumps({"events": len(events), "timer_reads": len(rec["timer_reads"]), "dumps": len(dumps)}))


if __name__ == "__main__":
    main()
