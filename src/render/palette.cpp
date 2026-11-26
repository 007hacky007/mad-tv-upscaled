#include "render/palette.h"
namespace mtv {
Result<Palette> parse_palette(std::span<const uint8_t> raw) {
    if (raw.size() < 2) return DataError{"rsc", "palette", 0, "missing count"};
    Palette p;
    p.count = uint16_t(raw[0] | raw[1] << 8);
    if (p.count == 0 || p.count > 256 || raw.size() < 2 + size_t(p.count) * 3) return DataError{"rsc", "palette", 0, "count does not fit the entry"};
    for (size_t i = 0; i < p.count; ++i)
        for (size_t c = 0; c < 3; ++c) {
            uint8_t v = raw[2 + i * 3 + c];
            if (v > 63) return DataError{"rsc", "palette", 2 + i * 3 + c, "DAC value above 63"};
            p.rgb6[i][c] = v;
        }
    return p;
}
std::vector<uint8_t> to_rgba(const IndexedImage& img, const Palette& pal, std::optional<uint8_t> transparent) {
    std::vector<uint8_t> out;
    out.reserve(img.indices.size() * 4);
    for (auto i : img.indices) {
        const auto& c = pal.rgb6[i];
        bool clear = transparent && i == *transparent;
        out.push_back(clear ? 0 : dac_to_8bit(c[0]));
        out.push_back(clear ? 0 : dac_to_8bit(c[1]));
        out.push_back(clear ? 0 : dac_to_8bit(c[2]));
        out.push_back(clear ? 0 : 255);
    }
    return out;
}
}
