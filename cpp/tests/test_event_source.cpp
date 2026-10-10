#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "../include/marketdata/event_source.hpp"
#include "../include/marketdata/recorder.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

using Catch::Approx;
namespace fs = std::filesystem;

static std::string tmp_path(const std::string& name) {
    return (fs::temp_directory_path() / ("lobsim_test_" + name)).string();
}

TEST_CASE("decimal::parse_scaled is exact", "[source]") {
    REQUIRE(decimal::parse_scaled("84475.90") == 84475900000000LL);
    REQUIRE(decimal::parse_scaled("0.001") == 1000000LL);
    REQUIRE(decimal::parse_scaled("1790985600.1199") == 1790985600119900000LL);
    REQUIRE(decimal::parse_scaled("0") == 0);
}

TEST_CASE("Bybit source: tick/lot auto-detect, cts timestamps, trades merged first, RPI skipped", "[source]") {
    std::string book = tmp_path("ob.data"), trades = tmp_path("trades.csv");
    {
        std::ofstream f(book);
        f << R"({"topic":"orderbook.200.X","type":"snapshot","ts":1000,"data":{"s":"X","b":[["100.0","1.5"],["99.8","0.001"]],"a":[["100.1","2"],["100.4","0.010"]],"u":1,"seq":5},"cts":999})" << "\n";
        f << R"({"topic":"orderbook.200.X","type":"delta","ts":1100,"data":{"s":"X","b":[["100.0","1.2"]],"a":[],"u":2,"seq":6},"cts":1098})" << "\n";
    }
    {
        std::ofstream f(trades);
        f << "timestamp,symbol,side,size,price,tickDirection,trdMatchID,grossValue,homeNotional,foreignNotional,RPI\n";
        f << "1.0500,X,Sell,0.3,100.0,MinusTick,a,1,0.3,30,0\n";
        f << "1.0600,X,Buy,0.5,100.1,PlusTick,b,1,0.5,50,1\n";     // RPI → skipped
        f << "1.0980,X,Buy,0.1,100.1,PlusTick,c,1,0.1,10,0\n";     // same ms as delta's cts → before it
    }
    BybitHistoricalSource::Config c;
    c.book_path = book;
    c.trades_path = trades;
    BybitHistoricalSource src(c);
    REQUIRE(src.instrument().tick_size == Approx(0.1));
    REQUIRE(src.instrument().lot_size == Approx(0.001));

    MarketEvent ev;
    REQUIRE(src.next(ev));
    REQUIRE(ev.type == EventType::Snapshot);
    REQUIRE(ev.ts == 999000);
    REQUIRE(ev.bids[0].price == 1000);
    REQUIRE(ev.bids[0].qty == 1500);
    REQUIRE(src.next(ev));
    REQUIRE(ev.type == EventType::Trade);
    REQUIRE(ev.ts == 1050000);
    REQUIRE(ev.trade.aggressor == Side::Ask);
    REQUIRE(ev.trade.qty == 300);
    REQUIRE(src.next(ev));
    REQUIRE(ev.type == EventType::Trade);
    REQUIRE(ev.ts == 1098000);
    REQUIRE(src.next(ev));
    REQUIRE(ev.type == EventType::Delta);
    REQUIRE(ev.bids[0].qty == 1200);
    REQUIRE_FALSE(src.next(ev));
    std::remove(book.c_str());
    std::remove(trades.c_str());
}

TEST_CASE("Recorder v2 round-trips through NormalizedCsvSource", "[source]") {
    std::string path = tmp_path("v2.csv");
    {
        Recorder r(path);
        std::vector<Recorder::StrLevel> b = {{"100.0", "1.5"}, {"99.9", "0.25"}}, a = {{"100.1", "2"}};
        auto [tick, lot] = detect_tick_lot(b, a);
        REQUIRE(tick == Approx(0.1));
        REQUIRE(lot == Approx(0.25));
        r.write_instrument("X", 0.1, 0.001);
        r.record_book(10, true, b, a);
        r.record_trade(15, Side::Bid, "100.1", "0.5");
        r.record_book(20, false, {{"100.0", "0"}}, {});
    }
    NormalizedCsvSource src(path);
    REQUIRE_FALSE(src.is_legacy());
    REQUIRE(src.instrument().tick_size == Approx(0.1));
    MarketEvent ev;
    REQUIRE(src.next(ev));
    REQUIRE(ev.type == EventType::Snapshot);
    REQUIRE(ev.bids.size() == 2);
    REQUIRE(ev.asks.size() == 1);
    REQUIRE(src.next(ev));
    REQUIRE(ev.type == EventType::Trade);
    REQUIRE(ev.trade.qty == 500);
    REQUIRE(src.next(ev));
    REQUIRE(ev.type == EventType::Delta);
    REQUIRE(ev.bids[0].qty == 0);
    REQUIRE_FALSE(src.next(ev));
    std::remove(path.c_str());
}

TEST_CASE("Legacy v1 CSV groups rows by update id", "[source]") {
    std::string path = tmp_path("v1.csv");
    {
        std::ofstream f(path);
        f << "ts,event_type,first_update_id,final_update_id,side,price_ticks,qty\n"
          << "1,depth,5,5,B,10000,100\n1,depth,5,5,A,10010,50\n2,depth,6,6,B,10000,0\n";
    }
    NormalizedCsvSource src(path);
    REQUIRE(src.is_legacy());
    REQUIRE_FALSE(src.has_trades());
    MarketEvent ev;
    REQUIRE(src.next(ev));
    REQUIRE(ev.bids.size() == 1);
    REQUIRE(ev.asks.size() == 1);
    REQUIRE(src.next(ev));
    REQUIRE(ev.bids[0].qty == 0);
    REQUIRE_FALSE(src.next(ev));
    std::remove(path.c_str());
}
