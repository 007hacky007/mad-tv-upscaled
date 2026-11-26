#include <doctest/doctest.h>
#include "debug/tick_input.h"
#include <filesystem>
#include <fstream>
using namespace mtv;
static std::filesystem::path tmp_file(const char* name, const std::string& text) {
    auto p = std::filesystem::temp_directory_path() / name;
    std::ofstream(p) << text;
    return p;
}
TEST_SUITE("debug.tick_input") {
TEST_CASE("tick_input_roundtrip") {
    TickRecording rec;
    rec.events.push_back({10, 160, 100, 1, {"e"}});
    rec.events.push_back({25, 63, 100, 0, {}});
    rec.timer_reads.push_back({30, 0x9034, 7});
    auto p = std::filesystem::temp_directory_path() / "mtv_tick_rt.json";
    REQUIRE(save_tick_recording(rec, p).has_value());
    auto back = load_tick_recording(p);
    REQUIRE(back.has_value());
    REQUIRE(back.value().events.size() == 2);
    CHECK(back.value().events[0].tick == 10);
    CHECK(back.value().events[0].keys == std::vector<std::string>{"e"});
    CHECK(back.value().events[1].mouse_x == 63);
    REQUIRE(back.value().timer_reads.size() == 1);
    CHECK(back.value().timer_reads[0].value == 7);
    CHECK(back.value().tick_hz == 100);
}
TEST_CASE("tick_input_rejects_non_monotonic_ticks") {
    auto p = tmp_file("mtv_tick_bad.json", R"({"schema_version":1,"tick_hz":100,"events":[{"tick":5,"mouse_x":0,"mouse_y":0,"buttons":0,"keys":[]},{"tick":4,"mouse_x":0,"mouse_y":0,"buttons":0,"keys":[]}],"timer_reads":[]})");
    CHECK_FALSE(load_tick_recording(p).has_value());
}
TEST_CASE("tick_input_rejects_out_of_range_mouse") {
    auto p = tmp_file("mtv_tick_mouse.json", R"({"schema_version":1,"tick_hz":100,"events":[{"tick":5,"mouse_x":320,"mouse_y":0,"buttons":0,"keys":[]}],"timer_reads":[]})");
    CHECK_FALSE(load_tick_recording(p).has_value());
}
}
