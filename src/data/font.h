#pragma once
#include "data/image.h"
#include "data/result.h"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
namespace mtv {
// The game font: glyph images are frames of image slot 3; DS:012E maps a CP437 byte to a 1-based frame,
// DS:022E holds the glyph width. FUN_205e_0266 advances width + 1 per glyph and 3 pixels per space.
struct Glyph { IndexedImage image; uint8_t width = 0; uint8_t advance = 0; };
struct Font { uint8_t height = 0; uint8_t space_advance = 3; std::array<std::optional<Glyph>, 256> glyphs; };
Result<Font> build_font(const std::vector<IndexedImage>& frames, std::span<const uint8_t, 256> glyph_map, std::span<const uint8_t, 256> widths);
}
