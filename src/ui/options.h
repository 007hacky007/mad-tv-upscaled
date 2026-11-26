#pragma once
// The port's own options overlay (F12): language, aspect, fullscreen, and building the upscaled pictures when they
// are missing. The development graphics toggle (Pause)
// stays out of it.
#include "platform/settings.h"
#include "render/viewport.h"
#include <optional>
#include <string>
#include <vector>
namespace mtv::ui {
enum class OptionItem { Language, Aspect, Fullscreen, BuildPack, Close };
enum class OptionChange { None, Language, Aspect, Fullscreen, BuildPack, Closed };
struct ItemRect { int x = 0, y = 0, w = 0, h = 0; }; // logical 320x200 coordinates
class OptionsMenu {
public:
    bool is_open() const { return open_; }
    void set_open(bool o) { open_ = o; if (o) selected_ = 0; }
    const std::vector<OptionItem>& items() const { return items_; }
    int selected() const { return selected_; }
    void move(int delta);
    OptionChange activate(OptionItem item, Settings& s);
    OptionChange activate_selected(Settings& s) { return activate(items_[size_t(selected_)], s); }
    std::string label(OptionItem item, const Settings& s) const;
    std::string title(const Settings& s) const;
    // Without an upscale pack the menu offers to build one.
    void set_pack_available(bool available);
    ItemRect panel() const { return {72, 52, 176, 32 + 16 * int(items_.size())}; }
    ItemRect item_rect(int i) const { return {84, 76 + i * 16, 152, 12}; }
    std::optional<int> item_at(LogicalPoint p) const;
private:
    bool open_ = false;
    int selected_ = 0;
    std::vector<OptionItem> items_{OptionItem::Language, OptionItem::Aspect, OptionItem::Fullscreen, OptionItem::Close};
};
}
