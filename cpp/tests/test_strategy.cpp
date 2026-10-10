#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "../include/sim/backtester.hpp"
#include "../include/strategy/estimators.hpp"
#include "../include/strategy/glft_market_maker.hpp"
#include "../include/strategy/imbalance_strategy.hpp"
#include "../include/strategy/twap_strategy.hpp"

#include <cmath>
#include <random>

using Catch::Approx;

static Instrument inst() {
    Instrument i;
    i.symbol = "TEST";
    i.tick_size = 0.1;
    i.lot_size = 0.001;
    return i;
}

TEST_CASE("GLFT closed form matches hand calculation", "[strategy]") {
    double hs, sk;
    const double g = 0.05, A = 2.0, k = 0.5, s = 3.0;
    GlftMarketMaker::glft_coefficients(g, A, k, s, hs, sk);
    double c1 = 1.0 / g * std::log(1 + g / k);
    double c2 = std::sqrt(g / (2 * A * k) * std::pow(1 + g / k, k / g + 1));
    REQUIRE(hs == Approx(c1 + 0.5 * s * c2));
    REQUIRE(sk == Approx(s * c2));
    // More risk-averse → larger skew; more volatile → wider spread.
    double hs2, sk2;
    GlftMarketMaker::glft_coefficients(0.5, A, k, s, hs2, sk2);
    REQUIRE(sk2 > sk);
    GlftMarketMaker::glft_coefficients(g, A, k, 2 * s, hs2, sk2);
    REQUIRE(hs2 > hs);
}

TEST_CASE("IntensityEstimator recovers A and k from synthetic exponential arrivals", "[strategy]") {
    // Per step, per side, P(max depth >= d) = 1 - exp(-A e^{-k d} dt).
    const double A = 3.0, k = 0.4, dt = 0.1;
    IntensityEstimator est(dt, 200000, 30);
    std::mt19937_64 rng(7);
    std::uniform_real_distribution<double> U(0, 1);
    for (int i = 0; i < 100000; ++i) {
        est.begin_step(0.0);
        for (int side = 0; side < 2; ++side) {
            // inverse-CDF sample of max depth
            double u = U(rng);
            double p0 = 1 - std::exp(-A * dt);
            if (u > p0) continue;  // no trade this step
            double d = -std::log(-std::log(1 - u) / (A * dt)) / k;
            est.on_trade(side == 0, side == 0 ? d : -d);
        }
    }
    est.begin_step(0.0);
    double Ahat, khat;
    REQUIRE(est.fit(Ahat, khat));
    // The fit targets λ ≈ P/dt, which is biased low where P is not small;
    // check it's in the right neighbourhood.
    REQUIRE(khat == Approx(k).epsilon(0.2));
    REQUIRE(Ahat == Approx(A).epsilon(0.5));
}

TEST_CASE("VolatilityEstimator scales to per-sqrt-second", "[strategy]") {
    VolatilityEstimator v(0.1, 100000);
    std::mt19937_64 rng(1);
    std::normal_distribution<double> N(0, 2.0);  // 2 ticks per 100 ms step
    double m = 0;
    for (int i = 0; i < 50000; ++i) { m += N(rng); v.add_mid(m); }
    REQUIRE(v.sigma() == Approx(2.0 * std::sqrt(10.0)).epsilon(0.03));
}

namespace {
MarketEvent snap(Timestamp ts, Price bid, Price ask, Qty bq = 100, Qty aq = 100) {
    MarketEvent e;
    e.ts = ts; e.type = EventType::Snapshot;
    e.bids = {{bid, bq}}; e.asks = {{ask, aq}};
    return e;
}
}  // namespace

TEST_CASE("ImbalanceStrategy enters on imbalance and exits after the hold time", "[strategy]") {
    std::vector<MarketEvent> evs;
    for (int i = 0; i < 100; ++i) evs.push_back(snap(i * 100'000, 1000, 1001, i < 10 ? 1000 : 100, 10));
    VectorSource src(inst(), evs);
    ImbalanceStrategy::Config c;
    c.threshold = 0.8;
    c.order_qty = 5;
    c.hold_us = 2'000'000;
    c.cooldown_us = 100'000'000;
    ImbalanceStrategy s(c);
    BacktestConfig cfg;
    cfg.quiet = true;
    Backtester bt(src, s, cfg);
    auto r = bt.run();
    REQUIRE(r.taker_fills == 2);          // one entry, one exit
    REQUIRE(bt.portfolio().position() == 0);
}

TEST_CASE("TWAP executes the full quantity in the scheduled number of slices", "[strategy]") {
    std::vector<MarketEvent> evs;
    for (int i = 0; i < 300; ++i) evs.push_back(snap(i * 100'000, 1000, 1001, 1000, 1000));
    VectorSource src(inst(), evs);
    TwapStrategy::Config c;
    c.total_qty = 100;
    c.num_slices = 10;
    c.interval_us = 1'000'000;
    TwapStrategy t(c);
    BacktestConfig cfg;
    cfg.quiet = true;
    Backtester bt(src, t, cfg);
    auto r = bt.run();
    REQUIRE(t.slices_sent() == 10);
    REQUIRE(bt.portfolio().position() == 100);
    REQUIRE(r.taker_fills == 10);
}

TEST_CASE("GLFT does not quote before warm-up and calibration", "[strategy]") {
    std::vector<MarketEvent> evs;
    for (int i = 0; i < 50; ++i) evs.push_back(snap(i * 100'000, 1000, 1001));
    VectorSource src(inst(), evs);
    GlftMarketMaker mm;
    BacktestConfig cfg;
    cfg.quiet = true;
    Backtester bt(src, mm, cfg);
    auto r = bt.run();
    REQUIRE(r.orders_sent == 0);
    REQUIRE_FALSE(mm.calibrated());
}
