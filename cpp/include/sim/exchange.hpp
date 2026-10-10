#pragma once
#include "../core/order_book.hpp"
#include "../marketdata/market_event.hpp"
#include "execution_report.hpp"
#include "portfolio.hpp"
#include "sim_order.hpp"
#include <vector>

// The simulated venue our orders trade against. The Backtester drives it in
// exchange time; implementations differ in how much they can know about queue
// priority:
//   ExchangeSim    — L2 data: queue position estimated with a queue model.
//   L3ExchangeSim  — L3 data: our orders sit in the real FIFO, exact priority.
class Exchange {
public:
    virtual ~Exchange() = default;

    // Order / cancel arriving at `ts` (after entry latency). `l2` is the
    // replayed market book at that moment.
    virtual void on_new_order(const OrderRequest& req, Timestamp ts, const OrderBook& l2,
                              Portfolio& pf, std::vector<ExecutionReport>& out) = 0;
    virtual void on_cancel(OrderId id, Timestamp ts, std::vector<ExecutionReport>& out) = 0;

    // One market event. For depth events `l2` already reflects it.
    virtual void on_market(const MarketEvent& ev, const OrderBook& l2, Portfolio& pf,
                           std::vector<ExecutionReport>& out) = 0;

    virtual size_t open_order_count() const = 0;
    // Displayed quantity ahead of a resting order (lots), or -1 if unknown.
    virtual double queue_ahead(OrderId id) const = 0;
    virtual void   resting_sides(bool& has_bid, bool& has_ask) const = 0;
};
