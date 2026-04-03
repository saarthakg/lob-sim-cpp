#include "../../include/sim/exchange_sim.hpp"
#include <algorithm>
#include <cmath>

ExchangeSim::ExchangeSim(LatencyModel latency, double taker_fee, double maker_fee)
    : latency_(latency), taker_fee_(taker_fee), maker_fee_(maker_fee) {}

OrderId ExchangeSim::next_order_id() { return next_id_++; }

std::vector<ExecutionReport> ExchangeSim::submit_market_order(
    const SimOrder&  order,
    const OrderBook& book,
    Portfolio&       portfolio) {

    std::vector<ExecutionReport> reports;
    Qty remaining = order.qty;
    int64_t total_cost = 0;
    Qty total_filled   = 0;

    // Walk the book levels
    auto levels = (order.side == Side::Bid)
        ? book.top_n_asks(20)   // buying: walk asks
        : book.top_n_bids(20);  // selling: walk bids

    for (const auto& lvl : levels) {
        if (remaining <= 0) break;
        Qty fill_qty = std::min(remaining, lvl.qty);
        total_cost  += lvl.price * fill_qty;
        total_filled += fill_qty;
        remaining    -= fill_qty;
    }

    if (total_filled == 0) {
        // No liquidity
        ExecutionReport rep{};
        rep.order_id     = order.order_id;
        rep.ts           = order.ts;
        rep.side         = order.side;
        rep.status       = OrderStatus::Rejected;
        rep.fill_price   = 0;
        rep.fill_qty     = 0;
        rep.remaining_qty = order.qty;
        rep.fee_paid     = 0.0;
        rep.strategy_name = order.strategy_name;
        reports.push_back(rep);
        return reports;
    }

    Price avg_price = total_cost / total_filled;
    double notional = static_cast<double>(avg_price) * static_cast<double>(total_filled);
    double fee      = notional * taker_fee_;

    portfolio.on_fill(order.side, avg_price, total_filled, fee);

    ExecutionReport rep{};
    rep.order_id      = order.order_id;
    rep.ts            = order.ts;
    rep.side          = order.side;
    rep.status        = (remaining > 0) ? OrderStatus::PartiallyFilled : OrderStatus::Filled;
    rep.fill_price    = avg_price;
    rep.fill_qty      = total_filled;
    rep.remaining_qty = remaining;
    rep.fee_paid      = fee;
    rep.strategy_name = order.strategy_name;
    reports.push_back(rep);
    pending_fills_.push_back(rep);  // also accumulate for drain_fills()
    return reports;
}

OrderId ExchangeSim::submit_limit_order(const SimOrder& order, const OrderBook& book) {
    PassiveOrderState state;
    state.order              = order;
    state.qty_ahead          = book.visible_qty_at_price(order.side, order.price);
    state.level_qty_at_entry = state.qty_ahead;
    passive_orders_[order.order_id] = state;
    return order.order_id;
}

void ExchangeSim::cancel_order(OrderId order_id) {
    passive_orders_.erase(order_id);
}

std::vector<ExecutionReport> ExchangeSim::on_market_event(
    const BinanceDepthEvent& event,
    const OrderBook&         book,
    Portfolio&               portfolio) {

    std::vector<ExecutionReport> reports;
    (void)book;
    advance_passive_orders(event, book, portfolio, reports);
    return reports;
}

void ExchangeSim::advance_passive_orders(
    const BinanceDepthEvent& event,
    const OrderBook&         book,
    Portfolio&               portfolio,
    std::vector<ExecutionReport>& reports) {

    // Build a map of price-level quantity changes from the event
    // delta = new_qty - old_qty; if negative, queue at that level shrank
    // We approximate: if visible qty at a bid/ask level decreased, orders ahead shrink
    std::unordered_map<Price, Qty> bid_new_qty, ask_new_qty;
    for (const auto& lvl : event.bid_updates) bid_new_qty[lvl.price] = lvl.qty;
    for (const auto& lvl : event.ask_updates) ask_new_qty[lvl.price] = lvl.qty;

    std::vector<OrderId> to_erase;

    for (auto& [oid, state] : passive_orders_) {
        const auto& ord = state.order;
        auto& qty_ahead = state.qty_ahead;

        // Look up this price level in the event updates
        auto& side_map = (ord.side == Side::Bid) ? bid_new_qty : ask_new_qty;
        auto it = side_map.find(ord.price);

        if (it != side_map.end()) {
            Qty new_qty = it->second;
            if (new_qty == 0) {
                // Level deleted — cancel passive order (level gone)
                ExecutionReport rep{};
                rep.order_id      = oid;
                rep.ts            = event.event_time;
                rep.side          = ord.side;
                rep.status        = OrderStatus::Cancelled;
                rep.fill_price    = 0;
                rep.fill_qty      = 0;
                rep.remaining_qty = ord.qty;
                rep.fee_paid      = 0.0;
                rep.strategy_name = ord.strategy_name;
                reports.push_back(rep);
                pending_fills_.push_back(rep);
                to_erase.push_back(oid);
            } else {
                // Level qty changed: compute decrease and subtract from qty_ahead
                // Approximate: if visible qty at our level shrank, our queue position shrank too
                if (new_qty < qty_ahead) {
                    qty_ahead = new_qty;  // snap to new visible qty
                }
            }
        }

        // Check if we should fill: qty_ahead <= 0
        if (qty_ahead <= 0) {
            Qty fill_qty = ord.qty;
            double notional = static_cast<double>(ord.price) * static_cast<double>(fill_qty);
            double fee      = notional * maker_fee_;

            portfolio.on_fill(ord.side, ord.price, fill_qty, fee);

            ExecutionReport rep{};
            rep.order_id      = oid;
            rep.ts            = event.event_time;
            rep.side          = ord.side;
            rep.status        = OrderStatus::Filled;
            rep.fill_price    = ord.price;
            rep.fill_qty      = fill_qty;
            rep.remaining_qty = 0;
            rep.fee_paid      = fee;
            rep.strategy_name = ord.strategy_name;
            reports.push_back(rep);
            to_erase.push_back(oid);
        }
    }

    for (auto oid : to_erase) passive_orders_.erase(oid);
}

size_t ExchangeSim::active_order_count() const { return passive_orders_.size(); }

std::vector<ExecutionReport> ExchangeSim::drain_fills() {
    std::vector<ExecutionReport> out;
    std::swap(out, pending_fills_);
    return out;
}
