#pragma once
#include "strategy.hpp"
#include <algorithm>
#include <vector>

// Keeps a side's resting orders equal to a desired set of prices with the
// fewest messages: orders already at a desired price are left alone (they
// keep their queue position — the single most valuable thing a passive order
// has), orders at prices no longer desired are cancelled, and missing prices
// are filled in. Orders with an in-flight cancel are ignored.
inline void sync_quotes(StrategyContext& ctx, Side side, const std::vector<Price>& desired, Qty qty,
                        TimeInForce tif = TimeInForce::PostOnly) {
    std::vector<Price> have;
    for (const auto& [id, o] : ctx.orders()) {
        if (o.side != side || o.type != OrderType::Limit || o.cancel_sent) continue;
        if (std::find(desired.begin(), desired.end(), o.price) == desired.end()) {
            ctx.cancel(id);
        } else {
            have.push_back(o.price);
        }
    }
    for (Price p : desired) {
        if (std::find(have.begin(), have.end(), p) != have.end()) continue;
        ctx.submit_limit(side, p, qty, tif);
    }
}

inline void cancel_side(StrategyContext& ctx, Side side) {
    std::vector<OrderId> ids;
    for (const auto& [id, o] : ctx.orders())
        if (o.side == side && o.type == OrderType::Limit && !o.cancel_sent) ids.push_back(id);
    for (OrderId id : ids) ctx.cancel(id);
}

// Fast "level is being swept" detector driven by trade prints. Book snapshots
// arrive every 100 ms; prints arrive individually. Once prints at our touch
// price exceed `frac` of that level's last displayed size, the guard cancels
// our orders there and blocks re-quoting that price until the next book
// update. frac <= 0 disables it.
class DepletionGuard {
public:
    explicit DepletionGuard(double frac = 0.0) : frac_(frac) {}

    void on_book() {
        traded_[0] = traded_[1] = 0.0;
        blocked_[0] = blocked_[1] = false;
    }
    void on_trade(StrategyContext& ctx, const TradePrint& tr) {
        if (frac_ <= 0.0 || ctx.book().empty()) return;
        const int s = (tr.aggressor == Side::Ask) ? 0 : 1;  // 0: bid being hit, 1: ask being lifted
        const Level lvl = (s == 0) ? *ctx.book().best_bid() : *ctx.book().best_ask();
        if (tr.price != lvl.price) return;
        traded_[s] += static_cast<double>(tr.qty);
        if (blocked_[s] || traded_[s] < frac_ * static_cast<double>(lvl.qty)) return;
        blocked_[s] = true;
        px_[s] = lvl.price;
        const Side side = (s == 0) ? Side::Bid : Side::Ask;
        std::vector<OrderId> ids;
        for (const auto& [id, o] : ctx.orders())
            if (o.side == side && o.price == lvl.price && !o.cancel_sent) ids.push_back(id);
        for (OrderId id : ids) ctx.cancel(id);
    }
    bool blocked(Side side, Price px) const {
        const int s = (side == Side::Bid) ? 0 : 1;
        return blocked_[s] && px_[s] == px;
    }

private:
    double frac_;
    double traded_[2] = {0.0, 0.0};
    bool   blocked_[2] = {false, false};
    Price  px_[2] = {0, 0};
};
