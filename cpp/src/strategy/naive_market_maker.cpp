#include "../../include/strategy/naive_market_maker.hpp"
#include "../../include/strategy/quote_manager.hpp"

void NaiveMarketMaker::on_timer(StrategyContext& ctx) {
    const OrderBook& book = ctx.book();
    if (book.empty()) return;
    const Price bid = book.best_bid()->price - cfg_.offset_ticks;
    const Price ask = book.best_ask()->price + cfg_.offset_ticks;
    const Qty pos = ctx.position();
    std::vector<Price> bids, asks;
    if (pos + cfg_.order_qty <= cfg_.max_position) bids.push_back(bid);
    if (pos - cfg_.order_qty >= -cfg_.max_position) asks.push_back(ask);
    sync_quotes(ctx, Side::Bid, bids, cfg_.order_qty);
    sync_quotes(ctx, Side::Ask, asks, cfg_.order_qty);
}
