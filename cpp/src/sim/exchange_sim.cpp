#include "../../include/sim/exchange_sim.hpp"
#include "../../include/core/types.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

ExchangeSim::ExchangeSim(LatencyModel latency, double taker_fee, double maker_fee,
                         Qty max_abs_position)
    : latency_(latency), taker_fee_(taker_fee), maker_fee_(maker_fee),
      max_abs_position_(max_abs_position) {}

OrderId ExchangeSim::next_order_id() { return next_id_++; }

bool ExchangeSim::breaches_risk_limit(Side side, Qty qty, const Portfolio& portfolio) const {
    if (max_abs_position_ <= 0) return false;  // risk check disabled
    Qty projected = portfolio.position() + (side == Side::Bid ? qty : -qty);
    return std::llabs(static_cast<long long>(projected)) > max_abs_position_;
}

bool ExchangeSim::would_self_cross(const SimOrder& order) const {
    auto crosses = [&](const SimOrder& resting) {
        if (resting.strategy_name != order.strategy_name) return false;
        if (resting.side == order.side) return false;
        return (order.side == Side::Bid) ? (order.price >= resting.price)
                                          : (order.price <= resting.price);
    };
    for (const auto& [oid, state] : passive_orders_) {
        (void)oid;
        if (crosses(state.order)) return true;
    }
    for (const auto& pending : pending_limit_orders_) {
        if (crosses(pending.order)) return true;
    }
    return false;
}

ExecutionReport ExchangeSim::execute_market_order_now(
    const SimOrder& order, const OrderBook& book, Portfolio& portfolio) {

    Qty remaining = order.qty;
    int64_t total_cost = 0;
    Qty total_filled   = 0;

    // Walk the full visible book depth on the contra side.
    auto levels = (order.side == Side::Bid)
        ? book.top_n_asks(static_cast<int>(book.ask_levels()))   // buying: walk asks
        : book.top_n_bids(static_cast<int>(book.bid_levels()));  // selling: walk bids

    for (const auto& lvl : levels) {
        if (remaining <= 0) break;
        Qty fill_qty = std::min(remaining, lvl.qty);
        total_cost  += lvl.price * fill_qty;
        total_filled += fill_qty;
        remaining    -= fill_qty;
    }

    ExecutionReport rep{};
    rep.order_id      = order.order_id;
    rep.ts            = order.ts;
    rep.side          = order.side;
    rep.order_type    = order.type;
    rep.strategy_name = order.strategy_name;

    if (total_filled == 0) {
        // No liquidity
        rep.status        = OrderStatus::Rejected;
        rep.fill_price    = 0;
        rep.fill_qty      = 0;
        rep.remaining_qty = order.qty;
        rep.fee_paid      = 0.0;
        return rep;
    }

    Price avg_price = total_cost / total_filled;
    double notional = (static_cast<double>(avg_price) / TICK_MULTIPLIER)
                    * (static_cast<double>(total_filled) / TICK_MULTIPLIER);
    double fee      = notional * taker_fee_;  // USD

    portfolio.on_fill(order.side, avg_price, total_filled, fee);

    rep.status        = (remaining > 0) ? OrderStatus::PartiallyFilled : OrderStatus::Filled;
    rep.fill_price    = avg_price;
    rep.fill_qty      = total_filled;
    rep.remaining_qty = remaining;
    rep.fee_paid      = fee;
    return rep;
}

std::vector<ExecutionReport> ExchangeSim::submit_market_order(
    const SimOrder&  order,
    const OrderBook& book,
    Portfolio&       portfolio) {

    if (breaches_risk_limit(order.side, order.qty, portfolio)) {
        ExecutionReport rep{};
        rep.order_id      = order.order_id;
        rep.ts            = order.ts;
        rep.side          = order.side;
        rep.order_type    = order.type;
        rep.status        = OrderStatus::Rejected;
        rep.fill_price    = 0;
        rep.fill_qty      = 0;
        rep.remaining_qty = order.qty;
        rep.fee_paid      = 0.0;
        rep.strategy_name = order.strategy_name;
        return {rep};
    }

    if (latency_.total_us() == 0) {
        ExecutionReport rep = execute_market_order_now(order, book, portfolio);
        if (rep.status != OrderStatus::Rejected) pending_fills_.push_back(rep);
        return {rep};
    }

    // Defer: order arrives and is matched against a later book state.
    pending_market_orders_.push_back({order, order.ts + latency_.total_us()});
    return {};
}

void ExchangeSim::register_passive_order_now(const SimOrder& order, const OrderBook& book) {
    PassiveOrderState state;
    state.order              = order;
    state.qty_ahead          = book.visible_qty_at_price(order.side, order.price);
    state.level_qty_at_entry = state.qty_ahead;
    passive_orders_[order.order_id] = state;
}

