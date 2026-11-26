#include <doctest/doctest.h>
#include "data/music.h"
#include "data/animation.h"
#include "support/corpus.h"
using namespace mtv;
static std::vector<uint8_t> plx(uint8_t mode, std::vector<uint16_t> offs, size_t size) {
    std::vector<uint8_t> s{'P', 'L', 'X', mode, 0x1F, 0x8A, 0x16};
    for (auto o : offs) { s.push_back(uint8_t(o)); s.push_back(uint8_t(o >> 8)); }
    s.resize(size, 0x55);
    return s;
}
TEST_SUITE("data.music") {
TEST_CASE("song_header_fields") {
    std::vector<uint16_t> offs{25, 30, 0, 0, 0, 0, 0, 0, 40};
    auto s = decode_song(plx(0, offs, 64));
    REQUIRE(s.has_value());
    CHECK(s.value().mode == 0);
    CHECK(s.value().speed == 0x1F);
    CHECK(s.value().tempo == 0x168A);
    CHECK(s.value().track_offsets == offs);
    CHECK(s.value().used_tracks() == 3);
}
TEST_CASE("rhythm_mode_has_eleven_tracks") {
    std::vector<uint16_t> offs(11, 0); offs[10] = 29;
    auto s = decode_song(plx(1, offs, 40));
    REQUIRE(s.has_value());
    CHECK(s.value().track_offsets.size() == 11);
}
TEST_CASE("song_rejects_track_offset_past_end") {
    std::vector<uint16_t> offs{25, 0, 0, 0, 0, 0, 0, 0, 900};
    CHECK_FALSE(decode_song(plx(0, offs, 64)).has_value());
}
TEST_CASE("song_rejects_bad_mode_and_magic") {
    CHECK_FALSE(decode_song(plx(3, {}, 64)).has_value());
    auto s = plx(0, std::vector<uint16_t>(9, 0), 64); s[0] = 'Q';
    CHECK_FALSE(decode_song(s).has_value());
}
}
TEST_SUITE("data.animation") {
TEST_CASE("animation_deltas_apply_cumulatively") {
    IndexedImage back{4, 2, {1, 1, 1, 1, 1, 1, 1, 1}};
    // frame count 2; frame 1: literal 7, skip 1, run of 2 x colour 9, end; frame 2: run-skip 5, literal 3, end
    std::vector<uint8_t> data{2, 7, 0x64, 0x63, 2, 9, 0x62, 0x63, 9, 0x64, 3, 0x62}; // rows are 8 wide: skip 9 = row 1, column 1
    auto a = decode_animation(data, back);
    REQUIRE(a.has_value());
    REQUIRE(a.value().size() == 2);
    CHECK(a.value()[0].indices == std::vector<uint8_t>{7, 1, 9, 9, 1, 1, 1, 1});
    CHECK(a.value()[1].indices == std::vector<uint8_t>{7, 1, 9, 9, 1, 3, 1, 1});
}
TEST_CASE("animation_run_length_extends_with_ff") {
    IndexedImage back{300, 1, std::vector<uint8_t>(300, 0)};
    std::vector<uint8_t> data{1, 0x63, 0xFF, 0x05, 4, 0x62}; // 255 + 5 = 260 pixels of colour 4
    auto a = decode_animation(data, back);
    REQUIRE(a.has_value());
    CHECK(a.value()[0].indices[259] == 4);
    CHECK(a.value()[0].indices[260] == 0);
}
TEST_CASE("animation_rows_use_padded_stride") {
    // The player writes into a buffer with rows padded to a multiple of 8 pixels (FUN_482c_000e layout).
    IndexedImage back{3, 2, {1, 1, 1, 1, 1, 1}};
    std::vector<uint8_t> data{1, 0x63, 8, 0x64, 5, 0x62}; // skip the whole first padded row, then write
    auto a = decode_animation(data, back);
    REQUIRE(a.has_value());
    CHECK(a.value()[0].indices == std::vector<uint8_t>{1, 1, 1, 5, 1, 1});
}
TEST_CASE("animation_write_past_buffer_fails") {
    IndexedImage back{2, 1, {0, 0}};
    std::vector<uint8_t> data{1, 0x63, 20, 9, 0x62};
    CHECK_FALSE(decode_animation(data, back).has_value());
    std::vector<uint8_t> unterminated{1, 5, 5};
    CHECK_FALSE(decode_animation(unterminated, back).has_value());
}
}
TEST_SUITE("corpus.music") {
TEST_CASE("every_music_slot_decodes") {
    auto idx = ResourceIndex::open_dir(mtv::test::corpus_dir());
    REQUIRE(idx.has_value());
    int songs = 0, anims = 0;
    for (auto slot : idx.value().slots()) {
        auto e = decode_rsc_entry(idx.value(), slot);
        REQUIRE(e.has_value());
        if (e.value().kind == ResourceKind::Music) { REQUIRE_MESSAGE(decode_song(e.value().bytes).has_value(), "slot " << slot); ++songs; }
        if (e.value().kind == ResourceKind::Animation) ++anims;
    }
    CHECK(songs == 24);
    CHECK(anims == 20);
}
TEST_CASE("every_animation_plays_over_its_backdrop") {
    auto idx = ResourceIndex::open_dir(mtv::test::corpus_dir());
    REQUIRE(idx.has_value());
    int checked = 0;
    for (const auto& seq : kAnimationSequences) {
        auto back = decode_image(decode_rsc_entry(idx.value(), seq.backdrop).value());
        REQUIRE(back.has_value());
        for (auto slot : seq.parts) {
            if (!slot) continue;
            auto e = decode_rsc_entry(idx.value(), slot);
            REQUIRE(e.has_value());
            REQUIRE(e.value().kind == ResourceKind::Animation);
            auto a = decode_animation(e.value().bytes, back.value()[0]);
            REQUIRE_MESSAGE(a.has_value(), "slot " << slot << ": " << a.error().reason);
            CHECK(a.value().size() == e.value().bytes[0]);
            ++checked;
        }
    }
    CHECK(checked == 20);
}
}
