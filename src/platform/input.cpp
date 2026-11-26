#include "platform/input.h"
namespace mtv {
std::optional<uint16_t> bios_key(SDL_Keycode key, SDL_Keymod mod) {
    if (mod & SDL_KMOD_GUI) return std::nullopt; // Cmd keys belong to macOS and to the port
    const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    static constexpr uint8_t kLetterScan[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                                0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    if (key >= SDLK_A && key <= SDLK_Z) {
        const int i = int(key - SDLK_A);
        return uint16_t(kLetterScan[i] << 8 | ((shift ? 'A' : 'a') + i));
    }
    if (key >= SDLK_1 && key <= SDLK_9) return uint16_t((0x02 + (key - SDLK_1)) << 8 | ('1' + (key - SDLK_1)));
    if (key >= SDLK_F1 && key <= SDLK_F10) return uint16_t((0x3B + (key - SDLK_F1)) << 8);
    switch (key) {
    case SDLK_0: return 0x0B30;
    case SDLK_RETURN: case SDLK_KP_ENTER: return 0x1C0D;
    case SDLK_SPACE: return 0x3920;
    case SDLK_ESCAPE: return 0x011B;
    case SDLK_BACKSPACE: return 0x0E08;
    case SDLK_TAB: return 0x0F09;
    case SDLK_UP: return 0x4800;
    case SDLK_DOWN: return 0x5000;
    case SDLK_LEFT: return 0x4B00;
    case SDLK_RIGHT: return 0x4D00;
    default: return std::nullopt;
    }
}
void InputState::mouse_button(int sdl_button, bool down) {
    const uint8_t bit = sdl_button == SDL_BUTTON_LEFT ? 1 : sdl_button == SDL_BUTTON_RIGHT ? 2 : 0;
    if (!bit) return;
    if (down) { held_ |= bit; latched_ |= bit; }
    else held_ &= uint8_t(~bit);
}
void InputState::key(SDL_Keycode key, SDL_Keymod mod) {
    if (auto k = bios_key(key, mod)) keys_.push_back(*k);
}
rt::HostInput InputState::take_tick() {
    rt::HostInput in;
    in.mouse_x = x_; in.mouse_y = y_;
    in.buttons = uint8_t(held_ | latched_);
    latched_ = 0;
    in.keys = std::move(keys_);
    keys_.clear();
    return in;
}
}
