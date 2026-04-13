#include "../include/core/order_book.hpp"
#include "../include/core/order_book_flat.hpp"
#include "../include/marketdata/replay_engine.hpp"
#include "../include/strategy/market_maker.hpp"
#include "../include/sim/exchange_sim.hpp"
#include "../include/sim/portfolio.hpp"
#include "../include/util/timer.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <numeric>
#include <random>

// ─── Synthetic event generator ───────────────────────────────────────────────

static std::vector<BinanceDepthEvent> generate_events(size_t n) {
    std::mt19937_64 rng(42);
    std::vector<BinanceDepthEvent> events;
    events.reserve(n);

    Price base_bid = 6500000;
    Price base_ask = 6500100;
    UpdateId uid = 101;

    for (size_t i = 0; i < n; ++i) {
        BinanceDepthEvent ev{};
        ev.event_time      = static_cast<Timestamp>(1712331000000000LL + (int64_t)i * 1000);
        ev.first_update_id = uid;
        ev.final_update_id = uid;
        ++uid;

        // 2-4 bid updates
        int nb = 2 + (int)(rng() % 3);
        for (int j = 0; j < nb; ++j) {
            Price px  = base_bid - (Price)(rng() % 20) * 100;
            Qty   qty = (rng() % 5 == 0) ? 0 : (Qty)(rng() % 10000);
            ev.bid_updates.push_back({px, qty});
        }
        // 2-4 ask updates
        int na = 2 + (int)(rng() % 3);
        for (int j = 0; j < na; ++j) {
            Price px  = base_ask + (Price)(rng() % 20) * 100;
            Qty   qty = (rng() % 5 == 0) ? 0 : (Qty)(rng() % 10000);
            ev.ask_updates.push_back({px, qty});
        }
        events.push_back(std::move(ev));
    }
    return events;
}

// ─── Benchmark book-only replay ──────────────────────────────────────────────

static void bench_book_only(const std::vector<BinanceDepthEvent>& events) {
    OrderBook book;
    BookSnapshot snap{};
    snap.last_update_id = 100;
    for (int i = 0; i < 20; ++i) {
        snap.bids.push_back({6500000 - i * 100, 1000});
        snap.asks.push_back({6500100 + i * 100, 1000});
    }
    book.load_snapshot(snap);

    std::vector<int64_t> per_event_us;
    per_event_us.reserve(events.size());

    Timer total_timer;
    total_timer.start();

    for (const auto& ev : events) {
        Timer ev_timer;
        ev_timer.start();

        for (const auto& lvl : ev.bid_updates)
            book.apply_update(Side::Bid, lvl.price, lvl.qty);
        for (const auto& lvl : ev.ask_updates)
            book.apply_update(Side::Ask, lvl.price, lvl.qty);

        ev_timer.stop();
        per_event_us.push_back(ev_timer.elapsed_us());
    }

    total_timer.stop();

    double elapsed_ms = total_timer.elapsed_ms();
    double eps = (elapsed_ms > 0) ? (events.size() / elapsed_ms * 1000.0) : 0.0;

    std::sort(per_event_us.begin(), per_event_us.end());
    int64_t p50 = per_event_us[per_event_us.size() / 2];
    int64_t p95 = per_event_us[static_cast<size_t>(per_event_us.size() * 0.95)];

    std::cout << "\n=== Book-Only Replay ===\n";
    std::cout << "  Events:        " << events.size() << "\n";
    std::cout << "  Total time:    " << elapsed_ms << " ms\n";
    std::cout << "  Throughput:    " << static_cast<int64_t>(eps) << " events/sec\n";
    std::cout << "  p50 per-event: " << p50 << " us\n";
    std::cout << "  p95 per-event: " << p95 << " us\n";
}

// ─── Benchmark replay with MarketMaker strategy ──────────────────────────────

