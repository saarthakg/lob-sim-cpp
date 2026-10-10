#pragma once
#include "market_event.hpp"
#include "../core/types.hpp"
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <vector>

// A time-ordered stream of MarketEvents. Implementations merge whatever raw
// files a venue publishes (book deltas, trade prints) into one stream ordered
// by exchange timestamp. Trades stamped at or before a book update are emitted
// *before* it: a conflated depth update reflects every match up to its
// timestamp, so the trades that caused a depth decrease must be seen first or
// the queue model would misclassify them as cancellations.
class EventSource {
public:
    virtual ~EventSource() = default;
    virtual bool next(MarketEvent& ev) = 0;
    virtual const Instrument& instrument() const = 0;
    virtual bool has_trades() const = 0;
    // True if events carry order-level (L3) records.
    virtual bool has_l3() const { return false; }
};

// Reads text lines from a plain file, or transparently decompresses `.gz`
// (gzip -dc) and `.zip` (unzip -p) through a pipe so multi-GB daily files
// never need to be expanded on disk.
class LineReader {
public:
    explicit LineReader(const std::string& path);
    ~LineReader();
    LineReader(const LineReader&) = delete;
    LineReader& operator=(const LineReader&) = delete;

    // Returns false at EOF. `line` excludes the trailing newline.
    bool getline(std::string& line);

private:
    FILE* fp_   = nullptr;
    bool  pipe_ = false;
};

// Fixed-point decimal parsing shared by the venue readers: "84475.90" →
// 84475900000000 (value × 1e9). Exact for every venue price/qty format we
// read, unlike strtod + division by tick size.
namespace decimal {
constexpr int64_t SCALE = 1'000'000'000LL;
int64_t parse_scaled(const char*& p);
int64_t parse_scaled(const std::string& s);
}  // namespace decimal

// ── Bybit public historical data ─────────────────────────────────────────────
//
//   Book:   https://quote-saver.bycsi.com/orderbook/linear/{SYM}/{DATE}_{SYM}_ob200.data.zip
//           one WebSocket JSON message per line (snapshot, then 100 ms deltas)
//   Trades: https://public.bybit.com/trading/{SYM}/{SYM}{DATE}.csv.gz
//
// Book updates are stamped with `cts` (matching-engine time) when present,
// which is the clock trade prints use; `ts` (push time) is a fallback.
//
// tick_size / lot_size <= 0 means auto-detect: the GCD of the price and qty
// grids observed in the first snapshot. This recovers the venue's real
// minimum increments (e.g. $0.10 / 0.001 BTC for BTCUSDT) instead of trusting
// the number of decimals in the price strings ("84475.90" has two decimals
// but the tick is 0.1).
class BybitHistoricalSource : public EventSource {
public:
    struct Config {
        std::string book_path;            // .data or .data.zip
        std::string trades_path;          // .csv or .csv.gz; empty = depth only
        std::string symbol      = "";
        double      tick_size   = 0.0;    // <= 0: auto-detect
        double      lot_size    = 0.0;    // <= 0: auto-detect
        // Retail-Price-Improvement (RPI) trades match against hidden RPI
        // orders that never appear in the public book, so they don't consume
        // the visible queue we model. Dropped by default.
        bool        skip_rpi_trades = true;
    };

    explicit BybitHistoricalSource(Config cfg);

    bool next(MarketEvent& ev) override;
    const Instrument& instrument() const override { return inst_; }
    bool has_trades() const override { return trades_ != nullptr; }

    size_t book_messages() const { return book_msgs_; }
    size_t trade_prints()  const { return trade_msgs_; }

private:
    struct RawLevel { int64_t px; int64_t qty; };  // decimal::SCALE units
    struct RawBook {
        Timestamp ts = 0;
        bool snapshot = false;
        std::vector<RawLevel> bids, asks;
    };
    struct RawTrade { Timestamp ts; Side aggressor; int64_t px; int64_t qty; };

    Config cfg_;
    Instrument inst_;
    std::unique_ptr<LineReader> book_;
    std::unique_ptr<LineReader> trades_;
    std::string line_;

