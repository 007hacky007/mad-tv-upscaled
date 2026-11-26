#include <doctest/doctest.h>
#include "data/rsc_codec.h"
#include "support/bit_writers.h"
#include "support/corpus.h"
using namespace mtv;
using namespace mtv::test;
namespace {
void escape_only_contexts(Bits& b, int skip_ctx = -1, const Bits* ctx = nullptr) {
    for (int i = 0; i < 256; ++i) {
        if (i == skip_ctx) b.v.insert(b.v.end(), ctx->v.begin(), ctx->v.end());
        else b.put_lsb(1, 1); // a single escape leaf
    }
}
}
TEST_SUITE("data.rsc_codec") {
TEST_CASE("escape_only_trees_give_raw_literals") {
    Bits b;
    b.put_lsb(1, 1);           // root tree: escape leaf only
    escape_only_contexts(b);
    for (uint8_t px : {1, 2, 3, 4, 5, 6}) b.put_lsb(px, 8); // every pixel is a raw literal
    auto enc = rsc_image({{3, 2}}, 1, 6, b);
    auto img = decode_rsc_image(enc);
    REQUIRE(img.has_value());
    REQUIRE(img.value().frames.size() == 1);
    const auto& f = img.value().frames[0];
    CHECK(f.width == 3);
    CHECK(f.height == 2);
    CHECK(f.stride == 8);
    CHECK(f.pixels == std::vector<uint8_t>{1, 2, 3, 0, 0, 0, 0, 0, 4, 5, 6, 0, 0, 0, 0, 0});
}
TEST_CASE("context_tree_follows_previous_byte") {
    Bits b;
    // root: internal(0x101) -> left escape, right leaf 5
    b.put_lsb(0, 1); b.put_lsb(1, 1); b.put_lsb(1, 1); b.put_lsb(5, 8);
    Bits ctx5; // context 5: internal(0x102) -> left escape, right leaf 7
    ctx5.put_lsb(0, 1); ctx5.put_lsb(1, 1); ctx5.put_lsb(1, 1); ctx5.put_lsb(7, 8);
    escape_only_contexts(b, 5, &ctx5);
    // pixels: 5 (root bit 1), 7 (ctx5 bit 1), then ctx7 escapes -> root bit 1 -> 5, then ctx5 bit 0 = escape -> root bit 0 = escape -> raw 9
    b.put_lsb(1, 1); b.put_lsb(1, 1); b.put_lsb(1, 1); b.put_lsb(0, 1); b.put_lsb(0, 1); b.put_lsb(9, 8);
    auto img = decode_rsc_image(rsc_image({{4, 1}}, 2, 4, b));
    REQUIRE(img.has_value());
    CHECK(std::vector<uint8_t>(img.value().frames[0].pixels.begin(), img.value().frames[0].pixels.begin() + 4) == std::vector<uint8_t>{5, 7, 5, 9});
}
TEST_CASE("multiple_frames_split_in_order") {
    Bits b;
    b.put_lsb(1, 1); escape_only_contexts(b);
    for (uint8_t px : {1, 2, 3}) b.put_lsb(px, 8);
    auto img = decode_rsc_image(rsc_image({{1, 1}, {2, 1}}, 1, 3, b));
    REQUIRE(img.has_value());
    REQUIRE(img.value().frames.size() == 2);
    CHECK(img.value().frames[0].pixels[0] == 1);
    CHECK(img.value().frames[1].pixels[0] == 2);
    CHECK(img.value().frames[1].pixels[1] == 3);
}
TEST_CASE("pixel_count_must_match_frames") {
    Bits b; b.put_lsb(1, 1); escape_only_contexts(b); b.put_lsb(1, 8);
    CHECK_FALSE(decode_rsc_image(rsc_image({{3, 2}}, 1, 1, b)).has_value());
}
TEST_CASE("truncated_bits_fail") {
    Bits b; b.put_lsb(1, 1); escape_only_contexts(b); b.put_lsb(1, 8);
    CHECK_FALSE(decode_rsc_image(rsc_image({{2, 1}}, 1, 2, b)).has_value());
}
TEST_CASE("node_count_too_small_fails") {
    Bits b;
    b.put_lsb(0, 1); b.put_lsb(1, 1); b.put_lsb(1, 1); b.put_lsb(5, 8);
    escape_only_contexts(b);
    b.put_lsb(1, 1);
    CHECK_FALSE(decode_rsc_image(rsc_image({{1, 1}}, 0, 1, b)).has_value());
}
}
TEST_SUITE("data.rsc_lzw") {
static std::string str(const std::vector<uint8_t>& v) { return std::string(v.begin(), v.end()); }
TEST_CASE("lzw_decodes_codes") {
    auto out = decode_rsc_lzw(rsc_lzw({{'A', 9}, {'B', 9}, {0x102, 9}, {0, 9}}, 4));
    REQUIRE(out.has_value());
    CHECK(str(out.value()) == "ABAB");
}
TEST_CASE("lzw_kwkwk_case") {
    auto out = decode_rsc_lzw(rsc_lzw({{'A', 9}, {0x102, 9}, {0, 9}}, 3));
    REQUIRE(out.has_value());
    CHECK(str(out.value()) == "AAA");
}
TEST_CASE("lzw_reset_code_restarts_dictionary") {
    auto out = decode_rsc_lzw(rsc_lzw({{'A', 9}, {'B', 9}, {0x100, 9}, {'C', 9}, {'D', 9}, {0x102, 9}, {0, 9}}, 6));
    REQUIRE(out.has_value());
    CHECK(str(out.value()) == "ABCDCD"); // after the reset, 0x102 is "CD", not "AB"
}
TEST_CASE("lzw_width_increase_code") {
    auto out = decode_rsc_lzw(rsc_lzw({{'A', 9}, {0x101, 9}, {'B', 10}, {0, 10}}, 2));
    REQUIRE(out.has_value());
    CHECK(str(out.value()) == "AB");
}
TEST_CASE("lzw_size_mismatch_fails") {
    CHECK_FALSE(decode_rsc_lzw(rsc_lzw({{'A', 9}, {'B', 9}, {0, 9}}, 5)).has_value());
}
TEST_CASE("lzw_packed_size_past_entry_fails") {
    auto e = rsc_lzw({{'A', 9}, {'B', 9}, {0, 9}}, 2);
    e[2] = 200;
    CHECK_FALSE(decode_rsc_lzw(e).has_value());
}
TEST_CASE("lzw_undefined_code_fails") {
    CHECK_FALSE(decode_rsc_lzw(rsc_lzw({{'A', 9}, {0x150, 9}, {0, 9}}, 2)).has_value());
}
}
TEST_SUITE("corpus.rsc_codec") {
TEST_CASE("every_used_slot_classifies") {
    auto idx = ResourceIndex::open_dir(corpus_dir());
    REQUIRE(idx.has_value());
    int images = 0, lzw = 0;
    for (auto slot : idx.value().slots()) {
        auto e = decode_rsc_entry(idx.value(), slot);
        REQUIRE_MESSAGE(e.has_value(), "slot " << slot);
        if (e.value().kind == ResourceKind::Image) ++images;
        if (e.value().kind == ResourceKind::Music || e.value().kind == ResourceKind::Animation) ++lzw;
    }
    CHECK(images == 164);
    CHECK(lzw == 44);
}
}
