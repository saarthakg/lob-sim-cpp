#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "../include/marketdata/parser.hpp"

TEST_CASE("Parser: parse Binance diff-depth JSON", "[parser]") {
    // Binance diff-depth WebSocket message
    std::string json = R"({
        "e": "depthUpdate",
        "E": 1712331000123,
        "s": "BTCUSDT",
        "U": 123456789,
        "u": 123456790,
        "b": [["65000.12","1.50"],["64999.00","0.00"]],
        "a": [["65001.00","0.75"]]
    })";

    BinanceDepthEvent ev = Parser::parse_depth_event(json);

    REQUIRE(ev.symbol          == "BTCUSDT");
    REQUIRE(ev.first_update_id == 123456789ULL);
    REQUIRE(ev.final_update_id == 123456790ULL);
    REQUIRE(ev.event_time      == 1712331000123LL * 1000LL);  // ms→us

    REQUIRE(ev.bid_updates.size() == 2);
    REQUIRE(ev.ask_updates.size() == 1);

    // 65000.12 × 100 = 6500012 ticks
    REQUIRE(ev.bid_updates[0].price == 6500012);
    REQUIRE(ev.bid_updates[0].qty   == 150);    // 1.50 × 100

    // qty == 0 → delete signal
    REQUIRE(ev.bid_updates[1].price == 6499900);
    REQUIRE(ev.bid_updates[1].qty   == 0);

    REQUIRE(ev.ask_updates[0].price == 6500100);
    REQUIRE(ev.ask_updates[0].qty   == 75);    // 0.75 × 100
}

TEST_CASE("Parser: parse Binance REST snapshot JSON", "[parser]") {
    std::string json = R"({
        "lastUpdateId": 999999,
        "bids": [["65000.00","2.00"],["64999.00","5.00"]],
        "asks": [["65001.00","1.00"],["65002.00","3.00"]]
    })";

    BookSnapshot snap = Parser::parse_snapshot(json);

    REQUIRE(snap.last_update_id == 999999ULL);
    REQUIRE(snap.bids.size()    == 2);
    REQUIRE(snap.asks.size()    == 2);

    REQUIRE(snap.bids[0].price == 6500000);
    REQUIRE(snap.bids[0].qty   == 200);   // 2.00 × 100

    REQUIRE(snap.asks[0].price == 6500100);
    REQUIRE(snap.asks[0].qty   == 100);
}

TEST_CASE("Parser: parse CSV row bid", "[parser]") {
    // ts,event_type,first_update_id,final_update_id,side,price_ticks,qty
    std::string row = "1712331000000000,depth,100,101,B,6500000,1200";
    BinanceDepthEvent ev = Parser::parse_csv_row(row);

    REQUIRE(ev.event_time      == 1712331000000000LL);
    REQUIRE(ev.first_update_id == 100ULL);
    REQUIRE(ev.final_update_id == 101ULL);
    REQUIRE(ev.bid_updates.size() == 1);
    REQUIRE(ev.ask_updates.empty());
    REQUIRE(ev.bid_updates[0].price == 6500000);
    REQUIRE(ev.bid_updates[0].qty   == 1200);
}

TEST_CASE("Parser: parse CSV row ask with qty==0", "[parser]") {
    std::string row = "1712331000000001,depth,102,103,A,6500100,0";
    BinanceDepthEvent ev = Parser::parse_csv_row(row);

    REQUIRE(ev.ask_updates.size() == 1);
    REQUIRE(ev.ask_updates[0].qty == 0);
}

TEST_CASE("Parser: to_ticks conversion is correct", "[parser]") {
    REQUIRE(Parser::to_ticks("65000.12") == 6500012);
    REQUIRE(Parser::to_ticks("65001.00") == 6500100);
    REQUIRE(Parser::to_ticks(0.0)        == 0);
    REQUIRE(Parser::to_ticks("0.00")     == 0);
}

TEST_CASE("Parser: malformed CSV row throws", "[parser]") {
    REQUIRE_THROWS(Parser::parse_csv_row("bad,data"));
}
