#include "../../include/marketdata/parser.hpp"
#include "../../include/util/json.hpp"
#include <cmath>
#include <stdexcept>
#include <sstream>

using json = nlohmann::json;

Price Parser::to_ticks(const std::string& price_str) {
    double d = std::stod(price_str);
    return static_cast<Price>(std::round(d * TICK_MULTIPLIER));
}

Price Parser::to_ticks(double price) {
    return static_cast<Price>(std::round(price * TICK_MULTIPLIER));
}

// Parse a Binance diff-depth WebSocket message.
// Example structure:
// {
//   "e": "depthUpdate", "E": 1712331000123, "s": "BTCUSDT",
//   "U": 123456789, "u": 123456790,
//   "b": [["65000.12","1.5"]], "a": [["65001.00","0.0"]]
// }
BinanceDepthEvent Parser::parse_depth_event(const std::string& json_str) {
    auto j = json::parse(json_str);
    BinanceDepthEvent ev;
    ev.event_time      = j.value("E", int64_t(0)) * 1000LL;  // ms → us
    ev.symbol          = j.value("s", std::string(""));
    ev.first_update_id = j["U"].get<uint64_t>();
    ev.final_update_id = j["u"].get<uint64_t>();

    for (const auto& entry : j["b"]) {
        Level lvl;
        lvl.price = to_ticks(entry[0].get<std::string>());
        lvl.qty   = static_cast<Qty>(std::round(std::stod(entry[1].get<std::string>()) * TICK_MULTIPLIER));
        ev.bid_updates.push_back(lvl);
    }
    for (const auto& entry : j["a"]) {
        Level lvl;
        lvl.price = to_ticks(entry[0].get<std::string>());
        lvl.qty   = static_cast<Qty>(std::round(std::stod(entry[1].get<std::string>()) * TICK_MULTIPLIER));
        ev.ask_updates.push_back(lvl);
    }
    return ev;
}

// Parse a Binance REST depth snapshot response.
// Example structure:
// { "lastUpdateId": 123456790, "bids": [["65000.12","1.5"]], "asks": [["65001.00","0.5"]] }
BookSnapshot Parser::parse_snapshot(const std::string& json_str) {
    auto j = json::parse(json_str);
    BookSnapshot snap;
    snap.last_update_id = j["lastUpdateId"].get<uint64_t>();

    for (const auto& entry : j["bids"]) {
        Level lvl;
        lvl.price = to_ticks(entry[0].get<std::string>());
        lvl.qty   = static_cast<Qty>(std::round(std::stod(entry[1].get<std::string>()) * TICK_MULTIPLIER));
        snap.bids.push_back(lvl);
    }
    for (const auto& entry : j["asks"]) {
        Level lvl;
        lvl.price = to_ticks(entry[0].get<std::string>());
        lvl.qty   = static_cast<Qty>(std::round(std::stod(entry[1].get<std::string>()) * TICK_MULTIPLIER));
        snap.asks.push_back(lvl);
    }
    return snap;
}

// Parse a single row from the normalized depth_events.csv format.
// Format: ts,event_type,first_update_id,final_update_id,side,price_ticks,qty
// Returns a BinanceDepthEvent with exactly one bid or ask update.
BinanceDepthEvent Parser::parse_csv_row(const std::string& line) {
    std::istringstream ss(line);
    std::string token;
    std::vector<std::string> fields;
    while (std::getline(ss, token, ',')) {
        fields.push_back(token);
    }
    if (fields.size() < 7) {
        throw std::runtime_error("CSV row has fewer than 7 fields: " + line);
    }

    BinanceDepthEvent ev;
    ev.event_time      = std::stoll(fields[0]);
    // fields[1] = event_type (ignored, always "depth")
    ev.first_update_id = std::stoull(fields[2]);
    ev.final_update_id = std::stoull(fields[3]);
    ev.symbol          = "";

    char side_char = fields[4].empty() ? 'B' : fields[4][0];
    Level lvl;
    lvl.price = std::stoll(fields[5]);
    lvl.qty   = std::stoll(fields[6]);

    if (side_char == 'B') {
        ev.bid_updates.push_back(lvl);
    } else {
        ev.ask_updates.push_back(lvl);
    }
    return ev;
}
