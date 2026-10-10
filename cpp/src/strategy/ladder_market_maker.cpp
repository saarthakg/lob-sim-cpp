#include "../../include/strategy/ladder_market_maker.hpp"
#include "../../include/strategy/quote_manager.hpp"

#include <algorithm>
#include <cmath>

void LadderMarketMaker::on_timer(StrategyContext& ctx) {
    const OrderBook& book = ctx.book();
    if (book.empty()) return;
    const Price bb = book.best_bid()->price;
    const Price ba = book.best_ask()->price;
    const double mid = book.mid_price();

    double alpha = 0.0;
    if (cfg_.alpha_micro != 0.0) alpha += cfg_.alpha_micro * (book.weighted_mid() - mid);
    if (cfg_.alpha_obi != 0.0) {
        Price band = std::max<Price>(1, static_cast<Price>(std::llround(mid * cfg_.obi_band_bps * 1e-4)));
        alpha += cfg_.alpha_obi * book.depth_imbalance(band);
    }
    const Qty pos = ctx.position();
    const double q = static_cast<double>(pos) / static_cast<double>(cfg_.order_qty);
    const double adj = alpha - cfg_.inventory_skew * q;

    const bool wide = (ba - bb) > cfg_.max_spread_ticks;
    std::vector<Price> bids, asks;
    for (int i = 0; i < cfg_.levels; ++i) {
        // On a wide book, don't stand at the touch (it's inside a gap that is
        // about to close); keep only the resting ladder behind it.
        bool touch = (i == 0);
        bool no_bid = touch && (wide || adj < -cfg_.pull_threshold || guard_.blocked(Side::Bid, bb));
        bool no_ask = touch && (wide || adj > cfg_.pull_threshold || guard_.blocked(Side::Ask, ba));
        if (pos + cfg_.order_qty <= cfg_.max_position && !no_bid) bids.push_back(bb - i);
        if (pos - cfg_.order_qty >= -cfg_.max_position && !no_ask) asks.push_back(ba + i);
    }
    // Keep (don't cancel) resting orders that are still inside the ladder
    // span, even if not in today's desired set — e.g. a touch order we'd no
    // longer *add* but which already has queue priority is only pulled when
    // the alpha says so, which `bids`/`asks` already encode for the touch.
    sync_quotes(ctx, Side::Bid, bids, cfg_.order_qty);
    sync_quotes(ctx, Side::Ask, asks, cfg_.order_qty);
}

void LadderMarketMaker::on_book(StrategyContext& ctx) {
    (void)ctx;
    guard_.on_book();
}

void LadderMarketMaker::on_trade(StrategyContext& ctx, const TradePrint& tr) { guard_.on_trade(ctx, tr); }
