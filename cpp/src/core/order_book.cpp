#include "../../include/core/order_book.hpp"
#include <stdexcept>
#include <iterator>

void OrderBook::clear() {
    bids_.clear();
    asks_.clear();
    update_id_ = 0;
}

void OrderBook::load_snapshot(const BookSnapshot& snapshot) {
    clear();
    for (const auto& lvl : snapshot.bids) {
        if (lvl.qty > 0) bids_[lvl.price] = lvl.qty;
    }
    for (const auto& lvl : snapshot.asks) {
        if (lvl.qty > 0) asks_[lvl.price] = lvl.qty;
    }
    update_id_ = snapshot.last_update_id;
}

void OrderBook::apply_update(Side side, Price price, Qty qty) {
    if (side == Side::Bid) {
        if (qty == 0) {
            bids_.erase(price);
        } else {
            bids_[price] = qty;
        }
    } else {
        if (qty == 0) {
            asks_.erase(price);
        } else {
            asks_[price] = qty;
        }
    }
}

std::optional<Level> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    auto it = bids_.begin();
    return Level{it->first, it->second};
}

std::optional<Level> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    auto it = asks_.begin();
    return Level{it->first, it->second};
}

double OrderBook::mid_price() const {
    auto bid = best_bid();
    auto ask = best_ask();
    if (!bid || !ask) return 0.0;
    return (static_cast<double>(bid->price) + static_cast<double>(ask->price)) / 2.0;
}

double OrderBook::spread() const {
    auto bid = best_bid();
    auto ask = best_ask();
    if (!bid || !ask) return 0.0;
    return static_cast<double>(ask->price - bid->price);
}

double OrderBook::imbalance(int depth_levels) const {
    int64_t bid_qty = 0, ask_qty = 0;
    int count = 0;
    for (auto it = bids_.begin(); it != bids_.end() && count < depth_levels; ++it, ++count) {
        bid_qty += it->second;
    }
    count = 0;
    for (auto it = asks_.begin(); it != asks_.end() && count < depth_levels; ++it, ++count) {
        ask_qty += it->second;
    }
    double total = static_cast<double>(bid_qty + ask_qty);
    if (total == 0.0) return 0.0;
    return static_cast<double>(bid_qty - ask_qty) / total;
}

Qty OrderBook::visible_qty_at_price(Side side, Price px) const {
    if (side == Side::Bid) {
        auto it = bids_.find(px);
        return (it != bids_.end()) ? it->second : 0;
    } else {
        auto it = asks_.find(px);
        return (it != asks_.end()) ? it->second : 0;
    }
}

std::vector<Level> OrderBook::top_n_bids(int n) const {
    std::vector<Level> result;
    result.reserve(n);
    int count = 0;
    for (auto it = bids_.begin(); it != bids_.end() && count < n; ++it, ++count) {
        result.push_back({it->first, it->second});
    }
    return result;
}

std::vector<Level> OrderBook::top_n_asks(int n) const {
    std::vector<Level> result;
    result.reserve(n);
    int count = 0;
    for (auto it = asks_.begin(); it != asks_.end() && count < n; ++it, ++count) {
        result.push_back({it->first, it->second});
    }
    return result;
}

UpdateId OrderBook::update_id() const { return update_id_; }
void OrderBook::set_update_id(UpdateId id) { update_id_ = id; }
