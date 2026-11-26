"""Run one REA tool call against a target through the project-local REA process.

Example: python3 tools/reference/rea_probe.py work/unpacked/MTV.EXE procedure_assembly '{"procedure":"0x49cde"}' --save mtv-rand-asm
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rea_client import Rea, ROOT  # noqa: E402


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("target")
    p.add_argument("tool")
    p.add_argument("args", nargs="?", default="{}")
    p.add_argument("--save", help="evidence name under analysis/evidence/rea/")
    a = p.parse_args()
    with Rea() as r:
        r.call("open_binary", {"path": str((ROOT / a.target).resolve()), "provider_id": "ghidra"})
        res, _ = r.call(a.tool, json.loads(a.args), save=a.save)
        print(json.dumps(res, indent=2) if not isinstance(res, str) else res)


if __name__ == "__main__":
    main()
