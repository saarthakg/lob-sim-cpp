#include <catch2/catch_test_macros.hpp>
#include "../include/util/spsc_ring.hpp"

#include <cstdint>
#include <string>
#include <thread>

TEST_CASE("SpscRing: FIFO order, full and empty", "[spsc]") {
    SpscRing<int> r(4);
    REQUIRE(r.capacity() == 4);
    int v;
    REQUIRE_FALSE(r.try_pop(v));
    for (int i = 0; i < 4; ++i) REQUIRE(r.try_push(i));
    REQUIRE_FALSE(r.try_push(99));               // full
    for (int i = 0; i < 4; ++i) {
        REQUIRE(r.try_pop(v));
        REQUIRE(v == i);
    }
    REQUIRE_FALSE(r.try_pop(v));
    REQUIRE(r.try_push(5));                      // wraps
    REQUIRE(r.try_pop(v));
    REQUIRE(v == 5);
}

TEST_CASE("SpscRing: capacity rounds up to a power of two; moves non-trivial types", "[spsc]") {
    SpscRing<std::string> r(5);
    REQUIRE(r.capacity() == 8);
    REQUIRE(r.try_push(std::string(100, 'x')));
    std::string s;
    REQUIRE(r.try_pop(s));
    REQUIRE(s.size() == 100);
}

TEST_CASE("SpscRing: two threads transfer 5M values in order with no loss", "[spsc]") {
    // A small ring forces constant full/empty transitions — the paths where
    // the cached indices are refreshed and where ordering bugs would surface.
    constexpr uint64_t N = 5'000'000;
    SpscRing<uint64_t> r(64);
    std::thread producer([&] {
        for (uint64_t i = 0; i < N; ++i)
            while (!r.try_push(i)) {}
    });
    uint64_t expected = 0, v = 0;
    bool in_order = true;
    while (expected < N) {
        if (r.try_pop(v)) {
            in_order &= (v == expected);
            ++expected;
        }
    }
    producer.join();
    REQUIRE(in_order);
    REQUIRE_FALSE(r.try_pop(v));
}
