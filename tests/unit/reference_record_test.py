import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/reference"))
import dump_state  # noqa: E402
import compare  # noqa: E402
import probe_patch  # noqa: E402
import record  # noqa: E402


class FakeSession:
    def __init__(self):
        self.commands = []
        self.ms = 0

    def command(self, c):
        self.commands.append(c)
        if c.startswith("step ms "):
            self.ms += int(c.split()[2])
        return {"ok": True, "ticks": self.ms}


class RecordTest(unittest.TestCase):
    def test_record_writes_one_entry_per_input_tick(self):
        script = [{"tick": 10, "keys": ["e"]}, {"tick": 30, "mouse": [63, 100], "buttons": 1}, {"tick": 31, "buttons": 0}]
        s = FakeSession()
        events = record.play(s, script)
        self.assertEqual([e["tick"] for e in events], [10, 30, 31])
        self.assertEqual(events[1]["mouse_x"], 63)
        self.assertEqual(events[2]["mouse_x"], 63)  # position persists
        self.assertEqual(events[2]["buttons"], 0)
        self.assertIn("mouse 630 1600", s.commands)
        self.assertIn("button 0 down", s.commands)
        self.assertIn("button 0 up", s.commands)
        self.assertEqual(s.ms, 310 + record.KEY_HOLD_TICKS * 0)  # ends at last event tick in ms

    def test_record_rejects_unsorted_script(self):
        with self.assertRaises(ValueError):
            record.play(FakeSession(), [{"tick": 5}, {"tick": 5}])

    def test_timer_log_parsed(self):
        log = "1230 238 7\n1240 238 8\n"
        reads = record.parse_iolog(log)
        self.assertEqual(reads, [{"tick": 123, "address": 0x9034, "value": 7}, {"tick": 124, "address": 0x9034, "value": 8}])


class DumpTest(unittest.TestCase):
    def test_dump_reads_layout_offsets(self):
        ds = bytearray(0x10000)
        ds[0x55C:0x55E] = (1).to_bytes(2, "little")
        ds[0x100:0x104] = (123456).to_bytes(4, "little", signed=True)
        layout = {"schema_version": 1, "fields": [
            {"name": "language", "offset": "0x55C", "type": "u16"},
            {"name": "money", "offset": "0x100", "type": "i32"},
            {"name": "raw", "offset": "0x200", "type": "bytes", "size": 2}]}
        d = dump_state.dump(bytes(ds), layout, tick=77)
        self.assertEqual(d["tick"], 77)
        self.assertEqual(d["language"], 1)
        self.assertEqual(d["money"], 123456)
        self.assertEqual(d["raw"], "0000")

    def test_find_ds_from_turbo_c_signature(self):
        mem = bytearray(0x20000)
        mem[0x12344:0x12344 + 28] = b"Turbo-C - Copyright (c) 1988"
        self.assertEqual(dump_state.find_ds(bytes(mem)), 0x1234)


class CompareTest(unittest.TestCase):
    def test_compare_reports_first_difference_per_dump(self):
        a = [{"tick": 1, "money": 200, "r": [1, 2]}, {"tick": 2, "money": 210}]
        b = [{"tick": 1, "money": 200, "r": [1, 3]}, {"tick": 2, "money": 210}]
        self.assertEqual(compare.compare_runs(a, b), [(1, ["/r/1"])])

    def test_compare_rejects_tick_mismatch(self):
        with self.assertRaises(ValueError):
            compare.compare_runs([{"tick": 1}], [{"tick": 2}])


class ProbePatchTest(unittest.TestCase):
    def test_getter_reports_counter_on_port(self):
        exe = bytearray(0x4680 + 0x365E4 + 16)
        exe[8:10] = (0x4680 // 16).to_bytes(2, "little")
        at = 0x4680 + 0x365DE
        exe[at:at + 6] = bytes.fromhex("a13490eb00cb")
        out = probe_patch.patch(bytes(exe))
        self.assertEqual(out[at:at + 6], bytes.fromhex("a13490e7eecb"))

    def test_patch_refuses_unexpected_bytes(self):
        exe = bytearray(0x4680 + 0x365E4 + 16)
        exe[8:10] = (0x4680 // 16).to_bytes(2, "little")
        with self.assertRaises(ValueError):
            probe_patch.patch(bytes(exe))


if __name__ == "__main__":
    unittest.main()
