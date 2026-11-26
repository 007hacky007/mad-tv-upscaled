"""Make a probe copy of the unpacked MTV.EXE that reports timer-counter reads.

The getter at 0x465DE (`mov ax,[9034]; jmp $+2; retf`) becomes `mov ax,[9034]; out 0xEE,ax; retf`.
The patched DOSBox-X logs every write to port 0xEE with the guest millisecond tick (`iolog`).
Only the private copy used by the reference harness is patched.
"""
import sys
from pathlib import Path

GETTER = 0x365DE  # load-image offset of FUN_465d_000e
ORIGINAL = bytes.fromhex("a13490eb00cb")
PATCHED = bytes.fromhex("a13490e7eecb")
PORT = 0xEE


def patch(exe: bytes) -> bytes:
    header = int.from_bytes(exe[8:10], "little") * 16
    at = header + GETTER
    if exe[at:at + 6] != ORIGINAL:
        raise ValueError("unexpected bytes at the counter getter; not the expected unpacked MTV.EXE")
    out = bytearray(exe)
    out[at:at + 6] = PATCHED
    return bytes(out)


if __name__ == "__main__":
    Path(sys.argv[2]).write_bytes(patch(Path(sys.argv[1]).read_bytes()))
