#include "platform/shortcuts.h"
namespace mtv {
namespace {
bool named(SDL_Keycode key, const std::string& name) { return !name.empty() && key == SDL_GetKeyFromName(name.c_str()); }
}
PortAction port_shortcut(SDL_Keycode key, SDL_Keymod mod, const KeyMap& keys) {
    if (named(key, keys.options)) return PortAction::Options;
    if (named(key, keys.fullscreen) || ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && (mod & SDL_KMOD_ALT))) return PortAction::Fullscreen;
    if (named(key, keys.graphics)) return PortAction::Pictures;
    if (mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) {
        switch (key) {
        case SDLK_O: case SDLK_COMMA: return PortAction::Options;
        case SDLK_F: return PortAction::Fullscreen;
        case SDLK_P: return PortAction::Pictures;
        default: break;
        }
    }
    return PortAction::None;
}
}
