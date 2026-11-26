#include <doctest/doctest.h>
#include "render/draw_list.h"
#include "render/gpu.h"
#include "render/viewport.h"
#include <SDL3/SDL.h>
using namespace mtv;
TEST_SUITE("render.viewport") {
TEST_CASE("crt_4x3_scales_y_by_1_2") {
    auto v = Viewport::fit(1600, 1200, AspectMode::Crt4x3);
    CHECK(v.x == 0); CHECK(v.y == 0); CHECK(v.width == 1600); CHECK(v.height == 1200);
    WindowMetrics m{1600, 1200, 1600, 1200};
    CHECK(Viewport::window_to_logical({5, 6}, m, AspectMode::Crt4x3) == LogicalPoint{1, 1});
    CHECK(Viewport::window_to_logical({4.9, 5.9}, m, AspectMode::Crt4x3) == LogicalPoint{0, 0});
}
TEST_CASE("square_mode_letterboxes_16_10") {
    auto v = Viewport::fit(1600, 1200, AspectMode::Square);
    CHECK(v.width == 1600); CHECK(v.height == 1000); CHECK(v.y == 100);
    WindowMetrics m{1600, 1200, 1600, 1200};
    CHECK_FALSE(Viewport::window_to_logical({0, 50}, m, AspectMode::Square).has_value());
    CHECK(Viewport::window_to_logical({0, 100}, m, AspectMode::Square) == LogicalPoint{0, 0});
}
TEST_CASE("pointer_maps_through_aspect_stretch") {
    WindowMetrics hidpi{800, 600, 1600, 1200}; // points vs pixels
    CHECK(Viewport::window_to_logical({400, 300}, hidpi, AspectMode::Crt4x3) == LogicalPoint{160, 100});
    CHECK(Viewport::window_to_logical({799.9, 599.9}, hidpi, AspectMode::Crt4x3) == LogicalPoint{319, 199});
}
TEST_CASE("pointer_outside_viewport_is_ignored") {
    WindowMetrics wide{2000, 1200, 2000, 1200};
    auto v = Viewport::fit(2000, 1200, AspectMode::Crt4x3);
    CHECK(v.x == 200);
    CHECK_FALSE(Viewport::window_to_logical({100, 600}, wide, AspectMode::Crt4x3).has_value());
    CHECK_FALSE(Viewport::window_to_logical({1900, 600}, wide, AspectMode::Crt4x3).has_value());
    CHECK_FALSE(Viewport::window_to_logical({10, 10}, WindowMetrics{0, 0, 0, 0}, AspectMode::Crt4x3).has_value());
}
}
TEST_SUITE("render.draw_list") {
TEST_CASE("draw_list_keeps_painter_order") {
    DrawList d;
    d.blit({{70}, 0, 10, 20, 0, DrawFlags::Masked});
    d.fill({0, 0, 5, 5}, 3);
    d.text({"Hi", 1, 2});
    REQUIRE(d.ops().size() == 3);
    CHECK(std::holds_alternative<DrawCmd>(d.ops()[0]));
    CHECK(std::holds_alternative<FillCmd>(d.ops()[1]));
    CHECK(std::get<TextCmd>(d.ops()[2]).utf8 == "Hi");
}
TEST_CASE("palette_variant_selected_per_draw") {
    DrawCmd a{{70}, 1, 0, 0, 0, DrawFlags::Opaque}, b = a;
    b.palette_id = 2;
    CHECK(texture_key(a, GraphicsMode::Nearest) != texture_key(b, GraphicsMode::Nearest));
    CHECK(texture_key(b, GraphicsMode::Nearest).palette_id == 2);
    CHECK(texture_key(a, GraphicsMode::Nearest) != texture_key(a, GraphicsMode::Upscaled));
}
}
TEST_SUITE("render.gpu") {
TEST_CASE("nearest_renders_at_6x_with_transparency") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window* w = SDL_CreateWindow("t", 320, 200, SDL_WINDOW_HIDDEN);
    REQUIRE_MESSAGE(w, std::string(SDL_GetError()));
    struct Source : FrameSource {
        Palette pal;
        Source() { pal.rgb6[1] = {63, 0, 0}; pal.rgb6[2] = {0, 63, 0}; }
        std::optional<IndexedImage> frame(ResourceId id, uint16_t) const override {
            if (id.slot == 1) return IndexedImage{2, 1, {1, 1}};
            if (id.slot == 2) return IndexedImage{2, 1, {0, 2}};
            return std::nullopt;
        }
        const Palette& palette() const override { return pal; }
        const Font* font() const override { return nullptr; }
    } src;
    auto r = Renderer::create(w, src, "software");
    REQUIRE_MESSAGE(r.has_value(), (r.has_value() ? "" : r.error().reason));
    DrawList d;
    d.blit({{1}, 0, 10, 5, 0, DrawFlags::Opaque});
    d.blit({{2}, 0, 10, 5, 0, DrawFlags::Masked}); // index 0 keeps the red underneath
    REQUIRE(r.value()->render_canvas(d, GraphicsMode::Nearest).has_value());
    auto px = r.value()->canvas_pixel(10 * 6 + 2, 5 * 6 + 2);
    REQUIRE(px.has_value());
    CHECK(px.value() == std::array<uint8_t, 3>{255, 0, 0});
    CHECK(r.value()->canvas_pixel(11 * 6 + 2, 5 * 6 + 2).value() == std::array<uint8_t, 3>{0, 255, 0});
    CHECK(r.value()->canvas_pixel(12 * 6 + 2, 5 * 6 + 2).value() == std::array<uint8_t, 3>{0, 0, 0});
    r.value().reset();
    SDL_DestroyWindow(w);
    SDL_Quit();
}
TEST_CASE("screen_frame_fills_canvas_nearest") {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window* w = SDL_CreateWindow("t", 320, 200, SDL_WINDOW_HIDDEN);
    REQUIRE(w);
    struct Source : FrameSource {
        Palette pal;
        std::optional<IndexedImage> frame(ResourceId, uint16_t) const override { return std::nullopt; }
        const Palette& palette() const override { return pal; }
        const Font* font() const override { return nullptr; }
    } src;
    auto r = Renderer::create(w, src, "software");
    REQUIRE(r.has_value());
    std::vector<uint8_t> rgb(320 * 200 * 3, 0);
    auto put = [&](int x, int y, uint8_t a, uint8_t b, uint8_t c) { size_t i = size_t(y * 320 + x) * 3; rgb[i] = a; rgb[i + 1] = b; rgb[i + 2] = c; };
    put(0, 0, 255, 0, 0);
    put(319, 199, 0, 255, 0);
    put(10, 5, 1, 2, 3);
    REQUIRE(r.value()->render_screen(rgb).has_value());
    CHECK(r.value()->canvas_pixel(0, 0) == std::array<uint8_t, 3>{255, 0, 0});
    CHECK(r.value()->canvas_pixel(5, 5) == std::array<uint8_t, 3>{255, 0, 0});
    CHECK(r.value()->canvas_pixel(6, 0) == std::array<uint8_t, 3>{0, 0, 0});
    CHECK(r.value()->canvas_pixel(1919, 1199) == std::array<uint8_t, 3>{0, 255, 0});
    CHECK(r.value()->canvas_pixel(63, 33) == std::array<uint8_t, 3>{1, 2, 3});
    r.value().reset();
    SDL_DestroyWindow(w);
    SDL_Quit();
}
}
