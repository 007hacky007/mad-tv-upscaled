#pragma once
#include "data/result.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace mtv {
inline constexpr int kTickHz = 100; // original game tick, derived from the 213.45 Hz PIT handler
struct TickInput {
    uint32_t tick = 0;                // 100 Hz ticks since program start
    int16_t mouse_x = 0, mouse_y = 0; // logical 320x200
    uint8_t buttons = 0;              // bit 0 left, bit 1 right
    std::vector<std::string> keys;    // key names pressed at this tick (harness names: "e", "enter", "f1", ...)
};
struct TimerRead {
    uint32_t tick = 0;     // tick at which the game read the counter
    uint16_t address = 0;  // DS offset of the counter that was read (0x9034 for random())
    uint16_t value = 0;
};
struct TickRecording {
    int tick_hz = kTickHz;
    std::vector<TickInput> events;     // strictly increasing ticks
    std::vector<TimerRead> timer_reads; // timing-dependent values observed in the reference, in order
};
Result<TickRecording> load_tick_recording(const std::filesystem::path& path);
Result<void> save_tick_recording(const TickRecording& rec, const std::filesystem::path& path);
}
