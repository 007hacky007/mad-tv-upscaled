#include "intro/ega.h"
namespace mtv {
void Ega::write_planes(uint32_t addr, uint8_t map_mask, uint8_t bitmask, std::array<uint8_t, 4> data, const std::array<Provenance, 8>& pr) {
    addr &= kBytes - 1;
    for (int b = 0; b < 8; ++b) {
        const uint8_t bit = uint8_t(0x80 >> b);
        if (!(bitmask & bit)) continue;
        uint32_t p = addr * 8 + uint32_t(b);
        uint8_t c = px_[p];
        for (int pl = 0; pl < 4; ++pl)
            if (map_mask & (1 << pl)) c = uint8_t((c & ~(1 << pl)) | ((data[size_t(pl)] & bit) ? (1 << pl) : 0));
        px_[p] = c;
        prov_[p] = pr[size_t(b)];
    }
}
void Ega::write_planes(uint32_t addr, uint8_t map_mask, uint8_t bitmask, std::array<uint8_t, 4> data, Provenance pr) {
    std::array<Provenance, 8> all; all.fill(pr);
    write_planes(addr, map_mask, bitmask, data, all);
}
void Ega::latch_copy(uint32_t src, uint32_t dst) {
    src &= kBytes - 1; dst &= kBytes - 1;
    for (uint32_t b = 0; b < 8; ++b) { px_[dst * 8 + b] = px_[src * 8 + b]; prov_[dst * 8 + b] = prov_[src * 8 + b]; }
}
void Ega::xor_plane_bit(uint32_t p, int plane) {
    p &= kBytes * 8 - 1;
    px_[p] ^= uint8_t(1 << plane);
    prov_[p] = Provenance{PixelSource::Solid, 0, 0, 0};
}
void Ega::clear(uint32_t addr, uint32_t bytes) {
    for (uint32_t i = 0; i < bytes * 8; ++i) { px_[((addr * 8) + i) & (kBytes * 8 - 1)] = 0; prov_[((addr * 8) + i) & (kBytes * 8 - 1)] = {}; }
}
IndexedImage Ega::page(uint32_t a) const {
    IndexedImage img{320, 200, {}};
    img.indices.assign(px_.begin() + long(a) * 8, px_.begin() + long(a) * 8 + 320 * 200);
    return img;
}
std::vector<Provenance> Ega::page_provenance(uint32_t a) const {
    return std::vector<Provenance>(prov_.begin() + long(a) * 8, prov_.begin() + long(a) * 8 + 320 * 200);
}
}
