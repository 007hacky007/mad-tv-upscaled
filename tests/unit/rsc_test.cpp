#include <doctest/doctest.h>
#include "data/rsc.h"
#include "support/corpus.h"
#include <numeric>
using namespace mtv;
namespace {
// Builds an RSC file: 600-slot table then payloads. entries: (slot, offset, size); offset 0 = auto-place.
std::vector<uint8_t> make_rsc(std::vector<std::tuple<uint16_t, uint32_t, uint16_t>> entries, size_t total = 0) {
    std::vector<uint8_t> f(kRscTableBytes, 0);
    uint32_t next = kRscTableBytes;
    for (auto& [slot, off, size] : entries) {
        if (off == 0) { off = next; next += size; }
        auto* e = f.data() + slot * 6;
        e[0] = uint8_t(off); e[1] = uint8_t(off >> 8); e[2] = uint8_t(off >> 16); e[3] = uint8_t(off >> 24);
        e[4] = uint8_t(size); e[5] = uint8_t(size >> 8);
    }
    f.resize(std::max<size_t>(total ? total : next, kRscTableBytes), 0xAB);
    return f;
}
}
TEST_SUITE("data.rsc") {
TEST_CASE("parses_600_slot_table") {
    auto f = make_rsc({{1, 0, 4}, {599, 0, 2}});
    f[kRscTableBytes] = 'A';
    auto a = RscArchive::open(f, "T.RSC");
    REQUIRE(a.has_value());
    REQUIRE(a.value().entries().size() == 2);
    CHECK(a.value().entries()[0].slot == 1);
    CHECK(a.value().entries()[0].offset == kRscTableBytes);
    CHECK(a.value().entries()[1].slot == 599);
    auto g = a.value().get(1);
    REQUIRE(g.has_value());
    CHECK(g.value().size() == 4);
    CHECK(g.value()[0] == 'A');
}
TEST_CASE("empty_slot_has_zero_offset") {
    auto f = make_rsc({{5, 0, 3}});
    // A slot with a size but zero offset is empty, as in the original loader (0x48640).
    f[7 * 6 + 4] = 9;
    auto a = RscArchive::open(f, "T.RSC");
    REQUIRE(a.has_value());
    CHECK(a.value().entries().size() == 1);
    CHECK_FALSE(a.value().get(7).has_value());
}
TEST_CASE("rsc_entry_past_eof_fails") {
    auto f = make_rsc({{3, kRscTableBytes + 10, 20}}, kRscTableBytes + 25);
    auto a = RscArchive::open(f, "T.RSC");
    REQUIRE_FALSE(a.has_value());
    CHECK(a.error().file == "T.RSC");
    CHECK(a.error().resource == "slot 3");
}
TEST_CASE("entry_inside_table_fails") {
    auto f = make_rsc({{3, 100, 20}}, kRscTableBytes + 25);
    CHECK_FALSE(RscArchive::open(f, "T.RSC").has_value());
}
TEST_CASE("truncated_table_fails") {
    std::vector<uint8_t> f(kRscTableBytes - 1, 0);
    CHECK_FALSE(RscArchive::open(f, "T.RSC").has_value());
}
TEST_CASE("overlapping_entries_reported") {
    auto f = make_rsc({{1, kRscTableBytes, 10}, {2, kRscTableBytes + 5, 10}}, kRscTableBytes + 20);
    auto a = RscArchive::open(f, "T.RSC");
    REQUIRE_FALSE(a.has_value());
    CHECK(a.error().reason.find("overlap") != std::string::npos);
}
TEST_CASE("get_unknown_slot_fails") {
    auto a = RscArchive::open(make_rsc({{1, 0, 1}}), "T.RSC");
    REQUIRE(a.has_value());
    CHECK_FALSE(a.value().get(2).has_value());
    CHECK_FALSE(a.value().get(600).has_value());
}
TEST_CASE("index_merges_archives_by_global_slot") {
    ResourceIndex idx;
    REQUIRE(idx.add(RscArchive::open(make_rsc({{1, 0, 1}}), "A.RSC").value()).has_value());
    REQUIRE(idx.add(RscArchive::open(make_rsc({{2, 0, 2}}), "B.RSC").value()).has_value());
    CHECK(idx.get(1).value().size() == 1);
    CHECK(idx.get(2).value().size() == 2);
    CHECK(idx.archive_of(2) == "B.RSC");
    CHECK_FALSE(idx.get(3).has_value());
}
TEST_CASE("index_rejects_duplicate_slot") {
    ResourceIndex idx;
    REQUIRE(idx.add(RscArchive::open(make_rsc({{1, 0, 1}}), "A.RSC").value()).has_value());
    CHECK_FALSE(idx.add(RscArchive::open(make_rsc({{1, 0, 1}}), "B.RSC").value()).has_value());
}
}
TEST_SUITE("corpus.rsc") {
TEST_CASE("source1_has_186_used_slots") {
    auto a = RscArchive::open(mtv::test::read_corpus_file("SOURCE1.RSC"), "SOURCE1.RSC");
    REQUIRE(a.has_value());
    CHECK(a.value().entries().size() == 186);
    uint32_t sum = 0, end = 0;
    for (auto& e : a.value().entries()) { sum += e.size; end = std::max(end, e.offset + e.size); }
    CHECK(sum == 665578);
    CHECK(end == 706783);
}
TEST_CASE("source2_has_56_used_slots") {
    auto a = RscArchive::open(mtv::test::read_corpus_file("SOURCE2.RSC"), "SOURCE2.RSC");
    REQUIRE(a.has_value());
    CHECK(a.value().entries().size() == 56);
    uint32_t sum = 0, end = 0;
    for (auto& e : a.value().entries()) { sum += e.size; end = std::max(end, e.offset + e.size); }
    CHECK(sum == 315741);
    CHECK(end == 319341);
}
TEST_CASE("corpus_index_open_dir") {
    auto idx = ResourceIndex::open_dir(mtv::test::corpus_dir());
    REQUIRE(idx.has_value());
    CHECK(idx.value().slots().size() == 242);
}
}
