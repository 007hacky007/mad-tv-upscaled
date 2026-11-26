#pragma once
#include "data/result.h"
#include "game/language.h"
#include "render/draw_list.h"
#include "render/viewport.h"
#include <filesystem>
#include <string>
namespace mtv {
struct KeyMap {
    std::string fullscreen = "F11"; // SDL key names
    std::string graphics = "Pause"; // shortcut for Pictures in the options: upscaled or original
    std::string options = "F12";    // the options overlay (F1-F10 are the game's own hotkeys)
    bool operator==(const KeyMap&) const = default;
};
struct Settings {
    game::Language language = game::Language::English;
    AspectMode aspect = AspectMode::Crt4x3;
    bool fullscreen = false;
    GraphicsMode graphics_dev = GraphicsMode::Upscaled;
    KeyMap keys;
    std::string data_dir; // UTF-8; where the imported game files are (Task 22)
    bool pack_build_tried = false; // the first-start art build ran (or was skipped); later builds come from F12
    bool operator==(const Settings&) const = default;
};
// A missing file gives defaults; unknown keys and invalid values are ignored; unreadable JSON is an error.
Result<Settings> load_settings(const std::filesystem::path& path);
// Written to a temporary file and renamed over the old one.
Result<void> save_settings(const Settings& s, const std::filesystem::path& path);
// settings.json in the per-user application directory (SDL_GetPrefPath).
std::filesystem::path default_settings_path();
}
