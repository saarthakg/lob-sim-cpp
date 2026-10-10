#pragma once
#include "types.hpp"
#include <cstdint>
#include <map>
#include <vector>

// One side of a book's price index: price → level index, plus "what is the
// next price in priority order?".
//
// Orders cluster near the touch, so prices inside a dense window of 2^k ticks
// live in a flat array (O(1) lookup, no allocation), with a two-level bitmap
// marking occupied ticks: finding the next occupied price after a level
// empties is a couple of count-leading/trailing-zero instructions rather than
// a tree walk. Prices outside the window (resting orders far from the market)
// go to a std::map overflow. When the best price leaves the window, the
// window is recentred on it — rare, and O(levels).
//
// `descending` = bid side (best = highest price).
class PriceLadder {
public:
    static constexpr uint32_t NIL = ~uint32_t{0};

    explicit PriceLadder(bool descending, int log2_window = 18);

    uint32_t find(Price p) const;
    void     insert(Price p, uint32_t level);   // p must be absent
    void     erase(Price p);                    // p must be present

    bool  empty() const { return count_ == 0; }
    size_t size() const { return count_; }
    Price best() const { return best_; }        // requires !empty()

    // Next price after p in priority order (lower for bids, higher for
    // asks). Returns false if there is none.
    bool next_worse(Price p, Price& out) const;

    // Visit up to n levels best-first: f(price, level_index).
    template <typename F>
    void for_each_best(size_t n, F&& f) const {
        if (empty()) return;
        Price p = best_;
        for (size_t k = 0; k < n; ++k) {
            f(p, find(p));
            if (!next_worse(p, p)) break;
        }
    }

    void clear();
    size_t recenters() const { return recenters_; }

private:
    bool    desc_;
    int64_t W_;
    Price   base_ = 0;
    bool    based_ = false;
    std::vector<uint32_t> slots_;     // W_ entries
    std::vector<uint64_t> words_;     // W_/64 occupancy bits
    std::vector<uint64_t> summary_;   // W_/4096: bit set if the word is non-zero
    std::map<Price, uint32_t> overflow_;
    size_t count_ = 0;
    Price  best_ = 0;
    size_t recenters_ = 0;

    bool in_window(Price p) const { return based_ && p >= base_ && p < base_ + W_; }
    void set_bit(int64_t i);
    void clear_bit(int64_t i);
    int64_t next_set(int64_t i) const;   // lowest set index > i, or -1
    int64_t prev_set(int64_t i) const;   // highest set index < i, or -1
    bool better(Price a, Price b) const { return desc_ ? a > b : a < b; }
    void recenter(Price center);
};
