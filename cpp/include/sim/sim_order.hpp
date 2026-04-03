#pragma once
#include "../core/types.hpp"
#include "../core/side.hpp"
#include <string>

enum class OrderType   { Limit, Market };
enum class OrderStatus { New, PartiallyFilled, Filled, Cancelled, Rejected };

struct SimOrder {
    OrderId     order_id;
    Timestamp   ts;
    Side        side;
    OrderType   type;
    Price       price;   // ignored for Market orders
    Qty         qty;     // total requested quantity
    std::string strategy_name;
};
