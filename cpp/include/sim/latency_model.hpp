#pragma once
#include "../core/types.hpp"

struct LatencyModel {
    int64_t market_data_us  = 0;  // delay before strategy sees an event
    int64_t decision_us     = 0;  // strategy processing time
    int64_t order_entry_us  = 0;  // delay from decision to order reaching sim exchange

    // ExchangeSim models the combined effect of all three stages as a single
    // order-arrival delay: an order submitted at time T is not matched against
    // the book until T + total_us(). The book (and thus fill price / queue
    // position) is evaluated as of the arrival time, not the decision time —
    // this is what lets latency actually move PnL, fill rate, and slippage.
    int64_t total_us() const { return market_data_us + decision_us + order_entry_us; }
};
