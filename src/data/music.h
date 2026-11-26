#pragma once
#include "data/result.h"
#include <cstdint>
#include <span>
#include <vector>
namespace mtv {
// Palladix PLX song as parsed by the AdLib driver (slot 98, routine at driver offset 0x1431):
// "PLX", mode (0 = 9 melodic tracks, 1 = OPL rhythm mode with 11 tracks, 2 = single stream), speed byte,
// tempo word, then for modes 0 and 1 a u16 offset per track (0 = unused).
struct PalladixSong {
    uint8_t mode = 0, speed = 0;
    uint16_t tempo = 0;
    std::vector<uint16_t> track_offsets;
    std::vector<uint8_t> data;
    int used_tracks() const { int n = 0; for (auto o : track_offsets) n += o != 0; return n; }
};
Result<PalladixSong> decode_song(std::span<const uint8_t> bytes);
}
