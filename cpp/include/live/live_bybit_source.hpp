#pragma once
#include "../marketdata/event_source.hpp"
#include "../util/spsc_ring.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Live Bybit public market data as an EventSource, so the unchanged
// Backtester runs against the real-time market ("live paper trading"):
//
//   IXWebSocket thread                        engine thread
//   ──────────────────                        ─────────────
//   frame arrives → stamp local_ns            Backtester::run()
//   parse (bybit_parse, fixed-point)            → next(): pop ring (spin)
//   build MarketEvent → SpscRing::try_push  ──▶   → exchange sim, strategy, orders
//
// Order book (orderbook.N, snapshot + deltas) and trades (publicTrade) come
// from one connection; event timestamps are matching-engine times, forced
// monotonic across the two streams. A reconnect resubscribes and the new
// snapshot arrives as a Snapshot event, which resets the book downstream.
class LiveBybitSource : public EventSource {
public:
    struct Config {
        std::string symbol     = "BTCUSDT";
        int         depth      = 50;       // orderbook.1 (10 ms), .50 (20 ms), .200 (100 ms)
        double      tick_size  = 0.0;      // <= 0: detect from the first snapshot
        double      lot_size   = 0.0;
        double      duration_s = 600;
        size_t      ring_capacity = 1 << 16;
        bool        skip_rpi   = true;
        std::string url        = "wss://stream.bybit.com/v5/public/linear";
    };

    struct Stats {
        uint64_t frames = 0, book_msgs = 0, trades = 0, overruns = 0, opens = 0;
        double parse_p50_ns = 0, parse_p99_ns = 0;
    };

    explicit LiveBybitSource(Config cfg);   // connects; blocks until the first snapshot
    ~LiveBybitSource() override;

    bool next(MarketEvent& ev) override;    // false once the duration elapses or stop() is called
    const Instrument& instrument() const override { return inst_; }
    bool has_trades() const override { return true; }

    void  stop() { stop_.store(true, std::memory_order_relaxed); }
    // Stops and joins the WebSocket thread (its state is then safe to read).
    Stats shutdown_and_stats();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Config cfg_;
    Instrument inst_;
    SpscRing<MarketEvent> ring_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> ready_{false};
    int64_t deadline_ns_ = 0;

    // WebSocket-thread state
    bybit::Grid grid_;
    bybit::BookMsg book_msg_;
    std::vector<bybit::TradeMsg> trade_msgs_;
    Timestamp last_ts_ = 0;
    std::vector<int64_t> parse_ns_;
    std::atomic<uint64_t> frames_{0}, book_n_{0}, trade_n_{0}, overruns_{0}, opens_{0};

    void on_frame(const std::string& text);
    void push(MarketEvent&& ev);
};
