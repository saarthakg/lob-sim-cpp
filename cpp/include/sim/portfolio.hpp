#pragma once
#include "../core/types.hpp"
#include "../core/side.hpp"

// Position and PnL accounting in real units (quote currency). Average-cost
// realized PnL; fees accumulate separately so gross and net are both exact.
class Portfolio {
public:
    explicit Portfolio(Instrument inst = Instrument::legacy()) : inst_(std::move(inst)) {}

    void on_fill(Side side, Price px, Qty qty, double fee);

    Qty    position()        const { return position_; }      // lots; + long, - short
    double position_units()  const { return inst_.qty(position_); }
    double cash()            const { return cash_; }          // net of fees
    double realized_pnl()    const { return realized_; }      // gross of fees
    double unrealized_pnl(double mark_px_ticks) const;        // mark in ticks (may be fractional)
    double total_pnl(double mark_px_ticks) const { return realized_ + unrealized_pnl(mark_px_ticks); }
    double total_fees_paid() const { return fees_; }          // negative if net rebates
    double net_pnl(double mark_px_ticks) const { return total_pnl(mark_px_ticks) - fees_; }
    double traded_notional() const { return notional_; }
    double avg_cost()        const { return avg_cost_; }      // quote ccy per unit

    const Instrument& instrument() const { return inst_; }

private:
    Instrument inst_;
    Qty    position_ = 0;
    double cash_     = 0.0;
    double avg_cost_ = 0.0;
    double realized_ = 0.0;
    double fees_     = 0.0;
    double notional_ = 0.0;
};
