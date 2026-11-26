#include "intro/intro.h"
namespace mtv {
namespace {
constexpr uint32_t kSave = 0x1F40;  // save-under area after the visible 8000 bytes of a page
constexpr uint32_t kOffscreen = 0x4000; // segment A400
}
Intro::Intro(const IntroData& d) : d_(d), dac_(d.dac), delay_(d.layer_delay), count_(d.phase_count) {
    mask_.resize(0x1180 / 4);
    for (size_t row = 0; row < 0x1180 / 16; ++row)
        for (size_t wd = 0; wd < 2; ++wd)
            for (size_t b = 0; b < 2; ++b) {
                uint8_t m = 0;
                for (size_t p = 0; p < 4; ++p) m |= d_.datav[row * 16 + p * 4 + wd * 2 + b];
                mask_[row * 4 + wd * 2 + b] = m;
            }
}
void Intro::capture() {
    frame_.image = v_.page(display_);
    frame_.provenance = v_.page_provenance(display_);
    for (size_t i = 0; i < 16; ++i) {
        const auto& c = dac_[d_.attr[i] < 17 ? d_.attr[i] : 0];
        frame_.rgb[i] = {uint8_t((c[0] << 2) | (c[0] >> 4)), uint8_t((c[1] << 2) | (c[1] >> 4)), uint8_t((c[2] << 2) | (c[2] >> 4))};
    }
    ++vsyncs_;
}
// Entry 0 (0x21): mode 0Dh, palette, clear both pages, no pending sprite.
void Intro::k0_init() { v_.clear(0, 0x4000); pending_spark_ = last_spark_ = -1; }
// Entry 1 (0xD3): spark frame f at (x, y) on the back page, saving 5x28 bytes underneath.
void Intro::k1_spark(int x, int y, int f) {
    const int shift = x & 7;
    uint32_t si = uint32_t(y * 40 + (x >> 3));
    pending_spark_ = int(si);
    const size_t sp = size_t(d_.spark_offset[size_t(f)]) * 4, mk = size_t(d_.spark_offset[size_t(f)]);
    uint32_t save = back_ + kSave;
    for (int row = 0; row < 28; ++row) {
        auto bytes40 = [&](const uint8_t* m) {
            uint64_t v = uint64_t(m[1]) << 32 | uint64_t(m[0]) << 24 | uint64_t(m[3]) << 16 | uint64_t(m[2]) << 8;
            return v >> shift;
        };
        std::array<uint64_t, 4> planes;
        for (int p = 0; p < 4; ++p) planes[size_t(p)] = bytes40(d_.datav.data() + sp + size_t(row * 16 + p * 4));
        const uint64_t mask = bytes40(mask_.data() + mk + size_t(row * 4));
        for (int col = 0; col < 5; ++col) {
            const uint32_t addr = back_ + si + uint32_t(col);
            v_.latch_copy(addr, save++);
            const int sh = 32 - col * 8;
            std::array<uint8_t, 4> data;
            for (int p = 0; p < 4; ++p) data[size_t(p)] = uint8_t(planes[size_t(p)] >> sh);
            std::array<Provenance, 8> pr;
            for (int b = 0; b < 8; ++b) pr[size_t(b)] = Provenance{PixelSource::Spark, uint16_t(f), uint8_t(col * 8 + b - shift), uint8_t(row)};
            v_.write_planes(addr, 0xF, uint8_t(mask >> sh), data, pr);
        }
        si += 40;
    }
}
// Entry 2 (0x318): put back the bytes saved under the last spark, then make the pending one current.
void Intro::k2_restore() {
    if (last_spark_ != -1) {
        uint32_t si = back_ + kSave, di = uint32_t(last_spark_);
        for (int row = 0; row < 28; ++row) {
            for (int col = 0; col < 5; ++col) v_.latch_copy(si++, back_ + di + uint32_t(col));
            di += 40;
        }
    }
    last_spark_ = pending_spark_;
    pending_spark_ = -1;
}
// Entry 3 (0x361): XOR plane 0 along a line from (x, y) towards (160, 200), skipping the first 5 rows.
void Intro::k3_fuse(int x, int y) {
    const int dx = std::abs(160 - x), dy = 200 - y;
    int err = dy / 2, skip = 5;
    uint32_t di = uint32_t(y * 40 + (x >> 3));
    uint8_t ah = d_.bit[size_t(x & 7)];
    for (int n = dy; n > 0; --n) {
        if (--skip < 0)
            for (int b = 0; b < 8; ++b) if (ah & (0x80 >> b)) v_.xor_plane_bit((back_ + di) * 8 + uint32_t(b), 0);
        di += 40;
        err -= dx;
        if (err >= 0) continue;
        err += dy;
        if (x < 160) { const bool c = ah & 1; ah = uint8_t((ah >> 1) | (ah << 7)); if (c) ++di; }
        else { const bool c = ah & 0x80; ah = uint8_t((ah << 1) | (ah >> 7)); if (c) --di; }
    }
}
// Entry 4 (0x3F0): fire tile of `layer` for path position `step`, plane by plane, on the back page.
void Intro::k4_tile(int step, int layer, uint8_t h) {
    const uint32_t dest = d_.tile_dest[size_t(step)];
    size_t src = 0x1180 + d_.tile_src[size_t(step)] + (size_t(d_.layer_add[size_t(layer)]) << 8);
    const int rows = h & 0x7F, wb = (h & 0x80) ? 2 : 1;
    const uint16_t frame = uint16_t(layer * 53 + step);
    for (int p = 0; p < 4; ++p)
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < wb; ++c) {
                std::array<uint8_t, 4> data{}; data[size_t(p)] = d_.datav[src++];
                std::array<Provenance, 8> pr;
                for (int b = 0; b < 8; ++b) pr[size_t(b)] = Provenance{PixelSource::Fire, frame, uint8_t(c * 8 + b), uint8_t(r)};
                v_.write_planes(back_ + dest + uint32_t(r * 40 + c), uint8_t(1 << p), 0xFF, data, pr);
            }
}
// Entry 6 (0x497): swap pages and show the one just drawn.
void Intro::k6_flip() { std::swap(back_, front_); display_ = front_; }
// Entry 7 (0x4BE): save a 18x19-byte band of the back page at A400:044A, copy the logo planes to A400:0.
void Intro::k7_logo_setup() {
    for (uint32_t r = 0; r < 19; ++r)
        for (uint32_t c = 0; c < 18; ++c) v_.latch_copy(back_ + 0x113B + r * 40 + c, kOffscreen + 0x44A + r * 18 + c);
    for (int p = 0; p < 4; ++p)
        for (uint32_t i = 0; i < 0x44A; ++i) {
            std::array<uint8_t, 4> data{}; data[size_t(p)] = d_.datav[0x5080 + size_t(p) * 0x44A + i];
            std::array<Provenance, 8> pr;
            for (int b = 0; b < 8; ++b) pr[size_t(b)] = Provenance{PixelSource::Logo, 0, uint8_t((i % 18) * 8 + uint32_t(b)), uint8_t(i / 18)};
            v_.write_planes(kOffscreen + i, uint8_t(1 << p), 0xFF, data, pr);
        }
}
// Entry 8 (0x519): move the 144-pixel band from row b (front page) to row a (back page), then fill rows
// c..a-1 with logo rows (c - 49) below row 125 and with black from there on.
void Intro::k8_logo(int a, int b, int c) {
    for (int r = 0, row = a; r < 0x3D && row < 200; ++r, ++row)
        for (uint32_t col = 0; col < 18; ++col) v_.latch_copy(front_ + uint32_t((b + r) * 40 + 11) + col, back_ + uint32_t(row * 40 + 11) + col);
    int row = c;
    if (row >= a) return;
    for (; row < a && row < 200 && row < 0x7D; ++row)
        for (uint32_t col = 0; col < 18; ++col) v_.latch_copy(kOffscreen + uint32_t((row - 0x31) * 18) + col, back_ + uint32_t(row * 40 + 11) + col);
    for (; row < a && row < 200; ++row)
        for (uint32_t col = 0; col < 18; ++col) v_.write_planes(back_ + uint32_t(row * 40 + 11) + col, 0xF, 0xFF, {0, 0, 0, 0}, Provenance{});
}
// Entry 9 (0x602): border pixels drawn straight onto the displayed page.
void Intro::k9_border(int i) {
    auto set = [&](uint32_t addr, uint8_t mapmask, uint8_t bitmask) {
        v_.write_planes(front_ + addr, mapmask, bitmask, {0xFF, 0xFF, 0xFF, 0xFF}, Provenance{PixelSource::Solid, 0, 0, 0});
    };
    if (i < 0x57) for (int r = 0; r < 4; ++r) set(uint32_t(r * 40 + (i >> 3)), 0x8, d_.bit[size_t(i & 7)]);
    const int x2 = 0x13F - i;
    if (x2 >= 0xE9) for (int r = 0; r < 4; ++r) set(uint32_t(r * 40 + (x2 >> 3)), 0x8, d_.bit[size_t(x2 & 7)]);
    if (0x30 - i >= 4) set(uint32_t((0x30 - i) * 40), 0xA, 0xF0);
    if (i + 0x6E < 200) set(uint32_t((i + 0x6E) * 40), 0xA, 0xF0);
    if (i + 4 <= 0x30) set(uint32_t((i + 4) * 40 + 0x27), 0x2, 0x0F);
}
// Entry 10 (0x6FA), one iteration: darken DAC entries 1-16 by 2.
void Intro::fade_step() {
    for (size_t e = 1; e < 17; ++e)
        for (auto& c : dac_[e]) c = c >= 2 ? uint8_t(c - 2) : 0;
}
// 0x6EF: advance the spark along the current phase.
void Intro::move_spark() {
    const uint16_t old = count_[size_t(phase_)];
    --count_[size_t(phase_)];
    if (old == 0 && phase_ < 4) ++phase_;
    x_ += d_.phase_dx[size_t(phase_)];
    y_ += d_.phase_dy[size_t(phase_)];
}
// 0x729: spark animation frame counter (0..17, frame = counter / 2).
void Intro::draw_spark(int x, int y) {
    const uint8_t old = spark_counter_++;
    if (old > 0x11) spark_counter_ = 0;
    k1_spark(x, y, spark_counter_ >> 1);
}
// 0x752: seven fire layers walk the 53 path positions, one every 4 calls, redrawing for the other page.
void Intro::burn_layers() {
    for (int s = 0; s < 7; ++s) {
        auto& cnt = delay_[size_t(s)]; auto& st = step_[size_t(s)];
        if (cnt == 1 && st < 0x35) k4_tile(st, s, d_.tile_height[st]);
        const uint8_t old = cnt--;
        if (old == 0 && st < 0x35) { cnt = 3; const uint8_t at = st++; k4_tile(at, s, d_.tile_height[at]); }
    }
}
// Resumable port of main(): each case ends at the next retrace wait.
bool Intro::step(bool key) {
    if (done_) return false;
    if (stage_ > 0) capture();
    skip_ = skip_ || key;
    for (;;) switch (stage_) {
    case 0: k0_init(); k6_flip(); stage_ = 1; wait_ = 0; return true;      // then 10 retrace waits
    case 1: if (++wait_ < 10) return true;
        x_ = 0x57; y_ = 0x6E; k3_fuse(x_, y_); prevx_ = prevy_ = 0; k6_flip(); stage_ = 2; return true;
    case 2: phase_ = 0; idx_ = 0; stage_ = 3; [[fallthrough]];
    case 3: // spark runs round the frame
        if (skip_ || idx_ != 0) { stage_ = 4; continue; }
        stage_ = 30; return true;
    case 30:
        if (prevy_ != 0) k3_fuse(prevx_, prevy_);
        prevy_ = y_; prevx_ = x_;
        move_spark(); k2_restore(); burn_layers(); draw_spark(x_ - 15, y_ - 14); k3_fuse(x_, y_);
        if (phase_ == 4) idx_ = 1;
        k6_flip(); stage_ = 3; continue;
    case 4: // two more frames to clear the spark from both pages
        if (skip_ || idx_ >= 3) { stage_ = 5; continue; }
        stage_ = 40; return true;
    case 40: k3_fuse(x_, y_); k2_restore(); burn_layers(); k6_flip(); ++idx_; stage_ = 4; continue;
    case 5: // let the fire finish
        if (skip_ || idx_ != 3) { stage_ = 6; continue; }
        stage_ = 50; return true;
    case 50: burn_layers(); k6_flip();
        if (step_[6] == 0x35) { idx_ = 4; k7_logo_setup(); y_ = 0x31; prevy_ = prevx_ = 0x31; phase_ = 0; }
        stage_ = 5; continue;
    case 6: // logo slides in
        if (skip_ || idx_ != 4) { stage_ = 7; continue; }
        stage_ = 60; return true;
    case 60: k8_logo(y_, prevy_, prevx_); prevx_ = prevy_; prevy_ = y_; y_ += d_.logo_step[size_t(phase_)]; ++phase_; k6_flip();
        if (phase_ == 0x1B) { idx_ = 5; phase_ = 0; }
        stage_ = 6; continue;
    case 7: // border, then hold until 800
        if (skip_ || idx_ != 5) { stage_ = 8; continue; }
        stage_ = 70; return true;
    case 70: { if (phase_ < 100) k9_border(phase_); const int old = phase_++; if (old > 800) idx_ = 6; stage_ = 7; continue; }
    case 8: // fade out (entry 10), one retrace per step until DAC entry 2 red is 0
        fade_step(); stage_ = 80; return true;
    case 80: if (dac_[2][0] != 0) { stage_ = 8; continue; }
        done_ = true; return false;
    default: done_ = true; return false;
    }
}
}
