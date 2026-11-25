#include "../../include/strategy/run_setup.hpp"
#include "../../include/strategy/execution_strategy.hpp"
#include "../../include/strategy/glft_market_maker.hpp"
#include "../../include/strategy/imbalance_strategy.hpp"
#include "../../include/strategy/ladder_market_maker.hpp"
#include "../../include/strategy/naive_market_maker.hpp"
#include "../../include/strategy/probe_strategy.hpp"
#include "../../include/strategy/twap_strategy.hpp"

#include <algorithm>
#include <stdexcept>

namespace {

class NullStrategy : public Strategy {
public:
    std::string name() const override { return "None"; }
};

}  // namespace

std::unique_ptr<Strategy> make_strategy(const std::string& name, const Options& a,
                                               const Instrument& inst, Qty order_qty, Qty max_pos,
                                               const std::string& out_dir) {
    if (name == "glft") {
        GlftMarketMaker::Config c;
        c.order_qty        = order_qty;
        c.max_position     = max_pos;
        c.gamma            = a.num("--gamma", c.gamma);
        c.levels           = static_cast<int>(a.num("--levels", c.levels));
        c.half_spread_mult = a.num("--half-spread-mult", c.half_spread_mult);
        c.skew_mult        = a.num("--skew-mult", c.skew_mult);
        c.alpha_micro      = a.num("--alpha-micro", c.alpha_micro);
        c.alpha_obi        = a.num("--alpha-obi", c.alpha_obi);
        c.obi_band_bps     = a.num("--obi-band-bps", c.obi_band_bps);
        // Scales a fitted (micro, obi) pair together — handy for sweeps.
        c.alpha_micro     *= a.num("--alpha-scale", 1.0);
        c.alpha_obi       *= a.num("--alpha-scale", 1.0);
        c.min_half_spread_ticks = a.num("--min-half-spread-ticks", c.min_half_spread_ticks);
        c.include_fee      = !a.has("--no-fee-in-quotes");
        c.warmup_us        = static_cast<int64_t>(a.num("--warmup-s", 600) * 1e6);
        c.max_depth_ticks  = static_cast<int>(a.num("--max-depth-ticks", c.max_depth_ticks));
        c.depletion_pull   = a.num("--depletion-pull", 0.0);
        c.quote_on_book    = a.has("--quote-on-book");
        if (a.has("--calib-log") && !out_dir.empty()) c.log_path = out_dir + "/calibration.csv";
        return std::make_unique<GlftMarketMaker>(c);
    }
    if (name == "ladder") {
        LadderMarketMaker::Config c;
        c.order_qty      = order_qty;
        c.max_position   = max_pos;
        c.levels         = static_cast<int>(a.num("--levels", c.levels));
        c.alpha_micro    = a.num("--alpha-micro", 0.0) * a.num("--alpha-scale", 1.0);
        c.alpha_obi      = a.num("--alpha-obi", 0.0) * a.num("--alpha-scale", 1.0);
        c.obi_band_bps   = a.num("--obi-band-bps", c.obi_band_bps);
        c.pull_threshold = a.num("--pull-threshold", c.pull_threshold);
        c.join_threshold = a.num("--join-threshold", c.join_threshold);
        c.inventory_skew = a.num("--inventory-skew", c.inventory_skew);
        c.max_spread_ticks = static_cast<int>(a.num("--max-spread-ticks", c.max_spread_ticks));
        c.depletion_pull = a.num("--depletion-pull", c.depletion_pull);
        if (a.has("--quote-on-book")) c.interval_us = 0;
        return std::make_unique<LadderMarketMaker>(c);
    }
    if (name == "naive" || name == "mm") {
        NaiveMarketMaker::Config c;
        c.order_qty    = order_qty;
        c.max_position = max_pos;
        c.offset_ticks = static_cast<int>(a.num("--offset-ticks", 0));
        if (a.has("--quote-on-book")) c.quote_interval_us = 0;
        return std::make_unique<NaiveMarketMaker>(c);
    }
    if (name == "imbalance") {
        ImbalanceStrategy::Config c;
        c.order_qty = order_qty;
        c.threshold = a.num("--threshold", c.threshold);
        c.hold_us   = static_cast<int64_t>(a.num("--hold-ms", 5000) * 1000);
        return std::make_unique<ImbalanceStrategy>(c);
    }
    if (name == "probe") {
        // Fill-model validation; see python/analysis/queue_model_validation.py.
        Side side = a.choose<Side>("--side", "buy", {{"buy", Side::Bid}, {"sell", Side::Ask}});
        return std::make_unique<ProbeStrategy>(a.str("--probes"), side, inst,
                                               out_dir.empty() ? "" : out_dir + "/probe_orders.csv");
    }
    if (name == "exec") {
        ExecutionStrategy::Config c;
        c.algo = a.choose<ExecutionStrategy::Algo>("--algo", "cross",
            {{"cross", ExecutionStrategy::Algo::Cross}, {"join", ExecutionStrategy::Algo::Join},
             {"peg", ExecutionStrategy::Algo::Peg}, {"smart", ExecutionStrategy::Algo::Smart}});
        c.slices          = static_cast<int>(a.num("--slices", c.slices));
        c.parent_qty      = order_qty * c.slices;
        c.horizon_us      = static_cast<int64_t>(a.num("--horizon-s", 300) * 1e6);
        c.gap_us          = static_cast<int64_t>(a.num("--gap-s", 60) * 1e6);
        c.cross_imbalance = a.num("--cross-imbalance", c.cross_imbalance);
        if (!out_dir.empty()) c.csv_path = out_dir + "/executions.csv";
        return std::make_unique<ExecutionStrategy>(c);
    }
    if (name == "twap") {
        TwapStrategy::Config c;
        c.side        = a.choose<Side>("--side", "buy", {{"buy", Side::Bid}, {"sell", Side::Ask}});
        c.num_slices  = static_cast<int>(a.num("--slices", 20));
        c.total_qty   = order_qty * c.num_slices;
        c.interval_us = static_cast<int64_t>(a.num("--interval-s", 60) * 1e6);
        c.passive     = a.has("--passive");
        return std::make_unique<TwapStrategy>(c);
    }
    if (name.empty() || name == "none") return std::make_unique<NullStrategy>();
    throw std::runtime_error("unknown strategy: " + name);
}

