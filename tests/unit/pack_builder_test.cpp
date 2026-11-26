// The in-app pack builder's failure paths; the comparison with build_pack.py is pack_builder_compare_test.py.
#include "art/pack_builder.h"
#include "support/corpus.h"
#include <doctest/doctest.h>
#include <atomic>
#include <cstdlib>
#include <filesystem>
using namespace mtv;
using namespace mtv::art;
namespace fs = std::filesystem;
namespace {
fs::path fresh(const fs::path& name) {
    auto d = fs::temp_directory_path() / "mtv-pack-builder-test" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}
// The runner and models used for the real builds (MTV_RUNNER overrides, e.g. on Windows).
fs::path runner() {
    if (const char* r = std::getenv("MTV_RUNNER")) return fs::path(r);
    return fs::path(MTV_SOURCE_DIR) / "tools/upscalers/realesrgan-20220424/realesrgan-ncnn-vulkan";
}
}
TEST_SUITE("art.pack_builder") {
TEST_CASE("runner_failure_leaves_no_pack") {
    auto d = fresh("fail");
    std::atomic<bool> cancel{false};
    auto r = build_pack(test::corpus_dir(), d / "no-such-runner", d / "models", d / "pack", d / "work", nullptr, cancel, 2);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().reason.find("upscaler") != std::string::npos);
    CHECK_FALSE(fs::exists(d / "pack"));
#ifndef _WIN32
    auto f = build_pack(test::corpus_dir(), "/usr/bin/false", d / "models", d / "pack", d / "work", nullptr, cancel, 2);
    REQUIRE_FALSE(f.has_value());
    CHECK_FALSE(fs::exists(d / "pack"));
#endif
}
TEST_CASE("cancel_leaves_no_pack") {
    auto d = fresh("cancel");
    std::atomic<bool> cancel{true};
    auto r = build_pack(test::corpus_dir(), runner(), runner().parent_path() / "models", d / "pack", d / "work", nullptr, cancel, 2);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().reason.find("cancelled") != std::string::npos);
    CHECK_FALSE(fs::exists(d / "pack"));
}
TEST_CASE("unicode_work_dir") {
    if (!fs::exists(runner())) { MESSAGE("no upscaler at " << runner().string()); return; }
    auto d = fresh(fs::path(u8"práce-ž"));
    std::atomic<bool> cancel{false};
    size_t last = 0, total = 0;
    auto r = build_pack(test::corpus_dir(), runner(), runner().parent_path() / "models", d / "pack", d / "work",
                        [&](PackProgress p) { last = p.done; total = p.total; }, cancel, 2);
    REQUIRE_MESSAGE(r.has_value(), (r.has_value() ? "" : r.error().reason));
    CHECK(fs::exists(d / "pack" / "pack.json"));
    CHECK(total == 4);
    CHECK(last == 4);
}
}
TEST_SUITE("art.pack_builder") {
// Esc or quitting after the runner finished (while the pictures are assembled) must still stop the build.
TEST_CASE("cancel_after_the_runner_leaves_no_pack") {
    if (!fs::exists(runner())) { MESSAGE("no upscaler at " << runner().string()); return; }
    auto d = fresh("cancel-late");
    std::atomic<bool> cancel{false};
    auto r = build_pack(test::corpus_dir(), runner(), runner().parent_path() / "models", d / "pack", d / "work",
                        [&](PackProgress p) { if (p.total && p.done == p.total) cancel = true; }, cancel, 2);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().reason.find("cancelled") != std::string::npos);
    CHECK_FALSE(fs::exists(d / "pack"));
    CHECK_FALSE(fs::exists(d / "pack.tmp"));
}
}
