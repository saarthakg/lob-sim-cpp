#pragma once
#include "strategy.hpp"
#include <string>
#include <vector>

// Places never-cancelled post-only orders of one lot at scheduled
// (timestamp, price) points read from a CSV (probe,ts,price), all on one side.
// Used to score the simulator's fill model against L3 ground truth
// (python/analysis/queue_model_validation.py): each probe's fill time here is
// compared with the time the same order would truly have filled.
class ProbeStrategy : public Strategy {
public:
    ProbeStrategy(const std::string& probes_csv, Side side, const Instrument& inst,
                  std::string map_out_csv);
    ~ProbeStrategy() override;

    std::string name() const override { return "ProbeStrategy"; }
    void on_book(StrategyContext& ctx) override { place_due(ctx); }
    void on_trade(StrategyContext& ctx, const TradePrint&) override { place_due(ctx); }

private:
    struct Probe { long long id; Timestamp ts; Price price; };
    std::vector<Probe> probes_;
    size_t next_ = 0;
    Side side_;
    std::string map_out_;
    std::vector<std::pair<long long, OrderId>> placed_;
    void place_due(StrategyContext& ctx);
};
