#include "audio/opl.h"
#include "ymfm_opl.h"
#include <algorithm>
#include <cmath>
namespace mtv::audio {
// ymfm gives the chip's own range (one operator peaks near 4096); nine voices rarely exceed 8000.
constexpr int32_t kGain = 4;
struct Opl::Impl : ymfm::ymfm_interface {
    ymfm::ym3812 chip{*this};
    Impl() { chip.reset(); }
};
Opl::Opl() : impl_(std::make_unique<Impl>()) {}
Opl::~Opl() = default;
void Opl::write(uint8_t reg, uint8_t value) {
    impl_->chip.write_address(reg);
    impl_->chip.write_data(value);
}
void Opl::generate(std::span<int16_t> out) {
    ymfm::ym3812::output_data o;
    for (auto& s : out) {
        impl_->chip.generate(&o, 1);
        s = int16_t(std::clamp<int32_t>(o.data[0] * kGain, -32768, 32767));
    }
}
void OplStream::render(uint64_t from, uint64_t to, std::span<const rt::OplWrite> writes, std::vector<int16_t>& out) {
    const double per_cycle = double(Opl::kSampleRate) / (double(cycles_per_ms_) * 1000.0);
    const double exact = double(to > from ? to - from : 0) * per_cycle + frac_;
    const size_t n = size_t(exact);
    frac_ = exact - double(n);
    const size_t base = out.size();
    out.resize(base + n);
    size_t done = 0;
    for (const auto& w : writes) {
        const size_t at = w.icount <= from ? 0 : std::min(n, size_t(double(w.icount - from) * per_cycle));
        if (at > done) { opl_.generate(std::span(out).subspan(base + done, at - done)); done = at; }
        opl_.write(uint8_t(w.reg), w.value);
    }
    if (n > done) opl_.generate(std::span(out).subspan(base + done, n - done));
}
}
