#include <doctest/doctest.h>
#include "platform/settings.h"
#include <filesystem>
#include <fstream>
using namespace mtv;
namespace {
std::filesystem::path temp(const char* name) {
    auto d = std::filesystem::temp_directory_path() / "mtv-settings-test";
    std::filesystem::create_directories(d);
    auto p = d / name;
    std::filesystem::remove(p);
    return p;
}
}
TEST_SUITE("platform.settings") {
TEST_CASE("settings_defaults") {
    Settings s;
    CHECK(s.language == game::Language::English);
    CHECK(s.aspect == AspectMode::Crt4x3);
    CHECK_FALSE(s.fullscreen);
    CHECK(s.graphics_dev == GraphicsMode::Upscaled);
}
TEST_CASE("settings_roundtrip") {
    Settings s;
    s.language = game::Language::German;
    s.aspect = AspectMode::Square;
    s.fullscreen = true;
    s.graphics_dev = GraphicsMode::Nearest;
    s.keys.fullscreen = "Home"; // F1-F10 are game keys and would be refused
    s.data_dir = "/Spiele/Mad TV ä";
    s.pack_build_tried = true;
    const auto p = temp("roundtrip.json");
    REQUIRE(save_settings(s, p).has_value());
    auto r = load_settings(p);
    REQUIRE(r.has_value());
    CHECK(r.value() == s);
}
TEST_CASE("settings_unknown_keys_ignored") {
    const auto p = temp("unknown.json");
    std::ofstream(p) << R"({"language":"de","colour":"blue","aspect":"square","future":{"x":1}})";
    auto r = load_settings(p);
    REQUIRE(r.has_value());
    CHECK(r.value().language == game::Language::German);
    CHECK(r.value().aspect == AspectMode::Square);
}
TEST_CASE("settings_bad_values_fall_back_to_defaults") {
    const auto p = temp("bad.json");
    std::ofstream(p) << R"({"language":"fr","aspect":7,"fullscreen":"yes"})";
    auto r = load_settings(p);
    REQUIRE(r.has_value());
    CHECK(r.value() == Settings{});
}
TEST_CASE("settings_missing_file_gives_defaults") {
    auto r = load_settings(temp("missing.json"));
    REQUIRE(r.has_value());
    CHECK(r.value() == Settings{});
}
TEST_CASE("settings_corrupt_file_is_an_error") {
    const auto p = temp("corrupt.json");
    std::ofstream(p) << "{not json";
    CHECK_FALSE(load_settings(p).has_value());
}
TEST_CASE("settings_refuse_game_keys_for_port_actions") {
    const auto p = temp("gamekeys.json");
    std::ofstream(p) << R"({"keys":{"graphics":"F8","options":"F1","fullscreen":"F10"}})";
    auto r = load_settings(p);
    REQUIRE(r.has_value());
    CHECK(r.value().keys == KeyMap{});
}
}
