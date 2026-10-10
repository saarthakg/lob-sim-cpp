#include "../../include/strategy/imbalance_strategy.hpp"

void ImbalanceStrategy::on_timer(StrategyContext& ctx) {
    const OrderBook& book = ctx.book();
    if (book.empty()) return;
    // Don't act while a market order is still in flight.
    pending_ = false;
    for (const auto& [id, o] : ctx.orders()) { (void)id; if (o.type == OrderType::Market) pending_ = true; }
    if (pending_) return;

    const double imb = book.imbalance(cfg_.depth_levels);
    const Qty pos = ctx.position();
    const Timestamp now = ctx.now();

    if (pos != 0) {
        const bool flipped = (pos > 0 && imb < -cfg_.threshold) || (pos < 0 && imb > cfg_.threshold);
        if (flipped || now - entry_ts_ >= cfg_.hold_us) {
            ctx.submit_market(pos > 0 ? Side::Ask : Side::Bid, pos > 0 ? pos : -pos);
            cooldown_until_ = now + cfg_.cooldown_us;
        }
        return;
    }
    if (now < cooldown_until_) return;
    if (imb > cfg_.threshold) {
        if (ctx.submit_market(Side::Bid, cfg_.order_qty)) entry_ts_ = now;
    } else if (imb < -cfg_.threshold) {
        if (ctx.submit_market(Side::Ask, cfg_.order_qty)) entry_ts_ = now;
    }
}
