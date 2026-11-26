#pragma once
#include <cstdint>
namespace mtv {
// Borland Turbo C rand()/srand() as used by MTV.EXE (0x49CDE / 0x49CCD).
class TurboCRandom {
public:
    void seed(uint16_t s) { state_ = s; }
    int16_t next() { state_ = state_ * 0x015A4E35u + 1u; return int16_t((state_ >> 16) & 0x7FFF); }
    uint32_t state() const { return state_; }
private:
    uint32_t state_ = 1;
};
// The game's random(n, salt) at 0x1D809: srand(timer + salt + base), then rand() % n + 1.
// `timer` is the 50 Hz counter DS:9034, `base` the 32-bit value at DS:8EAC.
inline int mad_random(int n, uint16_t salt, uint16_t timer, uint32_t base) {
    TurboCRandom r;
    r.seed(uint16_t(uint32_t(timer) + salt + base));
    return r.next() % n + 1;
}
}
