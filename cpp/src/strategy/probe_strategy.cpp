#include "../../include/strategy/probe_strategy.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

ProbeStrategy::ProbeStrategy(const std::string& probes_csv, Side side, const Instrument& inst,
                             std::string map_out_csv)
    : side_(side), map_out_(std::move(map_out_csv)) {
    std::ifstream f(probes_csv);
    if (!f) throw std::runtime_error("ProbeStrategy: cannot open " + probes_csv);
    std::string line;
    std::getline(f, line);  // header: probe,ts,price
    while (std::getline(f, line)) {
        std::stringstream ss(line);
        std::string a, b, c;
        if (!std::getline(ss, a, ',') || !std::getline(ss, b, ',') || !std::getline(ss, c, ',')) continue;
        probes_.push_back({std::stoll(a), std::stoll(b), inst.to_ticks(std::stod(c))});
    }
}

ProbeStrategy::~ProbeStrategy() {
    if (map_out_.empty()) return;
    std::ofstream f(map_out_);
    f << "probe,order_id\n";
    for (const auto& [p, id] : placed_) f << p << ',' << id << '\n';
}

void ProbeStrategy::place_due(StrategyContext& ctx) {
    while (next_ < probes_.size() && probes_[next_].ts <= ctx.now()) {
        const Probe& p = probes_[next_++];
        OrderId id = ctx.submit_limit(side_, p.price, 1, TimeInForce::PostOnly);
        placed_.push_back({p.id, id});
    }
}
