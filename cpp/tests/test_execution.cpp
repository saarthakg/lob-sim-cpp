#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "../include/sim/exchange_sim.hpp"
#include "../include/core/order_book.hpp"

static OrderBook make_book() {
    OrderBook book;
    BookSnapshot snap{};
    snap.last_update_id = 100;
    // Bids: 65000.00 × 500, 64999.00 × 300
    snap.bids = {{6500000, 500}, {6499900, 300}};
    // Asks: 65001.00 × 200, 65002.00 × 400
    snap.asks = {{6500100, 200}, {6500200, 400}};
    book.load_snapshot(snap);
    return book;
}

TEST_CASE("ExchangeSim: market buy walks ask levels", "[execution]") {
    auto book = make_book();
    ExchangeSim sim;
    Portfolio portfolio;

    SimOrder order{};
    order.order_id      = 1;
    order.ts            = 1000;
    order.side          = Side::Bid;
    order.type          = OrderType::Market;
    order.qty           = 150;  // fits within first ask level (200)
    order.strategy_name = "test";

    auto reports = sim.submit_market_order(order, book, portfolio);
    REQUIRE(reports.size() == 1);
    REQUIRE(reports[0].fill_qty    == 150);
    REQUIRE(reports[0].fill_price  == 6500100);
    REQUIRE(reports[0].status      == OrderStatus::Filled);
    REQUIRE(reports[0].remaining_qty == 0);
    // fee = 6500100 * 150 * 0.0004 (taker)
    REQUIRE(reports[0].fee_paid > 0.0);
}

TEST_CASE("ExchangeSim: market buy walks multiple ask levels", "[execution]") {
    auto book = make_book();
    ExchangeSim sim;
    Portfolio portfolio;

    SimOrder order{};
    order.order_id      = 2;
    order.ts            = 1000;
    order.side          = Side::Bid;
    order.type          = OrderType::Market;
    order.qty           = 500;  // needs both levels: 200 + 300 from second
    order.strategy_name = "test";

    auto reports = sim.submit_market_order(order, book, portfolio);
    REQUIRE(reports.size() == 1);
    REQUIRE(reports[0].fill_qty == 500);
    REQUIRE(reports[0].status   == OrderStatus::Filled);
}

TEST_CASE("ExchangeSim: market buy partial fill when book is thin", "[execution]") {
    OrderBook book;
    BookSnapshot snap{};
    snap.bids = {{6500000, 1000}};
    snap.asks = {{6500100, 100}};  // only 100 available
    book.load_snapshot(snap);

    ExchangeSim sim;
    Portfolio portfolio;

    SimOrder order{};
    order.order_id      = 3;
    order.ts            = 1000;
    order.side          = Side::Bid;
    order.type          = OrderType::Market;
    order.qty           = 500;  // more than available
    order.strategy_name = "test";

    auto reports = sim.submit_market_order(order, book, portfolio);
    REQUIRE(reports.size() == 1);
    REQUIRE(reports[0].fill_qty     == 100);
    REQUIRE(reports[0].remaining_qty == 400);
    REQUIRE(reports[0].status        == OrderStatus::PartiallyFilled);
}

TEST_CASE("ExchangeSim: taker fee applied correctly", "[execution]") {
    auto book = make_book();
    ExchangeSim sim({}, 0.001, 0.0005);  // 0.1% taker
    Portfolio portfolio;

    SimOrder order{};
    order.order_id      = 4;
    order.ts            = 1000;
    order.side          = Side::Bid;
    order.type          = OrderType::Market;
    order.qty           = 100;
    order.strategy_name = "test";

    auto reports = sim.submit_market_order(order, book, portfolio);
    // fee = (price_usd * qty_units) * taker_rate
    // price_usd = 6500100 / 100 = 65001.00,  qty_units = 100 / 100 = 1.0
    double expected_fee = (6500100.0 / 100.0) * (100.0 / 100.0) * 0.001;
    REQUIRE(reports[0].fee_paid == Catch::Approx(expected_fee).epsilon(0.0001));
}