static void bench_with_strategy(const std::vector<BinanceDepthEvent>& events) {
    OrderBook book;
    BookSnapshot snap{};
    snap.last_update_id = 100;
    for (int i = 0; i < 20; ++i) {
        snap.bids.push_back({6500000 - i * 100, 5000});
        snap.asks.push_back({6500100 + i * 100, 5000});
    }
    book.load_snapshot(snap);

    MarketMaker strategy;
    ExchangeSim exchange;
    Portfolio   portfolio;

    size_t peak_passive = 0;
    Timer total_timer;
    total_timer.start();

    for (const auto& ev : events) {
        for (const auto& lvl : ev.bid_updates)
            book.apply_update(Side::Bid, lvl.price, lvl.qty);
        for (const auto& lvl : ev.ask_updates)
            book.apply_update(Side::Ask, lvl.price, lvl.qty);
        book.set_update_id(ev.final_update_id);

        exchange.on_market_event(ev, book, portfolio);
        strategy.on_market_event(ev.event_time, book, exchange, portfolio);

        peak_passive = std::max(peak_passive, exchange.active_order_count());
    }

    total_timer.stop();

    double elapsed_ms = total_timer.elapsed_ms();
    double eps = (elapsed_ms > 0) ? (events.size() / elapsed_ms * 1000.0) : 0.0;

    std::cout << "\n=== Replay + MarketMaker Strategy ===\n";
    std::cout << "  Events:           " << events.size() << "\n";
    std::cout << "  Total time:       " << elapsed_ms << " ms\n";
    std::cout << "  Throughput:       " << static_cast<int64_t>(eps) << " events/sec\n";
    std::cout << "  Peak passive ords: " << peak_passive << "\n";
    std::cout << "  Final position:    " << portfolio.position() << "\n";
    std::cout << "  Final realized PnL: " << portfolio.realized_pnl() << "\n";
}

// ─── Benchmark flat-vector book (OrderBookFlat) ──────────────────────────────

struct BenchResult {
    std::string label;
    int64_t     throughput_eps;
    int64_t     p50_us;
    int64_t     p95_us;
};

static BenchResult bench_flat_book(const std::vector<BinanceDepthEvent>& events) {
    OrderBookFlat book;
    BookSnapshot snap{};
    snap.last_update_id = 100;
    for (int i = 0; i < 20; ++i) {
        snap.bids.push_back({6500000 - i * 100, 1000});
        snap.asks.push_back({6500100 + i * 100, 1000});
    }
    book.load_snapshot(snap);

    std::vector<int64_t> per_event_us;
    per_event_us.reserve(events.size());

    Timer total_timer;
    total_timer.start();

    for (const auto& ev : events) {
        Timer ev_timer;
        ev_timer.start();

        for (const auto& lvl : ev.bid_updates)
            book.apply_update(Side::Bid, lvl.price, lvl.qty);
        for (const auto& lvl : ev.ask_updates)
            book.apply_update(Side::Ask, lvl.price, lvl.qty);

        ev_timer.stop();
        per_event_us.push_back(ev_timer.elapsed_us());
    }

    total_timer.stop();

    double elapsed_ms = total_timer.elapsed_ms();
    double eps = (elapsed_ms > 0) ? (events.size() / elapsed_ms * 1000.0) : 0.0;

    std::sort(per_event_us.begin(), per_event_us.end());
    int64_t p50 = per_event_us[per_event_us.size() / 2];
    int64_t p95 = per_event_us[static_cast<size_t>(per_event_us.size() * 0.95)];

    std::cout << "\n=== Book-Only Replay (OrderBookFlat — sorted vector) ===\n";
    std::cout << "  Events:        " << events.size() << "\n";
    std::cout << "  Total time:    " << elapsed_ms << " ms\n";
    std::cout << "  Throughput:    " << static_cast<int64_t>(eps) << " events/sec\n";
    std::cout << "  p50 per-event: " << p50 << " us\n";
    std::cout << "  p95 per-event: " << p95 << " us\n";

    return {"OrderBookFlat (sorted vector)", static_cast<int64_t>(eps), p50, p95};
}

