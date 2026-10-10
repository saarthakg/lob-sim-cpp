#pragma once
#include "types.hpp"
#include "side.hpp"
#include "level.hpp"
#include "book_snapshot.hpp"
#include "price_ladder.hpp"
#include <optional>
#include <vector>

// Aggregated (L2) price-level book.
//
// Each side is a PriceLadder (dense price window + two-level occupancy bitmap,
// overflow map for far prices) mapping price → slot in a flat quantity array
// recycled through a free list. Applying a level update is O(1) with no heap
// allocation once warmed up, and the best price after a level empties is found
// with a couple of bit-scan instructions. (The original std::map version
// dominated the live engine's profile: every new level was a malloc and every
// update a tree walk, applied twice per event — exchange and strategy books.)
class OrderBook {
public:
    explicit OrderBook(int log2_window = 16);

    void clear();
    void load_snapshot(const BookSnapshot& snapshot);
    void apply_update(Side side, Price price, Qty qty);  // qty==0 → delete level

    std::optional<Level> best_bid() const;
    std::optional<Level> best_ask() const;

    double mid_price() const;                          // (best_bid + best_ask) / 2.0, in ticks
    double spread() const;                             // best_ask - best_bid in ticks
    double imbalance(int depth_levels = 5) const;      // (bid_qty - ask_qty) / (bid_qty + ask_qty)

    // Size-weighted mid (a.k.a. "weighted mid"/naive microprice):
    //   mid + spread * (I - 1/2),  I = Qb / (Qb + Qa) at the touch.
    // Leans toward the side with *less* displayed size — the side more likely
    // to be depleted next. Stoikov (2018) shows this over-reacts; strategies
    // shrink it toward mid with a weight in [0, 1].
    double weighted_mid() const;

    // Imbalance of cumulative displayed qty within `ticks` of the mid on each
    // side, in [-1, 1]. Price-range based (not level-count based) so it is
    // comparable across books with sparse vs dense levels.
    double depth_imbalance(Price ticks) const;

    Qty visible_qty_at_price(Side side, Price px) const;
    std::vector<Level> top_n_bids(int n) const;
    std::vector<Level> top_n_asks(int n) const;

    // Visit one side's levels best-first while f(price, qty) returns true.
    template <typename F>
    void for_each_level(Side side, F&& f) const {
        ladder(side).for_each_best_while([&](Price p, uint32_t i) { return f(p, qty_[i]); });
    }

    size_t bid_levels() const { return bids_.size(); }
    size_t ask_levels() const { return asks_.size(); }
    bool   empty()      const { return bids_.empty() || asks_.empty(); }
    bool   crossed()    const;

    UpdateId update_id() const;
    void set_update_id(UpdateId id);

private:
    PriceLadder bids_;
    PriceLadder asks_;
    std::vector<Qty>      qty_;    // slot → level quantity
    std::vector<uint32_t> free_;   // recycled slots
    UpdateId update_id_ = 0;

    const PriceLadder& ladder(Side s) const { return s == Side::Bid ? bids_ : asks_; }
    PriceLadder&       ladder(Side s)       { return s == Side::Bid ? bids_ : asks_; }
};
