#pragma once
#include "../sim/backtester.hpp"
#include "../util/options.hpp"
#include "strategy.hpp"

#include <memory>
#include <string>

// Turns Options into a configured run. Used by lob_sim and the Python module.
struct RunSetup {
    BacktestConfig cfg;
    Qty order_qty = 1;
    Qty max_pos   = 10;
};

// Exchange, latency, session, risk and output settings for `strategy`.
RunSetup make_run_setup(const Options& a, const Instrument& inst, const std::string& strategy,
                        const std::string& out_dir);

// A built-in strategy by name; "" or "none" does nothing (book metrics only).
// Throws on an unknown name.
std::unique_ptr<Strategy> make_strategy(const std::string& name, const Options& a, const Instrument& inst,
                                        Qty order_qty, Qty max_pos, const std::string& out_dir);
