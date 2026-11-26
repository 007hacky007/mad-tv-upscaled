#include <doctest/doctest.h>
#include "rt/dos.h"
#include <filesystem>
#include <fstream>
using namespace mtv;
namespace fs = std::filesystem;
TEST_SUITE("rt.files") {
TEST_CASE("replace_file_keeps_the_old_content_when_writing_fails") {
    const auto d = fs::temp_directory_path() / "mtv-rt-file";
    fs::remove_all(d);
    fs::create_directories(d);
    const auto f = d / "SOURCE1.RSC";
    std::ofstream(f, std::ios::binary) << "old";
    const std::vector<uint8_t> next{'n', 'e', 'w', '!'};
    CHECK(rt::replace_file(f, next));
    std::ifstream a(f, std::ios::binary); std::string got; a >> got;
    CHECK(got == "new!");
    // a directory where the temporary file must go makes the write fail; the old content stays
    fs::create_directories(d / "SOURCE1.RSC.tmp");
    CHECK_FALSE(rt::replace_file(f, std::vector<uint8_t>{'x'}));
    std::ifstream b(f, std::ios::binary); std::string kept; b >> kept;
    CHECK(kept == "new!");
}
}
