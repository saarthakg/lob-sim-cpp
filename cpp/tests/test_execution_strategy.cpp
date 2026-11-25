#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "../include/sim/backtester.hpp"
#include "../include/strategy/execution_strategy.hpp"

using Catch::Approx;

namespace {

constexpr Timestamp SEC = 1'000'000;

Instrument inst() {
    Instrument i;
    i.symbol = "T"; i.tick_size = 0.01; i.lot_size = 1.0;
    return i;
}
MarketEvent snap(Timestamp ts, Price bid, Price ask, Qty bq = 100, Qty aq = 100) {
    MarketEvent e;
    e.ts = ts; e.type = EventType::Snapshot;
    e.bids = {{bid, bq}};
    e.asks = {{ask, aq}};
    return e;
}
MarketEvent trade(Timestamp ts, Side aggr, Price px, Qty q) {
    MarketEvent e;
    e.ts = ts; e.type = EventType::Trade; e.trade = TradePrint{aggr, px, q};
    return e;
}
std::vector<ExecutionStrategy::Parent> run(std::vector<MarketEvent> ev, ExecutionStrategy::Algo algo, int64_t lat = 0,
                                           RunSummary* sum = nullptr) {
    ExecutionStrategy::Config c;
    c.algo = algo; c.parent_qty = 10; c.slices = 2; c.horizon_us = 10 * SEC; c.gap_us = 2 * SEC;
    ExecutionStrategy s(c);
    BacktestConfig cfg;
    cfg.quiet = true;
    cfg.exchange.fees.maker = 0.0; cfg.exchange.fees.taker = 0.0;
    cfg.latency.entry_us = cfg.latency.response_us = lat;
    VectorSource src(inst(), std::move(ev));
    Backtester bt(src, s, cfg);
    const RunSummary r = bt.run();
    if (sum) *sum = r;
    return s.done();
}
std::vector<MarketEvent> flat(int seconds, Price bid = 1000, Price ask = 1002) {
    std::vector<MarketEvent> ev;
    for (int i = 0; i < seconds * 10; ++i) ev.push_back(snap(SEC + i * SEC / 10, bid, ask));
    return ev;
}

}  // namespace

TEST_CASE("Execution: crossing pays half the spread and parents alternate sides", "[exec]") {
    const auto d = run(flat(30), ExecutionStrategy::Algo::Cross);
    REQUIRE(d.size() >= 2);
    REQUIRE(d[0].side == Side::Bid);
    REQUIRE(d[1].side == Side::Ask);
    REQUIRE(d[0].cost / 10 == Approx(1002));
    REQUIRE(d[1].cost / 10 == Approx(1000));
    REQUIRE(d[0].filled_maker == 0);
    // Second slice only at its slot: the parent spans the half-way point.
    REQUIRE(d[0].end - d[0].start >= 5 * SEC);
    REQUIRE(d[1].start - d[0].start == Approx(12 * SEC).margin(0.2 * SEC));
}

TEST_CASE("Execution: a joined slice fills as maker, and an unfilled one is swept", "[exec]") {
    auto ev = flat(14);
    // Enough selling in the first slot to clear the queue (100) and fill 5.
    for (size_t i = 0; i < ev.size(); ++i)
        if (ev[i].ts == 3 * SEC) { ev.insert(ev.begin() + static_cast<long>(i), trade(3 * SEC, Side::Ask, 1000, 105)); break; }
    const auto d = run(ev, ExecutionStrategy::Algo::Join);
    REQUIRE(d.size() == 1);
    REQUIRE(d[0].filled == 10);
    REQUIRE(d[0].filled_maker == 5);
    REQUIRE(d[0].cost == Approx(5 * 1000 + 5 * 1002));
}

TEST_CASE("Execution: peg follows the touch, join does not", "[exec]") {
    std::vector<MarketEvent> ev;
    for (int i = 0; i < 140; ++i) {
        const Timestamp t = SEC + i * SEC / 10;
        ev.push_back(i < 20 ? snap(t, 1000, 1002) : snap(t, 1005, 1007));
        if (i == 30) ev.push_back(trade(t + 1, Side::Ask, 1005, 105));
    }
    REQUIRE(run(ev, ExecutionStrategy::Algo::Join)[0].filled_maker == 0);
    REQUIRE(run(ev, ExecutionStrategy::Algo::Peg)[0].filled_maker == 5);
}

TEST_CASE("Execution: smart crosses at once when the book leans its way", "[exec]") {
    std::vector<MarketEvent> ev;
    for (int i = 0; i < 140; ++i) ev.push_back(snap(SEC + i * SEC / 10, 1000, 1002, 900, 100));   // bid-heavy: price about to rise
    const auto d = run(ev, ExecutionStrategy::Algo::Smart);
    REQUIRE(d[0].side == Side::Bid);
    REQUIRE(d[0].cost / 10 == Approx(1002));
    REQUIRE(d[0].filled_maker == 0);
}

TEST_CASE("Execution: never overfills when a fill races the cancel", "[exec]") {
    std::vector<MarketEvent> ev;
    for (int i = 0; i < 300; ++i) {
        const Timestamp t = SEC + i * SEC / 10;
        ev.push_back(snap(t, 1000 + i % 3, 1002 + i % 3));            // constant re-pegs
        ev.push_back(trade(t + 10, Side::Ask, 998, 3));               // and fills in flight
    }
    RunSummary s;
    for (const auto& p : run(ev, ExecutionStrategy::Algo::Peg, 250'000, &s)) REQUIRE(p.filled == 10);
    REQUIRE(std::abs(s.final_position) <= 10.0);
}
