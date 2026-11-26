#include <doctest/doctest.h>
#include "platform/input.h"
#include "platform/shortcuts.h"
using namespace mtv;
TEST_SUITE("platform.shortcuts") {
TEST_CASE("function_keys_still_work") {
    KeyMap k;
    CHECK(port_shortcut(SDLK_F12, SDL_KMOD_NONE, k) == PortAction::Options);
    CHECK(port_shortcut(SDLK_F11, SDL_KMOD_NONE, k) == PortAction::Fullscreen);
    CHECK(port_shortcut(SDLK_RETURN, SDL_KMOD_LALT, k) == PortAction::Fullscreen);
    CHECK(port_shortcut(SDLK_PAUSE, SDL_KMOD_NONE, k) == PortAction::Pictures);
}
TEST_CASE("cmd_or_ctrl_letters_work_without_function_keys") {
    KeyMap k;
    for (SDL_Keymod m : {SDL_KMOD_LGUI, SDL_KMOD_RGUI, SDL_KMOD_LCTRL, SDL_KMOD_RCTRL}) {
        CHECK(port_shortcut(SDLK_O, m, k) == PortAction::Options);
        CHECK(port_shortcut(SDLK_COMMA, m, k) == PortAction::Options);
        CHECK(port_shortcut(SDLK_F, m, k) == PortAction::Fullscreen);
        CHECK(port_shortcut(SDLK_P, m, k) == PortAction::Pictures);
    }
    CHECK(port_shortcut(SDLK_F, SDL_Keymod(SDL_KMOD_LGUI | SDL_KMOD_LCTRL), k) == PortAction::Fullscreen); // the macOS fullscreen keys
}
TEST_CASE("plain_letters_stay_with_the_game") {
    KeyMap k;
    for (SDL_Keycode key : {SDLK_O, SDLK_F, SDLK_P, SDLK_COMMA})
        for (SDL_Keymod m : {SDL_KMOD_NONE, SDL_KMOD_LSHIFT}) CHECK(port_shortcut(key, m, k) == PortAction::None);
    CHECK(bios_key(SDLK_P, SDL_KMOD_NONE) == uint16_t(0x1970));
    CHECK(bios_key(SDLK_P, SDL_KMOD_LSHIFT) == uint16_t(0x1950));
}
TEST_CASE("cmd_keys_never_reach_the_game") {
    // Cmd+Q, Cmd+H and the like belong to macOS; the game would see a plain letter
    CHECK_FALSE(bios_key(SDLK_Q, SDL_KMOD_LGUI).has_value());
    CHECK_FALSE(bios_key(SDLK_1, SDL_KMOD_RGUI).has_value());
}
}
