#pragma once
#include "../core/types.hpp"
#include "../core/side.hpp"
#include "../core/level.hpp"
#include "l3_record.hpp"
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
    // Order-level sources also attach the raw L3 event, so an exact-queue
    // exchange can track individual orders; L2-only consumers ignore it.
    bool               has_l3 = false;
    L3Record           l3{};
    // Live feeds only: local monotonic clock (ns) when the message arrived,
    // for measuring receive-to-engine and tick-to-trade latency. 0 in replay.
    int64_t            local_ns = 0;
};
