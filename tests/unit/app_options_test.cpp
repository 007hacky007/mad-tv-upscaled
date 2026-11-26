#include <doctest/doctest.h>
#include "platform/app.h"
#include <array>
using namespace mtv;
TEST_SUITE("platform.options") {
TEST_CASE("app_options_defaults") {
    auto r = parse_app_options({});
    REQUIRE(r.has_value());
    CHECK_FALSE(r.value().smoke_test);
    CHECK(r.value().frames == 3);
    CHECK(r.value().report_path.empty());
    CHECK(r.value().data_dir.empty());
}
TEST_CASE("smoke_frames_must_be_positive") {
    std::array<std::string_view,3> ok{"--smoke-test","--frames","3"};
    auto r = parse_app_options(ok);
    REQUIRE(r.has_value());
    CHECK(r.value().smoke_test);
    CHECK(r.value().frames == 3);
    std::array<std::string_view,3> zero{"--smoke-test","--frames","0"};
    CHECK_FALSE(parse_app_options(zero).has_value());
}
TEST_CASE("invalid_options_fail") {
    std::array<std::string_view,2> neg{"--frames","-1"};
    std::array<std::string_view,2> txt{"--frames","abc"};
    std::array<std::string_view,1> missing{"--report"};
    std::array<std::string_view,1> unknown{"--bogus"};
    CHECK_FALSE(parse_app_options(neg).has_value());
    CHECK_FALSE(parse_app_options(txt).has_value());
    CHECK_FALSE(parse_app_options(missing).has_value());
    CHECK_FALSE(parse_app_options(unknown).has_value());
    std::array<std::string_view,2> data{"--data","mad-tv"};
    auto d = parse_app_options(data);
    REQUIRE(d.has_value());
    CHECK(d.value().data_dir == "mad-tv");
}
TEST_CASE("screenshot_and_script_paths") {
    std::array<std::string_view,4> a{"--screenshot","shot.png","--script","in.json"};
    auto r = parse_app_options(a);
    REQUIRE(r.has_value());
    CHECK(r.value().screenshot_path == "shot.png");
    CHECK(r.value().script_path == "in.json");
    std::array<std::string_view,1> missing{"--screenshot"};
    CHECK_FALSE(parse_app_options(missing).has_value());
}
TEST_CASE("import_option_takes_a_folder") {
    std::array<std::string_view,2> a{"--import","/games/madtv"};
    auto r = parse_app_options(a);
    REQUIRE(r.has_value());
    CHECK(r.value().import_dir == "/games/madtv");
}
}
TEST_SUITE("platform.options") {
TEST_CASE("realtime_smoke_test") {
    std::array<std::string_view,2> a{"--smoke-test","--realtime"};
    auto r = parse_app_options(a);
    REQUIRE(r.has_value());
    CHECK(r.value().realtime);
    CHECK_FALSE(parse_app_options({}).value().realtime);
}
}
TEST_SUITE("platform.options") {
TEST_CASE("build_pack_smoke_option") {
    std::array<std::string_view,2> a{"--smoke-test","--build-pack"};
    auto r = parse_app_options(a);
    REQUIRE(r.has_value());
    CHECK(r.value().build_pack);
    CHECK_FALSE(parse_app_options({}).value().build_pack);
}
}
