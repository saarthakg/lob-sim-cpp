#pragma once
#include "strategy.hpp"

// Baseline: join the best bid and best ask with a fixed size, stop adding to
// a position beyond max_position. No volatility, intensity, inventory skew,
// fee awareness or alpha. This is what the original MarketMaker effectively
// was, and it is the control every smarter quoting rule has to beat: it
// captures the spread on paper and is adversely selected in practice.
class NaiveMarketMaker : public Strategy {
public:
    struct Config {
        Qty     order_qty         = 1;
        Qty     max_position      = 10;
        int     offset_ticks      = 0;        // 0 = join the touch; >0 = behind it
        int64_t quote_interval_us = 100'000;   // 0 = requote on every book update
    };
    NaiveMarketMaker() = default;
    explicit NaiveMarketMaker(Config cfg) : cfg_(cfg) {}

    std::string name() const override { return "NaiveMarketMaker"; }
    int64_t timer_interval_us() const override { return cfg_.quote_interval_us; }
    void on_timer(StrategyContext& ctx) override;
    void on_book(StrategyContext& ctx) override { if (cfg_.quote_interval_us == 0) on_timer(ctx); }

private:
    Config cfg_;
};
