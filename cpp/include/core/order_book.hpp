#pragma once
#include "types.hpp"
#include "side.hpp"
#include "level.hpp"
#include "book_snapshot.hpp"
#include <map>
#include <vector>
#include <optional>
#include <functional>

class OrderBook {
public:
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

    size_t bid_levels() const { return bids_.size(); }
    size_t ask_levels() const { return asks_.size(); }
    bool   empty()      const { return bids_.empty() || asks_.empty(); }
    bool   crossed()    const;

    const std::map<Price, Qty, std::greater<Price>>& bids() const { return bids_; }
    const std::map<Price, Qty, std::less<Price>>&    asks() const { return asks_; }

    UpdateId update_id() const;
    void set_update_id(UpdateId id);

private:
    std::map<Price, Qty, std::greater<Price>> bids_;  // descending: best bid first
    std::map<Price, Qty, std::less<Price>>    asks_;  // ascending: best ask first
    UpdateId update_id_ = 0;
};
