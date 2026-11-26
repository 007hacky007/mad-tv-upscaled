// The mtv-logic library as the app loads it (MTV_LOGIC_LIB is the library built next to the tests).
#include "platform/logic_loader.h"
#include "rt/machine.h"
#include <doctest/doctest.h>
#include <stdexcept>
using namespace mtv;
namespace {
// Every service the translated code asks for throws, so the first one reached unwinds through library frames.
struct Throwing : rt::Machine {
    void interrupt(rt::Cpu&, uint8_t, uint16_t) override { throw std::runtime_error("probe"); }
    uint8_t in8(rt::Cpu&, uint16_t) override { throw std::runtime_error("probe"); }
    void out8(rt::Cpu&, uint16_t, uint8_t) override { throw std::runtime_error("probe"); }
    void events(rt::Cpu&) override { throw std::runtime_error("probe"); }
    void call_near(rt::Cpu&, uint16_t) override { throw std::runtime_error("probe"); }
    void call_far(rt::Cpu&, uint16_t, uint16_t) override { throw std::runtime_error("probe"); }
    void jump_far(rt::Cpu&, uint16_t, uint16_t) override { throw std::runtime_error("probe"); }
    void jump_near(rt::Cpu&, uint16_t) override { throw std::runtime_error("probe"); }
};
}
TEST_SUITE("platform.logic") {
TEST_CASE("loads_matching_library") {
    auto l = load_logic(MTV_LOGIC_LIB, expected_logic_build_id());
    REQUIRE_MESSAGE(l.has_value(), (l.has_value() ? "" : l.error().reason));
    const auto& t = l.value()->tables();
    CHECK(t.abi == rt::kLogicAbi);
    CHECK(t.function_count > 500);
    CHECK(t.blob_count == 3);
    CHECK(std::string_view(t.build_id) == expected_logic_build_id());
}
TEST_CASE("refuses_other_build_id") {
    auto l = load_logic(MTV_LOGIC_LIB, "not-this-build");
    REQUIRE_FALSE(l.has_value());
    CHECK(l.error().reason.find("another build") != std::string::npos);
    auto missing = load_logic(std::filesystem::path(MTV_LOGIC_LIB).parent_path() / "no-such-library", expected_logic_build_id());
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().reason.find("missing") != std::string::npos);
}
TEST_CASE("exception_from_host_unwinds_through_library") {
    auto l = load_logic(MTV_LOGIC_LIB, expected_logic_build_id());
    REQUIRE(l.has_value());
    const auto& t = l.value()->tables();
    rt::Memory mem;
    Throwing mach;
    mach.load_seg = 0x1000;
    rt::Cpu c;
    c.m = &mem; c.mach = &mach;
    c.s = {0x1000, 0x1000, 0x2000, 0x1000};
    c.r[rt::SP] = 0xFFF0;
    c.next_event = 0;
    bool caught = false;
    try { t.functions[0].fn(c); } catch (const std::runtime_error& e) { caught = std::string(e.what()) == "probe"; }
    CHECK(caught);
}
}
