#pragma once
// Test utility: builds LZEXE 0.91 files from an explicit token list, so the
// unpacker can be tested without copyrighted data.
#include <cstdint>
#include <vector>
namespace mtv::test {
class LzBitWriter {
public:
    LzBitWriter() { reserve(); }
    void bit(int b) {
        if (b) { uint16_t w = uint16_t(out_[slot_] | (out_[slot_ + 1] << 8)) | uint16_t(1u << used_); out_[slot_] = uint8_t(w); out_[slot_ + 1] = uint8_t(w >> 8); }
        if (++used_ == 16) reserve();
    }
    void byte(uint8_t b) { out_.push_back(b); }
    void literal(uint8_t b) { bit(1); byte(b); }
    void short_ref(int len, int back) { // len 2..5, back 1..256
        bit(0); bit(0); bit(((len - 2) >> 1) & 1); bit((len - 2) & 1); byte(uint8_t(0x100 - back));
    }
    void long_ref(int len, int back) { // len 3..9 inline, larger via extra byte
        uint16_t span = uint16_t(0x10000 - back);
        bit(0); bit(1);
        uint8_t hi = uint8_t((span >> 5) & 0xF8);
        if (len <= 9) { byte(uint8_t(span)); byte(uint8_t(hi | (len - 2))); }
        else { byte(uint8_t(span)); byte(hi); byte(uint8_t(len - 1)); }
    }
    void segment_marker() { bit(0); bit(1); byte(0); byte(0); byte(1); }
    void end() { bit(0); bit(1); byte(0); byte(0); byte(0); }
    std::vector<uint8_t> bytes() const { return out_; }
private:
    void reserve() { slot_ = out_.size(); out_.push_back(0); out_.push_back(0); used_ = 0; }
    std::vector<uint8_t> out_;
    size_t slot_ = 0;
    int used_ = 0;
};
struct LzFileSpec {
    std::vector<uint8_t> stream;
    std::vector<uint8_t> reloc_table; // raw 0.91 relocation encoding, must end with 00 01 00
    uint16_t ip = 0x10, cs = 0x2, sp = 0x80, ss = 0x3, minalloc = 0x40, maxalloc = 0xFFFF;
};
inline void put16(std::vector<uint8_t>& v, size_t at, uint16_t x) { v[at] = uint8_t(x); v[at + 1] = uint8_t(x >> 8); }
inline std::vector<uint8_t> build_lzexe(const LzFileSpec& s, const char* sig = "LZ91") {
    const size_t header = 32;
    size_t data_end = header + s.stream.size();
    size_t loader = (data_end + 15) & ~size_t(15);
    std::vector<uint8_t> f(loader + 0x158 + s.reloc_table.size() + 16, 0);
    f[0] = 'M'; f[1] = 'Z';
    put16(f, 4, uint16_t((f.size() + 511) / 512)); put16(f, 2, uint16_t(f.size() % 512));
    put16(f, 8, 2); put16(f, 10, s.minalloc); put16(f, 12, s.maxalloc);
    put16(f, 0x14, 0); put16(f, 0x16, uint16_t(loader / 16 - 2)); put16(f, 0x18, 0x1C);
    for (int i = 0; i < 4; ++i) f[0x1C + i] = uint8_t(sig[i]);
    for (size_t i = 0; i < s.stream.size(); ++i) f[header + i] = s.stream[i];
    put16(f, loader + 0, s.ip); put16(f, loader + 2, s.cs); put16(f, loader + 4, s.sp); put16(f, loader + 6, s.ss);
    put16(f, loader + 8, uint16_t(s.stream.size() / 16 + 1)); put16(f, loader + 10, 1); put16(f, loader + 12, 0x200);
    for (size_t i = 0; i < s.reloc_table.size(); ++i) f[loader + 0x158 + i] = s.reloc_table[i];
    return f;
}
}
