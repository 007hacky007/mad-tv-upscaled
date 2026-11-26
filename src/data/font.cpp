#include "data/font.h"
namespace mtv {
Result<Font> build_font(const std::vector<IndexedImage>& frames, std::span<const uint8_t, 256> map, std::span<const uint8_t, 256> widths) {
    Font f;
    for (size_t c = 0; c < 256; ++c) {
        if (!map[c] || c == ' ') continue;
        if (map[c] > frames.size()) return DataError{"font", "glyph " + std::to_string(c), c, "glyph frame out of range"};
        const auto& img = frames[map[c] - 1u];
        f.height = std::max<uint8_t>(f.height, uint8_t(img.height));
        f.glyphs[c] = Glyph{img, widths[c], uint8_t(widths[c] + 1)};
    }
    return f;
}
}
