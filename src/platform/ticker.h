#pragma once
#include <cstdint>
namespace mtv {
// Turns wall-clock time into game ticks. One tick is one VGA retrace of mode 13h (70.086 Hz), the unit the
// translated program advances in; presentation runs at whatever rate the display has.
class Ticker {
public:
    static constexpr int64_t kPeriodNs = 14'268'000;
    static constexpr int kMaxCatchUp = 8; // after a stall, run at most this many ticks and drop the rest
    // `now_ns` is a monotonic clock. Returns how many ticks to run now.
    int advance(int64_t now_ns) {
        if (!started_) { started_ = true; last_ = now_ns; return 0; }
        acc_ += now_ns - last_;
        last_ = now_ns;
        int64_t n = acc_ / kPeriodNs;
        if (n > kMaxCatchUp) { acc_ = 0; return kMaxCatchUp; }
        acc_ -= n * kPeriodNs;
        return int(n);
    }
private:
    bool started_ = false;
    int64_t last_ = 0, acc_ = 0;
};
}
