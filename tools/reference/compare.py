"""Compare two lists of state dumps (dumps.json) tick by tick. Same semantics as mtv::compare_dumps."""
import json
import sys


def diff(a, b, path=""):
    if isinstance(a, dict) and isinstance(b, dict):
        out = []
        for k in a:
            p = path + "/" + k.replace("~", "~0").replace("/", "~1")
            out += [p] if k not in b else diff(a[k], b[k], p)
        out += [path + "/" + k.replace("~", "~0").replace("/", "~1") for k in b if k not in a]
        return out
    if isinstance(a, list) and isinstance(b, list):
        out = []
        for i in range(max(len(a), len(b))):
            p = f"{path}/{i}"
            out += [p] if i >= len(a) or i >= len(b) else diff(a[i], b[i], p)
        return out
    return [] if a == b else [path or "/"]


def compare_runs(a, b):
    """Return [(tick, [paths])] for every dump pair that differs."""
    if [d["tick"] for d in a] != [d["tick"] for d in b]:
        raise ValueError("dump ticks differ")
    return [(x["tick"], d) for x, y in zip(a, b) if (d := diff(x, y))]


if __name__ == "__main__":
    res = compare_runs(json.load(open(sys.argv[1])), json.load(open(sys.argv[2])))
    for tick, paths in res:
        print(tick, " ".join(paths))
    sys.exit(1 if res else 0)
