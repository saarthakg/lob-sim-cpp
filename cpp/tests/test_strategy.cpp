#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "../include/strategy/market_maker.hpp"
#include "../include/strategy/imbalance_strategy.hpp"
#include "../include/strategy/twap_strategy.hpp"
#include "../include/sim/exchange_sim.hpp"
#include "../include/sim/portfolio.hpp"
#include "../include/core/order_book.hpp"

static BinanceDepthEvent make_tick(Timestamp ts, UpdateId uid) {
    BinanceDepthEvent ev{};
    ev.event_time      = ts;
    ev.first_update_id = uid;
    ev.final_update_id = uid;
    return ev;
}

TEST_CASE("MarketMaker: inventory skew uses real position units, not raw scaled qty",
          "[strategy]") {
    // Regression test for a unit-scaling bug: pos (Qty) is scaled by
    // TICK_MULTIPLIER just like price, so skew_coefficient * pos was ~100x
    // too large a tick offset. Fixed version divides pos by TICK_MULTIPLIER
    // first. Uses a spread matched to 2*target_half_spread_ticks so the
    // (separate) touch-relative clamp is a no-op and skew is directly
    // observable in the quoted price.
    OrderBook book;
    BookSnapshot snap{};
    snap.last_update_id = 1;
    snap.bids = {{6500000, 1000}};
    snap.asks = {{6500010, 1000}};  // mid = 6500005, spread = 10 ticks
    book.load_snapshot(snap);

    MarketMaker::Config cfg;
    cfg.target_half_spread_ticks = 5;
    cfg.quote_size       = 100;
    cfg.max_inventory     = 1000;
    cfg.skew_coefficient  = 0.5;   // ticks of skew per real unit of inventory
    cfg.max_spread_ticks  = 1000;
    MarketMaker mm(cfg);

    ExchangeSim exchange;
    Portfolio portfolio;
    // Establish a long position of 3.00 real units (300 in scaled Qty) directly,
    // bypassing the exchange (Portfolio tracks fills independently of ExchangeSim).
    portfolio.on_fill(Side::Bid, 6500000, 300, 0.0);

    mm.on_market_event(1000, book, exchange, portfolio);

    // mid=6500005, half_spread=5, skew=round(0.5*3.0)=2:
    //   bid_px = 6500005 - 5 - 2 = 6499998  (>= best_bid+5=6500005 clamp? no, clamp is min(), unaffected)
    //   ask_px = 6500005 + 5 - 2 = 6500008  (<= best_ask-5=6500005 clamp? no, clamp is max(), unaffected)
    // Both land on fresh price levels (qty_ahead starts at 0), so a tick
    // touching those exact prices fills them, exposing the submitted price.
    BinanceDepthEvent tick = make_tick(2000, 101);
    tick.bid_updates = {{6499998, 500}};
    tick.ask_updates = {{6500008, 500}};
    auto reports = exchange.on_market_event(tick, book, portfolio);
    REQUIRE(reports.size() == 2);

    bool saw_bid = false, saw_ask = false;
    for (const auto& r : reports) {
        if (r.side == Side::Bid) { REQUIRE(r.fill_price == 6499998); saw_bid = true; }
        if (r.side == Side::Ask) { REQUIRE(r.fill_price == 6500008); saw_ask = true; }
    }
    REQUIRE(saw_bid);
    REQUIRE(saw_ask);
}

TEST_CASE("MarketMaker: quotes are clamped to a realistic distance from the real touch "
          "when the book spread is wide", "[strategy]") {
    // Regression test: with a fixed mid-relative offset, a wide book spread
    // would otherwise place quotes deep inside the spread at a synthetic
    // price with no resting depth and no realistic chance of ever being
    // touched by genuine trading activity (see docs/assumptions.md,
    // "Inside-Spread Quoting"). The fix clamps price improvement over the
    // real best bid/ask to target_half_spread_ticks.
    OrderBook book;
    BookSnapshot snap{};
    snap.last_update_id = 1;
    snap.bids = {{6500000, 1000}};
    snap.asks = {{6500200, 1000}};  // mid = 6500100, spread = 200 ticks
    book.load_snapshot(snap);

    MarketMaker::Config cfg;
    cfg.target_half_spread_ticks = 1;
    cfg.max_spread_ticks         = 1000;
    MarketMaker mm(cfg);

    ExchangeSim exchange;
    Portfolio portfolio;  // flat: no skew to worry about

    mm.on_market_event(1000, book, exchange, portfolio);

    // Raw mid-relative prices would be 6500099/6500101 (deep inside the
    // spread). Clamped: bid <= best_bid+1=6500001, ask >= best_ask-1=6500199.
    BinanceDepthEvent tick = make_tick(2000, 101);
    tick.bid_updates = {{6500001, 500}};
    tick.ask_updates = {{6500199, 500}};
    auto reports = exchange.on_market_event(tick, book, portfolio);
    REQUIRE(reports.size() == 2);

    bool saw_bid = false, saw_ask = false;
    for (const auto& r : reports) {
        if (r.side == Side::Bid) { REQUIRE(r.fill_price == 6500001); saw_bid = true; }
        if (r.side == Side::Ask) { REQUIRE(r.fill_price == 6500199); saw_ask = true; }
    }
    REQUIRE(saw_bid);
    REQUIRE(saw_ask);
}

