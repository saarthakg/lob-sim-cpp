// Throughput benchmark on synthetic data: book-only updates (std::map vs
// sorted-vector book) and the full event-driven backtest loop with a strategy.
// For real-data throughput, see the "Replayed ... events/s" line printed by
// `lob_sim --replay` on a Bybit day file.
#include "../include/core/order_book.hpp"
#include "../include/core/order_book_flat.hpp"
#include "../include/marketdata/event_source.hpp"
#include "../include/sim/backtester.hpp"
#include "../include/strategy/naive_market_maker.hpp"
#include "../include/util/timer.hpp"

#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <vector>

static std::vector<MarketEvent> generate_events(size_t n, int depth) {
    std::mt19937_64 rng(42);
    std::vector<MarketEvent> events;
    events.reserve(n + 1);

    const Price base_bid = 1'000'000, base_ask = 1'000'001;
    MarketEvent snap;
    snap.ts = 1'700'000'000'000'000LL;
    snap.type = EventType::Snapshot;
    for (int i = 0; i < depth; ++i) {
        snap.bids.push_back({base_bid - i, 1000});
        snap.asks.push_back({base_ask + i, 1000});
    }
    events.push_back(snap);

    for (size_t i = 0; i < n; ++i) {
        MarketEvent ev;
        ev.ts = snap.ts + static_cast<int64_t>(i + 1) * 1000;
        if (rng() % 5 == 0) {
            ev.type = EventType::Trade;
            bool buy = rng() % 2;
            ev.trade = TradePrint{buy ? Side::Bid : Side::Ask, buy ? base_ask : base_bid,
                                  static_cast<Qty>(1 + rng() % 50)};
        } else {
            ev.type = EventType::Delta;
            int nb = 2 + static_cast<int>(rng() % 3), na = 2 + static_cast<int>(rng() % 3);
            // Keep the touch alive (never delete level 0) so the book never crosses.
            for (int j = 0; j < nb; ++j)
                ev.bids.push_back({base_bid - static_cast<Price>(rng() % depth),
                                   static_cast<Qty>(1 + rng() % 5000)});
            for (int j = 0; j < na; ++j)
                ev.asks.push_back({base_ask + static_cast<Price>(rng() % depth),
                                   static_cast<Qty>(1 + rng() % 5000)});
        }
        events.push_back(std::move(ev));
    }
    return events;
}

template <typename Book>
static double bench_book(const std::vector<MarketEvent>& events) {
    Book book;
    Timer t;
    t.start();
    for (const auto& ev : events) {
        if (ev.type == EventType::Trade) continue;
        for (const auto& l : ev.bids) book.apply_update(Side::Bid, l.price, l.qty);
        for (const auto& l : ev.asks) book.apply_update(Side::Ask, l.price, l.qty);
    }
    t.stop();
    volatile auto bb = book.best_bid();
    (void)bb;
    return events.size() / std::max(t.elapsed_ms(), 1e-3) / 1e3;  // M events/s
}

static double bench_backtest(const std::vector<MarketEvent>& events) {
    Instrument inst;
    inst.symbol = "SYNTH";
    inst.tick_size = 0.1;
    inst.lot_size = 0.001;
    VectorSource src(inst, events);
    NaiveMarketMaker::Config c;
    c.order_qty = 10;
    c.max_position = 100;
    NaiveMarketMaker mm(c);
    BacktestConfig cfg;
    cfg.quiet = true;
    cfg.latency.entry_us = cfg.latency.response_us = cfg.latency.feed_us = 5000;
    Backtester bt(src, mm, cfg);
    RunSummary s = bt.run();
    std::cout << "    (" << s.maker_fills << " fills, " << s.orders_sent << " orders)\n";
    return s.market_events / std::max(s.wall_ms, 1e-3) / 1e3;
}

int main() {
    constexpr size_t N = 1'000'000;
    std::printf("%-8s %-28s %-28s %-28s\n", "depth", "std::map book (M ev/s)", "sorted-vector book (M ev/s)",
                "backtest + NaiveMM (M ev/s)");
    for (int depth : {20, 200}) {
        auto events = generate_events(N, depth);
        double m = bench_book<OrderBook>(events);
        double f = bench_book<OrderBookFlat>(events);
        double b = bench_backtest(events);
        std::printf("%-8d %-28.2f %-28.2f %-28.2f\n", depth, m, f, b);
    }
    return 0;
}
