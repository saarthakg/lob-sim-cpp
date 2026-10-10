#include <catch2/catch_test_macros.hpp>
#include "../include/core/price_ladder.hpp"
#include "../include/util/flat_hash_map.hpp"
#include "../include/util/object_pool.hpp"

#include <random>
#include <set>
#include <unordered_map>

TEST_CASE("FlatHashMap: insert, find, overwrite, erase", "[containers]") {
    FlatHashMap m(4);
    m.insert(10, 1);
    m.insert(20, 2);
    m.insert(10, 3);
    REQUIRE(m.size() == 2);
    REQUIRE(m.find(10) == 3);
    REQUIRE(m.find(20) == 2);
    REQUIRE(m.find(30) == FlatHashMap::NOT_FOUND);
    REQUIRE(m.erase(10));
    REQUIRE_FALSE(m.erase(10));
    REQUIRE(m.find(10) == FlatHashMap::NOT_FOUND);
    REQUIRE(m.find(20) == 2);
}

TEST_CASE("FlatHashMap: randomized against std::unordered_map", "[containers]") {
    // Heavy insert/erase churn exercises backward-shift deletion: a bug there
    // shows up as a key becoming unfindable after an unrelated erase.
    FlatHashMap m(8);
    std::unordered_map<uint64_t, uint32_t> ref;
    std::mt19937_64 rng(1);
    for (int i = 0; i < 200000; ++i) {
        uint64_t k = rng() % 5000;   // small key space → many collisions and erases
        if (rng() % 3 == 0) {
            REQUIRE(m.erase(k) == (ref.erase(k) == 1));
        } else {
            uint32_t v = static_cast<uint32_t>(rng());
            m.insert(k, v);
            ref[k] = v;
        }
    }
    REQUIRE(m.size() == ref.size());
    for (uint64_t k = 0; k < 5000; ++k) {
        auto it = ref.find(k);
        REQUIRE(m.find(k) == (it == ref.end() ? FlatHashMap::NOT_FOUND : it->second));
    }
}

TEST_CASE("ObjectPool: recycles indices", "[containers]") {
    ObjectPool<int> p(4);
    uint32_t a = p.alloc(), b = p.alloc();
    p[a] = 7;
    p.release(a);
    REQUIRE(p.live() == 1);
    uint32_t c = p.alloc();
    REQUIRE(c == a);         // reused
    REQUIRE(p[c] == 0);      // reset on reuse
    REQUIRE(b != c);
}

TEST_CASE("PriceLadder: best and next_worse on both sides", "[containers]") {
    PriceLadder bids(true, 12), asks(false, 12);
    for (Price p : {100, 105, 103}) bids.insert(p, static_cast<uint32_t>(p));
    for (Price p : {110, 108, 120}) asks.insert(p, static_cast<uint32_t>(p));
    REQUIRE(bids.best() == 105);
    REQUIRE(asks.best() == 108);
    Price n;
    REQUIRE(bids.next_worse(105, n));
    REQUIRE(n == 103);
    REQUIRE(asks.next_worse(108, n));
    REQUIRE(n == 110);
    bids.erase(105);
    REQUIRE(bids.best() == 103);
    REQUIRE(bids.find(100) == 100);
    REQUIRE(bids.find(105) == PriceLadder::NIL);
}

TEST_CASE("PriceLadder: far prices overflow and the window recentres", "[containers]") {
    // Window of 4096 ticks; place levels far beyond it on both sides.
    PriceLadder bids(true, 12);
    bids.insert(1'000'000, 1);
    bids.insert(10, 2);                 // far below: overflow
    bids.insert(5'000'000, 3);          // far above: becomes best, forces recentre
    REQUIRE(bids.best() == 5'000'000);
    REQUIRE(bids.recenters() >= 1);
    REQUIRE(bids.find(1'000'000) == 1);
    REQUIRE(bids.find(10) == 2);
    bids.erase(5'000'000);
    REQUIRE(bids.best() == 1'000'000);
    bids.erase(1'000'000);
    REQUIRE(bids.best() == 10);
}

TEST_CASE("PriceLadder: randomized against std::set", "[containers]") {
    for (bool desc : {true, false}) {
        PriceLadder lad(desc, 12);   // small window so overflow and recentring get exercised
        std::set<Price> ref;
        std::mt19937_64 rng(desc ? 7 : 8);
        for (int i = 0; i < 100000; ++i) {
            // Mostly near a drifting centre, sometimes far away.
            Price centre = 1'000'000 + static_cast<Price>(i / 50);
            Price p = (rng() % 20 == 0) ? static_cast<Price>(rng() % 3'000'000)
                                        : centre + static_cast<Price>(rng() % 6000) - 3000;
            if (ref.count(p)) {
                lad.erase(p);
                ref.erase(p);
            } else {
                lad.insert(p, static_cast<uint32_t>(p & 0xffffff));
                ref.insert(p);
            }
            REQUIRE(lad.size() == ref.size());
            if (!ref.empty()) {
                Price best = desc ? *ref.rbegin() : *ref.begin();
                REQUIRE(lad.best() == best);
                // next_worse from the best matches the set's neighbour
                Price n;
                bool has = lad.next_worse(best, n);
                if (desc) {
                    auto it = ref.find(best);
                    REQUIRE(has == (it != ref.begin()));
                    if (has) REQUIRE(n == *std::prev(it));
                } else {
                    auto it = std::next(ref.find(best));
                    REQUIRE(has == (it != ref.end()));
                    if (has) REQUIRE(n == *it);
                }
            }
        }
    }
}
