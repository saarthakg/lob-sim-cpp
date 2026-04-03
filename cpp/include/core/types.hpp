#pragma once
#include <cstdint>

using Price     = int64_t;   // integer ticks (price * TICK_MULTIPLIER)
using Qty       = int64_t;   // integer lot units or scaled quantity
using Timestamp = int64_t;   // microseconds since epoch
using UpdateId  = uint64_t;  // Binance update sequence ID
using OrderId   = uint64_t;  // internal simulator order ID

// Tick multiplier: price * TICK_MULTIPLIER gives integer ticks
// e.g., 65000.12 * 100 = 6500012 ticks (2 decimal places)
static constexpr double TICK_MULTIPLIER = 100.0;
