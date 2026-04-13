#include "../../include/sim/portfolio.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

void Portfolio::on_fill(Side side, Price px, Qty qty, double fee) {
    cash_ -= fee;

    if (side == Side::Bid) {
        // Buying: spend cash, increase position
        double fill_px = static_cast<double>(px)  / TICK_MULTIPLIER;  // USD
        double qty_d   = static_cast<double>(qty) / TICK_MULTIPLIER;  // units
        if (position_ >= 0) {
            // Adding to long position: update average cost
            double pos_units  = static_cast<double>(position_) / TICK_MULTIPLIER;
            double total_cost = avg_cost_ * pos_units + fill_px * qty_d;
            position_ += qty;
            double new_pos_units = static_cast<double>(position_) / TICK_MULTIPLIER;
            avg_cost_ = (position_ > 0) ? (total_cost / new_pos_units) : 0.0;
        } else {
            // Covering short position
            double pnl_per_unit = avg_cost_ - fill_px;
            Qty cover_qty = std::min(qty, -position_);
            realized_ += pnl_per_unit * (static_cast<double>(cover_qty) / TICK_MULTIPLIER);
            position_ += qty;
            if (position_ > 0) {
                // Flipped to long
                avg_cost_ = fill_px;
            } else if (position_ == 0) {
                avg_cost_ = 0.0;
            }
            // If still short, avg_cost stays the same
        }
        cash_ -= fill_px * qty_d;
    } else {
        // Selling: receive cash, decrease position
        double fill_px = static_cast<double>(px)  / TICK_MULTIPLIER;  // USD
        double qty_d   = static_cast<double>(qty) / TICK_MULTIPLIER;  // units
        if (position_ <= 0) {
            // Adding to short position
            double pos_units  = static_cast<double>(-position_) / TICK_MULTIPLIER;
            double total_cost = avg_cost_ * pos_units + fill_px * qty_d;
            position_ -= qty;
            double new_pos_units = static_cast<double>(-position_) / TICK_MULTIPLIER;
            avg_cost_ = (position_ < 0) ? (total_cost / new_pos_units) : 0.0;
        } else {
            // Reducing long position
            double pnl_per_unit = fill_px - avg_cost_;
            Qty reduce_qty = std::min(qty, position_);
            realized_ += pnl_per_unit * (static_cast<double>(reduce_qty) / TICK_MULTIPLIER);
            position_ -= qty;
            if (position_ < 0) {
                // Flipped to short
                avg_cost_ = fill_px;
            } else if (position_ == 0) {
                avg_cost_ = 0.0;
            }
        }
        cash_ += fill_px * qty_d;
    }
}

double Portfolio::realized_pnl() const { return realized_; }

double Portfolio::unrealized_pnl(double mark_price) const {
    // mark_price must be in USD
    if (position_ == 0) return 0.0;
    double pos_units = static_cast<double>(position_) / TICK_MULTIPLIER;
    return (mark_price - avg_cost_) * pos_units;
}

Qty    Portfolio::position() const { return position_; }
double Portfolio::cash()     const { return cash_; }

double Portfolio::total_pnl(double mark_price) const {
    return realized_ + unrealized_pnl(mark_price);
}

void Portfolio::log_state(Timestamp ts, double mark_price) const {
    std::printf("[Portfolio ts=%lld] pos=%lld cash=%.2f realized=%.4f unrealized=%.4f total=%.4f\n",
        (long long)ts,
        (long long)position_,
        cash_,
        realized_,
        unrealized_pnl(mark_price),
        total_pnl(mark_price));
}
