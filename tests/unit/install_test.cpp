#include "data/hash.h"
#include "platform/install.h"
#include "platform/logic_crypto.h"
#include "platform/logic_loader.h"
#include <doctest/doctest.h>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <thread>
using namespace mtv;
namespace fs = std::filesystem;
namespace {
fs::path fresh(const fs::path& name) {
    auto d = fs::temp_directory_path() / "mtv-install-test" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}
void put(const fs::path& p, const std::string& s) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << s; }
std::string get(const fs::path& p) { std::ifstream f(p, std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), {}); }
std::vector<std::byte> bytes(const std::string& s) { std::vector<std::byte> v(s.size()); std::memcpy(v.data(), s.data(), s.size()); return v; }
std::string hash_of(const std::string& s) { return sha256(bytes(s)); }
KnownFiles known() { return {{"MTV.EXE", hash_of("exe")}, {"SOURCE1.RSC", hash_of("rsc")}}; }
// A user folder after import, and a logic.bin holding `plain` for build `id`.
fs::path setup(const fs::path& name, const std::string& plain, const std::string& id) {
    auto root = fresh(name);
    put(root / "user" / "game" / "MTV.EXE", "exe");
    put(root / "user" / "game" / "SOURCE1.RSC", "rsc");
    const std::array<uint8_t, 24> nonce{};
    const auto box = encrypt_logic(bytes(plain), derive_logic_key(bytes("exe"), bytes("rsc")), id, nonce);
    std::ofstream(root / "logic.bin", std::ios::binary).write(reinterpret_cast<const char*>(box.data()), std::streamsize(box.size()));
    return root;
}
}
TEST_SUITE("platform.install") {
TEST_CASE("unlocks_into_build_id_folder") {
    auto root = setup("unlock", "library bytes", "b1");
    auto r = ensure_logic(root / "user", root / "logic.bin", "b1", known());
    REQUIRE_MESSAGE(r.has_value(), (r.has_value() ? "" : r.error().reason));
    CHECK(r.value() == root / "user" / "logic" / "b1" / logic_library_name());
    CHECK(get(r.value()) == "library bytes");
}
TEST_CASE("second_call_does_not_rewrite") {
    auto root = setup("again", "library bytes", "b1");
    auto first = ensure_logic(root / "user", root / "logic.bin", "b1", known());
    REQUIRE(first.has_value());
    const auto t = fs::last_write_time(first.value());
    fs::remove(root / "logic.bin"); // not needed once unlocked
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto second = ensure_logic(root / "user", root / "logic.bin", "b1", known());
    REQUIRE(second.has_value());
    CHECK(fs::last_write_time(second.value()) == t);
}
TEST_CASE("stale_build_id_reinstalls") {
    auto root = setup("stale", "new library", "b2");
    put(root / "user" / "logic" / "b1" / logic_library_name(), "old library");
    auto r = ensure_logic(root / "user", root / "logic.bin", "b2", known());
    REQUIRE(r.has_value());
    CHECK(get(r.value()) == "new library");
    CHECK_FALSE(fs::exists(root / "user" / "logic" / "b1"));
}
TEST_CASE("changed_game_file_is_refused") {
    auto root = setup("changed", "library", "b1");
    put(root / "user" / "game" / "SOURCE1.RSC", "rsX");
    auto r = ensure_logic(root / "user", root / "logic.bin", "b1", known());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().reason.find("SOURCE1.RSC") != std::string::npos);
    CHECK_FALSE(fs::exists(root / "user" / "logic" / "b1"));
}
TEST_CASE("interrupted_write_leaves_no_library") {
    auto root = setup("interrupted", "library", "b1");
    const auto dir = root / "user" / "logic" / "b1";
    put(dir / (std::string(logic_library_name()) + ".tmp"), "half");
    auto r = ensure_logic(root / "user", root / "logic.bin", "b1", known());
    REQUIRE(r.has_value());
    CHECK(get(r.value()) == "library");
    CHECK_FALSE(fs::exists(dir / (std::string(logic_library_name()) + ".tmp")));
}
TEST_CASE("missing_logic_file_is_reported") {
    auto root = setup("nobin", "library", "b1");
    fs::remove(root / "logic.bin");
    auto r = ensure_logic(root / "user", root / "logic.bin", "b1", known());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().reason.find("logic.bin") != std::string::npos);
}
}