    bool     have_book_  = false;
    bool     have_trade_ = false;
    RawBook  pend_book_;
    RawTrade pend_trade_{};
    int64_t  tick_scaled_ = 0;
    int64_t  lot_scaled_  = 0;
    size_t   book_msgs_ = 0, trade_msgs_ = 0;

    bool read_book(RawBook& out);
    bool read_trade(RawTrade& out);
    void detect_instrument(const RawBook& snap);
    Price to_px(int64_t scaled) const;
    Qty   to_qty(int64_t scaled) const;
};

// ── Normalized CSV (this project's own capture format) ───────────────────────
//
// v2 (written by --live-capture):
//   #instrument,<symbol>,<tick_size>,<lot_size>
//   ts,type,side,price,qty
//   1790985600124000,S,B,84475.9,5.794      S = snapshot level (a run of S rows
//   1790985600124000,S,A,84476.0,0.312          with one ts = one full snapshot)
//   1790985600224000,D,B,84475.8,0          D = delta level (qty 0 = delete)
//   1790985600225000,T,B,84476.0,0.005      T = trade, side = aggressor
//
// v1 (legacy, pre-overhaul captures): ts,event_type,first_update_id,
//   final_update_id,side,price_ticks,qty with price/qty scaled by 100. v1 has
//   no snapshot rows and no trades — the first replayed state is built from
//   deltas alone, so the early book is incomplete. Read for compatibility only.
class NormalizedCsvSource : public EventSource {
public:
    explicit NormalizedCsvSource(const std::string& path);

    bool next(MarketEvent& ev) override;
    const Instrument& instrument() const override { return inst_; }
    bool has_trades() const override { return !legacy_; }
    bool is_legacy() const { return legacy_; }

private:
    std::unique_ptr<LineReader> reader_;
    Instrument  inst_;
    bool        legacy_ = false;
    std::string line_;
    bool        have_pending_ = false;
    std::string pending_;

    bool parse_v2_row(const std::string& line, Timestamp& ts, char& type, Side& side,
                      Price& px, Qty& qty) const;
};

// ── Order-level (L3) binary file ─────────────────────────────────────────────
//
// Replays an .l3bin file (see l3_format.hpp). Each order event becomes a Delta
// for its one price level (the level's new aggregate size, from an internal
// market-only L3Book) with the raw record attached; each trade print becomes a
// Trade event, also with its record. Strategies therefore see the same L2 +
// trades stream as from any other source, while an L3ExchangeSim can follow
// individual orders.
class L3FileReader;
class L3Book;
class L3EventSource : public EventSource {
public:
    explicit L3EventSource(const std::string& path);
    ~L3EventSource() override;

    bool next(MarketEvent& ev) override;
    const Instrument& instrument() const override { return inst_; }
    bool has_trades() const override { return true; }
    bool has_l3() const override { return true; }

private:
    std::unique_ptr<L3FileReader> reader_;
    std::unique_ptr<L3Book> book_;
    Instrument inst_;
};

// In-memory source, used by tests and the benchmark.
class VectorSource : public EventSource {
public:
    VectorSource(Instrument inst, std::vector<MarketEvent> events, bool has_trades = true)
        : inst_(std::move(inst)), events_(std::move(events)), has_trades_(has_trades) {}
    bool next(MarketEvent& ev) override {
        if (pos_ >= events_.size()) return false;
        ev = events_[pos_++];
        return true;
    }
    const Instrument& instrument() const override { return inst_; }
    bool has_trades() const override { return has_trades_; }

private:
    Instrument inst_;
    std::vector<MarketEvent> events_;
    size_t pos_ = 0;
    bool has_trades_;
};

// Picks a source from file names: *.data / *.data.zip → Bybit historical
// (trades_path optional); *.l3bin → L3EventSource; anything else →
// NormalizedCsvSource.
std::unique_ptr<EventSource> open_event_source(const std::string& path,
                                               const std::string& trades_path,
                                               double tick_size = 0.0,
                                               double lot_size  = 0.0);
