#pragma once
#include "../core/order_book.hpp"
#include "../core/types.hpp"
#include "../sim/exchange_sim.hpp"
#include "../sim/portfolio.hpp"
#include <string>

class Strategy {
public:
    virtual ~Strategy() = default;

    virtual void on_market_event(
        Timestamp        ts,
        const OrderBook& book,
        ExchangeSim&     exchange,
        Portfolio&       portfolio) = 0;

    virtual std::string name() const = 0;
};