RunSetup make_run_setup(const Options& a, const Instrument& inst, const std::string& strategy,
                        const std::string& out_dir) {
    RunSetup rs;
    BacktestConfig& cfg = rs.cfg;
    cfg.output_dir = out_dir;
    cfg.quiet      = a.has("--quiet");
    cfg.sample_us  = static_cast<int64_t>(a.num("--sample-ms", 1000) * 1000);
    cfg.write_book_metrics = !a.has("--no-book-metrics");
    cfg.summary_only       = a.has("--summary-only");
    cfg.measure_latency    = a.has("--measure-latency");
    cfg.exchange.fees.maker = a.num("--maker-fee-bps", 2.0) * 1e-4;
    cfg.exchange.fees.taker = a.num("--taker-fee-bps", 5.5) * 1e-4;
    cfg.exchange.fees.maker_per_unit = a.num("--maker-fee-per-unit", 0.0);
    cfg.exchange.fees.taker_per_unit = a.num("--taker-fee-per-unit", 0.0);
    if (a.has("--maker-fee-per-unit") && !a.has("--maker-fee-bps")) cfg.exchange.fees.maker = 0.0;
    if (a.has("--taker-fee-per-unit") && !a.has("--taker-fee-bps")) cfg.exchange.fees.taker = 0.0;
    if (a.has("--session-start")) cfg.session_start_us = static_cast<int64_t>(time_of_day_ns(a.str("--session-start")) / 1000);
    if (a.has("--session-end")) cfg.session_end_us = static_cast<int64_t>(time_of_day_ns(a.str("--session-end")) / 1000);
    cfg.run_config = a.all();
    cfg.exchange.queue.kind = a.choose<QueueModel::Kind>("--queue", "power",
        {{"riskaverse", QueueModel::Kind::RiskAverse}, {"log", QueueModel::Kind::ProbLog}, {"power", QueueModel::Kind::ProbPower}});
    cfg.exchange.queue.power  = a.num("--queue-power", 2.0);
    cfg.exchange.consume_prints = a.has("--consume-prints");
    cfg.exchange.displace_volume = a.choose<bool>("--impact", "none", {{"none", false}, {"displace", true}});
    cfg.exchange.displaced_ttl_us = static_cast<int64_t>(a.num("--impact-ttl-ms", 0) * 1000);
    cfg.exchange.fill_on_cross = !a.has("--no-cross-fill");
    cfg.exchange_model = a.choose<ExchangeModel>("--exchange-model", "auto",
        {{"auto", ExchangeModel::Auto}, {"l2", ExchangeModel::L2Queue}, {"l3", ExchangeModel::L3Exact}});
    int64_t lat = static_cast<int64_t>(a.num("--latency-us", 0));
    cfg.latency.feed_us      = static_cast<int64_t>(a.num("--feed-latency-us", static_cast<double>(lat)));
    cfg.latency.entry_us     = static_cast<int64_t>(a.num("--entry-latency-us", static_cast<double>(lat)));
    cfg.latency.response_us  = static_cast<int64_t>(a.num("--response-latency-us", static_cast<double>(lat)));
    cfg.latency.jitter_sigma = a.num("--jitter", 0.0);

    Qty order_qty = std::max<Qty>(1, inst.to_lots(a.num("--order-size", inst.lot_size)));
    Qty max_pos   = std::max<Qty>(order_qty, inst.to_lots(a.num("--max-position", 10 * inst.qty(order_qty))));
    const int levels = static_cast<int>(a.num("--levels", strategy == "ladder" ? LadderMarketMaker::Config{}.levels : 1));
    // Gateway limit: strategy limit plus room for a full ladder of resting orders.
    cfg.max_position = max_pos + order_qty * levels;
    if (strategy == "probe") cfg.max_position = 0;  // probes accumulate by design
    // An execution algo's parent quantity *is* its position limit.
    if (strategy == "exec") cfg.max_position = 0;   // parents alternate sides; bounded by construction
    if (strategy == "twap")
        cfg.max_position = order_qty * static_cast<Qty>(a.num("--slices", 20));
    rs.order_qty = order_qty;
    rs.max_pos   = max_pos;
    return rs;
}
