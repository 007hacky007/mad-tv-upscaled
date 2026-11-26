#include <doctest/doctest.h>
#include "debug/state_dump.h"
using namespace mtv;
TEST_SUITE("debug.state_dump") {
TEST_CASE("state_dump_compare_reports_paths") {
    StateDump a = nlohmann::json::parse(R"({"tick":100,"money":200,"ratings":[1,2,3],"rivals":{"fun":{"money":5}}})");
    StateDump b = nlohmann::json::parse(R"({"tick":100,"money":250,"ratings":[1,2,4],"rivals":{"fun":{"money":5}}})");
    auto d = compare_dumps(a, b);
    CHECK(d == std::vector<std::string>{"/money", "/ratings/2"});
}
TEST_CASE("state_dump_equal_is_empty") {
    StateDump a = nlohmann::json::parse(R"({"tick":1,"x":[1,{"y":2}]})");
    CHECK(compare_dumps(a, a).empty());
}
TEST_CASE("state_dump_missing_key_reported") {
    StateDump a = nlohmann::json::parse(R"({"a":1,"b":2})");
    StateDump b = nlohmann::json::parse(R"({"a":1})");
    CHECK(compare_dumps(a, b) == std::vector<std::string>{"/b"});
}
}
