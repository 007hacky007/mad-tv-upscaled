#include <doctest/doctest.h>
#include "platform/input.h"
#include "platform/settings.h"
#include "platform/ticker.h"
using namespace mtv;
TEST_SUITE("platform.loop") {
TEST_CASE("fixed_tick_independent_of_present_rate") {
    // 10 s of wall time presented at 60, 120 and 144 Hz runs the same number of game ticks (70.086 Hz retraces).
    int counts[3] = {};
    const int rates[3] = {60, 120, 144};
    for (int r = 0; r < 3; ++r) {
        Ticker t;
        const int64_t frame_ns = 1'000'000'000LL / rates[r];
        for (int64_t now = 0; now <= 10'000'000'000LL; now += frame_ns) counts[r] += t.advance(now);
    }
    CHECK(counts[0] == counts[1]);
    CHECK(counts[1] == counts[2]);
    CHECK(counts[0] == doctest::Approx(700.86).epsilon(0.002));
}
TEST_CASE("ticker_drops_time_after_a_long_stall") {
    Ticker t;
    t.advance(0);
    CHECK(t.advance(5'000'000'000LL) == Ticker::kMaxCatchUp); // a 5 s stall (debugger, sleep) does not replay 5 s
    CHECK(t.advance(5'000'000'000LL + 14'268'000) == 1);
}
}
TEST_SUITE("platform.input") {
TEST_CASE("mouse_click_converted_once_per_tick") {
    InputState in;
    in.mouse_button(1, true);
    in.mouse_button(1, false); // press and release between two ticks
    auto a = in.take_tick();
    CHECK(a.buttons == 1);      // the game still sees the press
    auto b = in.take_tick();
    CHECK(b.buttons == 0);      // and the release on the next tick
}
TEST_CASE("held_button_stays_down") {
    InputState in;
    in.mouse_button(3, true);   // right button
    CHECK(in.take_tick().buttons == 2);
    CHECK(in.take_tick().buttons == 2);
    in.mouse_button(3, false);
    CHECK(in.take_tick().buttons == 0);
}
TEST_CASE("mouse_position_is_logical_and_kept") {
    InputState in;
    in.mouse_move(LogicalPoint{63, 100});
    auto a = in.take_tick();
    CHECK(a.mouse_x == 63);
    CHECK(a.mouse_y == 100);
    auto b = in.take_tick();
    CHECK(b.mouse_x == 63);
}
TEST_CASE("keys_become_bios_codes_once") {
    InputState in;
    in.key(SDLK_RETURN, SDL_KMOD_NONE);
    in.key(SDLK_E, SDL_KMOD_NONE);
    in.key(SDLK_E, SDL_KMOD_LSHIFT);
    in.key(SDLK_SPACE, SDL_KMOD_NONE);
    in.key(SDLK_ESCAPE, SDL_KMOD_NONE);
    auto a = in.take_tick();
    CHECK(a.keys == std::vector<uint16_t>{0x1C0D, 0x1265, 0x1245, 0x3920, 0x011B});
    CHECK(in.take_tick().keys.empty());
}
TEST_CASE("unmapped_keys_are_ignored") {
    InputState in;
    in.key(SDLK_F13, SDL_KMOD_NONE);
    CHECK(in.take_tick().keys.empty());
}
TEST_CASE("port_keys_are_not_game_keys") {
    // The original maps F1-F10 to hotkeys (scan table DS:1730); the port's own keys must not take any of them.
    KeyMap k;
    for (const auto& name : {k.options, k.graphics, k.fullscreen}) {
        const SDL_Keycode key = SDL_GetKeyFromName(name.c_str());
        CHECK_MESSAGE(key != SDLK_UNKNOWN, name);
        CHECK_MESSAGE(!bios_key(key, SDL_KMOD_NONE).has_value(), name);
    }
}
}
