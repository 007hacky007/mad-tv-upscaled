#include <doctest/doctest.h>
#include "render/compositor.h"
#include "render/pack.h"
#include "stb_image_write.h"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
using namespace mtv;
using namespace mtv::render;
namespace {
std::filesystem::path fresh(const char* name) {
    auto d = std::filesystem::temp_directory_path() / "mtv-pack-test" / name;
    std::filesystem::remove_all(d);
    std::filesystem::create_directories(d);
    return d;
}
// A w x h source image at 6x: every upscaled pixel colour (r, g, b), alpha from `alpha(x6, y6)`.
template <class A> void write_png(const std::filesystem::path& p, int w, int h, uint8_t r, uint8_t g, uint8_t b, A alpha) {
    std::vector<uint8_t> px(size_t(w * 6) * h * 6 * 4);
    for (int y = 0; y < h * 6; ++y)
        for (int x = 0; x < w * 6; ++x) {
            auto* q = &px[(size_t(y) * w * 6 + x) * 4];
            q[0] = r; q[1] = g; q[2] = b; q[3] = alpha(x, y);
        }
    REQUIRE(stbi_write_png(p.string().c_str(), w * 6, h * 6, 4, px.data(), w * 6 * 4));
}
void write_pack(const std::filesystem::path& d, nlohmann::json images) {
    std::ofstream(d / "pack.json") << nlohmann::json{{"schema_version", 1}, {"scale", 6}, {"images", images}}.dump();
}
}
TEST_SUITE("render.pack") {
TEST_CASE("pack_rejects_path_traversal") {
    auto d = fresh("traversal");
    write_pack(d, {{{"slot", 5}, {"frame", 0}, {"width", 2}, {"height", 2}, {"png", "../005-000.png"}}});
    CHECK_FALSE(Pack::open(d).has_value());
    write_pack(d, {{{"slot", 5}, {"frame", 0}, {"width", 2}, {"height", 2}, {"png", "/tmp/005-000.png"}}});
    CHECK_FALSE(Pack::open(d).has_value());
}
TEST_CASE("pack_rejects_missing_frame") {
    auto d = fresh("missing");
    write_pack(d, {{{"slot", 5}, {"frame", 0}, {"width", 2}, {"height", 2}, {"png", "005-000.png"}}});
    CHECK_FALSE(Pack::open(d).has_value());
}
TEST_CASE("pack_rejects_wrong_size_on_load") {
    auto d = fresh("size");
    write_png(d / "005-000.png", 3, 2, 1, 2, 3, [](int, int) { return 255; });
    write_pack(d, {{{"slot", 5}, {"frame", 0}, {"width", 2}, {"height", 2}, {"png", "005-000.png"}}});
    auto p = Pack::open(d);
    REQUIRE(p.has_value());
    CHECK(p.value()->get({5, 0}) == nullptr);
}
TEST_CASE("pack_loads_an_image_at_6x") {
    auto d = fresh("load");
    write_png(d / "005-001.png", 2, 3, 10, 20, 30, [](int, int) { return 255; });
    write_pack(d, {{{"slot", 5}, {"frame", 1}, {"width", 2}, {"height", 3}, {"png", "005-001.png"}}});
    auto p = Pack::open(d);
    REQUIRE(p.has_value());
    const auto* img = p.value()->get({5, 1});
    REQUIRE(img);
    CHECK(img->width == 12);
    CHECK(img->height == 18);
    CHECK(img->rgba[0] == 10);
    CHECK(p.value()->get({5, 0}) == nullptr);
    CHECK(p.value()->contains({5, 1}));
}
TEST_CASE("cache_evicts_lru") {
    auto d = fresh("lru");
    nlohmann::json list = nlohmann::json::array();
    for (int f = 0; f < 3; ++f) {
        char name[16]; std::snprintf(name, sizeof name, "005-%03d.png", f);
        write_png(d / name, 10, 10, uint8_t(f), 0, 0, [](int, int) { return 255; });
        list.push_back({{"slot", 5}, {"frame", f}, {"width", 10}, {"height", 10}, {"png", name}});
    }
    write_pack(d, list);
    auto p = Pack::open(d, 2 * 60 * 60 * 4); // room for two 60x60 images
    REQUIRE(p.has_value());
    auto& pack = *p.value();
    REQUIRE(pack.get({5, 0}));
    REQUIRE(pack.get({5, 1}));
    REQUIRE(pack.get({5, 0}));      // 0 is now the most recently used
    REQUIRE(pack.get({5, 2}));      // evicts 1
    CHECK(pack.cached({5, 0}));
    CHECK_FALSE(pack.cached({5, 1}));
    CHECK(pack.cached({5, 2}));
}
}
TEST_SUITE("render.compositor") {
namespace {
struct Screen {
    std::vector<uint8_t> pixels = std::vector<uint8_t>(64000, 0);
    std::vector<Tag> tags = std::vector<Tag>(64000, 0);
    std::array<std::array<uint8_t, 3>, 256> dac{};
};
uint32_t at(const Compositor& c, int x, int y) { return c.canvas()[size_t(y) * kCanvasWidth + x]; }
uint32_t rgba(uint8_t r, uint8_t g, uint8_t b) { return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | 0xFF000000u; }
}
TEST_CASE("untagged_pixel_is_its_palette_colour_6x6") {
    Screen s;
    s.dac[7] = {63, 0, 21};
    s.pixels[5 * 320 + 4] = 7;
    Compositor c(nullptr);
    c.update(s.pixels, s.tags, s.dac, [](Tag) { return std::optional<ImageRef>{}; });
    CHECK(at(c, 4 * 6, 5 * 6) == rgba(255, 0, 85));
    CHECK(at(c, 4 * 6 + 5, 5 * 6 + 5) == rgba(255, 0, 85));
    CHECK(at(c, 4 * 6 + 6, 5 * 6) == rgba(0, 0, 0));
}
TEST_CASE("tagged_pixel_uses_the_upscaled_block_and_transparent_subpixels_show_the_neighbour") {
    auto d = fresh("comp");
    // image 9/0: 2x1 sprite, upscaled colour (200, 100, 50); its left half of pixel (0,0) is transparent at 6x
    write_png(d / "009-000.png", 2, 1, 200, 100, 50, [](int x, int) { return x < 3 ? 0 : 255; });
    // image 4/0: 4x1 background, upscaled colour (10, 20, 30)
    write_png(d / "004-000.png", 4, 1, 10, 20, 30, [](int, int) { return 255; });
    write_pack(d, {{{"slot", 9}, {"frame", 0}, {"width", 2}, {"height", 1}, {"png", "009-000.png"}},
                   {{"slot", 4}, {"frame", 0}, {"width", 4}, {"height", 1}, {"png", "004-000.png"}}});
    auto p = Pack::open(d);
    REQUIRE(p.has_value());
    Screen s;
    s.dac[3] = {63, 63, 63}; s.dac[1] = {0, 0, 63};
    // screen (10, 0): background pixel 0; screen (11, 0): sprite pixel 0 over background pixel 1
    s.pixels[10] = 1; s.tags[10] = make_tag(1, 0, 0);
    s.pixels[11] = 3; s.tags[11] = make_tag(2, 0, 0);
    auto image_of = [](Tag t) -> std::optional<ImageRef> {
        if (tag_image(t) == 1) return ImageRef{4, 0};
        if (tag_image(t) == 2) return ImageRef{9, 0};
        return std::nullopt;
    };
    Compositor c(p.value().get());
    c.update(s.pixels, s.tags, s.dac, image_of);
    CHECK(at(c, 10 * 6, 0) == rgba(10, 20, 30));       // background from its upscaled image
    CHECK(at(c, 11 * 6 + 4, 2) == rgba(200, 100, 50)); // sprite where its upscaled mask is opaque
    CHECK(at(c, 11 * 6 + 1, 2) == rgba(10, 20, 30));   // background continued under the transparent part
}
TEST_CASE("an_upscaled_sprite_edge_reaches_into_the_transparent_neighbour") {
    auto d = fresh("edge");
    // sprite 9/0 is 2x1: pixel 0 opaque; pixel 1 transparent at the source, but its upscaled mask is opaque in
    // the left two columns (the smoothed edge of pixel 0)
    write_png(d / "009-000.png", 2, 1, 200, 100, 50, [](int x, int) { return x < 8 ? 255 : 0; });
    write_pack(d, {{{"slot", 9}, {"frame", 0}, {"width", 2}, {"height", 1}, {"png", "009-000.png"}}});
    auto p = Pack::open(d);
    REQUIRE(p.has_value());
    Screen s;
    s.dac[3] = {63, 63, 63}; s.dac[5] = {0, 0, 21};
    s.pixels[20] = 3; s.tags[20] = make_tag(1, 0, 0); // sprite pixel 0
    s.pixels[21] = 5;                                  // a flat fill where the sprite was transparent
    auto image_of = [](Tag t) -> std::optional<ImageRef> { return tag_image(t) == 1 ? std::optional<ImageRef>(ImageRef{9, 0}) : std::nullopt; };
    Compositor c(p.value().get());
    c.update(s.pixels, s.tags, s.dac, image_of);
    CHECK(at(c, 21 * 6 + 0, 3) == rgba(200, 100, 50)); // the sprite's smoothed edge
    CHECK(at(c, 21 * 6 + 1, 3) == rgba(200, 100, 50));
    CHECK(at(c, 21 * 6 + 3, 3) == rgba(0, 0, 85));     // the fill beyond it
}
TEST_CASE("a_fully_covering_neighbour_image_does_not_paint_over") {
    auto d = fresh("cover");
    write_png(d / "004-000.png", 4, 1, 10, 20, 30, [](int, int) { return 255; });
    write_pack(d, {{{"slot", 4}, {"frame", 0}, {"width", 4}, {"height", 1}, {"png", "004-000.png"}}});
    auto p = Pack::open(d);
    REQUIRE(p.has_value());
    Screen s;
    s.dac[5] = {0, 0, 21};
    s.pixels[30] = 1; s.tags[30] = make_tag(1, 0, 0);
    s.pixels[31] = 5; // something drawn on top of the background's pixel 1
    auto image_of = [](Tag t) -> std::optional<ImageRef> { return tag_image(t) == 1 ? std::optional<ImageRef>(ImageRef{4, 0}) : std::nullopt; };
    Compositor c(p.value().get());
    c.update(s.pixels, s.tags, s.dac, image_of);
    CHECK(at(c, 31 * 6 + 0, 3) == rgba(0, 0, 85));
}
TEST_CASE("unchanged_frame_has_no_dirty_area") {
    Screen s;
    s.pixels[100] = 2; s.dac[2] = {1, 2, 3};
    Compositor c(nullptr);
    auto none = [](Tag) { return std::optional<ImageRef>{}; };
    auto first = c.update(s.pixels, s.tags, s.dac, none);
    CHECK(first.w > 0);
    auto second = c.update(s.pixels, s.tags, s.dac, none);
    CHECK(second.w == 0);
    s.pixels[64000 - 1] = 2;
    auto third = c.update(s.pixels, s.tags, s.dac, none);
    CHECK(third.x <= 319 * 6);
    CHECK(third.x + third.w >= 320 * 6);
    CHECK(third.y + third.h >= 200 * 6);
}
}