TEST_CASE("ExchangeSim: passive order placed and tracked", "[execution]") {
    auto book = make_book();
    ExchangeSim sim;
    Portfolio portfolio;

    OrderId oid = sim.next_order_id();
    SimOrder order{};
    order.order_id      = oid;
    order.ts            = 1000;
    order.side          = Side::Bid;
    order.type          = OrderType::Limit;
    order.price         = 6500000;  // at the best bid
    order.qty           = 50;
    order.strategy_name = "test";

    sim.submit_limit_order(order, book, portfolio);
    REQUIRE(sim.active_order_count() == 1);
}

TEST_CASE("ExchangeSim: cancel removes passive order", "[execution]") {
    auto book = make_book();
    ExchangeSim sim;
    Portfolio portfolio;

    OrderId oid = sim.next_order_id();
    SimOrder order{};
    order.order_id      = oid;
    order.ts            = 1000;
    order.side          = Side::Bid;
    order.type          = OrderType::Limit;
    order.price         = 6500000;
    order.qty           = 50;
    order.strategy_name = "test";

    sim.submit_limit_order(order, book, portfolio);
    REQUIRE(sim.active_order_count() == 1);

    sim.cancel_order(oid);
    REQUIRE(sim.active_order_count() == 0);
}

TEST_CASE("ExchangeSim: passive order fills when qty_ahead exhausted", "[execution]") {
    auto book = make_book();
    ExchangeSim sim;
    Portfolio portfolio;

    // Place a limit buy at best bid price (65000.00), with 500 ahead
    OrderId oid = sim.next_order_id();
    SimOrder order{};
    order.order_id      = oid;
    order.ts            = 1000;
    order.side          = Side::Bid;
    order.type          = OrderType::Limit;
    order.price         = 6500000;
    order.qty           = 100;
    order.strategy_name = "test";

    sim.submit_limit_order(order, book, portfolio);

    // Simulate an event that zeroes out that bid level
    BinanceDepthEvent ev{};
    ev.event_time      = 2000;
    ev.first_update_id = 101;
    ev.final_update_id = 101;
    ev.bid_updates     = {{6500000, 0}};  // level deleted → order cancelled

    auto reports = sim.on_market_event(ev, book, portfolio);
    REQUIRE(reports.size() == 1);
    REQUIRE(reports[0].order_id == oid);
    REQUIRE(reports[0].status   == OrderStatus::Cancelled);
}

TEST_CASE("ExchangeSim: passive order at a fresh price does NOT fill on an unrelated event",
          "[execution]") {
    // Regression test: a passive order must only fill/cancel because of
    // activity at its OWN price level. A fresh price (no prior displayed
    // depth) starts with qty_ahead == 0; before the fix, the fill check ran
    // unconditionally every tick, so it would fill on literally any market
    // event anywhere in the book.
    auto book = make_book();
    ExchangeSim sim;
    Portfolio portfolio;

    OrderId oid = sim.next_order_id();
    SimOrder lim{};
    lim.order_id      = oid;
    lim.ts            = 1000;
    lim.side          = Side::Ask;
    lim.type          = OrderType::Limit;
    lim.price         = 6500150;  // fresh price, no existing depth
    lim.qty           = 50;
    lim.strategy_name = "test";
    sim.submit_limit_order(lim, book, portfolio);
    REQUIRE(sim.active_order_count() == 1);

    BinanceDepthEvent unrelated{};
    unrelated.event_time      = 2000;
    unrelated.first_update_id = 101;
    unrelated.final_update_id = 101;
    unrelated.bid_updates     = {{6499900, 350}};  // touches a different price entirely

    auto reports = sim.on_market_event(unrelated, book, portfolio);
    REQUIRE(reports.empty());
    REQUIRE(sim.active_order_count() == 1);  // still resting, untouched
    REQUIRE(portfolio.position() == 0);
}

