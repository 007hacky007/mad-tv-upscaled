#include <doctest/doctest.h>
#include "platform/settings.h"
#include "ui/options.h"
#include <algorithm>
#include <filesystem>
using namespace mtv;
TEST_SUITE("ui.options") {
TEST_CASE("options_changes_settings_and_persists") {
    Settings s;
    ui::OptionsMenu menu;
    CHECK(menu.activate(ui::OptionItem::Language, s) == ui::OptionChange::Language);
    CHECK(s.language == game::Language::German);
    CHECK(menu.activate(ui::OptionItem::Aspect, s) == ui::OptionChange::Aspect);
    CHECK(s.aspect == AspectMode::Square);
    CHECK(menu.activate(ui::OptionItem::Fullscreen, s) == ui::OptionChange::Fullscreen);
    CHECK(s.fullscreen);
    const auto p = std::filesystem::temp_directory_path() / "mtv-options-test.json";
    REQUIRE(save_settings(s, p).has_value());
    auto back = load_settings(p);
    REQUIRE(back.has_value());
    CHECK(back.value() == s);
}
TEST_CASE("options_menu_has_no_graphics_entry") {
    ui::OptionsMenu menu;
    const auto& items = menu.items();
    CHECK(std::find(items.begin(), items.end(), ui::OptionItem::Language) != items.end());
    CHECK(std::find(items.begin(), items.end(), ui::OptionItem::Aspect) != items.end());
    CHECK(std::find(items.begin(), items.end(), ui::OptionItem::Fullscreen) != items.end());
    Settings s;
    for (auto it : items) {
        const std::string l = menu.label(it, s);
        CHECK(l.find("raphic") == std::string::npos);
        CHECK(l.find("earest") == std::string::npos);
    }
}
TEST_CASE("labels_follow_the_language") {
    ui::OptionsMenu menu;
    Settings s;
    CHECK(menu.label(ui::OptionItem::Language, s) == "Language: English");
    s.language = game::Language::German;
    CHECK(menu.label(ui::OptionItem::Language, s) == "Sprache: Deutsch");
    CHECK(menu.label(ui::OptionItem::Aspect, s) == "Bildformat: 4:3");
}
TEST_CASE("keyboard_and_mouse_select_items") {
    ui::OptionsMenu menu;
    Settings s;
    menu.move(1);
    CHECK(menu.items()[size_t(menu.selected())] == ui::OptionItem::Aspect);
    menu.move(-5);
    CHECK(menu.selected() == 0);
    const auto r = menu.item_rect(2);
    CHECK(menu.item_at({r.x + 1, r.y + 1}) == 2);
    CHECK_FALSE(menu.item_at({0, 0}).has_value());
    CHECK(menu.activate(ui::OptionItem::Close, s) == ui::OptionChange::Closed);
}
}
TEST_SUITE("ui.options") {
TEST_CASE("build_pack_item_only_without_a_pack") {
    ui::OptionsMenu menu;
    Settings s;
    const auto has = [&] { const auto& i = menu.items(); return std::find(i.begin(), i.end(), ui::OptionItem::BuildPack) != i.end(); };
    CHECK_FALSE(has());
    menu.set_pack_available(false);
    REQUIRE(has());
    CHECK(menu.items().back() == ui::OptionItem::Close);
    CHECK(menu.activate(ui::OptionItem::BuildPack, s) == ui::OptionChange::BuildPack);
    CHECK_FALSE(menu.is_open());
    CHECK(menu.label(ui::OptionItem::BuildPack, s) == "Build upscaled pictures");
    s.language = game::Language::German;
    CHECK(menu.label(ui::OptionItem::BuildPack, s) == "Hochskalierte Bilder erstellen");
    // every item stays inside the panel
    const auto p = menu.panel();
    for (int i = 0; i < int(menu.items().size()); ++i) CHECK(menu.item_rect(i).y + menu.item_rect(i).h <= p.y + p.h);
    menu.set_pack_available(true);
    CHECK_FALSE(has());
}
}
