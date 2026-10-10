#pragma once
#include "../core/l3_book.hpp"
#include "itch.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

// Builds one order-level L3Book per Nasdaq symbol from an ITCH 5.0 stream —
// the full displayed book of every listed stock, order by order.
//
// Books are created on a symbol's first add and indexed by ITCH stock-locate
// code (dense, < 65536). Prices are kept exact: stocks first seen at >= $1
// use cents (Reg NMS Rule 612 forbids sub-penny quotes there), sub-dollar
// stocks keep ITCH's 1/10,000 units. That keeps each book's dense price
// window small enough for ~8k books in memory. A sub-penny price on a
// cents book is counted as a violation rather than silently rounded.
class ItchBookBuilder {
public:
    struct Stats {
        uint64_t messages = 0, adds = 0, executes = 0, cancels = 0, deletes = 0, replaces = 0, hidden_trades = 0;
        uint64_t unknown_ref = 0;        // execute/cancel/delete/replace for an order we don't hold
        uint64_t duplicate_add = 0;
        uint64_t over_execute = 0;       // executed/cancelled more shares than resting
        uint64_t subpenny_violations = 0;
        std::array<uint64_t, 256> by_type{};
    };

    explicit ItchBookBuilder(int log2_window = 12);

    // itch::parse_itch handler interface
    void on_add(uint16_t loc, uint64_t ts, uint64_t ref, uint8_t side, uint32_t shares, uint32_t price);
    void on_execute(uint16_t loc, uint64_t ts, uint64_t ref, uint32_t shares);
    void on_cancel(uint16_t loc, uint64_t ts, uint64_t ref, uint32_t shares);
    void on_delete(uint16_t loc, uint64_t ts, uint64_t ref);
    void on_replace(uint16_t loc, uint64_t ts, uint64_t old_ref, uint64_t new_ref, uint32_t shares, uint32_t price);
    void on_directory(uint16_t loc, const char* stock8);
    void on_hidden_trade(uint16_t, uint64_t, uint32_t, uint32_t) { ++stats_.hidden_trades; }
    void on_message(uint8_t type) {
        ++stats_.messages;
        ++stats_.by_type[type];
    }

    const Stats& stats() const { return stats_; }
    const L3Book* book(uint16_t loc) const { return books_[loc].get(); }
    const std::string& symbol(uint16_t loc) const { return symbols_[loc]; }
    // Price units per USD for a symbol's book (100 = cents, 10000 = raw ITCH).
    double units_per_usd(uint16_t loc) const { return scale_[loc] == 100 ? 100.0 : 10000.0; }
    size_t book_count() const;
    size_t resting_orders() const;
    // Locate code for a ticker, or -1.
    int find_symbol(const std::string& ticker) const;

private:
    int log2_window_;
    std::vector<std::unique_ptr<L3Book>> books_;   // by locate
    std::vector<uint8_t>  scale_;                  // 0 unknown, 1 raw, 100 cents
    std::vector<std::string> symbols_;
    Stats stats_;

    L3Book& get(uint16_t loc, uint32_t first_price);
    Price to_px(uint16_t loc, uint32_t price);
};
