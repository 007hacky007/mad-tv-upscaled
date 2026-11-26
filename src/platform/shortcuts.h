#pragma once
// The port's own keys. Function keys need Fn on a Mac, so each switch also has a Cmd or Ctrl letter: Cmd/Ctrl+O or
// Cmd/Ctrl+, the options, Cmd/Ctrl+F fullscreen, Cmd/Ctrl+P upscaled or original pictures. Plain letters stay with
// the game (save names are typed).
#include "platform/settings.h"
#include <SDL3/SDL.h>
namespace mtv {
enum class PortAction { None, Options, Fullscreen, Pictures };
PortAction port_shortcut(SDL_Keycode key, SDL_Keymod mod, const KeyMap& keys);
}
