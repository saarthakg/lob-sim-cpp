#pragma once
#include "../core/types.hpp"

struct LatencyModel {
    int64_t market_data_us  = 0;  // delay before strategy sees an event
    int64_t decision_us     = 0;  // strategy processing time
    int64_t order_entry_us  = 0;  // delay from decision to order reaching sim exchange
    // Total effective latency = market_data_us + decision_us + order_entry_us
};
