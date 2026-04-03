#pragma once
#include "../core/types.hpp"
#include "../core/side.hpp"

class Portfolio {
public:
    void on_fill(Side side, Price px, Qty qty, double fee);

    double realized_pnl()                    const;
    double unrealized_pnl(double mark_price) const;
    Qty    position()                        const;  // positive = long, negative = short
    double cash()                            const;  // in quote currency
    double total_pnl(double mark_price)     const;

    void log_state(Timestamp ts, double mark_price) const;

private:
    Qty    position_  = 0;
    double cash_      = 0.0;
    double avg_cost_  = 0.0;  // average entry price (in ticks)
    double realized_  = 0.0;
};
