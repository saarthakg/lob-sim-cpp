#pragma once
#include "strategy.hpp"

// Taker strategy on top-of-book imbalance. When the size-weighted imbalance
// at the touch exceeds a threshold it crosses the spread in that direction,
// then exits with a market order after `hold_us` (or immediately if the
// signal flips). It exists to measure whether a short-horizon book signal
// can pay for crossing the spread *and* the taker fee twice — the empirical
// answer on liquid perps is no, which is why the same signal is used inside
// the market maker (to skew passive quotes) instead.
//
// The original version only traded when flat and had no exit, so it opened
// one position and held it to the end of the data.
class ImbalanceStrategy : public Strategy {
public:
    struct Config {
        int     depth_levels   = 1;
        double  threshold      = 0.8;
        Qty     order_qty      = 1;
        int64_t hold_us        = 5'000'000;
        int64_t cooldown_us    = 1'000'000;
        int64_t interval_us    = 100'000;
    };
    ImbalanceStrategy() = default;
    explicit ImbalanceStrategy(Config cfg) : cfg_(cfg) {}

    std::string name() const override { return "ImbalanceStrategy"; }
    int64_t timer_interval_us() const override { return cfg_.interval_us; }
    void on_timer(StrategyContext& ctx) override;

private:
    Config    cfg_;
    Timestamp entry_ts_   = 0;
    Timestamp cooldown_until_ = 0;
    bool      pending_ = false;
};
