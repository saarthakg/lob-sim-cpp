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

// An order that has been accepted (passed risk/self-cross checks) but has not
// yet "arrived" at the exchange because LatencyModel::total_us() > 0. It sits
// here until a market event's timestamp reaches arrival_ts, at which point it
// is matched/registered against the book as it exists *then* — not as it was
// at decision time. This is what makes latency actually affect fills.
struct PendingMarketOrder {
    SimOrder  order;
    Timestamp arrival_ts;
};

struct PendingLimitOrder {
    SimOrder  order;
    Timestamp arrival_ts;
};

class ExchangeSim {
public:
    // max_abs_position == 0 disables the pre-trade position risk check.
    explicit ExchangeSim(LatencyModel latency = {},
                         double taker_fee = 0.0004,
                         double maker_fee = 0.0002,
                         Qty    max_abs_position = 0);

    // Submit a market order. If LatencyModel::total_us() == 0 (the default), it
    // is matched immediately against `book` and the fill report is returned
    // synchronously. If latency > 0, the order is queued and actually matched
    // (against a later book state) during a subsequent on_market_event() call;
    // this method then returns an empty vector — collect the eventual report
    // via drain_fills() / on_market_event()'s return value instead.
    // Rejected pre-trade (no liquidity, or would breach the position risk limit)
    // orders are always reported synchronously, regardless of latency.
    std::vector<ExecutionReport> submit_market_order(
        const SimOrder&  order,
        const OrderBook& book,
        Portfolio&       portfolio);

    // Place a passive limit order; tracked internally.
    // Rejected (order_id 0 returned, never issued by next_order_id()) if the
    // order would cross one of this strategy's own resting/in-flight orders,
    // or would breach the position risk limit if it were later fully filled.
    // Registration into the queue-position tracker is itself delayed by
    // LatencyModel::total_us() — qty_ahead is captured from the book at
    // arrival time, not submission time.
    OrderId submit_limit_order(const SimOrder& order, const OrderBook& book,
                               const Portfolio& portfolio);

    void cancel_order(OrderId order_id);

    // Call on every market event to advance passive order queue simulation
    // and to process any pending (in-flight) orders that have now arrived.
    std::vector<ExecutionReport> on_market_event(
        const BinanceDepthEvent& event,
        const OrderBook&         book,
        Portfolio&               portfolio);

    size_t active_order_count() const;   // resting passive orders only
    OrderId next_order_id();

    // Returns and clears all fills accumulated since the last call.
    // Includes fills from submit_market_order() and passive fills.
    std::vector<ExecutionReport> drain_fills();

private:
    std::vector<ExecutionReport> pending_fills_;
    LatencyModel latency_;
    double       taker_fee_;
    double       maker_fee_;
    Qty          max_abs_position_;
    OrderId      next_id_ = 1;
    std::unordered_map<OrderId, PassiveOrderState> passive_orders_;
    std::vector<PendingMarketOrder> pending_market_orders_;
    std::vector<PendingLimitOrder>  pending_limit_orders_;

    void advance_passive_orders(
        const BinanceDepthEvent& event,
        const OrderBook&         book,
        Portfolio&               portfolio,
        std::vector<ExecutionReport>& reports);

    void process_pending_orders(
        const BinanceDepthEvent& event,
        const OrderBook&         book,
        Portfolio&               portfolio,
        std::vector<ExecutionReport>& reports);

    // Matches `order` against `book` right now and returns the resulting
    // report. Does not touch pending_fills_ — callers decide whether/when
    // to accumulate it (immediate callers do; latency-deferred ones do too,
    // just later).
    ExecutionReport execute_market_order_now(
        const SimOrder& order, const OrderBook& book, Portfolio& portfolio);

    // Captures qty_ahead from `book` right now and starts tracking `order`
    // as a resting passive order.
    void register_passive_order_now(const SimOrder& order, const OrderBook& book);

    bool would_self_cross(const SimOrder& order) const;
    bool breaches_risk_limit(Side side, Qty qty, const Portfolio& portfolio) const;
};
