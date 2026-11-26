#pragma once
#include "data/image.h"
#include <array>
#include <cstdint>
#include <vector>
namespace mtv {
// Where a pixel of the intro came from, so the upscaled renderer can paint it from the upscaled picture.
enum class PixelSource : uint8_t { None, Solid, Spark, Fire, Logo };
struct Provenance { PixelSource source = PixelSource::None; uint16_t frame = 0; uint8_t sx = 0, sy = 0; };
// EGA planar video memory as used by MCV.BIN in mode 0Dh: 64 KiB per plane, 8 pixels per byte address,
// 40 bytes per 320-pixel row. Pixels are kept as 4-bit colour indices plus provenance.
class Ega {
public:
    static constexpr uint32_t kBytes = 0x10000;
    Ega() : px_(kBytes * 8, 0), prov_(kBytes * 8) {}
    uint8_t pixel(uint32_t p) const { return px_[p]; }
    const Provenance& provenance(uint32_t p) const { return prov_[p]; }
    void set_pixel(uint32_t p, uint8_t c, Provenance pr) { px_[p] = c & 0xF; prov_[p] = pr; }
    // Write mode 0: for each pixel whose bit is set in `bitmask`, planes enabled in `map_mask` take the
    // corresponding bit of `data[plane]`; other planes and unmasked pixels keep their value.
    void write_planes(uint32_t addr, uint8_t map_mask, uint8_t bitmask, std::array<uint8_t, 4> data, Provenance pr);
    // Same, with a provenance per pixel (index 0 = leftmost).
    void write_planes(uint32_t addr, uint8_t map_mask, uint8_t bitmask, std::array<uint8_t, 4> data, const std::array<Provenance, 8>& pr);
    void latch_copy(uint32_t src, uint32_t dst);  // all planes, all 8 pixels (latch read + write with bitmask 0)
    void xor_plane_bit(uint32_t p, int plane);    // one pixel, one plane
    void clear(uint32_t addr, uint32_t bytes);
    IndexedImage page(uint32_t page_addr) const;  // 320x200 at a byte address
    std::vector<Provenance> page_provenance(uint32_t page_addr) const;
private:
    std::vector<uint8_t> px_;
    std::vector<Provenance> prov_;
};
}
