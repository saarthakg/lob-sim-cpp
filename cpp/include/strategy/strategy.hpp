#pragma once
#include "../core/order_book.hpp"
#include "../core/types.hpp"
#include "../marketdata/market_event.hpp"
#include "../sim/execution_report.hpp"
#include "../sim/sim_order.hpp"
#include <string>
#include <unordered_map>

// What a strategy believes about one of its orders. This is the *local* view:
// it lags the exchange by the response latency, so an order the strategy
// thinks is open may already be filled (and a cancel for it will be rejected).
struct LocalOrder {
    OrderId     id      = 0;
    Side        side    = Side::Bid;
    OrderType   type    = OrderType::Limit;
    Price       price   = 0;
    Qty         qty     = 0;
    Qty         filled  = 0;
    bool        acked   = false;   // exchange confirmed it is resting
    bool        cancel_sent = false;
    Timestamp   sent_ts = 0;
    Qty leaves() const { return qty - filled; }
};

// The strategy's window onto the simulation. Everything here is what a real
// trading process could know at local time now(): a market-data book delayed
// by feed latency, and order/position state delayed by response latency.
class StrategyContext {
public:
    virtual ~StrategyContext() = default;

    virtual Timestamp          now() const = 0;
    virtual const OrderBook&   book() const = 0;
    virtual const Instrument&  instrument() const = 0;
    virtual Qty                position() const = 0;  // lots, as known locally
    virtual double             maker_fee() const = 0;
    virtual double             taker_fee() const = 0;
    virtual const std::unordered_map<OrderId, LocalOrder>& orders() const = 0;

    // Returns 0 if refused locally by the risk gateway (position limit or
    // self-cross). Otherwise the order is in flight to the exchange.
    virtual OrderId submit_limit(Side side, Price px, Qty qty,
                                 TimeInForce tif = TimeInForce::PostOnly) = 0;
    virtual OrderId submit_market(Side side, Qty qty) = 0;
    virtual void    cancel(OrderId id) = 0;
};

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual std::string name() const = 0;

    virtual void on_start(StrategyContext&) {}
    virtual void on_book(StrategyContext&) {}                         // local book changed
    virtual void on_trade(StrategyContext&, const TradePrint&) {}     // public trade print
    virtual void on_order_update(StrategyContext&, const ExecutionReport&) {}
    virtual void on_timer(StrategyContext&) {}
    // 0 = no timer. Timer fires on a fixed grid of local time.
    virtual int64_t timer_interval_us() const { return 0; }
    virtual void on_finish(StrategyContext&) {}
};