TEST_CASE("MarketMaker: suppresses the bid once long inventory hits the max", "[strategy]") {
    OrderBook book;
    BookSnapshot snap{};
    snap.last_update_id = 1;
    snap.bids = {{6500000, 1000}};
    snap.asks = {{6500200, 1000}};
    book.load_snapshot(snap);

    MarketMaker::Config cfg;
    cfg.max_inventory    = 500;
    cfg.max_spread_ticks = 1000;  // book spread is 200 ticks; don't skip quoting
    MarketMaker mm(cfg);

    ExchangeSim exchange;
    Portfolio portfolio;
    portfolio.on_fill(Side::Bid, 6500000, 500, 0.0);  // at the max long inventory

    mm.on_market_event(1000, book, exchange, portfolio);

    // Raw ask_px = mid+1-skew = 6500100+1-3 = 6500098, but the 200-tick spread
    // clamps it to best_ask-1 = 6500199. Bid is suppressed entirely (inventory
    // at max), so only the ask needs a touch.
    BinanceDepthEvent tick = make_tick(2000, 101);
    tick.ask_updates = {{6500199, 500}};
    auto reports = exchange.on_market_event(tick, book, portfolio);

    // Only the ask (inventory-reducing side) should have been placed and filled.
    REQUIRE(reports.size() == 1);
    REQUIRE(reports[0].side == Side::Ask);
}

TEST_CASE("ImbalanceStrategy: strong bid pressure triggers an aggressive buy", "[strategy]") {
    OrderBook book;
    BookSnapshot snap{};
    snap.last_update_id = 1;
    snap.bids = {{6500000, 3000}};
    snap.asks = {{6500100, 500}};  // imbalance(5) = (3000-500)/3500 ~= 0.714 > 0.6
    book.load_snapshot(snap);

    ImbalanceStrategy strat;
    ExchangeSim exchange;
    Portfolio portfolio;

    strat.on_market_event(1000, book, exchange, portfolio);
    REQUIRE(portfolio.position() == 100);  // default order_size, fully filled
}

TEST_CASE("ImbalanceStrategy: cooldown suppresses re-triggering immediately after a trade",
          "[strategy]") {
    OrderBook book;
    BookSnapshot snap{};
    snap.last_update_id = 1;
    snap.bids = {{6500000, 3000}};
    snap.asks = {{6500100, 500}};
    book.load_snapshot(snap);

    ImbalanceStrategy::Config cfg;
    cfg.cooldown_events = 3;
    ImbalanceStrategy strat(cfg);
    ExchangeSim exchange;
    Portfolio portfolio;

    strat.on_market_event(1000, book, exchange, portfolio);
    REQUIRE(portfolio.position() == 100);

    // Flatten so the "only trade when flat" guard doesn't also suppress it —
    // isolate the cooldown behaviour specifically.
    portfolio.on_fill(Side::Ask, 6500000, 100, 0.0);
    REQUIRE(portfolio.position() == 0);

    strat.on_market_event(1001, book, exchange, portfolio);
    REQUIRE(portfolio.position() == 0);  // still cooling down, no new trade
}

TEST_CASE("TwapStrategy: slices fire on schedule and sum to exactly total_qty", "[strategy]") {
    OrderBook book;
    BookSnapshot snap{};
    snap.last_update_id = 1;
    snap.bids = {{6500000, 100000}};
    snap.asks = {{6500100, 100000}};  // deep enough that every slice fills fully
    book.load_snapshot(snap);

    TwapStrategy::Config cfg;
    cfg.side        = Side::Bid;
    cfg.total_qty   = 1003;   // deliberately not evenly divisible by num_slices
    cfg.num_slices  = 5;
    cfg.interval_us = 1000;
    TwapStrategy twap(cfg);

    ExchangeSim exchange;
    Portfolio portfolio;

    twap.on_market_event(0, book, exchange, portfolio);
    REQUIRE(twap.slices_sent() == 1);
    REQUIRE(portfolio.position() == 200);

    twap.on_market_event(999, book, exchange, portfolio);
    REQUIRE(twap.slices_sent() == 1);  // not yet time for slice 2

    twap.on_market_event(1000, book, exchange, portfolio);
    REQUIRE(twap.slices_sent() == 2);

    // Jump far ahead: catches up on all remaining slices in one call.
    twap.on_market_event(10000, book, exchange, portfolio);
    REQUIRE(twap.finished());
    REQUIRE(twap.slices_sent() == 5);
    REQUIRE(portfolio.position() == 1003);  // remainder landed in the final slice

    // Further ticks after completion are no-ops.
    twap.on_market_event(20000, book, exchange, portfolio);
    REQUIRE(twap.slices_sent() == 5);
    REQUIRE(portfolio.position() == 1003);
}
