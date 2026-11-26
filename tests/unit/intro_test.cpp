#include <doctest/doctest.h>
#include "data/hash.h"
#include "intro/intro.h"
#include "support/corpus.h"
#include <fstream>
#include <nlohmann/json.hpp>
using namespace mtv;
namespace {
std::vector<uint8_t> rgb_of(const IntroFrame& f) {
    std::vector<uint8_t> rgb(320 * 200 * 3);
    for (size_t i = 0; i < f.image.indices.size(); ++i) for (size_t c = 0; c < 3; ++c) rgb[i * 3 + c] = f.rgb[f.image.indices[i]][c];
    return rgb;
}
}
TEST_SUITE("corpus.intro") {
TEST_CASE("intro_frames_match_reference") {
    auto data = IntroData::load(mtv::test::corpus_dir());
    REQUIRE_MESSAGE(data.has_value(), (data.has_value() ? "" : data.error().reason));
    std::ifstream f(test::source_dir() / "analysis/evidence/intro-frames.json");
    auto expected = nlohmann::json::parse(f)["sha256"].get<std::vector<std::string>>();
    Intro intro(data.value());
    std::vector<std::string> got;
    std::vector<uint8_t> last;
    int steps = 0;
    for (bool more = true; more; ++steps) {
        more = intro.step(false);
        if (steps == 0) continue;
        auto rgb = rgb_of(intro.frame());
        if (rgb == last) continue;
        got.push_back(sha256(std::as_bytes(std::span(rgb))));
        last = std::move(rgb);
    }
    CHECK(steps == 1110);
    CHECK(got == expected);
}
TEST_CASE("intro_skips_on_key") {
    auto data = IntroData::load(mtv::test::corpus_dir());
    REQUIRE(data.has_value());
    Intro intro(data.value());
    int steps = 0;
    while (intro.step(steps == 50) && steps < 2000) ++steps;
    CHECK(steps < 50 + 40); // a key ends the loops; only the 32-step fade remains
}
TEST_CASE("intro_logo_pixels_carry_provenance") {
    auto data = IntroData::load(mtv::test::corpus_dir());
    REQUIRE(data.has_value());
    Intro intro(data.value());
    while (intro.step(false)) {}
    int logo = 0, fire = 0;
    for (const auto& p : intro.frame().provenance) { logo += p.source == PixelSource::Logo; fire += p.source == PixelSource::Fire; }
    CHECK(logo > 1000);
    CHECK(fire > 1000);
}
}
