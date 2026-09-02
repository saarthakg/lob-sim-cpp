#pragma once
#include "strategy.hpp"

// TWAP (Time-Weighted Average Price) execution strategy.
//
// Unlike MarketMaker (passive, continuous quoting) or ImbalanceStrategy
// (signal-triggered, opportunistic), TWAP is a scheduled execution algorithm:
// it slices a single large parent order into `num_slices` equal child market
// orders, fired at fixed `interval_us` intervals starting from the first
// event it sees. This is the standard baseline execution algo used to work
// a large order into the market with minimal signaling/impact, and it's
// deliberately indifferent to price — it trades on the clock, not on signal.
class TwapStrategy : public Strategy {
public:
    struct Config {
        Side    side        = Side::Bid;       // direction to execute
        Qty     total_qty   = 10000;           // total quantity (scaled units) to execute
        int     num_slices  = 20;              // number of child market orders
        int64_t interval_us = 1'000'000;       // time between slices (default: 1s)
    };

    TwapStrategy();
    explicit TwapStrategy(Config config);

    void on_market_event(Timestamp ts, const OrderBook& book,
                         ExchangeSim& exchange, Portfolio& portfolio) override;

    std::string name() const override { return "TwapStrategy"; }

    // True once all slices have been sent (no more child orders to place).
    bool finished() const { return slices_sent_ >= config_.num_slices; }
    int  slices_sent() const { return slices_sent_; }

private:
    Config    config_;
    int       slices_sent_ = 0;
    Timestamp first_ts_    = 0;
    bool      started_     = false;

    Qty slice_qty(int index) const;
};
