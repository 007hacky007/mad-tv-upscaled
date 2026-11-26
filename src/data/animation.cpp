#include "data/animation.h"
namespace mtv {
Result<std::vector<IndexedImage>> decode_animation(std::span<const uint8_t> d, const IndexedImage& back) {
    auto fail = [](size_t at, const char* why) -> Result<std::vector<IndexedImage>> { return DataError{"rsc", "animation", at, why}; };
    if (d.empty()) return fail(0, "empty");
    const size_t stride = (size_t(back.width) + 7) / 8 * 8;
    std::vector<uint8_t> buf(stride * back.height, 0);
    for (size_t y = 0; y < back.height; ++y)
        std::copy_n(back.indices.begin() + long(y * back.width), back.width, buf.begin() + long(y * stride));
    std::vector<IndexedImage> frames;
    size_t p = 1;
    for (unsigned f = 0; f < d[0]; ++f) {
        size_t di = 0;
        for (;;) {
            if (p >= d.size()) return fail(p, "frame not terminated");
            uint8_t op = d[p++];
            if (op == 0x62) break;
            if (op == 0x63) {
                size_t count = 0; uint8_t b;
                do { if (p >= d.size()) return fail(p, "run truncated"); b = d[p++]; count += b; } while (b == 0xFF);
                if (p >= d.size()) return fail(p, "run truncated");
                uint8_t v = d[p++];
                if (di + count > buf.size()) return fail(p, "run past end of buffer");
                if (v != 0x64) std::fill_n(buf.begin() + long(di), count, v);
                di += count;
            } else if (op == 0x64) {
                ++di;
            } else {
                if (di >= buf.size()) return fail(p, "pixel past end of buffer");
                buf[di++] = op;
            }
        }
        IndexedImage img{back.width, back.height, {}};
        for (size_t y = 0; y < back.height; ++y)
            img.indices.insert(img.indices.end(), buf.begin() + long(y * stride), buf.begin() + long(y * stride + back.width));
        frames.push_back(std::move(img));
    }
    return frames;
}
}