static BenchResult bench_map_book(const std::vector<BinanceDepthEvent>& events) {
    OrderBook book;
    BookSnapshot snap{};
    snap.last_update_id = 100;
    for (int i = 0; i < 20; ++i) {
        snap.bids.push_back({6500000 - i * 100, 1000});
        snap.asks.push_back({6500100 + i * 100, 1000});
    }
    book.load_snapshot(snap);

    std::vector<int64_t> per_event_us;
    per_event_us.reserve(events.size());

    Timer total_timer;
    total_timer.start();

    for (const auto& ev : events) {
        Timer ev_timer;
        ev_timer.start();

        for (const auto& lvl : ev.bid_updates)
            book.apply_update(Side::Bid, lvl.price, lvl.qty);
        for (const auto& lvl : ev.ask_updates)
            book.apply_update(Side::Ask, lvl.price, lvl.qty);

        ev_timer.stop();
        per_event_us.push_back(ev_timer.elapsed_us());
    }

    total_timer.stop();

    double elapsed_ms = total_timer.elapsed_ms();
    double eps = (elapsed_ms > 0) ? (events.size() / elapsed_ms * 1000.0) : 0.0;

    std::sort(per_event_us.begin(), per_event_us.end());
    int64_t p50 = per_event_us[per_event_us.size() / 2];
    int64_t p95 = per_event_us[static_cast<size_t>(per_event_us.size() * 0.95)];

    std::cout << "\n=== Book-Only Replay (OrderBook — std::map) ===\n";
    std::cout << "  Events:        " << events.size() << "\n";
    std::cout << "  Total time:    " << elapsed_ms << " ms\n";
    std::cout << "  Throughput:    " << static_cast<int64_t>(eps) << " events/sec\n";
    std::cout << "  p50 per-event: " << p50 << " us\n";
    std::cout << "  p95 per-event: " << p95 << " us\n";

    return {"OrderBook (std::map)", static_cast<int64_t>(eps), p50, p95};
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    constexpr size_t N = 1'000'000;
    std::cout << "Generating " << N << " synthetic events...\n";

    auto events = generate_events(N);
    std::cout << "Generated. Running benchmarks...\n";

    bench_book_only(events);
    bench_with_strategy(events);

    auto r_map  = bench_map_book(events);
    auto r_flat = bench_flat_book(events);

    std::cout << "\n╔══════════════════════════════════╦══════════════════╦════════════╦════════════╗\n";
    std::cout << "║ Structure                        ║ Throughput       ║ p50 lat    ║ p95 lat    ║\n";
    std::cout << "╠══════════════════════════════════╬══════════════════╬════════════╬════════════╣\n";
    for (const auto& r : {r_map, r_flat}) {
        std::cout << "║ " << r.label;
        // pad to 32 chars
        for (int i = (int)r.label.size(); i < 32; ++i) std::cout << ' ';
        std::cout << " ║ ";
        std::string eps_s = std::to_string(r.throughput_eps) + " ev/s";
        std::cout << eps_s;
        for (int i = (int)eps_s.size(); i < 16; ++i) std::cout << ' ';
        std::cout << " ║ " << r.p50_us << " µs";
        for (int i = (int)std::to_string(r.p50_us).size() + 3; i < 10; ++i) std::cout << ' ';
        std::cout << " ║ " << r.p95_us << " µs";
        for (int i = (int)std::to_string(r.p95_us).size() + 3; i < 10; ++i) std::cout << ' ';
        std::cout << " ║\n";
    }
    std::cout << "╚══════════════════════════════════╩══════════════════╩════════════╩════════════╝\n";

    std::cout << "\nNote: synthetic book has 20 levels per side. Bybit orderbook.200 has up to\n"
              << "200 levels — run bench_map_book/bench_flat_book with deeper books for a\n"
              << "realistic comparison of O(log N) map vs O(N) vector shift.\n";

    std::cout << "\nDone.\n";
    return 0;
}
