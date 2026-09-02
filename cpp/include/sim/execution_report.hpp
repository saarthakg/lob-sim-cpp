#pragma once
#include "../core/types.hpp"
#include "../core/side.hpp"
#include "sim_order.hpp"

struct ExecutionReport {
    OrderId     order_id;
    Timestamp   ts;
    Side        side;
    OrderType   order_type;    // Market or Limit — set by ExchangeSim, not inferred downstream
    OrderStatus status;
    Price       fill_price;    // weighted average if multi-level
    Qty         fill_qty;
    Qty         remaining_qty;
    double      fee_paid;
    std::string strategy_name;
};
