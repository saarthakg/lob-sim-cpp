#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "../include/sim/backtester.hpp"
#include "../include/strategy/naive_market_maker.hpp"

#include <chrono>
#include <cmath>
#include <functional>

using Catch::Approx;

namespace {

Instrument inst() {
    Instrument i;
    i.symbol = "TEST";
    i.tick_size = 0.1;
    i.lot_size = 0.001;
    return i;
}

MarketEvent snapshot(Timestamp ts, Price bid, Price ask, Qty q = 100) {
    MarketEvent e;
    e.ts = ts; e.type = EventType::Snapshot;
    e.bids = {{bid, q}, {bid - 1, q}};
    e.asks = {{ask, q}, {ask + 1, q}};
    return e;
}
MarketEvent trade_ev(Timestamp ts, Side aggr, Price px, Qty q) {
    MarketEvent e;
    e.ts = ts; e.type = EventType::Trade; e.trade = TradePrint{aggr, px, q};
    return e;
}

// Scriptable strategy: runs a callback on each book event / report.
struct Probe : Strategy {
    std::function<void(StrategyContext&, int)> on_book_fn;
    std::vector<ExecutionReport> reports;
    std::vector<Timestamp> book_times;
    std::vector<Price> seen_bids;
    int n = 0;
    std::string name() const override { return "Probe"; }
    void on_book(StrategyContext& c) override {
        book_times.push_back(c.now());
        seen_bids.push_back(c.book().best_bid()->price);
        if (on_book_fn) on_book_fn(c, n);
        ++n;
    }
    void on_order_update(StrategyContext&, const ExecutionReport& r) override { reports.push_back(r); }
};

BacktestConfig quiet_cfg() {
    BacktestConfig c;
    c.quiet = true;
    c.exchange.fees.maker = 0.0;
    c.exchange.fees.taker = 0.0;
    return c;
}

}  // namespace

TEST_CASE("Zero latency: order rests and fills on a later trade", "[backtest]") {
    VectorSource src(inst(), {snapshot(0, 1000, 1001), snapshot(1000, 1000, 1001),
                              trade_ev(2000, Side::Ask, 999, 500)});
    Probe p;
    p.on_book_fn = [](StrategyContext& c, int n) {
        if (n == 0) REQUIRE(c.submit_limit(Side::Bid, 1000, 10) != 0);
    };
    Backtester bt(src, p, quiet_cfg());
    auto s = bt.run();
    REQUIRE(s.maker_fills == 1);
    REQUIRE(bt.portfolio().position() == 10);
}

TEST_CASE("Entry latency: the market can move before the order arrives", "[backtest]") {
    // Decision at t=0 to post-only bid 1000. The book moves (asks drop to
    // 1000) at t=500 — before the order lands at t=1000 — so it is rejected.
    auto moved = snapshot(500, 999, 1000);
    VectorSource src(inst(), {snapshot(0, 1000, 1001), moved, snapshot(2000, 999, 1000)});
    Probe p;
    p.on_book_fn = [](StrategyContext& c, int n) { if (n == 0) c.submit_limit(Side::Bid, 1000, 10); };
    auto cfg = quiet_cfg();
    cfg.latency.entry_us = 1000;
    Backtester bt(src, p, cfg);
    auto s = bt.run();
    REQUIRE(s.rejects == 1);
    REQUIRE(bt.portfolio().position() == 0);
}

