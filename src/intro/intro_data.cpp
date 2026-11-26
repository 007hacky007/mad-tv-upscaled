#include "intro/intro_data.h"
#include "data/lzexe.h"
#include "data/mz.h"
#include "data/powerpacker.h"
#include <algorithm>
#include <fstream>
#include <iterator>
namespace mtv {
namespace {
Result<std::vector<uint8_t>> read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return DataError{p.string(), "", 0, "cannot open"};
    return std::vector<uint8_t>{std::istreambuf_iterator<char>(f), {}};
}
uint16_t w(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
}
Result<IntroData> IntroData::load(const std::filesystem::path& dir) {
    auto pck = read_file(dir / "DATAV.PCK"); if (!pck.has_value()) return pck.error();
    auto mcv = read_file(dir / "MCV.BIN"); if (!mcv.has_value()) return mcv.error();
    auto exe = read_file(dir / "INTRO.EXE"); if (!exe.has_value()) return exe.error();
    IntroData d;
    auto un = unpack_pp20(pck.value()); if (!un.has_value()) return un.error();
    d.datav = std::move(un.value());
    if (d.datav.size() < 0x5080 + 4 * 0x44A) return DataError{"DATAV.PCK", "", 0, "unpacked data too small"};
    // MCV.BIN: word 0 = data paragraph offset of the code that starts at byte 2.
    const auto& m = mcv.value();
    if (m.size() < 4) return DataError{"MCV.BIN", "", 0, "too small"};
    const size_t data = size_t(w(m.data())) * 16 + 2;
    if (data + 0xF1 + 53 * 2 > m.size()) return DataError{"MCV.BIN", "", data, "data area truncated"};
    const uint8_t* D = m.data() + data;
    for (int i = 0; i < 17; ++i) { d.dac[size_t(i)] = {D[0x34 + 3 * i], D[0x35 + 3 * i], D[0x36 + 3 * i]}; d.attr[size_t(i)] = D[0x67 + i]; }
    for (int i = 0; i < 10; ++i) d.spark_offset[size_t(i)] = w(D + 0x20 + 2 * i);
    std::copy_n(D + 0x78, 8, d.bit.begin());
    std::copy_n(D + 0x80, 7, d.layer_add.begin());
    for (int i = 0; i < 53; ++i) { d.tile_dest[size_t(i)] = w(D + 0x87 + 2 * i); d.tile_src[size_t(i)] = w(D + 0xF1 + 2 * i); }
    // INTRO.EXE data segment, found by the Turbo C signature at DS:0004.
    auto unpacked = unlzexe91(exe.value()); if (!unpacked.has_value()) return unpacked.error();
    auto mz = parse_mz(unpacked.value()); if (!mz.has_value()) return mz.error();
    const auto& img = mz.value().image;
    static const char sig[] = "Turbo-C - Copyright (c) 1987";
    auto it = std::search(img.begin(), img.end(), sig, sig + sizeof sig - 1);
    if (it == img.end() || it - img.begin() < 4) return DataError{"INTRO.EXE", "", 0, "data segment not found"};
    const uint8_t* S = img.data() + (it - img.begin()) - 4;
    if (size_t((it - img.begin()) - 4) + 0xF8 + 53 > img.size()) return DataError{"INTRO.EXE", "", 0, "data segment truncated"};
    for (int i = 0; i < 27; ++i) d.logo_step[size_t(i)] = w(S + 0x96 + 2 * i);
    for (int i = 0; i < 5; ++i) { d.phase_count[size_t(i)] = w(S + 0xCC + 2 * i); d.phase_dx[size_t(i)] = int16_t(w(S + 0xD6 + 2 * i)); d.phase_dy[size_t(i)] = int16_t(w(S + 0xE0 + 2 * i)); }
    std::copy_n(S + 0xEA, 7, d.layer_delay.begin());
    std::copy_n(S + 0xF8, 53, d.tile_height.begin());
    return d;
}
IndexedImage IntroData::spark(int f) const {
    IndexedImage img{32, 28, std::vector<uint8_t>(32 * 28, 0)};
    const size_t base = size_t(spark_offset[size_t(f)]) * 4;
    static const int order[4] = {1, 0, 3, 2};
    for (int y = 0; y < 28; ++y) for (int x = 0; x < 32; ++x) {
        uint8_t c = 0;
        for (int p = 0; p < 4; ++p) if (datav[base + size_t(y * 16 + p * 4 + order[x / 8])] & (0x80 >> (x % 8))) c |= uint8_t(1 << p);
        img.indices[size_t(y * 32 + x)] = c;
    }
    return img;
}
IndexedImage IntroData::tile(int layer, int step) const {
    const int h = tile_height[size_t(step)] & 0x7F, wb = (tile_height[size_t(step)] & 0x80) ? 2 : 1;
    IndexedImage img{uint16_t(wb * 8), uint16_t(h), std::vector<uint8_t>(size_t(wb * 8 * h), 0)};
    const size_t base = 0x1180 + tile_src[size_t(step)] + (size_t(layer_add[size_t(layer)]) << 8);
    for (int y = 0; y < h; ++y) for (int x = 0; x < wb * 8; ++x) {
        uint8_t c = 0;
        for (int p = 0; p < 4; ++p) if (datav[base + size_t(p * h * wb + y * wb + x / 8)] & (0x80 >> (x % 8))) c |= uint8_t(1 << p);
        img.indices[size_t(y * wb * 8 + x)] = c;
    }
    return img;
}
IndexedImage IntroData::logo() const {
    IndexedImage img{144, 61, std::vector<uint8_t>(144 * 61, 0)};
    for (int y = 0; y < 61; ++y) for (int x = 0; x < 144; ++x) {
        uint8_t c = 0;
        for (int p = 0; p < 4; ++p) if (datav[0x5080 + size_t(p * 0x44A + y * 18 + x / 8)] & (0x80 >> (x % 8))) c |= uint8_t(1 << p);
        img.indices[size_t(y * 144 + x)] = c;
    }
    return img;
}
}
