#include <doctest/doctest.h>
#include "data/lzexe.h"
#include "data/mz.h"
#include "data/hash.h"
#include "support/corpus.h"
#include "support/lzexe_writer.h"
#include <string>
using namespace mtv;
using namespace mtv::test;
static std::string image_text(const MzImage& m) { return std::string(m.image.begin(), m.image.end()); }
static const std::vector<uint8_t> one_reloc_then_end{5, 0, 1, 0};
TEST_SUITE("data.lzexe") {
TEST_CASE("literal_only_stream") {
    LzBitWriter w; w.literal('A'); w.literal('B'); w.literal('C'); w.end();
    LzFileSpec spec{w.bytes(), one_reloc_then_end};
    auto out = unlzexe91(build_lzexe(spec));
    REQUIRE(out.has_value());
    auto mz = parse_mz(out.value());
    REQUIRE(mz.has_value());
    CHECK(image_text(mz.value()) == "ABC");
    REQUIRE(mz.value().relocations.size() == 1);
    CHECK(mz.value().relocations[0] == std::pair<uint16_t,uint16_t>{5, 0});
    CHECK(mz.value().header.ip == 0x10);
    CHECK(mz.value().header.cs == 0x2);
    CHECK(mz.value().header.sp == 0x80);
    CHECK(mz.value().header.ss == 0x3);
}
TEST_CASE("short_and_long_backrefs") {
    LzBitWriter w; w.literal('A'); w.literal('B'); w.short_ref(4, 2); w.literal('C'); w.long_ref(5, 7); w.long_ref(12, 3); w.end();
    auto out = unlzexe91(build_lzexe({w.bytes(), {0, 1, 0}}));
    REQUIRE(out.has_value());
    auto mz = parse_mz(out.value());
    REQUIRE(mz.has_value());
    // ABABAB C | back 7 len 5 -> ABABA | back 3 len 12 -> ABA repeated
    CHECK(image_text(mz.value()) == "ABABABCABABAABAABAABAABA");
}
TEST_CASE("segment_marker_continues") {
    LzBitWriter w; w.literal('X'); w.segment_marker(); w.literal('Y'); w.end();
    auto out = unlzexe91(build_lzexe({w.bytes(), {0, 1, 0}}));
    REQUIRE(out.has_value());
    CHECK(image_text(parse_mz(out.value()).value()) == "XY");
}
TEST_CASE("relocation_segment_carry") {
    LzBitWriter w; w.literal('Z'); w.end();
    // span 0x20 -> off 0x20 normalises to seg 2 off 0; then 00 00 00 adds 0x0FFF to seg; span 3 -> off 3
    auto out = unlzexe91(build_lzexe({w.bytes(), {0x20, 0, 0, 0, 3, 0, 1, 0}}));
    REQUIRE(out.has_value());
    auto mz = parse_mz(out.value());
    REQUIRE(mz.has_value());
    const auto& r = mz.value().relocations;
    REQUIRE(r.size() == 2);
    CHECK(r[0] == std::pair<uint16_t,uint16_t>{0, 2});
    CHECK(r[1] == std::pair<uint16_t,uint16_t>{3, 0x1001});
}
TEST_CASE("truncated_stream_fails") {
    LzBitWriter w; w.literal('A'); w.literal('B'); w.end();
    auto file = build_lzexe({w.bytes(), {0, 1, 0}});
    file.resize(34);
    CHECK_FALSE(unlzexe91(file).has_value());
}
TEST_CASE("backref_before_start_fails") {
    LzBitWriter w; w.literal('A'); w.short_ref(3, 5); w.end();
    CHECK_FALSE(unlzexe91(build_lzexe({w.bytes(), {0, 1, 0}})).has_value());
}
TEST_CASE("rejects_non_lz91") {
    LzBitWriter w; w.literal('A'); w.end();
    CHECK_FALSE(unlzexe91(build_lzexe({w.bytes(), {0, 1, 0}}, "LZ09")).has_value());
    std::vector<uint8_t> junk(64, 0);
    CHECK_FALSE(unlzexe91(junk).has_value());
}
}
TEST_SUITE("corpus.lzexe") {
TEST_CASE("unpacks_mtv_exe") {
    auto in = read_corpus_file("MTV.EXE");
    auto out = unlzexe91(in);
    REQUIRE(out.has_value());
    auto mz = parse_mz(out.value());
    REQUIRE(mz.has_value());
    CHECK(mz.value().image.size() == 280001);
    CHECK(mz.value().relocations.size() == 4503);
    CHECK(mz.value().header.cs == 0);
    CHECK(mz.value().header.ip == 0);
    CHECK(sha256(std::as_bytes(std::span(out.value()))) == "656e44191a6fa1bec77ee7ad26d94d05ff07c7c54980914564aa2d8803f841ca");
}
TEST_CASE("unpacks_intro_exe") {
    auto out = unlzexe91(read_corpus_file("INTRO.EXE"));
    REQUIRE(out.has_value());
    auto mz = parse_mz(out.value());
    REQUIRE(mz.has_value());
    CHECK(mz.value().image.size() == 11532);
    CHECK(mz.value().relocations.size() == 27);
}
}
