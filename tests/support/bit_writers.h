#pragma once
// Test encoders for synthetic PowerPacker and RSC image streams.
#include <cstdint>
#include <string>
#include <vector>
namespace mtv::test {
// Bits in the order the decoder consumes them.
struct Bits {
    std::vector<int> v;
    void put(uint32_t value, int n) { for (int i = n - 1; i >= 0; --i) v.push_back(int((value >> i) & 1)); } // MSB first (PowerPacker)
    void put_lsb(uint32_t value, int n) { for (int i = 0; i < n; ++i) v.push_back(int((value >> i) & 1)); } // LSB first (RSC)
};
// PowerPacker: bits are read from the end of the stream, LSB of each byte first.
inline std::vector<uint8_t> pp20_file(const Bits& b, uint32_t out_len, const uint8_t lens[4] = nullptr) {
    static const uint8_t def[4] = {9, 10, 11, 11};
    std::vector<uint8_t> bytes((b.v.size() + 7) / 8, 0);
    for (size_t i = 0; i < b.v.size(); ++i) if (b.v[i]) bytes[i / 8] |= uint8_t(1u << (i % 8));
    std::vector<uint8_t> f = {'P', 'P', '2', '0'};
    for (int i = 0; i < 4; ++i) f.push_back((lens ? lens : def)[i]);
    f.insert(f.end(), bytes.rbegin(), bytes.rend());
    f.push_back(uint8_t(out_len >> 16)); f.push_back(uint8_t(out_len >> 8)); f.push_back(uint8_t(out_len)); f.push_back(0);
    return f;
}
// RSC image stream: header (frame count + w,h pairs), node count (BE16), pixel count (BE32), LSB-first bits.
inline std::vector<uint8_t> rsc_image(std::vector<std::pair<uint8_t, uint8_t>> frames, uint16_t nodes, uint32_t count, const Bits& b) {
    std::vector<uint8_t> f{uint8_t(frames.size())};
    for (auto [w, h] : frames) { f.push_back(w); f.push_back(h); }
    f.push_back(uint8_t(nodes >> 8)); f.push_back(uint8_t(nodes));
    for (int s = 24; s >= 0; s -= 8) f.push_back(uint8_t(count >> s));
    std::vector<uint8_t> bytes((b.v.size() + 7) / 8, 0);
    for (size_t i = 0; i < b.v.size(); ++i) if (b.v[i]) bytes[i / 8] |= uint8_t(1u << (i % 8));
    f.insert(f.end(), bytes.begin(), bytes.end());
    return f;
}
}
namespace mtv::test {
// RSC LZW stream (music): header u16 unpacked size, u16 packed size, 00 08 09 00, then MSB-first codes.
// `codes` are (value, width); the last one is the dummy code the decoder reads but never processes.
inline std::vector<uint8_t> rsc_lzw(std::vector<std::pair<uint16_t, int>> codes, uint16_t unpacked) {
    std::vector<int> bits;
    for (auto [c, w] : codes) for (int i = w - 1; i >= 0; --i) bits.push_back((c >> i) & 1);
    const uint16_t packed = uint16_t(bits.size() / 8);
    while (bits.size() % 8) bits.push_back(0);
    std::vector<uint8_t> f{uint8_t(unpacked), uint8_t(unpacked >> 8), uint8_t(packed), uint8_t(packed >> 8), 0, 8, 9, 0};
    for (size_t i = 0; i < bits.size(); i += 8) {
        uint8_t b = 0;
        for (int k = 0; k < 8; ++k) b = uint8_t(b << 1 | bits[i + size_t(k)]);
        f.push_back(b);
    }
    return f;
}
}