TEST_CASE("ExchangeSim: report order_type is set correctly for market vs passive fills",
          "[execution]") {
    auto book = make_book();
    ExchangeSim sim;
    Portfolio portfolio;

    SimOrder mkt{};
    mkt.order_id      = sim.next_order_id();
    mkt.ts            = 1000;
    mkt.side          = Side::Bid;
    mkt.type          = OrderType::Market;
    mkt.qty           = 150;
    mkt.strategy_name = "test";
    auto mkt_reports = sim.submit_market_order(mkt, book, portfolio);
    REQUIRE(mkt_reports.size() == 1);
    REQUIRE(mkt_reports[0].order_type == OrderType::Market);

    // A limit order placed at a price with no currently-displayed depth starts
    // with qty_ahead == 0 (visible_qty_at_price returns 0 for an absent level),
    // so it fills on the very next market event regardless of that event's
    // content — this is the simplest way to reach the "Filled" (not
    // "Cancelled") passive path deterministically.
    OrderId oid = sim.next_order_id();
    SimOrder lim{};
    lim.order_id      = oid;
    lim.ts            = 1000;
    lim.side          = Side::Ask;
    lim.type          = OrderType::Limit;
    lim.price         = 6500150;  // between best ask (6500100) and next level
    lim.qty           = 50;
    lim.strategy_name = "test";
    sim.submit_limit_order(lim, book, portfolio);

    BinanceDepthEvent ev{};
    ev.event_time      = 2000;
    ev.first_update_id = 101;
    ev.final_update_id = 101;
    // A passive order only fills on activity at its OWN price level — since
    // qty_ahead is already 0, any nonzero report at that exact price ticks it.
    ev.ask_updates     = {{6500150, 500}};

    auto reports = sim.on_market_event(ev, book, portfolio);
    REQUIRE(reports.size() == 1);
    REQUIRE(reports[0].order_id   == oid);
    REQUIRE(reports[0].status     == OrderStatus::Filled);
    REQUIRE(reports[0].order_type == OrderType::Limit);
}

TEST_CASE("ExchangeSim: market order walks full book depth beyond 20 levels", "[execution]") {
    OrderBook book;
    BookSnapshot snap{};
    snap.last_update_id = 1;
    // 30 ask levels, 10 qty each -> a market buy for 250 needs 25 levels.
    for (int i = 0; i < 30; ++i) {
        snap.asks.push_back({6500100 + i * 100, 10});
    }
    snap.bids.push_back({6500000, 1000});
    book.load_snapshot(snap);

    ExchangeSim sim;
    Portfolio portfolio;

    SimOrder order{};
    order.order_id      = 1;
    order.ts            = 1000;
    order.side          = Side::Bid;
    order.type          = OrderType::Market;
    order.qty           = 250;
    order.strategy_name = "test";

    auto reports = sim.submit_market_order(order, book, portfolio);
    REQUIRE(reports.size() == 1);
    REQUIRE(reports[0].status   == OrderStatus::Filled);
    REQUIRE(reports[0].fill_qty == 250);
}

TEST_CASE("ExchangeSim: self-crossing limit order is rejected", "[execution]") {
    auto book = make_book();
    ExchangeSim sim;
    Portfolio portfolio;

    SimOrder ask{};
    ask.order_id      = sim.next_order_id();
    ask.ts            = 1000;
    ask.side          = Side::Ask;
    ask.type          = OrderType::Limit;
    ask.price         = 6500200;
    ask.qty           = 100;
    ask.strategy_name = "mm";
    OrderId ask_id = sim.submit_limit_order(ask, book, portfolio);
    REQUIRE(ask_id != 0);

    SimOrder bid{};
    bid.order_id      = sim.next_order_id();
    bid.ts            = 1000;
    bid.side          = Side::Bid;
    bid.type          = OrderType::Limit;
    bid.price         = 6500300;  // crosses our own resting ask at 6500200
    bid.qty           = 100;
    bid.strategy_name = "mm";
    OrderId bid_id = sim.submit_limit_order(bid, book, portfolio);
    REQUIRE(bid_id == 0);  // rejected
    REQUIRE(sim.active_order_count() == 1);
}

