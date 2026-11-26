#pragma once
#include "intro/ega.h"
#include "intro/intro_data.h"
#include <array>
#include <vector>
namespace mtv {
// One displayed frame of the intro: 320x200 colour indices (0-15), their provenance and the palette in effect.
struct IntroFrame {
    IndexedImage image;
    std::vector<Provenance> provenance;
    std::array<std::array<uint8_t, 3>, 16> rgb{}; // 8-bit RGB per index
};
// Port of INTRO.EXE main (0x38B) and the MCV.BIN driver entries it uses. step() advances by one vertical
// retrace (70 Hz in mode 0Dh): it captures the displayed page, then runs the program up to its next
// retrace wait. A key press ends the animation early (the original checks kbhit at each loop head).
class Intro {
public:
    explicit Intro(const IntroData& data);
    bool step(bool key_pressed);   // false once the intro has finished (text mode restored)
    const IntroFrame& frame() const { return frame_; }
    int vsyncs() const { return vsyncs_; }
private:
    // MCV.BIN entries (index = call offset / 3)
    void k0_init(); void k1_spark(int x, int y, int f); void k2_restore(); void k3_fuse(int x, int y);
    void k4_tile(int step, int layer, uint8_t h); void k6_flip(); void k7_logo_setup();
    void k8_logo(int a, int b, int c); void k9_border(int i); void fade_step();
    // INTRO.EXE helpers
    void move_spark(); void draw_spark(int x, int y); void burn_layers();
    void capture();
    const IntroData& d_;
    Ega v_;
    std::vector<uint8_t> mask_;               // OR of the four spark planes, 4 bytes per row (driver init)
    uint32_t back_ = 0, front_ = 0x2000, display_ = 0;
    int pending_spark_ = -1, last_spark_ = -1; // driver words 0x1E and 0x1C
    std::array<std::array<uint8_t, 3>, 17> dac_;
    // INTRO.EXE globals
    int x_ = 0, y_ = 0, phase_ = 0, idx_ = 0, prevx_ = 0, prevy_ = 0, stage_ = 0, wait_ = 0;
    uint8_t spark_counter_ = 0;
    std::array<uint8_t, 7> delay_{}, step_{};
    std::array<uint16_t, 5> count_{};
    bool skip_ = false, done_ = false;
    int vsyncs_ = 0;
    IntroFrame frame_;
};
}
