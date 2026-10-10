#pragma once
#include "strategy.hpp"
#include <unordered_map>

// TWAP execution: slice `total_qty` into `num_slices` child orders fired every
// `interval_us`. An execution algorithm, not an alpha strategy — judge it by
// implementation shortfall (average fill vs. arrival mid), not PnL.
//
// passive = true posts each slice at the near touch and only crosses the
// spread for whatever is still unfilled when the next slice is due, which
// typically saves most of the half-spread + (taker − maker) fee.
class TwapStrategy : public Strategy {
public:
    struct Config {
        Side    side        = Side::Bid;
        Qty     total_qty   = 100;
        int     num_slices  = 20;
        int64_t interval_us = 60'000'000;
        bool    passive     = false;
    };

    TwapStrategy() = default;
    explicit TwapStrategy(Config cfg) : cfg_(cfg) {}

    std::string name() const override { return "TwapStrategy"; }
    int64_t timer_interval_us() const override { return 100'000; }
    void on_timer(StrategyContext& ctx) override;
    void on_order_update(StrategyContext& ctx, const ExecutionReport& rep) override;

    bool   finished()    const { return slices_sent_ >= cfg_.num_slices && filled_ >= cfg_.total_qty; }
    int    slices_sent() const { return slices_sent_; }
    Qty    filled()      const { return filled_; }
    double arrival_mid() const { return arrival_mid_; }   // ticks
    double avg_fill_px() const { return filled_ > 0 ? fill_cost_ / static_cast<double>(filled_) : 0.0; }

private:
    Config    cfg_;
    int       slices_sent_ = 0;
    Timestamp first_ts_    = 0;
    Qty       sent_qty_    = 0;
    Qty       filled_      = 0;
    double    fill_cost_   = 0.0;   // Σ price_ticks · qty
    double    arrival_mid_ = 0.0;
    std::unordered_map<OrderId, Qty> passive_qty_;

    Qty slice_qty(int index) const;
};