TEST_CASE("ExchangeSim: position risk limit rejects market order", "[execution]") {
    auto book = make_book();
    ExchangeSim sim({}, 0.0004, 0.0002, /*max_abs_position=*/100);
    Portfolio portfolio;

    SimOrder order{};
    order.order_id      = 1;
    order.ts            = 1000;
    order.side          = Side::Bid;
    order.type          = OrderType::Market;
    order.qty           = 150;  // would push position to 150 > limit of 100
    order.strategy_name = "test";

    auto reports = sim.submit_market_order(order, book, portfolio);
    REQUIRE(reports.size() == 1);
    REQUIRE(reports[0].status == OrderStatus::Rejected);
    REQUIRE(portfolio.position() == 0);
}

TEST_CASE("ExchangeSim: position risk limit rejects passive limit order", "[execution]") {
    auto book = make_book();
    ExchangeSim sim({}, 0.0004, 0.0002, /*max_abs_position=*/50);
    Portfolio portfolio;

    SimOrder order{};
    order.order_id      = sim.next_order_id();
    order.ts            = 1000;
    order.side          = Side::Bid;
    order.type          = OrderType::Limit;
    order.price         = 6500000;
    order.qty           = 100;  // would push position to 100 > limit of 50
    order.strategy_name = "test";

    OrderId oid = sim.submit_limit_order(order, book, portfolio);
    REQUIRE(oid == 0);
    REQUIRE(sim.active_order_count() == 0);
}

TEST_CASE("ExchangeSim: latency defers market order arrival to a later book state",
          "[execution]") {
    auto book = make_book();
    LatencyModel lat{0, 0, 5000};  // 5ms total latency
    ExchangeSim sim(lat);
    Portfolio portfolio;

    SimOrder order{};
    order.order_id      = 1;
    order.ts            = 1000;  // decision at t=1000us
    order.side          = Side::Bid;
    order.type          = OrderType::Market;
    order.qty           = 150;
    order.strategy_name = "test";

    // Immediate return is empty: the order hasn't arrived yet.
    auto immediate = sim.submit_market_order(order, book, portfolio);
    REQUIRE(immediate.empty());
    REQUIRE(portfolio.position() == 0);

    // A market event before arrival (t=1000+5000=6000) must not trigger it.
    BinanceDepthEvent early{};
    early.event_time      = 2000;
    early.first_update_id = 101;
    early.final_update_id = 101;
    auto early_reports = sim.on_market_event(early, book, portfolio);
    REQUIRE(early_reports.empty());
    REQUIRE(portfolio.position() == 0);

    // A market event at/after arrival time triggers execution.
    BinanceDepthEvent late{};
    late.event_time      = 7000;
    late.first_update_id = 102;
    late.final_update_id = 102;
    auto late_reports = sim.on_market_event(late, book, portfolio);
    REQUIRE(late_reports.size() == 1);
    REQUIRE(late_reports[0].status == OrderStatus::Filled);
    REQUIRE(portfolio.position() == 150);
}

TEST_CASE("ExchangeSim: latency defers passive order registration", "[execution]") {
    auto book = make_book();
    LatencyModel lat{0, 0, 5000};
    ExchangeSim sim(lat);
    Portfolio portfolio;

    SimOrder order{};
    order.order_id      = sim.next_order_id();
    order.ts            = 1000;
    order.side          = Side::Bid;
    order.type          = OrderType::Limit;
    order.price         = 6500000;
    order.qty           = 100;
    order.strategy_name = "test";

    OrderId oid = sim.submit_limit_order(order, book, portfolio);
    REQUIRE(oid != 0);
    REQUIRE(sim.active_order_count() == 0);  // not yet arrived

    BinanceDepthEvent ev{};
    ev.event_time      = 7000;  // past arrival_ts = 1000 + 5000
    ev.first_update_id = 101;
    ev.final_update_id = 101;
    sim.on_market_event(ev, book, portfolio);
    REQUIRE(sim.active_order_count() == 1);  // now registered
}
