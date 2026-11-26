#include "data/music.h"
namespace mtv {
Result<PalladixSong> decode_song(std::span<const uint8_t> b) {
    auto fail = [](size_t at, const char* why) -> Result<PalladixSong> { return DataError{"rsc", "song", at, why}; };
    if (b.size() < 7 || b[0] != 'P' || b[1] != 'L' || b[2] != 'X') return fail(0, "not a PLX song");
    PalladixSong s;
    s.mode = b[3];
    if (s.mode > 2) return fail(3, "mode above 2 (driver returns -4)");
    s.speed = b[4];
    s.tempo = uint16_t(b[5] | b[6] << 8);
    const size_t tracks = s.mode == 0 ? 9 : s.mode == 1 ? 11 : 0;
    if (7 + tracks * 2 > b.size()) return fail(7, "track table truncated");
    for (size_t i = 0; i < tracks; ++i) {
        uint16_t o = uint16_t(b[7 + i * 2] | b[8 + i * 2] << 8);
        if (o != 0 && (o < 7 + tracks * 2 || o >= b.size())) return fail(7 + i * 2, "track offset outside the song");
        s.track_offsets.push_back(o);
    }
    s.data.assign(b.begin(), b.end());
    return s;
}
}
