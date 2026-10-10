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
// code (dense, < 65536). Prices map to ticks with an exact, order-preserving
// piecewise scale: below $1, ITCH's 1/10,000 units (sub-penny quotes are
// legal there); at or above $1, cents (Reg NMS Rule 612 forbids sub-penny
// quotes priced >= $1). Every legal price is exact, and books stay dense in
// cents, so each book's price window is small enough for ~8k books in
// memory. A sub-penny price >= $1 (illegal, so never expected) is counted.
// The rule depends on the *order's* price, not the stock's: a $0.0001 stub
// bid on a $12 stock is legal and exact.
class ItchBookBuilder {
public:
    struct Stats {
        uint64_t messages = 0, adds = 0, executes = 0, cancels = 0, deletes = 0, replaces = 0, hidden_trades = 0;
        uint64_t unknown_ref = 0;        // execute/cancel/delete/replace for an order we don't hold
        uint64_t duplicate_add = 0;
        uint64_t over_execute = 0;       // executed/cancelled more shares than resting
        uint64_t subpenny_violations = 0;   // sub-penny price >= $1
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
    // ITCH price (1/10,000 USD) <-> book ticks.
    static Price to_ticks(uint32_t itch_price) {
        return itch_price < 10000 ? static_cast<Price>(itch_price)
                                  : static_cast<Price>(9900 + (itch_price + 50) / 100);
    }
    static double to_usd(Price ticks) {
        return ticks < 10000 ? static_cast<double>(ticks) / 1e4 : static_cast<double>(ticks - 9900) / 100.0;
    }
    size_t book_count() const;
    size_t resting_orders() const;
    // Locate code for a ticker, or -1.
    int find_symbol(const std::string& ticker) const;

private:
    int log2_window_;
    std::vector<std::unique_ptr<L3Book>> books_;   // by locate
    std::vector<std::string> symbols_;
    Stats stats_;

    L3Book& get(uint16_t loc);
    Price to_px(uint32_t price);
};
