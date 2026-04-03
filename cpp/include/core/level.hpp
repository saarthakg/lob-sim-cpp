#pragma once
#include "types.hpp"

struct Level {
    Price price;
    Qty   qty;   // qty == 0 means "delete this level" in Binance updates
};
