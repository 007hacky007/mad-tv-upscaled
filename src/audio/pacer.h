#pragma once
#include <cstddef>
#include <cstdint>
namespace mtv::audio {
// Keeps the device queue near a target latency: after a stall (or at start) it is refilled with silence, and
// when the game runs ahead the new block is cut so the queue stays under the limit.
class AudioPacer {
public:
    AudioPacer(uint32_t rate, double target_ms, double max_ms)
        : target_(size_t(rate * target_ms / 1000.0)), max_(size_t(rate * max_ms / 1000.0)) {}
    struct Plan { size_t silence = 0, drop = 0; };
    Plan plan(size_t queued, size_t block) const {
        Plan p;
        if (queued < target_ / 2) p.silence = target_ - queued;
        const size_t total = queued + p.silence + block;
        if (total > max_) p.drop = std::min(block, total - target_);
        return p;
    }
private:
    size_t target_, max_;
};
}
