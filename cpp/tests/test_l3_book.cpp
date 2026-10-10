#include <catch2/catch_test_macros.hpp>
#include "../include/core/l3_book.hpp"

#include <list>
#include <map>
#include <random>
#include <tuple>
#include <unordered_map>
#include <vector>

TEST_CASE("L3Book: FIFO within a level and queue_ahead", "[l3]") {
    L3Book b(12);
    REQUIRE(b.add(1, Side::Bid, 100, 5));
    REQUIRE(b.add(2, Side::Bid, 100, 7));
    REQUIRE(b.add(3, Side::Bid, 99, 4));
    REQUIRE_FALSE(b.add(1, Side::Bid, 100, 1));   // duplicate id
    REQUIRE(b.best_price(Side::Bid) == 100);
    REQUIRE(b.best_qty(Side::Bid) == 12);
    REQUIRE(b.queue_ahead(2) == 5);
    REQUIRE(b.queue_ahead(1) == 0);
    REQUIRE(b.level_orders(Side::Bid, 100) == 2);
}

TEST_CASE("L3Book: cancel and execute keep the book consistent", "[l3]") {
    L3Book b(12);
    b.add(1, Side::Ask, 101, 5);
    b.add(2, Side::Ask, 101, 3);
    b.add(3, Side::Ask, 102, 9);
    REQUIRE(b.execute(1, 2) == 2);          // partial: stays at the front
    REQUIRE(b.queue_ahead(2) == 3);
    REQUIRE(b.execute(1, 10) == 3);         // capped at remaining size, removed
    REQUIRE_FALSE(b.contains(1));
    REQUIRE(b.cancel(2));
    REQUIRE(b.best_price(Side::Ask) == 102);  // level 101 emptied
    REQUIRE(b.level_count(Side::Ask) == 1);
    REQUIRE_FALSE(b.cancel(2));
}

TEST_CASE("L3Book: modify down keeps priority, modify up loses it", "[l3]") {
    L3Book b(12);
    b.add(1, Side::Bid, 100, 5);
    b.add(2, Side::Bid, 100, 5);
    REQUIRE(b.modify(1, 3));
    REQUIRE(b.queue_ahead(2) == 3);         // 1 still ahead
    REQUIRE(b.modify(1, 8));
    REQUIRE(b.queue_ahead(1) == 5);         // 1 now behind 2
    REQUIRE(b.level_qty(Side::Bid, 100) == 13);
}

TEST_CASE("L3Book: match sweeps in price-time priority up to the limit", "[l3]") {
    L3Book b(12);
    b.add(1, Side::Ask, 101, 5);
    b.add(2, Side::Ask, 101, 5);
    b.add(3, Side::Ask, 102, 5);
    b.add(4, Side::Ask, 104, 5);
    std::vector<std::tuple<uint64_t, Price, Qty>> fills;
    Qty f = b.match(Side::Bid, 102, 12, [&](uint64_t id, Price p, Qty q) { fills.emplace_back(id, p, q); });
    REQUIRE(f == 12);
    REQUIRE(fills.size() == 3);
    REQUIRE(fills[0] == std::make_tuple(uint64_t{1}, Price{101}, Qty{5}));
    REQUIRE(fills[1] == std::make_tuple(uint64_t{2}, Price{101}, Qty{5}));
    REQUIRE(fills[2] == std::make_tuple(uint64_t{3}, Price{102}, Qty{2}));
    REQUIRE(b.best_price(Side::Ask) == 102);
    REQUIRE(b.best_qty(Side::Ask) == 3);
    // Limit respected: nothing at or below 102 left beyond order 3.
    f = b.match(Side::Bid, 103, 100, [](uint64_t, Price, Qty) {});
    REQUIRE(f == 3);
    REQUIRE(b.best_price(Side::Ask) == 104);
}

TEST_CASE("L3Book: top_levels gives the aggregated L2 view", "[l3]") {
    L3Book b(12);
    b.add(1, Side::Bid, 100, 1);
    b.add(2, Side::Bid, 100, 2);
    b.add(3, Side::Bid, 98, 4);
    b.add(4, Side::Bid, 99, 8);
    std::vector<Level> lv;
    b.top_levels(Side::Bid, 2, lv);
    REQUIRE(lv.size() == 2);
    REQUIRE(lv[0].price == 100);
    REQUIRE(lv[0].qty == 3);
    REQUIRE(lv[1].price == 99);
}

// ── differential test against a deliberately simple reference book ──────────

namespace {
struct RefBook {
    struct O { uint64_t id; Qty qty; };
    std::map<Price, std::list<O>> side[2];   // [0] bids, [1] asks; FIFO per level
    std::unordered_map<uint64_t, std::tuple<int, Price, std::list<O>::iterator>> where;

