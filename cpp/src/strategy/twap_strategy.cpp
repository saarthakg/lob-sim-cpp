#include "../../include/strategy/twap_strategy.hpp"
#include <algorithm>

TwapStrategy::TwapStrategy(Config config) : config_(config) {}
TwapStrategy::TwapStrategy() : TwapStrategy(Config{}) {}

Qty TwapStrategy::slice_qty(int index) const {
    Qty base      = config_.total_qty / config_.num_slices;
    Qty remainder = config_.total_qty - base * config_.num_slices;
    // Put any remainder (from integer division) into the final slice so the
    // full total_qty is always executed, not silently truncated.
    return base + (index == config_.num_slices - 1 ? remainder : 0);
}

void TwapStrategy::on_market_event(Timestamp ts, const OrderBook& book,
                                    ExchangeSim& exchange, Portfolio& portfolio) {
    (void)portfolio;
    if (finished() || config_.num_slices <= 0 || config_.total_qty <= 0) return;

    auto bid_opt = book.best_bid();
    auto ask_opt = book.best_ask();
    if (!bid_opt || !ask_opt) return;

    if (!started_) {
        first_ts_ = ts;
        started_  = true;
    }

    // How many slices should have fired by now, given elapsed time since the
    // schedule started? Fire every slice whose scheduled time has passed —
    // usually just one per call, but this catches up if events are sparse.
    int64_t elapsed = ts - first_ts_;
    int due_slices = static_cast<int>(elapsed / config_.interval_us) + 1;
    due_slices = std::min(due_slices, config_.num_slices);

    while (slices_sent_ < due_slices) {
        Qty qty = slice_qty(slices_sent_);
        if (qty > 0) {
            OrderId oid = exchange.next_order_id();
            SimOrder order{};
            order.order_id      = oid;
            order.ts            = ts;
            order.side          = config_.side;
            order.type          = OrderType::Market;
            order.price         = 0;
            order.qty           = qty;
            order.strategy_name = name();
            exchange.submit_market_order(order, book, portfolio);
        }
        ++slices_sent_;
    }
}
