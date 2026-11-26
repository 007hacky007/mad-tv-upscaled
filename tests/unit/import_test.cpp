#include <doctest/doctest.h>
#include "data/hash.h"
#include "platform/import.h"
#include <filesystem>
#include <fstream>
using namespace mtv;
namespace fs = std::filesystem;
namespace {
fs::path fresh(const fs::path& name) {
    auto d = fs::temp_directory_path() / "mtv-import-test" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}
void put(const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; }
std::string hash_of(const std::string& s) { return sha256(std::as_bytes(std::span(s.data(), s.size()))); }
// Two small stand-in files with known hashes.
KnownFiles known() { return {{"MTV.EXE", hash_of("exe")}, {"SOURCE1.RSC", hash_of("rsc")}}; }
}
TEST_SUITE("platform.import") {
TEST_CASE("import_copies_the_known_files") {
    auto src = fresh("ok-src"), user = fresh("ok-user");
    put(src / "MTV.EXE", "exe");
    put(src / "source1.rsc", "rsc"); // DOS names in any case
    auto r = import_game_data(src, user, known());
    REQUIRE_MESSAGE(r.has_value(), (r.has_value() ? "" : r.error().reason));
    CHECK(fs::exists(user / "game" / "MTV.EXE"));
    CHECK(fs::exists(user / "game" / "SOURCE1.RSC"));
    CHECK_FALSE(r.value().pack);
}
TEST_CASE("import_rejects_modified_file_and_keeps_previous") {
    auto src = fresh("mod-src"), user = fresh("mod-user");
    put(src / "MTV.EXE", "exe");
    put(src / "SOURCE1.RSC", "rsc");
    REQUIRE(import_game_data(src, user, known()).has_value());
    put(src / "SOURCE1.RSC", "rsX");
    auto r = import_game_data(src, user, known());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().reason.find("SOURCE1.RSC") != std::string::npos);
    std::ifstream f(user / "game" / "SOURCE1.RSC");
    std::string kept; std::getline(f, kept);
    CHECK(kept == "rsc");
}
TEST_CASE("import_missing_file_lists_name") {
    auto src = fresh("missing-src"), user = fresh("missing-user");
    put(src / "MTV.EXE", "exe");
    auto r = import_game_data(src, user, known());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().reason.find("SOURCE1.RSC") != std::string::npos);
    CHECK_FALSE(fs::exists(user / "game"));
}
TEST_CASE("import_unicode_destination") {
    auto src = fresh("uni-src");
    auto user = fresh(fs::path(u8"Benutzer äöü 漢"));
    put(src / "MTV.EXE", "exe");
    put(src / "SOURCE1.RSC", "rsc");
    auto r = import_game_data(src, user, known());
    REQUIRE(r.has_value());
    CHECK(fs::exists(user / "game" / "MTV.EXE"));
}
TEST_CASE("import_takes_a_pack_found_next_to_the_files") {
    auto src = fresh("pack-src"), user = fresh("pack-user");
    put(src / "MTV.EXE", "exe");
    put(src / "SOURCE1.RSC", "rsc");
    fs::create_directories(src / "pack");
    put(src / "pack" / "pack.json", R"({"schema_version":1,"scale":6,"images":[]})");
    auto r = import_game_data(src, user, known());
    REQUIRE(r.has_value());
    CHECK(r.value().pack);
    CHECK(fs::exists(user / "pack" / "pack.json"));
}
}
