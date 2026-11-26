#include "platform/settings.h"
#include "platform/input.h"
#include <SDL3/SDL.h>
#include <fstream>
#include <nlohmann/json.hpp>
namespace mtv {
namespace {
DataError fail(const std::filesystem::path& p, std::string why) { return DataError{p.u8string().empty() ? "" : reinterpret_cast<const char*>(p.u8string().c_str()), "", 0, std::move(why)}; }
}
Result<Settings> load_settings(const std::filesystem::path& path) {
    Settings s;
    std::ifstream f(path, std::ios::binary);
    if (!f) return s;
    auto j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail(path, "settings file is not a JSON object");
    auto str = [&](const char* k) -> std::optional<std::string> { if (auto it = j.find(k); it != j.end() && it->is_string()) return it->get<std::string>(); return std::nullopt; };
    if (auto v = str("language")) { if (*v == "en") s.language = game::Language::English; else if (*v == "de") s.language = game::Language::German; }
    if (auto v = str("aspect")) { if (*v == "4:3") s.aspect = AspectMode::Crt4x3; else if (*v == "square") s.aspect = AspectMode::Square; }
    if (auto it = j.find("fullscreen"); it != j.end() && it->is_boolean()) s.fullscreen = it->get<bool>();
    if (auto it = j.find("pack_build_tried"); it != j.end() && it->is_boolean()) s.pack_build_tried = it->get<bool>();
    if (auto v = str("graphics_dev")) { if (*v == "upscaled") s.graphics_dev = GraphicsMode::Upscaled; else if (*v == "nearest") s.graphics_dev = GraphicsMode::Nearest; }
    if (auto it = j.find("keys"); it != j.end() && it->is_object()) {
        // a port key must be a key the game does not read (older settings used F1 and F8, game hotkeys)
        auto take = [&](const char* name, std::string& dst) {
            auto k = it->find(name);
            if (k == it->end() || !k->is_string()) return;
            const SDL_Keycode key = SDL_GetKeyFromName(k->get<std::string>().c_str());
            if (key != SDLK_UNKNOWN && !bios_key(key, SDL_KMOD_NONE)) dst = k->get<std::string>();
        };
        take("fullscreen", s.keys.fullscreen);
        take("graphics", s.keys.graphics);
        take("options", s.keys.options);
    }
    if (auto v = str("data_dir")) s.data_dir = *v;
    return s;
}
Result<void> save_settings(const Settings& s, const std::filesystem::path& path) {
    nlohmann::json j{{"language", s.language == game::Language::English ? "en" : "de"},
                     {"aspect", s.aspect == AspectMode::Crt4x3 ? "4:3" : "square"},
                     {"fullscreen", s.fullscreen},
                     {"graphics_dev", s.graphics_dev == GraphicsMode::Upscaled ? "upscaled" : "nearest"},
                     {"keys", {{"fullscreen", s.keys.fullscreen}, {"graphics", s.keys.graphics}, {"options", s.keys.options}}},
                     {"data_dir", s.data_dir}, {"pack_build_tried", s.pack_build_tried}};
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << j.dump(1) << '\n';
        if (!f) return fail(tmp, "cannot write settings");
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) return fail(path, "cannot replace settings: " + ec.message());
    return {};
}
std::filesystem::path default_settings_path() {
    char* base = SDL_GetPrefPath("", "MadTV");
    std::filesystem::path p = base ? std::filesystem::path(reinterpret_cast<const char8_t*>(base)) : std::filesystem::current_path();
    SDL_free(base);
    return p / "settings.json";
}
}
