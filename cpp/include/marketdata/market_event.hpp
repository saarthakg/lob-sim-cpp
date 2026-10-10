#pragma once
#include "../core/types.hpp"
#include "../core/side.hpp"
#include "../core/level.hpp"
#include <vector>

// Venue-agnostic market data event, timestamped in *exchange* (matching
// engine) time. Strategy-side latency is applied on top by the Backtester.
enum class EventType : uint8_t {
    Snapshot,  // full book replacement (bids/asks hold every level)
    Delta,     // incremental update: absolute new qty per level, 0 = delete
    Trade      // a public trade print; `aggressor` is the taker side
};

struct TradePrint {
    Side  aggressor = Side::Bid;  // Bid = buyer-initiated (lifted the ask)
    Price price     = 0;
    Qty   qty       = 0;
};

struct MarketEvent {
    Timestamp          ts   = 0;
    EventType          type = EventType::Delta;
    std::vector<Level> bids;
    std::vector<Level> asks;
    TradePrint         trade;
};
