#pragma once
// Pixel provenance tags: which decoded image pixel a byte of video memory was copied from.
// Layout: bit 31 set, image id (13 bits) << 18, y (9 bits) << 9, x (9 bits). Image ids name (slot, frame) pairs
// in the order the program first decodes them; 0 means no provenance.
#include <cstdint>
#include <utility>
namespace mtv::render {
using Tag = uint32_t;
inline constexpr uint32_t kMaxImageIds = 1u << 13;
inline constexpr Tag make_tag(uint32_t image_id, uint32_t x, uint32_t y) {
    return 0x80000000u | (image_id & 0x1FFF) << 18 | (y & 0x1FF) << 9 | (x & 0x1FF);
}
inline constexpr bool tag_valid(Tag t) { return (t & 0x80000000u) != 0; }
inline constexpr uint32_t tag_image(Tag t) { return (t >> 18) & 0x1FFF; }
inline constexpr std::pair<uint32_t, uint32_t> tag_xy(Tag t) { return {t & 0x1FF, (t >> 9) & 0x1FF}; }
struct ImageRef { uint16_t slot = 0, frame = 0; bool operator==(const ImageRef&) const = default; };
}
