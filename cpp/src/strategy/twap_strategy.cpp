#include "../../include/strategy/twap_strategy.hpp"
#include "../../include/strategy/quote_manager.hpp"
#include <algorithm>

Qty TwapStrategy::slice_qty(int index) const {
    Qty base = cfg_.total_qty / cfg_.num_slices;
    Qty rem  = cfg_.total_qty - base * cfg_.num_slices;
    return base + (index == cfg_.num_slices - 1 ? rem : 0);
}

void TwapStrategy::on_order_update(StrategyContext& ctx, const ExecutionReport& rep) {
    if (rep.fill_qty > 0) {
        filled_    += rep.fill_qty;
        fill_cost_ += static_cast<double>(rep.fill_price) * static_cast<double>(rep.fill_qty);
    }
    // A passive slice we cancelled: cross the spread for its unfilled part.
    // Waiting for the cancel ack (instead of sweeping when the cancel is
    // sent) avoids over-filling if the resting order fills in the meantime.
    if ((rep.status == OrderStatus::Cancelled || rep.status == OrderStatus::Rejected) &&
        rep.order_type == OrderType::Limit) {
        auto it = passive_qty_.find(rep.order_id);
        if (it != passive_qty_.end()) {
            Qty unfilled = it->second - rep.cum_qty;
            passive_qty_.erase(it);
            if (unfilled > 0) ctx.submit_market(cfg_.side, unfilled);
        }
    } else if (rep.status == OrderStatus::Filled) {
        passive_qty_.erase(rep.order_id);
    }
}

void TwapStrategy::on_timer(StrategyContext& ctx) {
    if (cfg_.num_slices <= 0 || cfg_.total_qty <= 0) return;
    const OrderBook& book = ctx.book();
    if (book.empty()) return;
    const Timestamp now = ctx.now();
    if (first_ts_ == 0) {
        first_ts_    = now;
        arrival_mid_ = book.mid_price();
    }
    const int due = std::min<int>(cfg_.num_slices,
                                  static_cast<int>((now - first_ts_) / cfg_.interval_us) + 1);
    if (due <= slices_sent_) return;

    // A new slice is due. In passive mode, first sweep whatever the previous
    // passive slice left unfilled.
    if (cfg_.passive) cancel_side(ctx, cfg_.side);
    while (slices_sent_ < due) {
        Qty q = slice_qty(slices_sent_++);
        if (q <= 0) continue;
        if (cfg_.passive && slices_sent_ < cfg_.num_slices) {
            Price px = (cfg_.side == Side::Bid) ? book.best_bid()->price : book.best_ask()->price;
            OrderId id = ctx.submit_limit(cfg_.side, px, q, TimeInForce::PostOnly);
            if (id) passive_qty_[id] = q;
            else    ctx.submit_market(cfg_.side, q);
        } else {
            ctx.submit_market(cfg_.side, q);
        }
        sent_qty_ += q;
    }
}
