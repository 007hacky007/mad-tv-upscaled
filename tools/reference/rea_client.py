"""Project-local REA MCP client.

Starts REA with a scoped environment: Ghidra from $GHIDRA_INSTALL_DIR (default ~/tools), no
REA_GHIDRA_CHECKPOINT_DIR (checkpoints reject DOS MZ). Global settings are untouched.
"""
import json
import os
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
GHIDRA = os.environ.get("GHIDRA_INSTALL_DIR", str(pathlib.Path.home() / "tools/ghidra_12.2_PUBLIC"))
JAVA_HOME = "/Library/Java/JavaVirtualMachines/temurin-27.jdk/Contents/Home"


def scrub(text):
    return text.replace(str(ROOT) + "/", "").replace(os.path.expanduser("~") + "/", "<user-home>/")


class Rea:
    def __init__(self, log=ROOT / "work/rea-stderr.log"):
        env = {k: v for k, v in os.environ.items() if not k.startswith("REA_")}
        env.update(GHIDRA_INSTALL_DIR=GHIDRA, JAVA_HOME=JAVA_HOME, TMPDIR=str(ROOT / "tools/cache/ghidra"))
        self.log = open(log, "w")
        self.p = subprocess.Popen(["node", str(ROOT / "tools/rea/node_modules/rea-agents/scripts/rea.mjs"), "--mcp"],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log, env=env, text=True, bufsize=1)
        self.next_id = 1
        self._rpc("initialize", {"protocolVersion": "2024-11-05", "capabilities": {}, "clientInfo": {"name": "mad-tv-analysis", "version": "1"}})
        self.p.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
        self.p.stdin.flush()

    def _rpc(self, method, params):
        i = self.next_id
        self.next_id += 1
        self.p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": i, "method": method, "params": params}) + "\n")
        self.p.stdin.flush()
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("REA closed, exit " + str(self.p.poll()))
            r = json.loads(line)
            if r.get("id") == i:
                return r

    def call(self, name, args=None, save=None):
        """Call one tool. Returns (structured_result_or_text, raw). `save` writes scrubbed evidence under analysis/evidence/rea/."""
        raw = self._rpc("tools/call", {"name": name, "arguments": args or {}})
        if save:
            (ROOT / "analysis/evidence/rea" / (save + ".json")).write_text(scrub(json.dumps(raw, indent=2)))
        res = raw.get("result", {})
        if "structuredContent" in res:
            return res["structuredContent"], raw
        text = "".join(c.get("text", "") for c in res.get("content", []))
        try:
            return json.loads(text), raw
        except ValueError:
            return text, raw

    def close(self):
        try:
            self.call("close_binary")
        finally:
            self.p.stdin.close()
            try:
                self.p.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.p.terminate()
                self.p.wait(timeout=20)
            self.log.close()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()
