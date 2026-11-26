#include "data/hash.h"
#include "platform/logic_crypto.h"
#include <doctest/doctest.h>
#include <string>
using namespace mtv;
namespace {
std::vector<std::byte> bytes(const std::string& s) {
    std::vector<std::byte> v(s.size());
    for (size_t i = 0; i < s.size(); ++i) v[i] = std::byte(uint8_t(s[i]));
    return v;
}
const std::array<uint8_t, 24> kNonce{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
std::string hex(const std::array<uint8_t, 32>& k) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (auto b : k) { s += d[b >> 4]; s += d[b & 15]; }
    return s;
}
}
TEST_SUITE("platform.logic_crypto") {
TEST_CASE("round_trip") {
    const auto key = derive_logic_key(bytes("exe bytes"), bytes("rsc bytes"));
    const auto plain = bytes(std::string(5000, 'x') + "library");
    const auto box = encrypt_logic(plain, key, "build-1", kNonce);
    CHECK(box.size() == plain.size() + 8 + 4 + 1 + 7 + 24 + 8 + 16);
    auto back = decrypt_logic(box, key, "build-1");
    REQUIRE(back.has_value());
    CHECK(back.value() == plain);
}
TEST_CASE("wrong_key_is_refused") {
    const auto box = encrypt_logic(bytes("library"), derive_logic_key(bytes("exe"), bytes("rsc")), "b", kNonce);
    auto r = decrypt_logic(box, derive_logic_key(bytes("exf"), bytes("rsc")), "b");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().reason.find("do not unlock") != std::string::npos);
}
TEST_CASE("flipped_byte_is_refused") {
    const auto key = derive_logic_key(bytes("exe"), bytes("rsc"));
    const auto box = encrypt_logic(bytes("some library code"), key, "b", kNonce);
    for (size_t i = 0; i < box.size(); ++i) {
        auto bad = box;
        bad[i] ^= std::byte{0x01};
        CHECK_FALSE_MESSAGE(decrypt_logic(bad, key, "b").has_value(), "byte " << i);
    }
    CHECK_FALSE(decrypt_logic(std::span(box).first(box.size() - 1), key, "b").has_value());
    CHECK_FALSE(decrypt_logic(std::span(box).first(10), key, "b").has_value());
}
TEST_CASE("other_build_id_is_refused") {
    const auto key = derive_logic_key(bytes("exe"), bytes("rsc"));
    const auto box = encrypt_logic(bytes("library"), key, "build-1", kNonce);
    auto r = decrypt_logic(box, key, "build-2");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().reason.find("another build") != std::string::npos);
}
TEST_CASE("key_depends_on_both_files") {
    const auto k = derive_logic_key(bytes("exe"), bytes("rsc"));
    CHECK(k != derive_logic_key(bytes("exe"), bytes("rsd")));
    CHECK(k != derive_logic_key(bytes("exf"), bytes("rsc")));
    // the lengths are part of the input: moving a byte from one file to the other changes the key
    CHECK(derive_logic_key(bytes("ab"), bytes("c")) != derive_logic_key(bytes("a"), bytes("bc")));
    CHECK(k == derive_logic_key(bytes("exe"), bytes("rsc")));
}
TEST_CASE("key_is_not_a_file_hash") {
    const auto e = bytes("exe"), r = bytes("rsc");
    const auto k = hex(derive_logic_key(e, r));
    CHECK(k != sha256(e));
    CHECK(k != sha256(r));
}
}
