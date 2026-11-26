#include <doctest/doctest.h>
#include "data/powerpacker.h"
#include "support/bit_writers.h"
#include "support/corpus.h"
#include <string>
using namespace mtv;
using namespace mtv::test;
TEST_SUITE("data.pp20") {
TEST_CASE("decodes_literal_run") {
    // Output is produced backwards: "ABC" is emitted as C, B, A.
    Bits b;
    b.put(0, 1);            // literal block follows
    b.put(2, 2);            // todo = 1 + 2 = 3 literals
    b.put('C', 8); b.put('B', 8); b.put('A', 8);
    auto out = unpack_pp20(pp20_file(b, 3));
    REQUIRE(out.has_value());
    CHECK(std::string(out.value().begin(), out.value().end()) == "ABC");
}
TEST_CASE("decodes_known_vector") {
    // "ABABAB": literals B,A (written at the end: "AB"), then a match of 4 copying from offset 1.
    const uint8_t lens[4] = {1, 2, 3, 4};
    Bits b;
    b.put(0, 1); b.put(1, 2); b.put('B', 8); b.put('A', 8); // 2 literals -> "..AB"
    b.put(2, 2);            // match: x=2 -> length 4, offset bits lens[2]=3
    b.put(1, 3);            // offset 1: copy out[1] repeatedly
    auto out = unpack_pp20(pp20_file(b, 6, lens));
    REQUIRE(out.has_value());
    CHECK(std::string(out.value().begin(), out.value().end()) == "ABABAB");
}
TEST_CASE("bad_magic_fails") {
    Bits b; b.put(0, 1); b.put(0, 2); b.put('x', 8);
    auto f = pp20_file(b, 1);
    f[0] = 'X';
    CHECK_FALSE(unpack_pp20(f).has_value());
}
TEST_CASE("truncated_fails") {
    Bits b; b.put(0, 1); b.put(0, 2); b.put('x', 8);
    auto f = pp20_file(b, 50); // claims more output than the bits can produce
    CHECK_FALSE(unpack_pp20(f).has_value());
    std::vector<uint8_t> tiny{'P', 'P', '2', '0'};
    CHECK_FALSE(unpack_pp20(tiny).has_value());
}
}
TEST_SUITE("corpus.pp20") {
TEST_CASE("datav_pck_unpacks") {
    auto in = read_corpus_file("DATAV.PCK");
    auto out = unpack_pp20(in);
    REQUIRE(out.has_value());
    uint32_t declared = uint32_t(in[in.size() - 4]) << 16 | uint32_t(in[in.size() - 3]) << 8 | in[in.size() - 2];
    CHECK(out.value().size() == declared);
}
}
