#pragma once
#include <cmath>
#include <cstdint>
#include <string>

using Price     = int64_t;   // integer multiples of the instrument's tick size
using Qty       = int64_t;   // integer multiples of the instrument's lot size
using Timestamp = int64_t;   // microseconds since epoch
using UpdateId  = uint64_t;  // venue update sequence ID
using OrderId   = uint64_t;  // internal simulator order ID

// Scale used by the *legacy* v1 normalized CSV format and by the Binance
// JSON parser: price_ticks = price * 100, qty = qty * 100. New data paths use
// Instrument (below), where one Price unit is exactly one venue tick and one
// Qty unit is exactly one venue lot — so "1 tick" in strategy code always
// means a real, tradeable price increment.
static constexpr double TICK_MULTIPLIER = 100.0;

// Static contract specification. All conversions between integer book units
// and real-world USD / base-asset units go through here.
struct Instrument {
    std::string symbol    = "UNKNOWN";
    double      tick_size = 0.01;   // quote currency per Price unit
    double      lot_size  = 0.01;   // base asset per Qty unit

    double price(Price p) const { return static_cast<double>(p) * tick_size; }
    double qty(Qty q)     const { return static_cast<double>(q) * lot_size; }
    Price  to_ticks(double px) const { return static_cast<Price>(std::llround(px / tick_size)); }
    Qty    to_lots(double q)   const { return static_cast<Qty>(std::llround(q / lot_size)); }
    // Quote-currency notional of `q` lots at `p` ticks.
    double notional(Price p, Qty q) const { return price(p) * qty(q); }

    // Legacy v1 CSV / Binance-parser scale (0.01 price, 0.01 qty).
    static Instrument legacy() {
        Instrument i;
        i.symbol    = "LEGACY";
        i.tick_size = 1.0 / TICK_MULTIPLIER;
        i.lot_size  = 1.0 / TICK_MULTIPLIER;
        return i;
    }
};
