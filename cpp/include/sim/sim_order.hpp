#pragma once
#include "../core/types.hpp"
#include "../core/side.hpp"

enum class OrderType   { Limit, Market };
enum class TimeInForce {
    GTC,       // rests; if it crosses on arrival, the crossing part takes liquidity
    PostOnly,  // rejected if it would cross on arrival (Bybit/Binance "PostOnly"/"GTX")
    IOC        // takes what it can up to its limit, the rest is cancelled
};
enum class OrderStatus {
    New,              // accepted and resting
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected,         // refused on entry (post-only cross, no liquidity, risk check)
    CancelRejected    // cancel arrived after the order had already fully filled
};
enum class Liquidity { Maker, Taker, None };
enum class FillCause {
    None,
    Taker,         // our order crossed the spread
    TradeAtPrice,  // a print at our price exhausted the queue ahead of us
    TradeThrough,  // a print beyond our price: the level was swept
    BookCross      // the opposite best quote moved through our price
};

struct OrderRequest {
    OrderId     order_id = 0;
    Side        side     = Side::Bid;
    OrderType   type     = OrderType::Limit;
    TimeInForce tif      = TimeInForce::PostOnly;
    Price       price    = 0;   // ignored for Market
    Qty         qty      = 0;
};