OrderId ExchangeSim::submit_limit_order(const SimOrder& order, const OrderBook& book,
                                        const Portfolio& portfolio) {
    if (would_self_cross(order)) return 0;
    if (breaches_risk_limit(order.side, order.qty, portfolio)) return 0;

    if (latency_.total_us() == 0) {
        register_passive_order_now(order, book);
    } else {
        pending_limit_orders_.push_back({order, order.ts + latency_.total_us()});
    }
    return order.order_id;
}

void ExchangeSim::cancel_order(OrderId order_id) {
    passive_orders_.erase(order_id);
    pending_limit_orders_.erase(
        std::remove_if(pending_limit_orders_.begin(), pending_limit_orders_.end(),
            [&](const PendingLimitOrder& p) { return p.order.order_id == order_id; }),
        pending_limit_orders_.end());
}

std::vector<ExecutionReport> ExchangeSim::on_market_event(
    const BinanceDepthEvent& event,
    const OrderBook&         book,
    Portfolio&               portfolio) {

    std::vector<ExecutionReport> reports;
    advance_passive_orders(event, book, portfolio, reports);
    process_pending_orders(event, book, portfolio, reports);
    return reports;
}

void ExchangeSim::process_pending_orders(
    const BinanceDepthEvent& event,
    const OrderBook&         book,
    Portfolio&               portfolio,
    std::vector<ExecutionReport>& reports) {

    for (auto it = pending_market_orders_.begin(); it != pending_market_orders_.end();) {
        if (it->arrival_ts <= event.event_time) {
            ExecutionReport rep = execute_market_order_now(it->order, book, portfolio);
            reports.push_back(rep);
            if (rep.status != OrderStatus::Rejected) pending_fills_.push_back(rep);
            it = pending_market_orders_.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = pending_limit_orders_.begin(); it != pending_limit_orders_.end();) {
        if (it->arrival_ts <= event.event_time) {
            register_passive_order_now(it->order, book);
            it = pending_limit_orders_.erase(it);
        } else {
            ++it;
        }
    }
}

void ExchangeSim::advance_passive_orders(
    const BinanceDepthEvent& event,
    const OrderBook&         book,
    Portfolio&               portfolio,
    std::vector<ExecutionReport>& reports) {
    (void)book;

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

        // Look up this price level in the event updates. A passive order can
        // only fill or be cancelled because of activity at its OWN price
        // level — if this event didn't touch that level, skip it entirely.
        // (Previously the qty_ahead<=0 fill check below ran unconditionally
        // for every resting order on every event; a freshly-placed order at
        // a price with no prior displayed depth starts with qty_ahead == 0,
        // so it would fill on the very next *unrelated* market event anywhere
        // in the book. In practice this made MarketMaker's bid and ask both
        // fill simultaneously on almost every quote cycle — a risk-free
        // spread capture that never actually held inventory. Gating on
        // `it != side_map.end()` requires genuine activity at the order's
        // exact price before it can fill, which is the minimum realistic bar
        // for an L2-only approximation.)
        auto& side_map = (ord.side == Side::Bid) ? bid_new_qty : ask_new_qty;
        auto it = side_map.find(ord.price);
        if (it == side_map.end()) continue;

        Qty new_qty = it->second;
        if (new_qty == 0) {
            // Level deleted — cancel passive order (level gone)
            ExecutionReport rep{};
            rep.order_id      = oid;
            rep.ts            = event.event_time;
            rep.side          = ord.side;
            rep.order_type    = ord.type;
            rep.status        = OrderStatus::Cancelled;
            rep.fill_price    = 0;
            rep.fill_qty      = 0;
            rep.remaining_qty = ord.qty;
            rep.fee_paid      = 0.0;
            rep.strategy_name = ord.strategy_name;
            reports.push_back(rep);
            pending_fills_.push_back(rep);
            to_erase.push_back(oid);
            continue;
        }

        // Level qty changed: compute decrease and subtract from qty_ahead
        // Approximate: if visible qty at our level shrank, our queue position shrank too
        if (new_qty < qty_ahead) {
            qty_ahead = new_qty;  // snap to new visible qty
        }

        // Check if we should fill: qty_ahead <= 0
        if (qty_ahead <= 0) {
            Qty fill_qty = ord.qty;
            double notional = (static_cast<double>(ord.price) / TICK_MULTIPLIER)
                            * (static_cast<double>(fill_qty)  / TICK_MULTIPLIER);
            double fee      = notional * maker_fee_;  // USD

            portfolio.on_fill(ord.side, ord.price, fill_qty, fee);

            ExecutionReport rep{};
            rep.order_id      = oid;
            rep.ts            = event.event_time;
            rep.side          = ord.side;
            rep.order_type    = ord.type;
            rep.status        = OrderStatus::Filled;
            rep.fill_price    = ord.price;
            rep.fill_qty      = fill_qty;
            rep.remaining_qty = 0;
            rep.fee_paid      = fee;
            rep.strategy_name = ord.strategy_name;
            reports.push_back(rep);
            pending_fills_.push_back(rep);
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
