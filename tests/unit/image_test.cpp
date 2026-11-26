#include <doctest/doctest.h>
#include "data/image.h"
#include "render/palette.h"
#include "support/corpus.h"
using namespace mtv;
TEST_SUITE("data.image") {
TEST_CASE("chunky_frame_crops_stride") {
    DecodedEntry e;
    e.kind = ResourceKind::Image;
    ChunkyFrame f; f.width = 3; f.height = 2; f.stride = 8;
    f.pixels = {1, 2, 3, 9, 9, 9, 9, 9, 4, 5, 6, 9, 9, 9, 9, 9};
    e.image.frames.push_back(f);
    auto imgs = decode_image(e);
    REQUIRE(imgs.has_value());
    REQUIRE(imgs.value().size() == 1);
    CHECK(imgs.value()[0].width == 3);
    CHECK(imgs.value()[0].indices == std::vector<uint8_t>{1, 2, 3, 4, 5, 6});
}
TEST_CASE("decode_image_rejects_non_image") {
    DecodedEntry e; e.kind = ResourceKind::Table;
    CHECK_FALSE(decode_image(e).has_value());
}
TEST_CASE("image_dimensions_bounded_by_entry") {
    DecodedEntry e; e.kind = ResourceKind::Image;
    ChunkyFrame f; f.width = 9; f.height = 2; f.stride = 8; f.pixels.assign(16, 0); // stride smaller than width
    e.image.frames.push_back(f);
    CHECK_FALSE(decode_image(e).has_value());
}
TEST_CASE("masked_blit_uses_transparent_index") {
    IndexedImage img{2, 1, {0, 5}};
    Palette pal; pal.rgb6[5] = {63, 0, 0};
    auto rgba = to_rgba(img, pal, kTransparentIndex);
    CHECK(rgba == std::vector<uint8_t>{0, 0, 0, 0, 255, 0, 0, 255});
    auto opaque = to_rgba(img, pal, std::nullopt);
    CHECK(opaque[3] == 255);
}
TEST_CASE("palette_6bit_to_8bit_scales_63_to_255") {
    CHECK(dac_to_8bit(0) == 0);
    CHECK(dac_to_8bit(63) == 255);
    CHECK(dac_to_8bit(32) == 130);
}
TEST_CASE("palette_parses_counted_dac_block") {
    std::vector<uint8_t> raw{2, 0, 1, 2, 3, 63, 62, 61};
    auto p = parse_palette(raw);
    REQUIRE(p.has_value());
    CHECK(p.value().count == 2);
    CHECK(p.value().rgb6[1] == std::array<uint8_t, 3>{63, 62, 61});
    raw[0] = 3;
    CHECK_FALSE(parse_palette(raw).has_value());
    std::vector<uint8_t> bad{1, 0, 64, 0, 0};
    CHECK_FALSE(parse_palette(bad).has_value());
}
}
TEST_SUITE("corpus.image") {
TEST_CASE("every_image_slot_decodes") {
    auto idx = ResourceIndex::open_dir(mtv::test::corpus_dir());
    REQUIRE(idx.has_value());
    auto pal = parse_palette(idx.value().get(1).value());
    REQUIRE(pal.has_value());
    int images = 0;
    for (auto slot : idx.value().slots()) {
        auto e = decode_rsc_entry(idx.value(), slot);
        REQUIRE(e.has_value());
        if (e.value().kind != ResourceKind::Image) continue;
        auto frames = decode_image(e.value());
        REQUIRE_MESSAGE(frames.has_value(), "slot " << slot);
        for (auto& f : frames.value())
            for (auto v : f.indices) REQUIRE_MESSAGE(v < pal.value().count, "slot " << slot << " uses index " << int(v));
        ++images;
    }
    CHECK(images == 164);
}
}
