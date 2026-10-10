#pragma once
#include "side.hpp"
#include "types.hpp"

#include <algorithm>
#include <list>
#include <map>
#include <tuple>
#include <unordered_map>
#include <vector>

// The textbook L3 book: std::map of price levels, std::list FIFO per level,
// std::unordered_map from order id to list iterator. Same semantics as
// L3Book, written for obviousness rather than speed. It is the oracle for
// L3Book's randomized differential test and the baseline in bench_l3.
struct ReferenceL3Book {
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
