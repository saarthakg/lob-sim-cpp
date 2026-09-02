#include "../../include/strategy/market_maker.hpp"
#include <cmath>

MarketMaker::MarketMaker(Config config) : config_(config) {}
MarketMaker::MarketMaker() : MarketMaker(Config{}) {}

void MarketMaker::on_market_event(Timestamp ts, const OrderBook& book,
                                   ExchangeSim& exchange, Portfolio& portfolio) {
    auto bid_opt = book.best_bid();
    auto ask_opt = book.best_ask();
    if (!bid_opt || !ask_opt) return;

    double spread_ticks = book.spread();
    if (spread_ticks > static_cast<double>(config_.max_spread_ticks)) return;

    Price mid = static_cast<Price>(std::round(book.mid_price()));

    // Re-quote if mid moved by more than 1 tick
    if (last_mid_ != 0 && std::abs(static_cast<long long>(mid - last_mid_)) <= 1) return;

    cancel_existing(exchange);
    last_mid_ = mid;
    place_quotes(ts, book, exchange, portfolio);
}

void MarketMaker::cancel_existing(ExchangeSim& exchange) {
    if (bid_order_id_ != 0) {
        exchange.cancel_order(bid_order_id_);
        bid_order_id_ = 0;
    }
    if (ask_order_id_ != 0) {
        exchange.cancel_order(ask_order_id_);
        ask_order_id_ = 0;
    }
}

void MarketMaker::place_quotes(Timestamp ts, const OrderBook& book,
                                ExchangeSim& exchange, Portfolio& portfolio) {
    Price mid = static_cast<Price>(std::round(book.mid_price()));
    Qty   pos = portfolio.position();

    // Inventory skew: positive inventory → shift quotes down to sell more
    Price skew = static_cast<Price>(std::round(config_.skew_coefficient * static_cast<double>(pos)));

    Price bid_px = mid - static_cast<Price>(config_.target_half_spread_ticks) - skew;
    Price ask_px = mid + static_cast<Price>(config_.target_half_spread_ticks) - skew;

    // Don't quote if inventory exceeds max (let position unwind first)
    if (std::abs(static_cast<long long>(pos)) >= config_.max_inventory) {
        // Only quote the side that reduces inventory
        if (pos > 0) bid_px = 0;     // suppress bid
        else          ask_px = 0;    // suppress ask
    }

    if (bid_px > 0) {
        OrderId oid = exchange.next_order_id();
        SimOrder order{};
        order.order_id      = oid;
        order.ts            = ts;
        order.side          = Side::Bid;
        order.type          = OrderType::Limit;
        order.price         = bid_px;
        order.qty           = config_.quote_size;
        order.strategy_name = name();
        exchange.submit_limit_order(order, book);
        bid_order_id_ = oid;
    }

    if (ask_px > 0) {
        OrderId oid = exchange.next_order_id();
        SimOrder order{};
        order.order_id      = oid;
        order.ts            = ts;
        order.side          = Side::Ask;
        order.type          = OrderType::Limit;
        order.price         = ask_px;
        order.qty           = config_.quote_size;
        order.strategy_name = name();
        exchange.submit_limit_order(order, book);
        ask_order_id_ = oid;
    }
}
