#pragma once
#include "../core/l3_book.hpp"
#include "itch.hpp"
#include "l3_format.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Splits a Nasdaq ITCH 5.0 day into one .l3bin file per chosen symbol, so a
// single stock can be backtested with exact queue priority without touching
// the other ~9,000 in the feed.
//
// Mapping (tick $0.01, lot 1 share):
//   Add (A/F)               → Add
//   Executed (E)            → Trade print naming the maker, then its Modify/Delete
//   Executed w/ price (C)   → same when printable; non-printable (crosses,
//                             bulk prints) only changes the book
//   Cancel (X) / Delete (D) → Modify / Delete
//   Replace (U)             → Delete + Add (a replace loses time priority)
//   Hidden trade (P)        → Trade print with maker id 0 (no displayed maker).
//                             Prints at a sub-penny price are dropped: they
//                             are midpoint-style executions strictly inside a
//                             penny, so they say nothing about a displayed queue.
// Orders priced below $1 (sub-penny stub quotes) are skipped: they cannot be
// represented on a cent grid and never trade in a stock worth backtesting.
//
// Timestamps are exchange-local: microseconds since the epoch *as if* US
// Eastern wall-clock time were UTC, so time-of-day is ts % 86,400 s.
// Microseconds (not ITCH's nanoseconds) are the engine's clock; events keep
// their feed order within a microsecond.
class ItchL3Extractor : public itch::HandlerBase {
public:
    struct SymbolStats {
        std::string symbol;
        uint64_t records = 0, adds = 0, prints = 0, hidden_prints = 0, subpenny_prints = 0;
        uint64_t skipped_subdollar = 0, unknown_ref = 0, nonprintable = 0;
        uint64_t halts_in_session = 0;   // trading actions other than 'T' after 09:30
        uint64_t open_cross_shares = 0;  // shares in the first cross of the day
        bool     found = false;
    };

    // `midnight_us` = epoch microseconds of the trading day's 00:00 (see above).
    // Records at or after end_ns (nanoseconds since midnight) are dropped.
    ItchL3Extractor(std::vector<std::string> symbols, const std::string& out_dir, const std::string& date,
                    int64_t midnight_us, uint64_t end_ns);

    void on_directory(uint16_t loc, const char* stock8);
    void on_add(uint16_t loc, uint64_t ts, uint64_t ref, uint8_t side, uint32_t shares, uint32_t price);
    void on_execute(uint16_t loc, uint64_t ts, uint64_t ref, uint32_t shares);
    void on_execute_with_price(uint16_t loc, uint64_t ts, uint64_t ref, uint32_t shares, bool printable, uint32_t price);
    void on_cancel(uint16_t loc, uint64_t ts, uint64_t ref, uint32_t shares);
    void on_delete(uint16_t loc, uint64_t ts, uint64_t ref);
    void on_replace(uint16_t loc, uint64_t ts, uint64_t old_ref, uint64_t new_ref, uint32_t shares, uint32_t price);
    void on_hidden_trade(uint16_t loc, uint64_t ts, uint32_t shares, uint32_t price);
    void on_cross(uint16_t loc, uint64_t ts, uint64_t shares, uint32_t price);
    void on_trading_action(uint16_t loc, uint64_t ts, uint8_t state);
    void on_system_event(uint64_t ts, uint8_t code);
    void on_message(uint8_t) { ++messages_; }
    void on_malformed(uint8_t) { ++malformed_; }

    // Past end_ns: the caller may stop reading.
    bool done() const { return done_; }
    uint64_t messages() const { return messages_; }
    uint64_t malformed() const { return malformed_; }
    // Closes every file (throws on write errors) and returns per-symbol stats.
    std::vector<SymbolStats> finish();

private:
    struct Sym {
        SymbolStats st;
        std::unique_ptr<L3FileWriter> w;
        std::unique_ptr<L3Book> book;
        std::unordered_map<uint64_t, uint8_t> subdollar;   // skipped ref → side
    };
    std::vector<std::unique_ptr<Sym>> syms_;
    std::vector<Sym*> by_loc_;
    std::string out_dir_, date_;
    int64_t  midnight_us_;
    uint64_t end_ns_;
    bool     done_ = false;
    uint64_t messages_ = 0, malformed_ = 0;

    Sym* get(uint16_t loc, uint64_t ts) {
        Sym* s = by_loc_[loc];
        if (!s) return nullptr;
        if (ts >= end_ns_) { done_ = true; return nullptr; }
        return s;
    }
    void emit(Sym& s, uint64_t ts, L3Type type, uint64_t id, Side side, Price px, Qty qty);
    void add(Sym& s, uint64_t ts, uint64_t ref, Side side, uint32_t shares, uint32_t price);
    void reduce(Sym& s, uint64_t ts, const L3Book::Order& o, Qty by);
};

// Epoch microseconds of YYYY-MM-DD 00:00 (proleptic Gregorian). Throws on a
// malformed date.
int64_t date_to_us(const std::string& yyyy_mm_dd);
