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

    double mid_price() const;                          // (best_bid + best_ask) / 2.0
    double spread() const;                             // best_ask - best_bid in ticks
    double imbalance(int depth_levels = 5) const;      // (bid_qty - ask_qty) / (bid_qty + ask_qty)

    Qty visible_qty_at_price(Side side, Price px) const;
    std::vector<Level> top_n_bids(int n) const;
    std::vector<Level> top_n_asks(int n) const;

    UpdateId update_id() const;
    void set_update_id(UpdateId id);

private:
    std::map<Price, Qty, std::greater<Price>> bids_;  // descending: best bid first
    std::map<Price, Qty, std::less<Price>>    asks_;  // ascending: best ask first
    UpdateId update_id_ = 0;
};
