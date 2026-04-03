#pragma once
#include "strategy.hpp"

class ImbalanceStrategy : public Strategy {
public:
    struct Config {
        int    depth_levels        = 5;
        double imbalance_threshold = 0.6;
        Qty    order_size          = 100;
        int    cooldown_events     = 50;  // events to skip after a trade
    };

    ImbalanceStrategy();
    explicit ImbalanceStrategy(Config config);

    void on_market_event(Timestamp ts, const OrderBook& book,
                         ExchangeSim& exchange, Portfolio& portfolio) override;

    std::string name() const override { return "ImbalanceStrategy"; }

private:
    Config config_;
    int    cooldown_remaining_ = 0;
};
