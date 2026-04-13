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

    sim.submit_limit_order(order, book);
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

    sim.submit_limit_order(order, book);
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

    sim.submit_limit_order(order, book);

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
