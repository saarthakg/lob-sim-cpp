#include "../../include/sim/portfolio.hpp"
#include <algorithm>
#include <cstdlib>

void Portfolio::on_fill(Side side, Price px, Qty qty, double fee) {
    if (qty <= 0) return;
    const double fill_px = inst_.price(px);
    const double units   = inst_.qty(qty);
    const Qty    signed_q = (side == Side::Bid) ? qty : -qty;

    cash_     -= fee;
    fees_     += fee;
    notional_ += fill_px * units;
    cash_     += (side == Side::Bid) ? -fill_px * units : fill_px * units;

    const bool same_dir = (position_ == 0) || ((position_ > 0) == (signed_q > 0));
    if (same_dir) {
        const double old_units = inst_.qty(std::llabs(position_));
        avg_cost_ = (avg_cost_ * old_units + fill_px * units) / (old_units + units);
        position_ += signed_q;
        return;
    }
    // Reducing (and possibly flipping) the position.
    const Qty    closing   = std::min<Qty>(qty, std::llabs(position_));
    const double close_u   = inst_.qty(closing);
    const double per_unit  = (position_ > 0) ? (fill_px - avg_cost_) : (avg_cost_ - fill_px);
    realized_ += per_unit * close_u;
    position_ += signed_q;
    if (position_ == 0)            avg_cost_ = 0.0;
    else if (qty > closing)        avg_cost_ = fill_px;   // flipped: remainder opened at fill_px
}

double Portfolio::unrealized_pnl(double mark_px_ticks) const {
    if (position_ == 0) return 0.0;
    const double mark = mark_px_ticks * inst_.tick_size;
    return (mark - avg_cost_) * inst_.qty(position_);
}
