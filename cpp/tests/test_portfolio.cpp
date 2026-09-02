#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "../include/sim/portfolio.hpp"

TEST_CASE("Portfolio: realized_pnl and total_pnl are gross of fees", "[portfolio]") {
    // Regression guard for a real gap: Portfolio deducts fees from cash_ but
    // never from realized_/total_pnl, so those two are trading PnL only.
    // Buy 1.00 unit @ 100.00, fee 5.00; sell 1.00 unit @ 110.00, fee 5.00.
    Portfolio p;
    p.on_fill(Side::Bid, 10000, 100, 5.0);   // price=100.00, qty=1.00
    p.on_fill(Side::Ask, 11000, 100, 5.0);   // price=110.00, qty=1.00

    REQUIRE(p.position() == 0);
    REQUIRE(p.realized_pnl() == Catch::Approx(10.0));   // (110-100)*1.00, no fee deduction
    REQUIRE(p.total_pnl(110.0) == Catch::Approx(10.0)); // flat, so == realized
    REQUIRE(p.total_fees_paid() == Catch::Approx(10.0));
}

TEST_CASE("Portfolio: net_pnl subtracts cumulative fees from total_pnl", "[portfolio]") {
    Portfolio p;
    p.on_fill(Side::Bid, 10000, 100, 5.0);
    p.on_fill(Side::Ask, 11000, 100, 5.0);

    REQUIRE(p.net_pnl(110.0) == Catch::Approx(p.total_pnl(110.0) - p.total_fees_paid()));
    REQUIRE(p.net_pnl(110.0) == Catch::Approx(0.0));  // $10 gross profit, exactly offset by $10 fees
}

TEST_CASE("Portfolio: net_pnl reflects unrealized fees on an open position", "[portfolio]") {
    Portfolio p;
    p.on_fill(Side::Bid, 10000, 100, 2.5);  // still long 1.00 @ avg cost 100.00

    REQUIRE(p.position() == 100);
    REQUIRE(p.total_fees_paid() == Catch::Approx(2.5));
    // Mark at 105.00: unrealized = (105-100)*1.00 = 5.00, gross total = 5.00
    REQUIRE(p.total_pnl(105.0) == Catch::Approx(5.0));
    REQUIRE(p.net_pnl(105.0) == Catch::Approx(2.5));  // 5.00 gross - 2.50 fee paid so far
}

TEST_CASE("Portfolio: total_fees_paid accumulates across many fills", "[portfolio]") {
    Portfolio p;
    for (int i = 0; i < 5; ++i) {
        p.on_fill(Side::Bid, 10000, 20, 1.0);
    }
    REQUIRE(p.total_fees_paid() == Catch::Approx(5.0));
    REQUIRE(p.position() == 100);
}
