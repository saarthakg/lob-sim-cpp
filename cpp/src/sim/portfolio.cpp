#include "../../include/sim/portfolio.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

void Portfolio::on_fill(Side side, Price px, Qty qty, double fee) {
    cash_ -= fee;

    if (side == Side::Bid) {
        // Buying: spend cash, increase position
        double fill_px = static_cast<double>(px);
        if (position_ >= 0) {
            // Adding to long position: update average cost
            double total_cost = avg_cost_ * static_cast<double>(position_) + fill_px * static_cast<double>(qty);
            position_ += qty;
            avg_cost_ = (position_ > 0) ? (total_cost / static_cast<double>(position_)) : 0.0;
        } else {
            // Covering short position
            double pnl_per_unit = avg_cost_ - fill_px;
            Qty cover_qty = std::min(qty, -position_);
            realized_ += pnl_per_unit * static_cast<double>(cover_qty);
            position_ += qty;
            if (position_ > 0) {
                // Flipped to long
                avg_cost_ = fill_px;
            } else if (position_ == 0) {
                avg_cost_ = 0.0;
            }
            // If still short, avg_cost stays the same
        }
        cash_ -= fill_px * static_cast<double>(qty);
    } else {
        // Selling: receive cash, decrease position
        double fill_px = static_cast<double>(px);
        if (position_ <= 0) {
            // Adding to short position
            double total_cost = avg_cost_ * static_cast<double>(-position_) + fill_px * static_cast<double>(qty);
            position_ -= qty;
            avg_cost_ = (position_ < 0) ? (total_cost / static_cast<double>(-position_)) : 0.0;
        } else {
            // Reducing long position
            double pnl_per_unit = fill_px - avg_cost_;
            Qty reduce_qty = std::min(qty, position_);
            realized_ += pnl_per_unit * static_cast<double>(reduce_qty);
            position_ -= qty;
            if (position_ < 0) {
                // Flipped to short
                avg_cost_ = fill_px;
            } else if (position_ == 0) {
                avg_cost_ = 0.0;
            }
        }
        cash_ += fill_px * static_cast<double>(qty);
    }
}

double Portfolio::realized_pnl() const { return realized_; }

double Portfolio::unrealized_pnl(double mark_price) const {
    if (position_ == 0) return 0.0;
    return (mark_price - avg_cost_) * static_cast<double>(position_);
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
