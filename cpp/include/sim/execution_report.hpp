#pragma once
#include "../core/types.hpp"
#include "../core/side.hpp"
#include "sim_order.hpp"

// One message from the simulated exchange to the strategy. A taker order that
// sweeps several price levels produces one report per level, so fill_price is
// always a real tick — never a fractional VWAP.
struct ExecutionReport {
    OrderId     order_id   = 0;
    Timestamp   ts         = 0;      // exchange time the event happened
    Side        side       = Side::Bid;
    OrderType   order_type = OrderType::Limit;
    OrderStatus status     = OrderStatus::New;
    Price       price      = 0;      // order's limit price
    Price       fill_price = 0;      // this fill's price (0 if no fill)
    Qty         fill_qty   = 0;      // this fill's qty
    Qty         cum_qty    = 0;      // total filled so far
    Qty         leaves_qty = 0;      // still working
    double      fee        = 0.0;    // quote ccy; negative = rebate received
    Liquidity   liquidity  = Liquidity::None;
    // Diagnostics for passive fills: why we were filled, and the estimated
    // displayed qty still ahead of us just before the fill.
    FillCause   cause       = FillCause::None;
    double      queue_ahead = 0.0;
};
