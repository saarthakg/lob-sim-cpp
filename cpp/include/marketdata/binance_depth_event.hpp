#pragma once
#include "../core/types.hpp"
#include "../core/level.hpp"
#include <vector>
#include <string>

struct BinanceDepthEvent {
    Timestamp          event_time;
    std::string        symbol;
    UpdateId           first_update_id;  // "U" field in Binance JSON
    UpdateId           final_update_id;  // "u" field in Binance JSON
    std::vector<Level> bid_updates;
    std::vector<Level> ask_updates;
};
