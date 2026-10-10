#pragma once
#include "../core/side.hpp"
#include "../core/types.hpp"
#include <cstdint>
#include <vector>

// Allocation-light parsers for Bybit v5 public messages. The historical
// order-book archive stores exactly the WebSocket `orderbook.N` messages, one
// per line, so one parser serves both the backtest reader and the live feed.
// Prices/sizes are exact fixed-point (decimal::SCALE units) until converted
// with a Grid.
namespace bybit {

struct RawLevel {
    int64_t px;    // decimal::SCALE units
    int64_t qty;
};

struct BookMsg {
    Timestamp ts = 0;          // matching-engine time (cts) if present, else push time; us
    bool snapshot = false;
    std::vector<RawLevel> bids, asks;
};

struct TradeMsg {
    Timestamp ts = 0;          // us
    Side      aggressor = Side::Bid;
    int64_t   px = 0, qty = 0; // decimal::SCALE units
    bool      rpi = false;     // Retail-Price-Improvement fill (hidden liquidity)
};

// Parses an orderbook.N message (snapshot or delta). False if not one.
bool parse_book(const char* msg, BookMsg& out);
// Parses a publicTrade message, appending each print. Returns prints added.
size_t parse_trades(const char* msg, std::vector<TradeMsg>& out);

// Venue price/size grid in decimal::SCALE units, with conversion to the
// integer Price/Qty used everywhere else.
struct Grid {
    int64_t tick_scaled = 0;
    int64_t lot_scaled  = 0;
    Price px(int64_t scaled) const { return (scaled + tick_scaled / 2) / tick_scaled; }
    Qty   qty(int64_t scaled) const { return (scaled + lot_scaled / 2) / lot_scaled; }
};
// tick/lot <= 0: detect from the snapshot as the GCD of price differences and
// of sizes (prices print more decimals than the real tick, e.g. "84475.90").
Grid detect_grid(const BookMsg& snapshot, double tick, double lot);

}  // namespace bybit
