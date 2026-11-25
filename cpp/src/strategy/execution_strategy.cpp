#include "../../include/strategy/execution_strategy.hpp"

#include <algorithm>

ExecutionStrategy::ExecutionStrategy(Config cfg) : cfg_(std::move(cfg)) {
    cfg_.slices = std::max(cfg_.slices, 1);
    if (!cfg_.csv_path.empty()) {
        out_ = std::fopen(cfg_.csv_path.c_str(), "w");
        if (out_)
            std::fprintf(out_, "id,side,start,end,arrival_mid,arrival_spread_bps,qty,maker_share,avg_px,"
                               "shortfall_bps,fee_bps,total_bps\n");
    }
}

ExecutionStrategy::~ExecutionStrategy() {
    if (out_) std::fclose(out_);
}

Qty ExecutionStrategy::due(int slots) const {
    return cfg_.parent_qty * std::clamp(slots, 0, cfg_.slices) / cfg_.slices;
}

void ExecutionStrategy::step(StrategyContext& ctx) {
    const OrderBook& book = ctx.book();
    if (book.empty() || book.crossed()) return;
    const Timestamp now = ctx.now();
    const int64_t period = cfg_.horizon_us + cfg_.gap_us;
    if (t0_ == 0) t0_ = now;

    if (!active_) {
        if (now < t0_ + next_id_ * period) return;
        // Skip slots missed entirely (a data gap or an overrunning parent).
        next_id_ = static_cast<int>((now - t0_) / period);
        if (now - (t0_ + next_id_ * period) > cfg_.gap_us) { ++next_id_; return; }
        p_ = Parent{};
        p_.id    = next_id_++;
        p_.side  = (p_.id % 2 == 0) ? Side::Bid : Side::Ask;
        p_.start = now;
        p_.arrival_mid    = book.mid_price();
        p_.arrival_spread = book.spread();
        active_ = true;
    }

    const int64_t slot_us = std::max<int64_t>(cfg_.horizon_us / cfg_.slices, 1);
    const int slot = static_cast<int>(std::min<int64_t>((now - p_.start) / slot_us, cfg_.slices));
    const bool passive = cfg_.algo != Algo::Cross;
    const Qty overdue = due(passive ? slot : slot + 1) - p_.filled;   // must cross now
    const Qty working = due(slot + 1) - p_.filled;                    // may still rest

    const Level near = (p_.side == Side::Bid) ? *book.best_bid() : *book.best_ask();
    bool run_away = false;   // Smart: the book says the price is about to leave us behind
    if (cfg_.algo == Algo::Smart) {
        const double imb = book.imbalance(1);
        run_away = (p_.side == Side::Bid) ? imb > cfg_.cross_imbalance : imb < -cfg_.cross_imbalance;
    }

    if (child_ != 0) {
        if (child_cancelling_ || child_is_market_) return;   // wait for the exchange
        const bool moved_away = (p_.side == Side::Bid) ? child_px_ < near.price : child_px_ > near.price;
        const bool repeg = moved_away && cfg_.algo != Algo::Join;
        if (overdue > 0 || repeg || run_away) {
            ctx.cancel(child_);
            child_cancelling_ = true;
        }
        return;
    }
    const Qty cross_now = run_away ? working : overdue;
    if (cross_now > 0) {
        child_ = ctx.submit_market(p_.side, cross_now);
        child_is_market_ = true;
    } else if (passive && working > 0) {
        child_ = ctx.submit_limit(p_.side, near.price, working, TimeInForce::PostOnly);
        child_px_ = near.price;
        child_is_market_ = false;
    }
    child_cancelling_ = false;
}

void ExecutionStrategy::on_order_update(StrategyContext& ctx, const ExecutionReport& rep) {
    if (rep.order_id != child_ || child_ == 0) return;
    if (rep.fill_qty > 0) {
        p_.filled += rep.fill_qty;
        p_.cost   += static_cast<double>(rep.fill_price) * static_cast<double>(rep.fill_qty);
        p_.fees   += rep.fee;
        if (rep.liquidity == Liquidity::Maker) p_.filled_maker += rep.fill_qty;
    }
    const bool dead = rep.status == OrderStatus::Filled || rep.status == OrderStatus::Cancelled ||
                      rep.status == OrderStatus::Rejected;
    if (!dead) return;
    child_ = 0;
    child_cancelling_ = false;
    if (active_ && p_.filled >= cfg_.parent_qty) finish(ctx);
}

void ExecutionStrategy::finish(StrategyContext& ctx) {
    active_ = false;
    p_.end  = ctx.now();
    done_.push_back(p_);
    if (!out_) return;
    const Instrument& inst = ctx.instrument();
    const double avg   = p_.cost / static_cast<double>(p_.filled);
    const double sign  = (p_.side == Side::Bid) ? 1.0 : -1.0;
    const double notional = p_.cost * inst.tick_size * inst.lot_size;
    const double sf    = sign * (avg - p_.arrival_mid) / p_.arrival_mid * 1e4;
    const double feebp = p_.fees / notional * 1e4;
    std::fprintf(out_, "%d,%s,%lld,%lld,%.8g,%.4f,%.8g,%.4f,%.10g,%.4f,%.4f,%.4f\n", p_.id,
                 p_.side == Side::Bid ? "buy" : "sell", static_cast<long long>(p_.start),
                 static_cast<long long>(p_.end), p_.arrival_mid * inst.tick_size,
                 p_.arrival_spread / p_.arrival_mid * 1e4, inst.qty(p_.filled),
                 static_cast<double>(p_.filled_maker) / static_cast<double>(p_.filled), avg * inst.tick_size, sf, feebp,
                 sf + feebp);
}
