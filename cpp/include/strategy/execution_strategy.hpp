#pragma once
#include "strategy.hpp"
#include <cstdio>
#include <string>

// Executes a long series of parent orders with one algorithm and logs each
// one's cost, so execution algorithms can be compared on hundreds of parents
// per day instead of one. Parent k starts at first_event + k·(horizon + gap)
// and alternates buy/sell; the schedule doesn't depend on the algorithm, so
// two runs over the same data are paired parent by parent.
//
// A parent is cut into `slices` equal pieces; piece j must be done by the end
// of its time slot, and whatever is not is bought/sold with a market order.
//
//   Cross   cross the spread at the start of every slot (aggressive TWAP)
//   Join    post at the near touch at the start of the slot, leave it, sweep
//           the remainder at the end of the slot
//   Peg     like Join, but follow the touch when the market moves away
//   Smart   like Peg, but cross immediately while touch imbalance says the
//           price is about to move away from us (buying into a bid-heavy book)
//
// At most one child order is alive at a time, and nothing new is sent until
// the previous one's cancel is confirmed: a parent can never be overfilled.
class ExecutionStrategy : public Strategy {
public:
    enum class Algo { Cross, Join, Peg, Smart };
    struct Config {
        Algo    algo          = Algo::Cross;
        Qty     parent_qty    = 20;
        int     slices        = 20;
        int64_t horizon_us    = 300'000'000;   // 5 min per parent
        int64_t gap_us        = 60'000'000;
        double  cross_imbalance = 0.6;         // Smart: cross above this touch imbalance in our direction
        std::string csv_path;                  // one row per completed parent
    };
    struct Parent {
        int       id = 0;
        Side      side = Side::Bid;
        Timestamp start = 0, end = 0;
        double    arrival_mid = 0.0;    // ticks
        double    arrival_spread = 0.0; // ticks
        Qty       filled = 0, filled_maker = 0;
        double    cost = 0.0;           // Σ price_ticks · qty
        double    fees = 0.0;           // quote ccy
    };

    explicit ExecutionStrategy(Config cfg);
    ~ExecutionStrategy() override;

    std::string name() const override { return "ExecutionStrategy"; }
    int64_t timer_interval_us() const override { return 100'000; }
    void on_timer(StrategyContext& ctx) override { step(ctx); }
    void on_book(StrategyContext& ctx) override { if (active_) step(ctx); }
    void on_order_update(StrategyContext& ctx, const ExecutionReport& rep) override;

    const std::vector<Parent>& done() const { return done_; }

private:
    Config cfg_;
    FILE*  out_ = nullptr;
    Timestamp t0_ = 0;
    int    next_id_ = 0;
    bool   active_ = false;
    Parent p_;
    std::vector<Parent> done_;
    OrderId child_ = 0;       // the one live child, 0 if none
    bool    child_cancelling_ = false;
    Price   child_px_ = 0;
    bool    child_is_market_ = false;

    Qty  due(int slots) const;   // cumulative quantity due after `slots` slots
    void step(StrategyContext& ctx);
    void finish(StrategyContext& ctx);
};
