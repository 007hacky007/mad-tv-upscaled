#include "data/lzexe.h"
#include "data/mz.h"
#include <algorithm>
#include <cstring>
namespace mtv {
namespace {
struct In {
    std::span<const uint8_t> d; size_t pos;
    bool ok(size_t n) const { return pos <= d.size() && n <= d.size() - pos; }
    bool u8(uint8_t& v) { if (!ok(1)) return false; v = d[pos++]; return true; }
    bool u16(uint16_t& v) { if (!ok(2)) return false; v = uint16_t(d[pos] | (d[pos + 1] << 8)); pos += 2; return true; }
};
struct Bits {
    In& in; uint16_t buf = 0; int count = 0;
    bool start() { count = 16; return in.u16(buf); }
    bool bit(int& b) {
        b = buf & 1;
        if (--count == 0) { count = 16; return in.u16(buf); }
        buf >>= 1; return true;
    }
};
DataError fail(size_t at, const char* why) { return DataError{"lzexe", "", at, why}; }
void put16(std::vector<uint8_t>& v, size_t at, uint16_t x) { v[at] = uint8_t(x); v[at + 1] = uint8_t(x >> 8); }
}
Result<std::vector<uint8_t>> unlzexe91(std::span<const uint8_t> file) {
    auto head = parse_mz(file);
    if (!head.has_value()) return head.error();
    if (file.size() < 0x20 || std::memcmp(file.data() + 0x1C, "LZ91", 4) != 0) return fail(0x1C, "not an LZEXE 0.91 file");
    const auto& h = head.value().header;
    const size_t loader = (size_t(h.header_paragraphs) + h.cs) * 16;
    In li{file, loader};
    uint16_t inf[8];
    for (auto& w : inf) if (!li.u16(w)) return fail(loader, "loader block truncated");
    // Relocations: 0.91 delta encoding at loader + 0x158.
    std::vector<std::pair<uint16_t, uint16_t>> relocs;
    In ri{file, loader + 0x158};
    uint32_t off = 0, seg = 0;
    for (;;) {
        uint8_t span8; if (!ri.u8(span8)) return fail(ri.pos, "relocation table truncated");
        uint16_t span = span8;
        if (span == 0) {
            if (!ri.u16(span)) return fail(ri.pos, "relocation table truncated");
            if (span == 0) { seg += 0x0FFF; continue; }
            if (span == 1) break;
        }
        off += span; seg += (off & ~0xFu) >> 4; off &= 0xF;
        if (seg > 0xFFFF) return fail(ri.pos, "relocation segment overflow");
        relocs.emplace_back(uint16_t(off), uint16_t(seg));
    }
    // Compressed load module.
    std::vector<uint8_t> out;
    In di{file, size_t(h.header_paragraphs) * 16};
    Bits bits{di};
    if (!bits.start()) return fail(di.pos, "stream truncated");
    for (;;) {
        int b;
        if (!bits.bit(b)) return fail(di.pos, "stream truncated");
        if (b) { uint8_t v; if (!di.u8(v)) return fail(di.pos, "stream truncated"); out.push_back(v); continue; }
        if (!bits.bit(b)) return fail(di.pos, "stream truncated");
        int len; uint16_t span;
        if (!b) {
            int b1, b2;
            if (!bits.bit(b1) || !bits.bit(b2)) return fail(di.pos, "stream truncated");
            len = ((b1 << 1) | b2) + 2;
            uint8_t lo; if (!di.u8(lo)) return fail(di.pos, "stream truncated");
            span = uint16_t(lo | 0xFF00);
        } else {
            uint8_t lo, hi; if (!di.u8(lo) || !di.u8(hi)) return fail(di.pos, "stream truncated");
            span = uint16_t(lo | ((hi & ~7) << 5) | 0xE000);
            len = (hi & 7) + 2;
            if (len == 2) {
                uint8_t n; if (!di.u8(n)) return fail(di.pos, "stream truncated");
                if (n == 0) break;
                if (n == 1) continue;
                len = n + 1;
            }
        }
        size_t back = 0x10000 - span;
        if (back > out.size()) return fail(di.pos, "back reference before start of output");
        size_t from = out.size() - back;
        for (int i = 0; i < len; ++i) out.push_back(out[from + i]);
    }
    // Rebuild an uncompressed MZ file.
    size_t header_len = (0x1C + 4 * relocs.size() + 15) & ~size_t(15);
    size_t total = header_len + out.size();
    if (relocs.size() > 0xFFFF || total > 0xFFFF * 512) return fail(0, "unpacked image too large");
    std::vector<uint8_t> mz(header_len, 0);
    mz[0] = 'M'; mz[1] = 'Z';
    long minalloc = long(h.minalloc) - (long(inf[5]) + ((long(inf[6]) + 15) >> 4) + 9);
    put16(mz, 2, uint16_t(total % 512)); put16(mz, 4, uint16_t((total + 511) / 512));
    put16(mz, 6, uint16_t(relocs.size())); put16(mz, 8, uint16_t(header_len / 16));
    put16(mz, 10, uint16_t(std::max(0L, minalloc))); put16(mz, 12, h.maxalloc);
    put16(mz, 14, inf[3]); put16(mz, 16, inf[2]); put16(mz, 20, inf[0]); put16(mz, 22, inf[1]); put16(mz, 24, 0x1C);
    for (size_t i = 0; i < relocs.size(); ++i) { put16(mz, 0x1C + 4 * i, relocs[i].first); put16(mz, 0x1E + 4 * i, relocs[i].second); }
    mz.insert(mz.end(), out.begin(), out.end());
    return mz;
}
}
