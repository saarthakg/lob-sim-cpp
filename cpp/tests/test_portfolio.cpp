#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "../include/sim/portfolio.hpp"

using Catch::Approx;

static Instrument inst() {
    Instrument i;
    i.tick_size = 0.01;
    i.lot_size  = 0.01;
    return i;
}

TEST_CASE("Portfolio: round trip realizes PnL; fees tracked separately", "[portfolio]") {
    Portfolio p(inst());
    p.on_fill(Side::Bid, 10000, 100, 5.0);   // buy 1.00 @ 100.00
    p.on_fill(Side::Ask, 11000, 100, 5.0);   // sell 1.00 @ 110.00
    REQUIRE(p.position() == 0);
    REQUIRE(p.realized_pnl() == Approx(10.0));
    REQUIRE(p.total_fees_paid() == Approx(10.0));
    REQUIRE(p.net_pnl(11000) == Approx(0.0));
    REQUIRE(p.cash() == Approx(0.0));
    REQUIRE(p.traded_notional() == Approx(210.0));
}

TEST_CASE("Portfolio: unrealized PnL marks at a fractional-tick mid", "[portfolio]") {
    Portfolio p(inst());
    p.on_fill(Side::Bid, 10000, 200, 0.0);   // long 2 @ 100
    REQUIRE(p.unrealized_pnl(10050.5) == Approx(2 * 0.505));
    REQUIRE(p.net_pnl(10050.5) == Approx(p.total_pnl(10050.5)));
}

TEST_CASE("Portfolio: average cost across adds, partial close, and flip", "[portfolio]") {
    Portfolio p(inst());
    p.on_fill(Side::Ask, 10000, 100, 0.0);   // short 1 @ 100
    p.on_fill(Side::Ask, 10200, 100, 0.0);   // short 2 @ avg 101
    REQUIRE(p.avg_cost() == Approx(101.0));
    p.on_fill(Side::Bid, 9900, 100, 0.0);    // cover 1 @ 99 → +2
    REQUIRE(p.realized_pnl() == Approx(2.0));
    REQUIRE(p.position() == -100);
    p.on_fill(Side::Bid, 10000, 300, 0.0);   // cover 1 @ 100 (+1), then long 2 @ 100
    REQUIRE(p.realized_pnl() == Approx(3.0));
    REQUIRE(p.position() == 200);
    REQUIRE(p.avg_cost() == Approx(100.0));
}

TEST_CASE("Portfolio: rebates make net exceed gross", "[portfolio]") {
    Portfolio p(inst());
    p.on_fill(Side::Bid, 10000, 100, -0.01);
    p.on_fill(Side::Ask, 10000, 100, -0.01);
    REQUIRE(p.total_pnl(10000) == Approx(0.0));
    REQUIRE(p.net_pnl(10000) == Approx(0.02));
}
