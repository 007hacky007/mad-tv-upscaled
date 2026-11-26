"""LZEXE 0.91 unpacker (Python twin of src/data/lzexe.cpp, used by the build-time generator)."""
import struct


def unpack(d):
    h = list(struct.unpack("<14H", d[:28]))
    if d[0x1C:0x20] != b"LZ91":
        raise ValueError("not LZEXE 0.91")
    fpos = (h[0x0B] + h[4]) << 4
    inf = struct.unpack("<8H", d[fpos:fpos + 16])
    p = fpos + 0x158; relocs = []; off = 0; seg = 0
    while True:
        span = d[p]; p += 1
        if span == 0:
            span = struct.unpack("<H", d[p:p + 2])[0]; p += 2
            if span == 0:
                seg += 0x0FFF; continue
            if span == 1:
                break
        off += span; seg += (off & ~0xF) >> 4; off &= 0xF
        relocs.append((off, seg))
    q = h[4] << 4; out = bytearray()
    buf = struct.unpack("<H", d[q:q + 2])[0]; q += 2; cnt = 16

    def bit():
        nonlocal buf, cnt, q
        b = buf & 1; cnt -= 1
        if cnt == 0:
            buf = struct.unpack("<H", d[q:q + 2])[0]; q += 2; cnt = 16
        else:
            buf >>= 1
        return b
    while True:
        if bit():
            out.append(d[q]); q += 1; continue
        if bit() == 0:
            ln = bit() << 1; ln |= bit(); ln += 2
            span = d[q] | 0xFF00; q += 1
        else:
            span = d[q]; ln = d[q + 1]; q += 2
            span |= ((ln & ~7) << 5) | 0xE000
            ln = (ln & 7) + 2
            if ln == 2:
                ln = d[q]; q += 1
                if ln == 0:
                    break
                if ln == 1:
                    continue
                ln += 1
        span -= 0x10000
        for _ in range(ln):
            out.append(out[len(out) + span])
    hdrlen = (0x1C + 4 * len(relocs) + 15) & ~15
    hdr = bytearray(hdrlen)
    total = hdrlen + len(out)
    minalloc = max(0, h[5] - (inf[5] + ((inf[6] + 15) >> 4) + 9))
    struct.pack_into("<14H", hdr, 0, 0x5A4D, total % 512, (total + 511) // 512, len(relocs), hdrlen >> 4, minalloc, h[6], inf[3], inf[2], 0, inf[0], inf[1], 0x1C, 0)
    for i, (o, s) in enumerate(relocs):
        struct.pack_into("<HH", hdr, 0x1C + 4 * i, o, s)
    return bytes(hdr + out)
