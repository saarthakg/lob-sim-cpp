#pragma once
#include "../core/order_book.hpp"
#include "../marketdata/binance_depth_event.hpp"
#include "sim_order.hpp"
#include "execution_report.hpp"
#include "portfolio.hpp"
#include "latency_model.hpp"
#include <vector>
#include <unordered_map>

struct PassiveOrderState {
    SimOrder order;
    Qty      qty_ahead;            // estimated queue position (L2 approximation)
    Qty      level_qty_at_entry;   // visible qty at this level when order was placed
};

class ExchangeSim {
public:
    explicit ExchangeSim(LatencyModel latency = {},
                         double taker_fee = 0.0004,
                         double maker_fee = 0.0002);

    // Execute a market order immediately against visible book levels.
    // Walks multiple levels if needed; partial fill if book is thin.
    std::vector<ExecutionReport> submit_market_order(
        const SimOrder&  order,
        const OrderBook& book,
        Portfolio&       portfolio);

    // Place a passive limit order; tracked internally.
    OrderId submit_limit_order(const SimOrder& order, const OrderBook& book);

    void cancel_order(OrderId order_id);

    // Call on every market event to advance passive order queue simulation.
    std::vector<ExecutionReport> on_market_event(
        const BinanceDepthEvent& event,
        const OrderBook&         book,
        Portfolio&               portfolio);

    size_t active_order_count() const;
    OrderId next_order_id();

    // Returns and clears all fills accumulated since the last call.
    // Includes fills from submit_market_order() and passive fills.
    std::vector<ExecutionReport> drain_fills();

private:
    std::vector<ExecutionReport> pending_fills_;
    LatencyModel latency_;
    double       taker_fee_;
    double       maker_fee_;
    OrderId      next_id_ = 1;
    std::unordered_map<OrderId, PassiveOrderState> passive_orders_;

    void advance_passive_orders(
        const BinanceDepthEvent& event,
        const OrderBook&         book,
        Portfolio&               portfolio,
        std::vector<ExecutionReport>& reports);
};
