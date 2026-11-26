#pragma once
#include "data/image.h"
#include "data/result.h"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace mtv {
// The masked blitters (0x477AC, 0x47865, 0x4795C, 0x47A21) skip pixels whose index is 0.
inline constexpr uint8_t kTransparentIndex = 0;
// VGA DAC block as loaded by FUN_4743_0001 (INT 10h AX=1012h): u16 count, then count x (r, g, b) 6-bit values.
struct Palette { uint16_t count = 256; std::array<std::array<uint8_t, 3>, 256> rgb6{}; };
Result<Palette> parse_palette(std::span<const uint8_t> raw);
inline uint8_t dac_to_8bit(uint8_t v) { return uint8_t((v << 2) | (v >> 4)); }
std::vector<uint8_t> to_rgba(const IndexedImage& img, const Palette& pal, std::optional<uint8_t> transparent_index);
}
