#pragma once
#include "estimators.hpp"
#include "quote_manager.hpp"
#include "strategy.hpp"
#include <cstdio>
#include <memory>

// Inventory-aware market maker after Guéant, Lehalle & Fernandez-Tapia (2013)
// "Dealing with the inventory risk", using the closed-form asymptotic quotes
// in the form popularised by the hftbacktest GLFT tutorial:
//
//   c1 = (1/ξΔ)·ln(1 + ξΔ/k)
//   c2 = sqrt( γ/(2AΔk) · (1 + ξΔ/k)^(k/(ξΔ)+1) )
//   half_spread = c1 + (Δ/2)·σ·c2        skew = σ·c2         (ξ = γ, Δ = 1)
//   bid = fair − (half_spread + skew·q + fee)    ask = fair + (half_spread − skew·q + fee)
//
// with q = position / order_qty, σ in ticks/√s and λ(δ) = A·e^{−kδ} fitted
// online from public trades (see estimators.hpp). Additions beyond the paper:
//
//  * fee — the maker fee in ticks is added to each side's depth: with a
//    per-fill cost c the optimal exponential-intensity quote shifts by c. At
//    VIP0 on BTCUSDT this is ~170 ticks, which is the whole story of why
//    retail-fee market making there doesn't work; with a rebate it is
//    negative and pulls quotes to the touch.
//  * alpha — fair = mid + α, α = alpha_micro·(weighted_mid − mid)
//    + alpha_obi·depth_imbalance (ticks). Coefficients are meant to be fitted
//    offline by regressing future mid changes on these signals
//    (python/analysis/signal_research.py), not hand-tuned.
//  * grid — `levels` orders per side spaced `max(1, round(half_spread))`
//    ticks apart; only orders whose price leaves the grid are cancelled.
class GlftMarketMaker : public Strategy {
public:
    struct Config {
        double  gamma          = 0.05;     // risk aversion (per unit of q)
        double  half_spread_mult = 1.0;    // scales c-derived half spread
        double  skew_mult      = 1.0;      // scales inventory skew
        Qty     order_qty      = 1;        // lots per quote
        Qty     max_position   = 10;       // lots; stop quoting the growing side beyond
        int     levels         = 1;        // quotes per side
        bool    include_fee    = true;
        double  alpha_micro    = 0.0;      // weight on (weighted_mid - mid), ticks/tick
        double  alpha_obi      = 0.0;      // ticks per unit depth imbalance
        double  obi_band_bps   = 5.0;      // depth imbalance band around mid
        double  min_half_spread_ticks = 0.0;   // floor on c-derived half spread (before fee)
        int64_t quote_interval_us = 100'000;   // requote cadence
        int64_t calib_interval_us = 5'000'000; // re-fit A, k, σ every …
        size_t  window_steps   = 6000;     // 10 min of 100 ms steps
        int64_t warmup_us      = 600'000'000;  // don't quote before the window has data
        int     max_depth_ticks = 50;      // λ(δ) fit range
        double  depletion_pull = 0.0;      // see DepletionGuard; 0 = off
        std::string log_path;              // optional calibration log CSV
    };

    GlftMarketMaker();
    explicit GlftMarketMaker(Config cfg);
    ~GlftMarketMaker() override;

    std::string name() const override { return "GlftMarketMaker"; }
    int64_t timer_interval_us() const override { return cfg_.quote_interval_us; }
    void on_start(StrategyContext& ctx) override;
    void on_trade(StrategyContext& ctx, const TradePrint& tr) override;
    void on_book(StrategyContext& ctx) override { (void)ctx; guard_.on_book(); }
    void on_timer(StrategyContext& ctx) override;

    double A() const { return A_; }
    double k() const { return k_; }
    double sigma() const { return sigma_; }
    bool   calibrated() const { return calibrated_; }

    // Closed-form GLFT coefficients (exposed for tests).
    static void glft_coefficients(double gamma, double A, double k, double sigma,
                                  double& half_spread, double& skew);

private:
    Config cfg_;
    VolatilityEstimator vol_;
    IntensityEstimator  inten_;
    double A_ = 0.0, k_ = 0.0, sigma_ = 0.0;
    double half_ = 0.0, skew_ = 0.0;
    bool   calibrated_ = false;
    Timestamp start_ts_ = 0, next_calib_ = 0;
    FILE* log_ = nullptr;
    DepletionGuard guard_;

    void calibrate(StrategyContext& ctx);
    void quote(StrategyContext& ctx);
};
