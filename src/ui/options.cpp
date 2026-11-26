#include "ui/options.h"
#include <algorithm>
namespace mtv::ui {
void OptionsMenu::move(int delta) { selected_ = std::clamp(selected_ + delta, 0, int(items_.size()) - 1); }
OptionChange OptionsMenu::activate(OptionItem item, Settings& s) {
    switch (item) {
    case OptionItem::Language:
        s.language = s.language == game::Language::English ? game::Language::German : game::Language::English;
        return OptionChange::Language;
    case OptionItem::Aspect:
        s.aspect = s.aspect == AspectMode::Crt4x3 ? AspectMode::Square : AspectMode::Crt4x3;
        return OptionChange::Aspect;
    case OptionItem::Fullscreen:
        s.fullscreen = !s.fullscreen;
        return OptionChange::Fullscreen;
    case OptionItem::Pictures:
        s.graphics_dev = s.graphics_dev == GraphicsMode::Upscaled ? GraphicsMode::Nearest : GraphicsMode::Upscaled;
        open_ = false;
        return OptionChange::Pictures;
    case OptionItem::BuildPack:
        open_ = false;
        return OptionChange::BuildPack;
    case OptionItem::Close:
        open_ = false;
        return OptionChange::Closed;
    }
    return OptionChange::None;
}
void OptionsMenu::set_pack_available(bool available) {
    items_ = {OptionItem::Language, OptionItem::Aspect, OptionItem::Fullscreen};
    items_.push_back(available ? OptionItem::Pictures : OptionItem::BuildPack);
    items_.push_back(OptionItem::Close);
    selected_ = std::min(selected_, int(items_.size()) - 1);
}
std::string OptionsMenu::title(const Settings& s) const { return s.language == game::Language::German ? "Optionen" : "Options"; }
std::string OptionsMenu::label(OptionItem item, const Settings& s) const {
    const bool de = s.language == game::Language::German;
    switch (item) {
    case OptionItem::Language: return de ? "Sprache: Deutsch" : "Language: English";
    case OptionItem::Aspect: {
        const char* v = s.aspect == AspectMode::Crt4x3 ? "4:3" : (de ? "quadratische Pixel" : "square pixels");
        return std::string(de ? "Bildformat: " : "Aspect: ") + v;
    }
    case OptionItem::Fullscreen: return std::string(de ? "Vollbild: " : "Fullscreen: ") + (s.fullscreen ? (de ? "an" : "on") : (de ? "aus" : "off"));
    case OptionItem::Pictures:
        if (s.graphics_dev == GraphicsMode::Upscaled) return de ? "Bilder: hochskaliert" : "Pictures: upscaled";
        return de ? "Bilder: Original" : "Pictures: original";
    case OptionItem::BuildPack: return de ? "Hochskalierte Bilder erstellen" : "Build upscaled pictures";
    case OptionItem::Close: return de ? "Weiterspielen" : "Back to the game";
    }
    return {};
}
std::optional<int> OptionsMenu::item_at(LogicalPoint p) const {
    for (int i = 0; i < int(items_.size()); ++i) {
        const auto r = item_rect(i);
        if (p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h) return i;
    }
    return std::nullopt;
}
}
