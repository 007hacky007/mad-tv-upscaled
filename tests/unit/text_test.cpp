#include <doctest/doctest.h>
#include "data/font.h"
#include "data/text.h"
#include "data/exe_tables.h"
#include "data/lzexe.h"
#include "data/rsc_codec.h"
#include "support/corpus.h"
using namespace mtv;
static std::vector<uint8_t> bytes(std::initializer_list<int> v) { std::vector<uint8_t> o; for (int x : v) o.push_back(uint8_t(x)); return o; }
TEST_SUITE("data.text") {
TEST_CASE("length_prefixed_strings") {
    // OffsetsRecords: each record starts with a length byte (slot 45/250 layout).
    auto e = bytes({4, 0, 10, 0, 3, 'A', 'b', 'c', 0, 0, 2, 'X', 'y'});
    auto t = decode_text(e, TextLayout::OffsetsRecords);
    REQUIRE(t.has_value());
    CHECK(t.value().utf8 == std::vector<std::string>{"Abc", "Xy"});
}
TEST_CASE("empty_last_record_is_empty_string") {
    auto e = bytes({4, 0, 7, 0, 2, 'O', 'k'});
    CHECK(decode_text(e, TextLayout::OffsetsRecords).value().utf8 == std::vector<std::string>{"Ok", ""});
}
TEST_CASE("offsets_nul_strings") {
    auto e = bytes({4, 0, 7, 0, 'H', 'i', 0, 'Y', 'o', 0});
    auto t = decode_text(e, TextLayout::OffsetsNul);
    REQUIRE(t.has_value());
    CHECK(t.value().utf8 == std::vector<std::string>{"Hi", "Yo"});
}
TEST_CASE("offsets_raw_strings") {
    auto e = bytes({4, 0, 6, 0, 'a', 'b', 'c'});
    CHECK(decode_text(e, TextLayout::OffsetsRaw).value().utf8 == std::vector<std::string>{"ab", "c"});
}
TEST_CASE("sequence_strings") {
    // FUN_1054_0a0b reads a fixed number of NUL-terminated strings from offset 0 (430 for slot 199/253).
    auto e = bytes({'x', 0, 'O', 'n', 0, 'O', 'f', 'f', 0});
    CHECK(decode_text(e, TextLayout::Sequence, 3).value().utf8 == std::vector<std::string>{"x", "On", "Off"});
    CHECK_FALSE(decode_text(e, TextLayout::Sequence, 4).has_value());
}
TEST_CASE("length_blocks_strings") {
    // two blocks of 2 entries: [table][strings][table][strings]; entry 0 is unused when zero
    auto e = bytes({0, 0, 2, 0, 'A', 'b', 0, 0, 1, 0, 'Z'});
    auto t = decode_text(e, TextLayout::LengthBlocks, 2);
    REQUIRE(t.has_value());
    CHECK(t.value().utf8 == std::vector<std::string>{"", "Ab", "", "Z"});
}
TEST_CASE("offsets_out_of_range_fail") {
    auto e = bytes({4, 0, 99, 0, 'a'});
    CHECK_FALSE(decode_text(e, TextLayout::OffsetsNul).has_value());
    auto l = bytes({5, 0, 'a'});
    CHECK_FALSE(decode_text(l, TextLayout::LengthBlocks, 1).has_value());
}
TEST_CASE("umlauts_map_to_utf8") {
    CHECK(cp437_to_utf8("k\x94nnen") == "k\xC3\xB6nnen");                 // ö
    CHECK(cp437_to_utf8("\x81\x84\x8e\x99\x9a\xe1") == "\xC3\xBC\xC3\xA4\xC3\x84\xC3\x96\xC3\x9C\xC3\x9F"); // ü ä Ä Ö Ü ß
    CHECK(cp437_to_utf8("\x82\xa4") == "\xC3\xA9\xC3\xB1");              // é ñ
    CHECK(utf8_to_cp437(cp437_to_utf8("\x01\xc5~#s")) == "\x01\xc5~#s");   // game icons round-trip
}
TEST_CASE("language_slot_pairs") {
    const uint16_t de[] = {199, 42, 207, 47, 45, 46, 49, 44};
    const uint16_t en[] = {253, 246, 252, 251, 250, 249, 248, 247};
    for (int i = 0; i < 8; ++i) {
        CHECK(language_slot(de[i], Language::English) == en[i]);
        CHECK(language_slot(de[i], Language::German) == de[i]);
        CHECK(language_slot(en[i], Language::German) == de[i]);
    }
    CHECK(language_slot(3, Language::English) == 3);
}
TEST_CASE("font_glyph_widths") {
    std::vector<IndexedImage> frames{{2, 3, {1, 1, 1, 1, 1, 1}}, {1, 3, {2, 2, 2}}};
    std::array<uint8_t, 256> map{}, widths{};
    map['A'] = 1; widths['A'] = 2;
    map['i'] = 2; widths['i'] = 1;
    auto f = build_font(frames, map, widths);
    REQUIRE(f.has_value());
    CHECK(f.value().height == 3);
    REQUIRE(f.value().glyphs['A'].has_value());
    CHECK(f.value().glyphs['A']->advance == 3);  // width + 1 (FUN_205e_0266)
    CHECK(f.value().glyphs['i']->image.indices[0] == 2);
    CHECK_FALSE(f.value().glyphs['Z'].has_value());
    CHECK(f.value().space_advance == 3);
    map['B'] = 9; // frame out of range
    CHECK_FALSE(build_font(frames, map, widths).has_value());
}
}
TEST_SUITE("corpus.text") {
TEST_CASE("german_and_english_tables_have_equal_counts") {
    auto idx = ResourceIndex::open_dir(mtv::test::corpus_dir());
    REQUIRE(idx.has_value());
    const uint16_t de[] = {199, 42, 207, 47, 45, 46, 49, 44};
    for (auto d : de) {
        auto spec = text_layout_for_slot(d);
        REQUIRE(spec.has_value());
        auto g = decode_text(idx.value().get(d).value(), spec->layout, spec->block_entries);
        auto e = decode_text(idx.value().get(language_slot(d, Language::English)).value(), spec->layout, spec->block_entries);
        REQUIRE_MESSAGE(g.has_value(), "slot " << d);
        REQUIRE_MESSAGE(e.has_value(), "slot " << language_slot(d, Language::English));
        CHECK_MESSAGE(g.value().utf8.size() == e.value().utf8.size(), "slot " << d);
    }
    auto t = decode_text(idx.value().get(253).value(), TextLayout::Sequence, 430);
    REQUIRE(t.has_value());
    CHECK(t.value().utf8[1] == "Information Board");
    CHECK(std::find(t.value().utf8.begin(), t.value().utf8.end(), "Welcome to MadTV!~I wish you the best of luck in making our network a success. So, what are you waiting for?") != t.value().utf8.end());
}
TEST_CASE("exe_tables_and_font_from_user_files") {
    auto exe = unlzexe91(mtv::test::read_corpus_file("MTV.EXE"));
    REQUIRE(exe.has_value());
    auto tables = read_exe_tables(exe.value());
    REQUIRE(tables.has_value());
    CHECK(tables.value().glyph_map['A'] == 1);
    CHECK(tables.value().glyph_map[0x94] == 57);
    CHECK(tables.value().glyph_widths['0'] == 4);
    auto idx = ResourceIndex::open_dir(mtv::test::corpus_dir());
    auto frames = decode_image(decode_rsc_entry(idx.value(), 3).value());
    REQUIRE(frames.has_value());
    auto font = build_font(frames.value(), tables.value().glyph_map, tables.value().glyph_widths);
    REQUIRE(font.has_value());
    CHECK(font.value().height == 8);
    CHECK(font.value().glyphs[0xE1].has_value()); // ß
}
}
