"""Guest-only DOSBox-X control; no macOS capture or accessibility APIs."""
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
BINARY = ROOT / "tools/cache/dosbox-mac/dosbox-x-dosbox-x-v2026.10.01/src/dosbox-x"


def quote(path):
    return json.dumps(str(path), ensure_ascii=False)


class Session:
    """One private patched DOSBox-X process controlled over a Unix socket. `conf` is the full config text."""

    def __init__(self, directory, conf):
        self.directory = Path(directory).resolve()
        self.directory.mkdir(parents=True, exist_ok=True)
        (self.directory / "drive-c").mkdir(exist_ok=True)
        self.conf = conf
        self.process = None
        self.socket = None
        self.reader = None
        self.transcript = []

    def __enter__(self):
        # macOS sockaddr_un has a 104-byte sun_path, so use a private short path.
        self.socket_dir = Path(tempfile.mkdtemp(prefix="dbxref-", dir="/tmp"))
        self.socket_path = self.socket_dir / "control.sock"
        config = self.directory / "dosbox.conf"
        config.write_text(self.conf)
        env = os.environ.copy()
        env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                   SDL_AUDIO_DRIVER="dummy", DOSBOX_REF_SOCKET=str(self.socket_path))
        self.log = (self.directory / "process.log").open("wb")
        try:
            self.process = subprocess.Popen([str(BINARY), "-conf", str(config), "-noconfig", "-nomenu"],
                                            cwd=self.directory, env=env, stdin=subprocess.DEVNULL,
                                            stdout=self.log, stderr=subprocess.STDOUT)
            end = time.monotonic() + 30
            while not self.socket_path.exists():
                if self.process.poll() is not None:
                    raise RuntimeError("DOSBox exited: " + (self.directory / "process.log").read_text(errors="replace")[-3000:])
                if time.monotonic() > end:
                    raise TimeoutError("DOSBox socket startup; see " + str(self.directory / "process.log"))
                time.sleep(.02)
            self.reconnect()
            return self
        except BaseException:
            self.close()
            raise

    def reconnect(self):
        if self.reader:
            self.reader.close()
        if self.socket:
            self.socket.close()
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(180)
        # The socket file exists slightly before DOSBox-X calls listen(); retry briefly.
        end = time.monotonic() + 10
        while True:
            try:
                self.socket.connect(str(self.socket_path))
                break
            except ConnectionRefusedError:
                if time.monotonic() > end:
                    raise
                time.sleep(.02)
        self.reader = self.socket.makefile("rb")

    def command(self, command):
        if "\n" in command or len(command.encode()) > 8192:
            raise ValueError("one command of at most 8192 bytes")
        self.socket.sendall(command.encode() + b"\n")
        line = self.reader.readline(8193)
        if not line.endswith(b"\n"):
            raise RuntimeError("incomplete protocol reply")
        response = json.loads(line)
        self.transcript.append({"command": command, "response": response})
        if not response.get("ok"):
            raise RuntimeError(response.get("error", "DOSBox command failed"))
        return response

    def step(self, ms):
        return self.command(f"step ms {ms}")

    def key(self, name, ms=30):
        self.command(f"key {name} down")
        self.step(ms)
        self.command(f"key {name} up")
        self.step(ms)

    def click(self, x, y, button=0):
        self.command(f"mouse {x} {y}")
        self.step(50)
        self.command(f"button {button} down")
        self.step(50)
        self.command(f"button {button} up")
        self.step(100)

    def frame(self, name):
        path = self.directory / name
        self.command("frame " + quote(path))
        return path

    def memory(self, path, start=0, size=32 * 1024 * 1024):
        self.command(f"memory {start} {size} " + quote(path))
        return Path(path).read_bytes()

    def close(self):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        if self.reader:
            self.reader.close()
        if self.socket:
            self.socket.close()
        if hasattr(self, "log"):
            self.log.close()
        if hasattr(self, "socket_dir"):
            shutil.rmtree(self.socket_dir)

    def __exit__(self, *args):
        (self.directory / "protocol.json").write_text(json.dumps(self.transcript, indent=2) + "\n")
        self.close()
