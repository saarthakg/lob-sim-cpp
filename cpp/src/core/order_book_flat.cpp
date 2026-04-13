#include "../../include/core/order_book_flat.hpp"

#include <algorithm>
#include <functional>

// ── internal helpers ─────────────────────────────────────────────────────────

template <typename Comp>
std::vector<Level>::iterator OrderBookFlat::find_price(
    std::vector<Level>& vec, Price price, Comp cmp)
{
    // Binary search for the insertion point, then check if it's an exact match.
    auto it = std::lower_bound(vec.begin(), vec.end(), Level{price, 0},
        [&](const Level& a, const Level& b) { return cmp(a.price, b.price); });
    if (it != vec.end() && it->price == price)
        return it;
    return vec.end();
}

// ── public API ───────────────────────────────────────────────────────────────

void OrderBookFlat::clear() {
    bids_.clear();
    asks_.clear();
    update_id_ = 0;
}

void OrderBookFlat::load_snapshot(const BookSnapshot& snapshot) {
    clear();
    update_id_ = snapshot.last_update_id;

    bids_ = snapshot.bids;
    asks_ = snapshot.asks;

    // Sort bids descending, asks ascending
    std::sort(bids_.begin(), bids_.end(),
        [](const Level& a, const Level& b) { return a.price > b.price; });
    std::sort(asks_.begin(), asks_.end(),
        [](const Level& a, const Level& b) { return a.price < b.price; });

    // Remove zero-qty levels (shouldn't appear in snapshots, but be defensive)
    auto nz = [](const Level& l) { return l.qty == 0; };
    bids_.erase(std::remove_if(bids_.begin(), bids_.end(), nz), bids_.end());
    asks_.erase(std::remove_if(asks_.begin(), asks_.end(), nz), asks_.end());
}

void OrderBookFlat::apply_update(Side side, Price price, Qty qty) {
    apply_update(side, Level{price, qty});
}

void OrderBookFlat::apply_update(Side side, Level level) {
    if (side == Side::Bid) {
        // Bids are descending
        auto it = find_price(bids_, level.price, std::greater<Price>{});
        if (level.qty == 0) {
            if (it != bids_.end()) bids_.erase(it);
        } else {
            if (it != bids_.end()) {
                it->qty = level.qty;
            } else {
                // Insert in sorted position (descending)
                auto pos = std::lower_bound(bids_.begin(), bids_.end(), level,
                    [](const Level& a, const Level& b) { return a.price > b.price; });
                bids_.insert(pos, level);
            }
        }
    } else {
        // Asks are ascending
        auto it = find_price(asks_, level.price, std::less<Price>{});
        if (level.qty == 0) {
            if (it != asks_.end()) asks_.erase(it);
        } else {
            if (it != asks_.end()) {
                it->qty = level.qty;
            } else {
                // Insert in sorted position (ascending)
                auto pos = std::lower_bound(asks_.begin(), asks_.end(), level,
                    [](const Level& a, const Level& b) { return a.price < b.price; });
                asks_.insert(pos, level);
            }
        }
    }
}

std::optional<Level> OrderBookFlat::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.front();
}

std::optional<Level> OrderBookFlat::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.front();
}

double OrderBookFlat::mid_price() const {
    auto bb = best_bid();
    auto ba = best_ask();
    if (!bb || !ba) return 0.0;
    return (static_cast<double>(bb->price) + static_cast<double>(ba->price)) / 2.0;
}

double OrderBookFlat::spread() const {
    auto bb = best_bid();
    auto ba = best_ask();
    if (!bb || !ba) return 0.0;
    return static_cast<double>(ba->price - bb->price);
}

std::vector<Level> OrderBookFlat::top_n_bids(int n) const {
    int cnt = std::min(n, static_cast<int>(bids_.size()));
    return {bids_.begin(), bids_.begin() + cnt};
}

std::vector<Level> OrderBookFlat::top_n_asks(int n) const {
    int cnt = std::min(n, static_cast<int>(asks_.size()));
    return {asks_.begin(), asks_.begin() + cnt};
}

UpdateId OrderBookFlat::update_id() const { return update_id_; }
void OrderBookFlat::set_update_id(UpdateId id) { update_id_ = id; }
