#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "../include/core/order_book.hpp"

TEST_CASE("OrderBook: load snapshot sets best bid and ask", "[orderbook]") {
    OrderBook book;
    BookSnapshot snap{};
    snap.last_update_id = 42;
    snap.bids = {{6500000, 1000}, {6499900, 2000}};
    snap.asks = {{6500100, 500},  {6500200, 800}};
    book.load_snapshot(snap);

    auto bb = book.best_bid();
    auto ba = book.best_ask();
    REQUIRE(bb.has_value());
    REQUIRE(ba.has_value());
    REQUIRE(bb->price == 6500000);
    REQUIRE(bb->qty   == 1000);
    REQUIRE(ba->price == 6500100);
    REQUIRE(ba->qty   == 500);
    REQUIRE(book.update_id() == 42);
}

TEST_CASE("OrderBook: apply update modifies a level", "[orderbook]") {
    OrderBook book;
    BookSnapshot snap{};
    snap.bids = {{6500000, 1000}};
    snap.asks = {{6500100, 500}};
    book.load_snapshot(snap);

    book.apply_update(Side::Bid, 6500000, 2500);
    REQUIRE(book.visible_qty_at_price(Side::Bid, 6500000) == 2500);
}

TEST_CASE("OrderBook: apply update with qty==0 deletes level", "[orderbook]") {
    OrderBook book;
    BookSnapshot snap{};
    snap.bids = {{6500000, 1000}, {6499900, 500}};
    snap.asks = {{6500100, 300}};
    book.load_snapshot(snap);

    book.apply_update(Side::Bid, 6500000, 0);
    auto bb = book.best_bid();
    REQUIRE(bb.has_value());
    REQUIRE(bb->price == 6499900);  // second level is now best
}

TEST_CASE("OrderBook: top-N ordering is correct", "[orderbook]") {
    OrderBook book;
    BookSnapshot snap{};
    snap.bids = {{6499800, 100}, {6500000, 300}, {6499900, 200}};
    snap.asks = {{6500200, 400}, {6500100, 500}, {6500300, 600}};
    book.load_snapshot(snap);

    auto bids = book.top_n_bids(3);
    REQUIRE(bids.size() == 3);
    REQUIRE(bids[0].price == 6500000);  // highest bid first
    REQUIRE(bids[1].price == 6499900);
    REQUIRE(bids[2].price == 6499800);

    auto asks = book.top_n_asks(3);
    REQUIRE(asks.size() == 3);
    REQUIRE(asks[0].price == 6500100);  // lowest ask first
    REQUIRE(asks[1].price == 6500200);
    REQUIRE(asks[2].price == 6500300);
}

TEST_CASE("OrderBook: spread and mid price", "[orderbook]") {
    OrderBook book;
    BookSnapshot snap{};
    snap.bids = {{6500000, 1000}};
    snap.asks = {{6500200, 500}};
    book.load_snapshot(snap);

    REQUIRE(book.spread() == Catch::Approx(200.0));
    REQUIRE(book.mid_price() == Catch::Approx(6500100.0));
}

TEST_CASE("OrderBook: imbalance calculation", "[orderbook]") {
    OrderBook book;
    BookSnapshot snap{};
    // 3000 total bid qty, 1000 total ask qty → imbalance = (3000-1000)/(3000+1000) = 0.5
    snap.bids = {{6500000, 1500}, {6499900, 1500}};
    snap.asks = {{6500100, 500},  {6500200, 500}};
    book.load_snapshot(snap);

    REQUIRE(book.imbalance(5) == Catch::Approx(0.5));
}

TEST_CASE("OrderBook: imbalance with equal sides is zero", "[orderbook]") {
    OrderBook book;
    BookSnapshot snap{};
    snap.bids = {{6500000, 1000}};
    snap.asks = {{6500100, 1000}};
    book.load_snapshot(snap);

    REQUIRE(book.imbalance(5) == Catch::Approx(0.0));
}

TEST_CASE("OrderBook: clear empties the book", "[orderbook]") {
    OrderBook book;
    BookSnapshot snap{};
    snap.bids = {{6500000, 1000}};
    snap.asks = {{6500100, 500}};
    book.load_snapshot(snap);

    book.clear();
    REQUIRE(!book.best_bid().has_value());
    REQUIRE(!book.best_ask().has_value());
}

TEST_CASE("OrderBook: spread is non-negative after valid bid/ask updates", "[orderbook]") {
    OrderBook book;
    BookSnapshot snap{};
    snap.bids = {{6500000, 1000}};
    snap.asks = {{6500200, 500}};
    book.load_snapshot(snap);

    book.apply_update(Side::Bid, 6500100, 800);
    REQUIRE(book.spread() >= 0.0);
    book.apply_update(Side::Ask, 6500300, 600);
    REQUIRE(book.spread() >= 0.0);
    book.apply_update(Side::Bid, 6500200, 0);  // delete crossing level
    REQUIRE(book.spread() >= 0.0);
}

TEST_CASE("OrderBook: delete nonexistent level is a no-op", "[orderbook]") {
    OrderBook book;
    BookSnapshot snap{};
    snap.bids = {{6500000, 1000}};
    snap.asks = {{6500100, 500}};
    book.load_snapshot(snap);

    // Deleting a level that doesn't exist should not crash or corrupt state
    REQUIRE_NOTHROW(book.apply_update(Side::Bid, 9999999, 0));
    auto bb = book.best_bid();
    REQUIRE(bb.has_value());
    REQUIRE(bb->price == 6500000);
}
