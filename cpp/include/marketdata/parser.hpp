#pragma once
#include "binance_depth_event.hpp"
#include "../core/book_snapshot.hpp"
#include <string>

class Parser {
public:
    // Parse a single Binance diff-depth WebSocket JSON message
    static BinanceDepthEvent parse_depth_event(const std::string& json_str);

    // Parse a Binance REST /api/v3/depth snapshot response
    static BookSnapshot parse_snapshot(const std::string& json_str);

    // Parse a single row from the normalized depth_events.csv format.
    // Returns a BinanceDepthEvent with a single-level update (caller groups by update ID).
    static BinanceDepthEvent parse_csv_row(const std::string& line);

    // Convert double price string to integer ticks
    static Price to_ticks(const std::string& price_str);
    static Price to_ticks(double price);
};
