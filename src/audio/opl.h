#pragma once
// The AdLib's YM3812 (OPL2), emulated by ymfm, and the renderer that turns the game's timed register writes
// into samples.
#include "rt/dos.h"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
namespace mtv::audio {
class Opl {
public:
    static constexpr uint32_t kClock = 3579545;              // AdLib crystal
    static constexpr uint32_t kSampleRate = kClock / 72;     // 49716 Hz, the chip's own output rate
    Opl();
    ~Opl();
    void write(uint8_t reg, uint8_t value);
    void generate(std::span<int16_t> out); // mono
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Renders spans of the program's virtual time; each register write takes effect at the sample matching its
// instruction count. Fractions of a sample carry over to the next span.
class OplStream {
public:
    explicit OplStream(uint32_t cycles_per_ms) : cycles_per_ms_(cycles_per_ms) {}
    void render(uint64_t from, uint64_t to, std::span<const rt::OplWrite> writes, std::vector<int16_t>& out);
private:
    Opl opl_;
    uint32_t cycles_per_ms_;
    double frac_ = 0;
};
}
