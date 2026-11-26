#include <doctest/doctest.h>
#include "data/byte_reader.h"
#include <array>
using namespace mtv;
static std::array<std::byte,6> bytes6() { return {std::byte{0x34},std::byte{0x12},std::byte{0x78},std::byte{0x56},std::byte{0x34},std::byte{0x12}}; }
TEST_SUITE("data.bytes") {
TEST_CASE("byte_reader_reads_le16_le32") {
    auto b = bytes6();
    ByteReader r(b);
    CHECK(r.u16le().value() == 0x1234);
    CHECK(r.u32le().value() == 0x12345678u);
    CHECK(r.remaining() == 0);
}
TEST_CASE("byte_reader_rejects_read_past_end") {
    auto b = bytes6();
    ByteReader r(b, "f", "res");
    REQUIRE(r.take(5).has_value());
    auto e = r.u16le();
    REQUIRE_FALSE(e.has_value());
    CHECK(e.error().byte_offset == 5);
    CHECK(e.error().file == "f");
    CHECK(r.position() == 5);
}
}
#include "data/hash.h"
#include <string_view>
TEST_SUITE("data.hash") {
TEST_CASE("sha256_known_vector") {
    std::string_view abc = "abc";
    CHECK(sha256(std::as_bytes(std::span(abc.data(), abc.size()))) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
}
