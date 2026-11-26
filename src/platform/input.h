#pragma once
// Host input gathered between ticks and handed to the game once per tick.
#include "render/viewport.h"
#include "rt/dos.h"
#include <SDL3/SDL.h>
#include <optional>
namespace mtv {
// BIOS keystroke (scan << 8 | ascii) for a host key, or nullopt for keys the game never sees.
std::optional<uint16_t> bios_key(SDL_Keycode key, SDL_Keymod mod);
class InputState {
public:
    void mouse_button(int sdl_button, bool down);
    void mouse_move(LogicalPoint p) { x_ = p.x; y_ = p.y; }
    void key(SDL_Keycode key, SDL_Keymod mod);
    void raw_key(uint16_t bios) { keys_.push_back(bios); }
    // A press that was released before the tick still shows as pressed for that one tick.
    rt::HostInput take_tick();
private:
    int x_ = 160, y_ = 100;
    uint8_t held_ = 0, latched_ = 0;
    std::vector<uint16_t> keys_;
};
}
