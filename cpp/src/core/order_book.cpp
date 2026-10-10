#include "../../include/core/order_book.hpp"

OrderBook::OrderBook(int log2_window) : bids_(true, log2_window), asks_(false, log2_window) {
    qty_.reserve(1024);
    free_.reserve(1024);
}

void OrderBook::clear() {
    bids_.clear();
    asks_.clear();
    qty_.clear();
    free_.clear();
    update_id_ = 0;
}

void OrderBook::load_snapshot(const BookSnapshot& snapshot) {
    clear();
    for (const auto& lvl : snapshot.bids) if (lvl.qty > 0) apply_update(Side::Bid, lvl.price, lvl.qty);
    for (const auto& lvl : snapshot.asks) if (lvl.qty > 0) apply_update(Side::Ask, lvl.price, lvl.qty);
    update_id_ = snapshot.last_update_id;
}

void OrderBook::apply_update(Side side, Price price, Qty qty) {
    PriceLadder& lad = ladder(side);
    const uint32_t slot = lad.find(price);
    if (qty == 0) {
        if (slot != PriceLadder::NIL) {
            lad.erase(price);
            free_.push_back(slot);
        }
        return;
    }
    if (slot != PriceLadder::NIL) {
        qty_[slot] = qty;
        return;
    }
    uint32_t s;
    if (!free_.empty()) {
        s = free_.back();
        free_.pop_back();
        qty_[s] = qty;
    } else {
        s = static_cast<uint32_t>(qty_.size());
        qty_.push_back(qty);
    }
    lad.insert(price, s);
}

std::optional<Level> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    const Price p = bids_.best();
    return Level{p, qty_[bids_.find(p)]};
}

std::optional<Level> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    const Price p = asks_.best();
    return Level{p, qty_[asks_.find(p)]};
}

double OrderBook::mid_price() const {
    if (empty()) return 0.0;
    return (static_cast<double>(bids_.best()) + static_cast<double>(asks_.best())) / 2.0;
}

double OrderBook::spread() const {
    if (empty()) return 0.0;
    return static_cast<double>(asks_.best() - bids_.best());
}

double OrderBook::imbalance(int depth_levels) const {
    int64_t bq = 0, aq = 0;
    int n = 0;
    for_each_level(Side::Bid, [&](Price, Qty q) { bq += q; return ++n < depth_levels; });
    n = 0;
    for_each_level(Side::Ask, [&](Price, Qty q) { aq += q; return ++n < depth_levels; });
    const double total = static_cast<double>(bq + aq);
    if (total == 0.0) return 0.0;
    return static_cast<double>(bq - aq) / total;
}

double OrderBook::weighted_mid() const {
    auto bid = best_bid();
    auto ask = best_ask();
    if (!bid || !ask) return 0.0;
    double qb = static_cast<double>(bid->qty), qa = static_cast<double>(ask->qty);
    if (qb + qa <= 0.0) return mid_price();
    // (Pb*Qa + Pa*Qb) / (Qb + Qa)
    return (static_cast<double>(bid->price) * qa + static_cast<double>(ask->price) * qb) / (qb + qa);
}

double OrderBook::depth_imbalance(Price ticks) const {
    if (empty()) return 0.0;
    const double mid = mid_price();
    const double band = static_cast<double>(ticks);
    double bq = 0.0, aq = 0.0;
    for_each_level(Side::Bid, [&](Price p, Qty q) {
        if (mid - static_cast<double>(p) > band) return false;
        bq += static_cast<double>(q);
        return true;
    });
    for_each_level(Side::Ask, [&](Price p, Qty q) {
        if (static_cast<double>(p) - mid > band) return false;
        aq += static_cast<double>(q);
        return true;
    });
    if (bq + aq <= 0.0) return 0.0;
    return (bq - aq) / (bq + aq);
}

bool OrderBook::crossed() const {
    if (empty()) return false;
    return bids_.best() >= asks_.best();
}

Qty OrderBook::visible_qty_at_price(Side side, Price px) const {
    const uint32_t s = ladder(side).find(px);
    return s == PriceLadder::NIL ? 0 : qty_[s];
}

std::vector<Level> OrderBook::top_n_bids(int n) const {
    std::vector<Level> out;
    out.reserve(static_cast<size_t>(n));
    for_each_level(Side::Bid, [&](Price p, Qty q) { out.push_back({p, q}); return static_cast<int>(out.size()) < n; });
    return out;
}

std::vector<Level> OrderBook::top_n_asks(int n) const {
    std::vector<Level> out;
    out.reserve(static_cast<size_t>(n));
    for_each_level(Side::Ask, [&](Price p, Qty q) { out.push_back({p, q}); return static_cast<int>(out.size()) < n; });
    return out;
}

UpdateId OrderBook::update_id() const { return update_id_; }
void OrderBook::set_update_id(UpdateId id) { update_id_ = id; }
