#include "platform/pack_job.h"
#include "support/corpus.h"
#include <doctest/doctest.h>
#include <filesystem>
#include <thread>
using namespace mtv;
namespace fs = std::filesystem;
TEST_SUITE("platform.pack_job") {
TEST_CASE("failed_job_reports_and_leaves_no_pack") {
    const auto d = fs::temp_directory_path() / "mtv-pack-job-test";
    fs::remove_all(d);
    PackJob job;
    job.start(test::corpus_dir(), {d / "no-runner", d / "models"}, d / "pack", d / "work");
    while (job.running()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(job.finished());
    REQUIRE(job.error());
    CHECK(job.error()->find("upscaler") != std::string::npos);
    CHECK_FALSE(fs::exists(d / "pack"));
    CHECK_FALSE(fs::exists(d / "work"));
}
TEST_CASE("upscaler_location") {
    const auto u = upscaler_in(fs::path("base"));
    CHECK(u.models == fs::path("base") / "upscaler" / "models");
#ifdef _WIN32
    CHECK(u.runner == fs::path("base") / "upscaler" / "realesrgan-ncnn-vulkan.exe");
#else
    CHECK(u.runner == fs::path("base") / "upscaler" / "realesrgan-ncnn-vulkan");
#endif
}
}
