#pragma once
#include "../core/types.hpp"
#include "../core/side.hpp"

class Portfolio {
public:
    void on_fill(Side side, Price px, Qty qty, double fee);

    double realized_pnl()                    const;  // gross of fees
    double unrealized_pnl(double mark_price) const;
    Qty    position()                        const;  // positive = long, negative = short
    double cash()                            const;  // in quote currency, net of fees
    double total_pnl(double mark_price)     const;   // gross of fees: realized + unrealized

    // Fees are deducted from cash_ on every fill but never subtracted from
    // realized_/total_pnl — those are trading PnL only. total_fees_paid() and
    // net_pnl() are what you actually want for "did this strategy make money":
    // total_pnl() alone systematically overstates results by the fee total.
    double total_fees_paid()                 const;
    double net_pnl(double mark_price)        const { return total_pnl(mark_price) - total_fees_paid(); }

    void log_state(Timestamp ts, double mark_price) const;

private:
    Qty    position_         = 0;
    double cash_             = 0.0;
    double avg_cost_         = 0.0;  // average entry price (in ticks)
    double realized_         = 0.0;
    double total_fees_paid_  = 0.0;
};
