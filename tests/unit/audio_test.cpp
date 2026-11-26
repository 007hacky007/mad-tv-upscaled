#include <doctest/doctest.h>
#include "audio/opl.h"
#include "audio/pacer.h"
#include "platform/ticker.h"
#include <cmath>
#include <vector>
using namespace mtv;
using namespace mtv::audio;
namespace {
// Channel 0 as a plain sine voice (modulator silent), frequency number and block for about 440 Hz.
void program_a440(Opl& opl, bool key_on) {
    opl.write(0x01, 0x20);           // waveform select enable
    opl.write(0x20, 0x21); opl.write(0x23, 0x21); // sustained envelope, multiplier 1
    opl.write(0x40, 0x3F); opl.write(0x43, 0x00); // modulator muted, carrier loud
    opl.write(0x60, 0xF0); opl.write(0x63, 0xF0); // fast attack
    opl.write(0x80, 0x0F); opl.write(0x83, 0x0F);
    opl.write(0xC0, 0x01);           // additive
    // f = fnum * 49716 / 2^(20 - block): block 4, fnum 580 -> 439.9 Hz
    opl.write(0xA0, 580 & 0xFF);
    opl.write(0xB0, uint8_t((key_on ? 0x20 : 0) | (4 << 2) | (580 >> 8)));
}
double pitch(const std::vector<int16_t>& s) {
    int crossings = 0;
    for (size_t i = 1; i < s.size(); ++i) crossings += (s[i - 1] < 0) != (s[i] < 0);
    return crossings / 2.0 / (double(s.size()) / Opl::kSampleRate);
}
int peak(const std::vector<int16_t>& s, size_t from = 0, size_t to = SIZE_MAX) {
    int p = 0;
    for (size_t i = from; i < std::min(to, s.size()); ++i) p = std::max(p, std::abs(int(s[i])));
    return p;
}
}
TEST_SUITE("audio.opl") {
TEST_CASE("opl_plays_a_tone_at_the_programmed_pitch") {
    Opl opl;
    program_a440(opl, true);
    std::vector<int16_t> s(Opl::kSampleRate / 2);
    opl.generate(s);
    CHECK(peak(s) > 1000);
    CHECK(pitch(s) == doctest::Approx(439.9).epsilon(0.03));
}
TEST_CASE("opl_is_silent_without_key_on") {
    Opl opl;
    program_a440(opl, false);
    std::vector<int16_t> s(Opl::kSampleRate / 10);
    opl.generate(s);
    CHECK(peak(s) < 64);
}
TEST_CASE("opl_stream_applies_writes_at_their_virtual_time") {
    // 8000 instructions per ms; a key-on half way through a 20 ms span starts the tone there.
    OplStream st(8000);
    std::vector<rt::OplWrite> setup;
    // the register list of program_a440 as timed writes at time 0, without key-on
    const std::pair<uint8_t, uint8_t> regs[] = {{0x01, 0x20}, {0x20, 0x21}, {0x23, 0x21}, {0x40, 0x3F}, {0x43, 0x00}, {0x60, 0xF0}, {0x63, 0xF0},
                                                {0x80, 0x0F}, {0x83, 0x0F}, {0xC0, 0x01}, {0xA0, 580 & 0xFF}};
    for (auto [r, v] : regs) setup.push_back({0, r, v});
    setup.push_back({80000, 0xB0, uint8_t(0x20 | (4 << 2) | (580 >> 8))});
    std::vector<int16_t> out;
    st.render(0, 160000, setup, out);
    CHECK(out.size() == doctest::Approx(Opl::kSampleRate * 0.020).epsilon(0.01));
    const size_t half = out.size() / 2;
    CHECK(peak(out, 0, half - 10) < 64);
    CHECK(peak(out, half + 50) > 1000);
}
TEST_CASE("opl_stream_carries_fractional_samples_across_ticks") {
    OplStream st(8000);
    std::vector<int16_t> out;
    uint64_t t = 0;
    for (int i = 0; i < 701; ++i) { st.render(t, t + 114144, {}, out); t += 114144; } // 701 retraces = 10.003 s
    const double want = 701.0 * 114144 / 8000.0 / 1000.0 * Opl::kSampleRate;
    CHECK(std::abs(double(out.size()) - want) < 2);
}
}
TEST_SUITE("audio.pacer") {
TEST_CASE("mixer_never_underflows_at_256_frames") {
    // The game makes 14.268 ms of sound per tick at its own rate; the device pulls 256 frames at 48 kHz.
    // Presents come at 60 Hz with up to 6 ms jitter. After the first 100 ms the queue never runs dry and
    // never grows past the limit.
    AudioPacer pacer(Opl::kSampleRate, 60, 150);
    Ticker ticker;
    double queued = 0, frac = 0;
    int underflows = 0;
    double max_queued = 0;
    const double pull = 256.0 * Opl::kSampleRate / 48000.0; // source samples per device callback
    double next_pull_ms = 0;
    uint32_t rng = 12345;
    for (double now_ms = 0; now_ms < 60000; ) {
        rng = rng * 1103515245 + 12345;
        const double jitter = double((rng >> 16) % 600) / 100.0;
        const double present = now_ms + 16.667 + (jitter - 3);
        while (next_pull_ms < present) {
            if (queued < pull && next_pull_ms > 100) ++underflows;
            queued = std::max(0.0, queued - pull);
            next_pull_ms += 256.0 / 48.0;
        }
        now_ms = present;
        const int ticks = ticker.advance(int64_t(now_ms * 1e6));
        for (int i = 0; i < ticks; ++i) {
            frac += Opl::kSampleRate * 0.014268;
            const size_t block = size_t(frac);
            frac -= double(block);
            const auto plan = pacer.plan(size_t(queued), block);
            queued += double(plan.silence) + double(block - std::min(block, plan.drop));
            max_queued = std::max(max_queued, queued);
        }
    }
    CHECK(underflows == 0);
    CHECK(max_queued <= Opl::kSampleRate * 0.150 + 1);
}
TEST_CASE("pacer_refills_after_a_stall_and_drops_when_too_far_ahead") {
    AudioPacer pacer(Opl::kSampleRate, 60, 150);
    auto p = pacer.plan(0, 700);
    CHECK(p.silence == size_t(Opl::kSampleRate * 0.060));
    CHECK(p.drop == 0);
    p = pacer.plan(size_t(Opl::kSampleRate * 0.149), 700);
    CHECK(p.silence == 0);
    CHECK(p.drop > 0);
}
}
