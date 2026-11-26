#include <doctest/doctest.h>
#include "debug/rng.h"
using namespace mtv;
TEST_SUITE("debug.rng") {
TEST_CASE("turbo_c_rand_seed_1") {
    TurboCRandom r;
    r.seed(1);
    CHECK(r.next() == 346);
    CHECK(r.next() == 130);
    CHECK(r.next() == 10982);
    CHECK(r.next() == 1090);
    CHECK(r.next() == 11656);
}
TEST_CASE("turbo_c_rand_default_seed_is_1") {
    TurboCRandom a, b;
    b.seed(1);
    for (int i = 0; i < 4; ++i) CHECK(a.next() == b.next());
}
TEST_CASE("srand_clears_high_word") {
    TurboCRandom r;
    r.seed(7); r.next(); r.next();
    r.seed(7);
    CHECK(r.state() == 7u);
}
TEST_CASE("mad_random_reseeds_from_timer_salt_and_base") {
    // srand(uint16(timer + salt + base)); rand() % n + 1  (original 0x1D809)
    CHECK(mad_random(6, 3, 5, 0) == 5);
    CHECK(mad_random(100, 0x1234, 0xFFF0, 0x20) == 93);
    CHECK(mad_random(10, 0, 0, 0x10000) == 1); // 32-bit sum, srand keeps the low word only
}
}
