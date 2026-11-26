#include "debug/tick_input.h"
#include <nlohmann/json.hpp>
#include <fstream>
namespace mtv {
Result<TickRecording> load_tick_recording(const std::filesystem::path& path) {
    auto fail = [&](std::size_t i, std::string why) -> Result<TickRecording> { return DataError{path.string(), "tick recording", i, std::move(why)}; };
    std::ifstream f(path);
    if (!f) return fail(0, "cannot open");
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail(0, "not a JSON object");
    if (j.value("schema_version", 0) != 1) return fail(0, "unsupported schema_version");
    TickRecording r;
    r.tick_hz = j.value("tick_hz", 0);
    if (r.tick_hz != kTickHz) return fail(0, "tick_hz must be 100");
    try {
        std::size_t i = 0;
        for (const auto& e : j.at("events")) {
            TickInput t;
            t.tick = e.at("tick").get<uint32_t>();
            int x = e.at("mouse_x").get<int>(), y = e.at("mouse_y").get<int>();
            if (x < 0 || x >= 320 || y < 0 || y >= 200) return fail(i, "mouse outside 320x200");
            t.mouse_x = int16_t(x); t.mouse_y = int16_t(y);
            t.buttons = e.at("buttons").get<uint8_t>();
            t.keys = e.at("keys").get<std::vector<std::string>>();
            if (!r.events.empty() && t.tick <= r.events.back().tick) return fail(i, "event ticks must strictly increase");
            r.events.push_back(std::move(t));
            ++i;
        }
        for (const auto& e : j.at("timer_reads"))
            r.timer_reads.push_back({e.at("tick").get<uint32_t>(), e.at("address").get<uint16_t>(), e.at("value").get<uint16_t>()});
    } catch (const nlohmann::json::exception& e) {
        return fail(0, e.what());
    }
    return r;
}
Result<void> save_tick_recording(const TickRecording& rec, const std::filesystem::path& path) {
    nlohmann::json j{{"schema_version", 1}, {"tick_hz", rec.tick_hz}, {"events", nlohmann::json::array()}, {"timer_reads", nlohmann::json::array()}};
    for (const auto& e : rec.events)
        j["events"].push_back({{"tick", e.tick}, {"mouse_x", e.mouse_x}, {"mouse_y", e.mouse_y}, {"buttons", e.buttons}, {"keys", e.keys}});
    for (const auto& t : rec.timer_reads)
        j["timer_reads"].push_back({{"tick", t.tick}, {"address", t.address}, {"value", t.value}});
    std::ofstream f(path);
    f << j.dump(1) << '\n';
    if (!f) return DataError{path.string(), "tick recording", 0, "write failed"};
    return {};
}
}
