#include <doctest/doctest.h>
#include "intro/ega.h"
using namespace mtv;
TEST_SUITE("intro.ega") {
TEST_CASE("latch_copy_moves_all_planes_and_provenance") {
    Ega v;
    v.set_pixel(0x10 * 8 + 3, 9, Provenance{PixelSource::Logo, 0, 7, 8});
    v.latch_copy(0x10, 0x2000 + 0x10);
    CHECK(v.pixel(0x2000 * 8 + 0x10 * 8 + 3) == 9);
    CHECK(v.provenance(0x2000 * 8 + 0x10 * 8 + 3).source == PixelSource::Logo);
    CHECK(v.provenance(0x2000 * 8 + 0x10 * 8 + 3).sx == 7);
}
TEST_CASE("plane_write_with_bitmask_keeps_unmasked_pixels") {
    Ega v;
    for (int i = 0; i < 8; ++i) v.set_pixel(i, 5, {});
    // write plane 1 (bit 1) = 1 for the two leftmost pixels only
    v.write_planes(0, 0x2, 0xC0, std::array<uint8_t, 4>{0, 0xFF, 0, 0}, Provenance{PixelSource::Fire, 2, 0, 0});
    CHECK(v.pixel(0) == 7);
    CHECK(v.pixel(1) == 7);
    CHECK(v.pixel(2) == 5);
    CHECK(v.provenance(0).source == PixelSource::Fire);
    CHECK(v.provenance(2).source == PixelSource::None);
}
TEST_CASE("xor_pixel_flips_plane_bit_and_marks_solid") {
    Ega v;
    v.xor_plane_bit(40 * 8 * 3 + 17, 1);
    CHECK(v.pixel(40 * 8 * 3 + 17) == 2);
    CHECK(v.provenance(40 * 8 * 3 + 17).source == PixelSource::Solid);
    v.xor_plane_bit(40 * 8 * 3 + 17, 1);
    CHECK(v.pixel(40 * 8 * 3 + 17) == 0);
}
TEST_CASE("page_extracts_320x200") {
    Ega v;
    v.set_pixel(0x2000 * 8 + 199 * 320 + 319, 12, {});
    auto img = v.page(0x2000);
    CHECK(img.width == 320);
    CHECK(img.indices.back() == 12);
    CHECK(img.indices.front() == 0);
}
}