    static int s(Side x) { return x == Side::Bid ? 0 : 1; }
    bool add(uint64_t id, Side sd, Price p, Qty q) {
        if (q <= 0 || where.count(id)) return false;
        auto& lvl = side[s(sd)][p];
        lvl.push_back({id, q});
        where[id] = {s(sd), p, std::prev(lvl.end())};
        return true;
    }
    void remove(uint64_t id) {
        auto [si, p, it] = where.at(id);
        auto& lvl = side[si][p];
        lvl.erase(it);
        if (lvl.empty()) side[si].erase(p);
        where.erase(id);
    }
    bool cancel(uint64_t id) {
        if (!where.count(id)) return false;
        remove(id);
        return true;
    }
    bool modify(uint64_t id, Qty q) {
        if (!where.count(id)) return false;
        auto [si, p, it] = where.at(id);
        if (q <= 0) { remove(id); return true; }
        if (q <= it->qty) { it->qty = q; return true; }
        remove(id);
        return add(id, si == 0 ? Side::Bid : Side::Ask, p, q);
    }
    Qty execute(uint64_t id, Qty q) {
        if (!where.count(id) || q <= 0) return 0;
        auto it = std::get<2>(where.at(id));
        if (q >= it->qty) { Qty r = it->qty; remove(id); return r; }
        it->qty -= q;
        return q;
    }
    Qty match(Side aggr, Price limit, Qty q, std::vector<std::tuple<uint64_t, Price, Qty>>& fills) {
        int ps = aggr == Side::Bid ? 1 : 0;
        Qty filled = 0;
        while (filled < q && !side[ps].empty()) {
            auto lit = ps == 1 ? side[ps].begin() : std::prev(side[ps].end());
            Price px = lit->first;
            if (aggr == Side::Bid ? px > limit : px < limit) break;
            O& o = lit->second.front();
            Qty f = std::min(o.qty, q - filled);
            fills.emplace_back(o.id, px, f);
            filled += f;
            if (f == o.qty) remove(o.id);
            else o.qty -= f;
        }
        return filled;
    }
    Qty level_qty(Side sd, Price p) const {
        auto& m = side[s(sd)];
        auto it = m.find(p);
        if (it == m.end()) return 0;
        Qty t = 0;
        for (auto& o : it->second) t += o.qty;
        return t;
    }
    Qty queue_ahead(uint64_t id) const {
        auto [si, p, it] = where.at(id);
        Qty t = 0;
        for (auto& o : side[si].at(p)) { if (o.id == id) break; t += o.qty; }
        return t;
    }
};
}  // namespace

TEST_CASE("L3Book: randomized differential test against a reference book", "[l3]") {
    L3Book b(12, 64);    // tiny window and pools: exercise overflow, recentring and pool growth
    RefBook r;
    std::mt19937_64 rng(42);
    std::vector<uint64_t> live;
    uint64_t next_id = 1;
    auto rnd = [&](uint64_t n) { return rng() % n; };

    for (int step = 0; step < 300000; ++step) {
        const Price mid = 1'000'000 + static_cast<Price>(step / 200);   // drifting market
        const int op = static_cast<int>(rnd(100));
        if (op < 45 || live.empty()) {                                   // add
            Side sd = rnd(2) ? Side::Bid : Side::Ask;
            Price p = (rnd(50) == 0) ? static_cast<Price>(rnd(2'000'000))           // far away
                                     : (sd == Side::Bid ? mid - 1 - static_cast<Price>(rnd(40))
                                                        : mid + 1 + static_cast<Price>(rnd(40)));
            Qty q = 1 + static_cast<Qty>(rnd(100));
            uint64_t id = next_id++ * 2654435761ULL;   // non-sequential ids
            REQUIRE(b.add(id, sd, p, q) == r.add(id, sd, p, q));
            live.push_back(id);
        } else if (op < 75) {                                            // cancel
            size_t k = rnd(live.size());
            uint64_t id = live[k];
            REQUIRE(b.cancel(id) == r.cancel(id));
            live[k] = live.back();
            live.pop_back();
        } else if (op < 85) {                                            // modify
            uint64_t id = live[rnd(live.size())];
            Qty q = static_cast<Qty>(rnd(120));
            REQUIRE(b.modify(id, q) == r.modify(id, q));
        } else if (op < 95) {                                            // execute
            uint64_t id = live[rnd(live.size())];
            Qty q = 1 + static_cast<Qty>(rnd(60));
            REQUIRE(b.execute(id, q) == r.execute(id, q));
        } else {                                                         // aggressive match
            Side aggr = rnd(2) ? Side::Bid : Side::Ask;
            Price lim = aggr == Side::Bid ? mid + static_cast<Price>(rnd(20)) : mid - static_cast<Price>(rnd(20));
            Qty q = 1 + static_cast<Qty>(rnd(300));
            std::vector<std::tuple<uint64_t, Price, Qty>> fb, fr;
            Qty a = b.match(aggr, lim, q, [&](uint64_t id, Price p, Qty x) { fb.emplace_back(id, p, x); });
            Qty c = r.match(aggr, lim, q, fr);
            REQUIRE(a == c);
            REQUIRE(fb == fr);
        }
        // Prune ids that are gone (filled or cancelled) from the live list lazily.
        if (step % 1000 == 0) {
            std::vector<uint64_t> keep;
            for (auto id : live) if (r.where.count(id)) keep.push_back(id);
            live.swap(keep);
        }
        // Invariants every step.
        REQUIRE(b.order_count() == r.where.size());
        for (Side sd : {Side::Bid, Side::Ask}) {
            const auto& m = r.side[RefBook::s(sd)];
            REQUIRE(b.empty(sd) == m.empty());
            REQUIRE(b.level_count(sd) == m.size());
            if (!m.empty()) {
                Price best = sd == Side::Bid ? m.rbegin()->first : m.begin()->first;
                REQUIRE(b.best_price(sd) == best);
                REQUIRE(b.best_qty(sd) == r.level_qty(sd, best));
            }
        }
        if (step % 97 == 0 && !live.empty()) {
            uint64_t id = live[rnd(live.size())];
            if (r.where.count(id)) REQUIRE(b.queue_ahead(id) == r.queue_ahead(id));
        }
    }
}
