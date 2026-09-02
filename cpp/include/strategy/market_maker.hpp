#pragma once
#include "strategy.hpp"

class MarketMaker : public Strategy {
public:
    struct Config {
        int    target_half_spread_ticks = 1;    // ticks from mid for each quote
        Qty    quote_size               = 100;  // quantity per quote
        Qty    max_inventory            = 500;  // max position before skewing
        double skew_coefficient         = 0.5;  // ticks of skew per unit of inventory
        int    max_spread_ticks         = 20;   // don't quote if book spread exceeds this

        // Minimum quotable price increment, in raw internal ticks. Real
        // venues often have a coarser minimum price increment than the raw
        // currency precision suggests — e.g. this project's Bybit BTCUSDT
        // capture only ever shows resting depth on multiples of 10 raw
        // ticks ($0.10), never on odd ticks. A quote that isn't a multiple
        // of the real tick size can never coincide with genuine market
        // activity, so it can never realistically fill. Set this to the
        // venue's actual tick size; 1 (the default) assumes every raw tick
        // is quotable, which is only true for a synthetic/idealized book.
        int    quote_tick_size          = 1;
    };

    MarketMaker();
    explicit MarketMaker(Config config);

    void on_market_event(Timestamp ts, const OrderBook& book,
                         ExchangeSim& exchange, Portfolio& portfolio) override;

    std::string name() const override { return "MarketMaker"; }

private:
    Config  config_;
    OrderId bid_order_id_ = 0;
    OrderId ask_order_id_ = 0;
    Price   last_mid_     = 0;

    void cancel_existing(ExchangeSim& exchange);
    void place_quotes(Timestamp ts, const OrderBook& book,
                      ExchangeSim& exchange, Portfolio& portfolio);
};
