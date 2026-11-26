#include "data/powerpacker.h"
namespace mtv {
Result<std::vector<uint8_t>> unpack_pp20(std::span<const uint8_t> f) {
    auto fail = [](size_t at, const char* why) -> Result<std::vector<uint8_t>> { return DataError{"pp20", "", at, why}; };
    if (f.size() < 12 || f[0] != 'P' || f[1] != 'P' || f[2] != '2' || f[3] != '0') return fail(0, "not a PP20 file");
    const uint8_t lens[4] = {f[4], f[5], f[6], f[7]};
    for (auto l : lens) if (l == 0 || l > 15) return fail(4, "invalid offset bit length");
    const size_t n = f.size();
    const uint32_t out_len = uint32_t(f[n - 4]) << 16 | uint32_t(f[n - 3]) << 8 | f[n - 2];
    const uint8_t skip = f[n - 1];
    std::vector<uint8_t> out(out_len);
    size_t src = n - 4;           // bytes are consumed backwards from here down to 8
    const size_t src_begin = 8;
    uint32_t buffer = 0; int left = 0;
    auto bits = [&](int count, uint32_t& v) -> bool {
        while (left < count) {
            if (src <= src_begin) return false;
            buffer |= uint32_t(f[--src]) << left;
            left += 8;
        }
        v = 0;
        for (int i = 0; i < count; ++i) { v = (v << 1) | (buffer & 1); buffer >>= 1; }
        left -= count;
        return true;
    };
    size_t pos = out_len; // output is written backwards
    uint32_t x;
    if (!bits(skip, x)) return fail(n - 1, "stream truncated");
    while (pos > 0) {
        if (!bits(1, x)) return fail(src, "stream truncated");
        if (x == 0) {
            uint32_t todo = 1;
            do { if (!bits(2, x)) return fail(src, "stream truncated"); todo += x; } while (x == 3);
            while (todo--) {
                if (!bits(8, x)) return fail(src, "stream truncated");
                if (pos == 0) return fail(src, "literal past start of output");
                out[--pos] = uint8_t(x);
            }
            if (pos == 0) break;
        }
        if (!bits(2, x)) return fail(src, "stream truncated");
        uint32_t offbits = lens[x], todo = x + 2, offset;
        if (x == 3) {
            if (!bits(1, x)) return fail(src, "stream truncated");
            if (x == 0) offbits = 7;
            if (!bits(int(offbits), offset)) return fail(src, "stream truncated");
            do { if (!bits(3, x)) return fail(src, "stream truncated"); todo += x; } while (x == 7);
        } else if (!bits(int(offbits), offset)) return fail(src, "stream truncated");
        if (pos + offset >= out_len) return fail(src, "match reaches past end of output");
        while (todo--) {
            if (pos == 0) return fail(src, "match past start of output");
            out[pos - 1] = out[pos + offset];
            --pos;
        }
    }
    return out;
}
}