TEST_CASE("Cancel latency: a stale quote is picked off while the cancel is in flight", "[backtest]") {
    VectorSource src(inst(), {snapshot(0, 1000, 1001), snapshot(10'000, 1000, 1001),
                              trade_ev(10'500, Side::Ask, 999, 100), snapshot(20'000, 999, 1000)});
    Probe p;
    p.on_book_fn = [](StrategyContext& c, int n) {
        if (n == 0) c.submit_limit(Side::Bid, 1000, 10);
        if (n == 1) for (auto& [id, o] : c.orders()) c.cancel(id);   // cancel sent at t=10000
    };
    auto cfg = quiet_cfg();
    cfg.latency.entry_us = 1000;   // cancel lands at 11000, after the 10500 trade
    Backtester bt(src, p, cfg);
    auto s = bt.run();
    REQUIRE(s.maker_fills == 1);
    REQUIRE(s.cancel_rejects == 1);
    REQUIRE(bt.portfolio().position() == 10);
}

TEST_CASE("Feed latency: strategy sees the book late", "[backtest]") {
    VectorSource src(inst(), {snapshot(0, 1000, 1001), snapshot(1000, 1005, 1006), snapshot(5000, 1005, 1006)});
    Probe p;
    auto cfg = quiet_cfg();
    cfg.latency.feed_us = 300;
    Backtester bt(src, p, cfg);
    bt.run();
    REQUIRE(p.book_times.size() >= 2);
    REQUIRE(p.book_times[0] == 300);
    REQUIRE(p.book_times[1] == 1300);
    REQUIRE(p.seen_bids[1] == 1005);
}

TEST_CASE("Zero latency: fill is known before the next book callback", "[backtest]") {
    VectorSource src(inst(), {snapshot(0, 1000, 1001), trade_ev(500, Side::Ask, 999, 100),
                              snapshot(1000, 999, 1000)});
    Probe p;
    Qty pos_seen = -1;
    p.on_book_fn = [&](StrategyContext& c, int n) {
        if (n == 0) c.submit_limit(Side::Bid, 1000, 10);
        if (n == 1) pos_seen = c.position();
    };
    Backtester bt(src, p, quiet_cfg());
    bt.run();
    REQUIRE(pos_seen == 10);
}

TEST_CASE("Gateway refuses self-crossing and over-limit orders", "[backtest]") {
    VectorSource src(inst(), {snapshot(0, 1000, 1005)});
    Probe p;
    p.on_book_fn = [](StrategyContext& c, int n) {
        if (n != 0) return;
        REQUIRE(c.submit_limit(Side::Ask, 1003, 5) != 0);
        REQUIRE(c.submit_limit(Side::Bid, 1003, 5) == 0);   // would cross own ask
        REQUIRE(c.submit_limit(Side::Bid, 1001, 50) == 0);  // over max_position
        REQUIRE(c.submit_limit(Side::Bid, 1001, 20) != 0);
    };
    auto cfg = quiet_cfg();
    cfg.max_position = 20;
    Backtester bt(src, p, cfg);
    auto s = bt.run();
    REQUIRE(s.local_refusals == 2);
}

TEST_CASE("Edge and markout attribution", "[backtest]") {
    // Bid fills at 1000 with mid 1000.5 (edge +0.5 tick); 2 s later mid is 990.5.
    VectorSource src(inst(), {snapshot(0, 1000, 1001), trade_ev(100, Side::Ask, 1000, 1000),
                              snapshot(2'000'000, 990, 991)});
    Probe p;
    p.on_book_fn = [](StrategyContext& c, int n) { if (n == 0) c.submit_limit(Side::Bid, 1000, 900); };
    Backtester bt(src, p, quiet_cfg());
    auto s = bt.run();
    REQUIRE(bt.fills().size() == 1);
    // queue ahead was 100 lots, trade 1000 → our 900 lots fill in full
    const double units = 0.9;
    REQUIRE(s.edge == Approx(0.5 * 0.1 * units));
    REQUIRE(s.markout[1] == Approx(-9.5 * 0.1 * units));   // 1 s horizon
    REQUIRE(s.net_pnl == Approx(s.gross_pnl));
}

TEST_CASE("Naive market maker trades both sides on a two-way market", "[backtest]") {
    std::vector<MarketEvent> evs;
    Timestamp t = 0;
    for (int i = 0; i < 200; ++i) {
        evs.push_back(snapshot(t, 1000, 1001));
        t += 100'000;
        evs.push_back(trade_ev(t - 50'000, i % 2 ? Side::Ask : Side::Bid, i % 2 ? 1000 : 1001, 500));
    }
    VectorSource src(inst(), evs);
    NaiveMarketMaker::Config c;
    c.order_qty = 10;
    c.max_position = 30;
    NaiveMarketMaker mm(c);
    Backtester bt(src, mm, quiet_cfg());
    auto s = bt.run();
    REQUIRE(s.maker_fills > 10);
    REQUIRE(std::fabs(s.final_position) <= 0.031);
    REQUIRE(s.gross_pnl > 0);   // flat mid, alternating flow: pure spread capture
}

TEST_CASE("Live latency instrumentation counts reactions, not timer or replay events", "[backtest]") {
    auto now_ns = [] {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    };
    // Exchange timestamps past the 1 s warm-up window, which is excluded.
    std::vector<MarketEvent> evs = {snapshot(0, 1000, 1001), snapshot(2'000'000, 1000, 1001),
                                    snapshot(3'000'000, 1000, 1001), snapshot(4'000'000, 1000, 1001)};
    for (auto& e : evs) e.local_ns = now_ns();   // as a live feed would stamp them
    VectorSource src(inst(), evs);
    Probe p;
    p.on_book_fn = [](StrategyContext& c, int n) {
        if (n == 0) c.submit_limit(Side::Bid, 998, 1);     // during warm-up: not counted
        if (n == 1) c.submit_limit(Side::Bid, 999, 1);     // reaction to event 1
        if (n == 2) for (auto& [id, o] : c.orders()) c.cancel(id);   // reactions to event 2
    };
    Backtester bt(src, p, quiet_cfg());
    auto s = bt.run();
    REQUIRE(s.recv_to_engine.n == 3);
    REQUIRE(s.tick_to_trade.n == 3);                    // 1 order + 2 cancels after warm-up
    REQUIRE(s.engine_to_trade.n == 3);
    REQUIRE(s.tick_to_trade.p50 > 0);

    // Replay data (local_ns == 0) records nothing.
    VectorSource src2(inst(), {snapshot(0, 1000, 1001)});
    Probe p2;
    p2.on_book_fn = [](StrategyContext& c, int) { c.submit_limit(Side::Bid, 999, 1); };
    Backtester bt2(src2, p2, quiet_cfg());
    auto s2 = bt2.run();
    REQUIRE(s2.recv_to_engine.n == 0);
    REQUIRE(s2.tick_to_trade.n == 0);
}
