#include <doctest/doctest.h>
#include "coverage.h"
#include "support/corpus.h"
using namespace mtv;
TEST_SUITE("tools.coverage") {
TEST_CASE("coverage_fails_on_unknown_kind") {
    std::vector<CoverageEntry> entries{{1, "A.RSC", ResourceKind::Palette, true, ""}, {7, "A.RSC", ResourceKind::Unknown, false, "no decoder"}};
    auto r = summarize_coverage(entries);
    CHECK_FALSE(r.complete);
    CHECK(r.unknown == 1);
    CHECK(r.by_kind.at("Palette") == 1);
}
TEST_CASE("coverage_fails_on_decode_error") {
    std::vector<CoverageEntry> entries{{2, "A.RSC", ResourceKind::Image, false, "truncated"}};
    CHECK_FALSE(summarize_coverage(entries).complete);
}
TEST_CASE("coverage_complete_when_all_decode") {
    std::vector<CoverageEntry> entries{{2, "A.RSC", ResourceKind::Image, true, ""}, {3, "A.RSC", ResourceKind::Font, true, ""}};
    CHECK(summarize_coverage(entries).complete);
}
TEST_CASE("slot_kind_overrides") {
    CHECK(slot_kind(1, ResourceKind::Unknown) == ResourceKind::Palette);
    CHECK(slot_kind(98, ResourceKind::Unknown) == ResourceKind::Driver);
    CHECK(slot_kind(3, ResourceKind::Image) == ResourceKind::Font);
    CHECK(slot_kind(246, ResourceKind::Unknown) == ResourceKind::Text);
    CHECK(slot_kind(209, ResourceKind::Unknown) == ResourceKind::Table);
    CHECK(slot_kind(500, ResourceKind::Unknown) == ResourceKind::Unknown);
    CHECK(slot_kind(70, ResourceKind::Image) == ResourceKind::Image);
}
}
TEST_SUITE("corpus.coverage") {
TEST_CASE("coverage_has_no_unknown") {
    auto idx = ResourceIndex::open_dir(mtv::test::corpus_dir());
    REQUIRE(idx.has_value());
    auto entries = build_coverage(idx.value());
    auto r = summarize_coverage(entries);
    for (auto& e : entries) CHECK_MESSAGE(e.decoded, "slot " << e.slot << ": " << e.note);
    CHECK(r.unknown == 0);
    CHECK(r.complete);
    CHECK(entries.size() == 242);
}
}
