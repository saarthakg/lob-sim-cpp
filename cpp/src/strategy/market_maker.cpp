#include "../../include/strategy/market_maker.hpp"
#include "../../include/core/types.hpp"
#include <algorithm>
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
    Price real_best_bid = book.best_bid()->price;  // on_market_event already verified both exist
    Price real_best_ask = book.best_ask()->price;

    // Inventory skew: positive inventory → shift quotes down to sell more.
    // pos is in scaled Qty units (× TICK_MULTIPLIER, same as price ticks), so it
    // must be converted to real inventory units before applying skew_coefficient
    // ("ticks of skew per unit of inventory") — otherwise skew ends up ~100x too
    // large (e.g. one 1.00-unit fill would swing quotes by 50 ticks against a
    // 1-tick target half-spread).
    double pos_units = static_cast<double>(pos) / TICK_MULTIPLIER;
    Price skew = static_cast<Price>(std::round(config_.skew_coefficient * pos_units));

    Price bid_px = mid - static_cast<Price>(config_.target_half_spread_ticks) - skew;
    Price ask_px = mid + static_cast<Price>(config_.target_half_spread_ticks) - skew;

    // Clamp price improvement over the REAL touch to target_half_spread_ticks.
    // mid-relative pricing alone breaks down when the book spread is wider
    // than 2*target_half_spread_ticks: e.g. a 10-tick spread with a 1-tick
    // target would otherwise quote 4 ticks inside best_bid/best_ask — a
    // synthetic price with no resting depth and no realistic chance a real
    // order ever trades through it (see docs/assumptions.md, "Inside-Spread
    // Quoting"). Capping keeps quotes within a realistic distance of the
    // touch regardless of how wide the prevailing spread happens to be; it's
    // a no-op whenever the spread is already <= 2*target_half_spread_ticks.
    Price improve = static_cast<Price>(config_.target_half_spread_ticks);
    bid_px = std::min(bid_px, real_best_bid + improve);
    ask_px = std::max(ask_px, real_best_ask - improve);

    // Snap to the venue's real minimum price increment. Even a touch-relative
    // quote can miss every level the market actually trades at if the venue's
    // tick size is coarser than our raw internal tick (see Config::quote_tick_size).
    // Bid rounds down (never bid more than intended) and ask rounds up (never
    // offer less than intended), so both stay within the real touch.
    if (config_.quote_tick_size > 1) {
        Price t = static_cast<Price>(config_.quote_tick_size);
        if (bid_px > 0) bid_px = (bid_px / t) * t;
        if (ask_px > 0) ask_px = ((ask_px + t - 1) / t) * t;
    }

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
        // 0 == rejected (self-cross or risk limit) — don't track a phantom order.
        bid_order_id_ = exchange.submit_limit_order(order, book, portfolio);
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
        ask_order_id_ = exchange.submit_limit_order(order, book, portfolio);
    }
}
