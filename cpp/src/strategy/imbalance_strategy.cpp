#include "../../include/strategy/imbalance_strategy.hpp"

ImbalanceStrategy::ImbalanceStrategy(Config config) : config_(config) {}
ImbalanceStrategy::ImbalanceStrategy() : ImbalanceStrategy(Config{}) {}

void ImbalanceStrategy::on_market_event(Timestamp ts, const OrderBook& book,
                                         ExchangeSim& exchange, Portfolio& portfolio) {
    if (cooldown_remaining_ > 0) {
        --cooldown_remaining_;
        return;
    }

    // Only trade when flat (simple version)
    if (portfolio.position() != 0) return;

    double imb = book.imbalance(config_.depth_levels);

    auto best_bid = book.best_bid();
    auto best_ask = book.best_ask();
    if (!best_bid || !best_ask) return;

    if (imb > config_.imbalance_threshold) {
        // Strong buy pressure → aggressive buy
        OrderId oid = exchange.next_order_id();
        SimOrder order{};
        order.order_id      = oid;
        order.ts            = ts;
        order.side          = Side::Bid;
        order.type          = OrderType::Market;
        order.price         = 0;
        order.qty           = config_.order_size;
        order.strategy_name = name();
        exchange.submit_market_order(order, book, portfolio);
        cooldown_remaining_ = config_.cooldown_events;

    } else if (imb < -config_.imbalance_threshold) {
        // Strong sell pressure → aggressive sell
        OrderId oid = exchange.next_order_id();
        SimOrder order{};
        order.order_id      = oid;
        order.ts            = ts;
        order.side          = Side::Ask;
        order.type          = OrderType::Market;
        order.price         = 0;
        order.qty           = config_.order_size;
        order.strategy_name = name();
        exchange.submit_market_order(order, book, portfolio);
        cooldown_remaining_ = config_.cooldown_events;
    }
}
