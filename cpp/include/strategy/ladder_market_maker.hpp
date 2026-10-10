#pragma once
#include "quote_manager.hpp"
#include "strategy.hpp"

// Queue-priority ladder market maker for *large-tick* contracts — books where
// the spread is pinned at one tick and the tick itself is wide relative to fees
// (e.g. ATOMUSDT at ~6 bp/tick). In such books the price rarely moves and the
// economics are about queue position (Moallemi & Yuan, "A model for queue
// position valuation in a limit order book"): an order at the *back* of a
// long queue only fills when the whole level is being swept — i.e. right
// before the price ticks through it — so it is adversely selected almost by
// construction. An order near the *front* gets filled by ordinary two-way flow
// and earns the spread.
//
// The only way to be near the front is to be early. So instead of chasing the
// touch, this strategy keeps a ladder of resting orders at fixed prices from
// the touch to `levels` ticks behind it, and leaves them alone as the market
// moves: when the price comes to them, they have priority. An order is only
// cancelled when it drifts out of the ladder, or when it is at the touch and
// the book-imbalance alpha says that level is about to be run over (Cont,
// Kukanov & Stoikov 2014; Stoikov 2018 — on these contracts touch imbalance
// predicts the next 1–5 s move with an out-of-sample correlation of ~0.2–0.3).
//
//   alpha (ticks) = alpha_micro·(weighted_mid − mid) + alpha_obi·depth_imbalance
//   adj           = alpha − inventory_skew·(position / order_qty)
//   touch bid is pulled while adj < −pull_threshold; touch ask while adj > +pull_threshold
//
// Book snapshots arrive only every 100 ms, but trade prints arrive one by one.
// `depletion_pull` uses them as the fast signal: once prints at our touch
// price have eaten more than that fraction of the level's last displayed size,
// the level is being swept and our touch order is cancelled immediately
// (subject to entry latency) instead of waiting for the next book update.
class LadderMarketMaker : public Strategy {
public:
    struct Config {
        Qty     order_qty       = 1;
        Qty     max_position    = 10;
        int     levels          = 3;        // ticks of ladder per side, touch included
        double  alpha_micro     = 0.0;
        double  alpha_obi       = 0.0;
        double  obi_band_bps    = 5.0;
        double  pull_threshold  = 0.1;      // ticks of adverse predicted move that pulls the touch quote
        double  inventory_skew  = 0.0;      // ticks of alpha per order-size unit of inventory
        int     max_spread_ticks = 1;       // only make markets when the book is this tight
        double  depletion_pull  = 0.0;      // 0 = off; e.g. 0.5 = pull after half the level traded
        int64_t interval_us     = 100'000;   // 0 = requote on every book update
    };

    LadderMarketMaker() = default;
    explicit LadderMarketMaker(Config cfg) : cfg_(cfg), guard_(cfg.depletion_pull) {}

    std::string name() const override { return "LadderMarketMaker"; }
    int64_t timer_interval_us() const override { return cfg_.interval_us; }
    void on_timer(StrategyContext& ctx) override;
    void on_book(StrategyContext& ctx) override;
    void on_trade(StrategyContext& ctx, const TradePrint& tr) override;

private:
    Config cfg_;
    DepletionGuard guard_;
};
