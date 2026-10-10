#include "../include/core/order_book.hpp"
#include "../include/marketdata/event_source.hpp"
#include "../include/marketdata/parser.hpp"
#include "../include/sim/backtester.hpp"
#include "../include/strategy/glft_market_maker.hpp"
#include "../include/strategy/imbalance_strategy.hpp"
#include "../include/strategy/ladder_market_maker.hpp"
#include "../include/strategy/naive_market_maker.hpp"
#include "../include/strategy/twap_strategy.hpp"
#include "../include/util/timer.hpp"
#include "live_capture.hpp"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>

static void print_help() {
    std::cout << R"(
Usage: lob_sim <mode> [options]

Modes
  --replay <book> [--trades <trades>] [--strategy glft|ladder|naive|imbalance|twap]
        Event-driven backtest. <book> is either a Bybit historical order book
        file (*_ob200.data[.zip], from quote-saver.bycsi.com) or a normalized
        CSV from --live-capture. <trades> is a Bybit trades file
        (SYMBOLYYYY-MM-DD.csv[.gz], from public.bybit.com). With no strategy,
        only book_metrics.csv is produced.
  --stats <book> [--trades <trades>]
        Data profile: instrument, spread distribution (ticks and bps), trade
        flow, and the break-even arithmetic for passive quoting at given fees.
  --live-capture <SYMBOL> --output <csv> [--exchange bybit|binance] [--duration-minutes N]
  --load-snapshot <binance_rest_snapshot.json>

Instrument
  --tick-size X --lot-size Y        override auto-detection (from the first snapshot)

Exchange model
  --maker-fee-bps F                 default 2.0 (Bybit VIP0). Negative = rebate.
  --taker-fee-bps F                 default 5.5 (Bybit VIP0)
  --queue riskaverse|power|log      cancel-attribution queue model (default power)
  --queue-power N                   exponent for --queue power (default 2)
  --no-cross-fill                   don't fill on the book crossing our price
                                    (only trade prints fill; ignored without trades)

Latency (microseconds)
  --latency-us N                    sets feed, entry and response latency to N
  --feed-latency-us N  --entry-latency-us N  --response-latency-us N
  --jitter S                        lognormal sigma on entry/response latency

Strategy
  --order-size Q                    base-asset units per order (default: 1 lot)
  --max-position Q                  base-asset units (default: 10 x order size)
  glft:      --gamma G --levels N --half-spread-mult M --skew-mult M
             --alpha-micro A --alpha-obi B --alpha-scale S --obi-band-bps W
             --no-fee-in-quotes --warmup-s S --calib-log --depletion-pull F
  ladder:    --levels N --alpha-micro A --alpha-obi B --alpha-scale S
             --pull-threshold T --inventory-skew K --max-spread-ticks N --depletion-pull F
  naive:     --offset-ticks N
  imbalance: --threshold T --hold-ms H
  twap:      --side buy|sell --slices N --interval-s S --passive

Output
  --output <dir>    write summary.json, fills.csv, equity.csv, book_metrics.csv
  --sample-ms N     equity/book sampling interval (default 1000)
  --quiet
)";
}

struct Args {
    std::map<std::string, std::string> kv;
    std::map<std::string, bool> flags;
    bool has(const std::string& k) const { return kv.count(k) || flags.count(k); }
    std::string str(const std::string& k, const std::string& d = "") const {
        auto it = kv.find(k);
        return it == kv.end() ? d : it->second;
    }
    double num(const std::string& k, double d) const {
        auto it = kv.find(k);
        return it == kv.end() ? d : std::stod(it->second);
    }
};

static Args parse_args(int argc, char** argv) {
    static const std::map<std::string, bool> is_flag = {
        {"--no-cross-fill", true}, {"--no-fee-in-quotes", true}, {"--calib-log", true},
        {"--passive", true}, {"--quiet", true}, {"--synthetic-demo", true}, {"--help", true},
        {"-h", true}};
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        if (is_flag.count(k)) { a.flags[k] = true; continue; }
        if (k.rfind("--", 0) == 0 && i + 1 < argc) { a.kv[k] = argv[++i]; continue; }
        std::cerr << "Ignoring argument: " << k << "\n";
    }
    return a;
}

static std::string default_output_dir() {
    std::time_t t = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "data/processed/run_%Y%m%d_%H%M%S", std::localtime(&t));
    return buf;
}

// ── --stats ──────────────────────────────────────────────────────────────────

static int run_stats(EventSource& src, const Args& a) {
    const Instrument& inst = src.instrument();
    OrderBook book;
    MarketEvent ev;
    std::map<int64_t, size_t> spread_hist;
    size_t book_events = 0, trades = 0, samples = 0;
    double buy_vol = 0, sell_vol = 0, notional = 0, spread_bps_sum = 0, touch_qty_sum = 0;
    Timestamp t0 = 0, t1 = 0, next_sample = 0;
    double first_mid = 0, last_mid = 0;
    Timer timer;
    timer.start();
    while (src.next(ev)) {
        if (t0 == 0) t0 = ev.ts;
        t1 = ev.ts;
        if (ev.type == EventType::Trade) {
            ++trades;
            double u = inst.qty(ev.trade.qty);
            (ev.trade.aggressor == Side::Bid ? buy_vol : sell_vol) += u;
            notional += u * inst.price(ev.trade.price);
            continue;
        }
        ++book_events;
        if (ev.type == EventType::Snapshot) book.clear();
        for (auto& l : ev.bids) book.apply_update(Side::Bid, l.price, l.qty);
        for (auto& l : ev.asks) book.apply_update(Side::Ask, l.price, l.qty);
        if (book.empty() || book.crossed()) continue;
        if (ev.ts >= next_sample) {  // time-weighted: one sample per 100 ms
            next_sample = ev.ts + 100'000;
            int64_t s = static_cast<int64_t>(book.spread());
            ++spread_hist[s];
            double mid = book.mid_price();
            spread_bps_sum += book.spread() / mid * 1e4;
            touch_qty_sum += inst.qty(book.best_bid()->qty + book.best_ask()->qty) / 2.0;
            if (first_mid == 0) first_mid = mid;
            last_mid = mid;
            ++samples;
        }
    }
    timer.stop();
    const double hours = (t1 - t0) / 3.6e9;
    const double mid_px = inst.price(0) + last_mid * inst.tick_size;
    const double tick_bps = inst.tick_size / mid_px * 1e4;
    const double maker = a.num("--maker-fee-bps", 2.0), taker = a.num("--taker-fee-bps", 5.5);
    const double avg_spread_bps = samples ? spread_bps_sum / samples : 0;

    std::printf("Instrument      %s  tick=%g  lot=%g\n", inst.symbol.c_str(), inst.tick_size, inst.lot_size);
    std::printf("Coverage        %.2f h, %zu book events, %zu trades (%.1fs wall)\n", hours, book_events,
                trades, timer.elapsed_ms() / 1000.0);
    std::printf("Price           %.4f -> %.4f   (1 tick = %.3f bps)\n", first_mid * inst.tick_size,
                mid_px, tick_bps);
    std::printf("Trade flow      buy %.3f / sell %.3f units, $%.1fM notional (%.1f trades/s)\n", buy_vol,
                sell_vol, notional / 1e6, hours > 0 ? trades / (hours * 3600) : 0.0);
    std::printf("Touch size      %.4f units avg (per side)\n", samples ? touch_qty_sum / samples : 0.0);
    std::printf("Spread          mean %.3f bps; distribution (ticks: %% of time):\n", avg_spread_bps);
    size_t shown = 0;
    for (auto& [s, n] : spread_hist) {
        if (shown++ >= 8) break;
        std::printf("                  %3lld: %5.1f%%\n", static_cast<long long>(s), 100.0 * n / samples);
    }
    // A round trip captured fully at the quoted spread earns spread_bps and pays
    // 2 maker fees; anything below zero here can't be fixed by quoting smarter.
    std::printf("Break-even      quoted spread %.3f bps vs 2 x maker fee %.2f bps -> %+.3f bps per round trip\n",
                avg_spread_bps, 2 * maker, avg_spread_bps - 2 * maker);
    std::printf("                (taker round trip costs %.2f bps + spread)\n", 2 * taker);
    return 0;
}

// ── --replay ─────────────────────────────────────────────────────────────────

static std::unique_ptr<Strategy> make_strategy(const std::string& name, const Args& a,
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
        c.inventory_skew = a.num("--inventory-skew", c.inventory_skew);
        c.max_spread_ticks = static_cast<int>(a.num("--max-spread-ticks", c.max_spread_ticks));
        c.depletion_pull = a.num("--depletion-pull", c.depletion_pull);
        return std::make_unique<LadderMarketMaker>(c);
    }
    if (name == "naive" || name == "mm") {
        NaiveMarketMaker::Config c;
        c.order_qty    = order_qty;
        c.max_position = max_pos;
        c.offset_ticks = static_cast<int>(a.num("--offset-ticks", 0));
        return std::make_unique<NaiveMarketMaker>(c);
    }
    if (name == "imbalance") {
        ImbalanceStrategy::Config c;
        c.order_qty = order_qty;
        c.threshold = a.num("--threshold", c.threshold);
        c.hold_us   = static_cast<int64_t>(a.num("--hold-ms", 5000) * 1000);
        return std::make_unique<ImbalanceStrategy>(c);
    }
    if (name == "twap") {
        TwapStrategy::Config c;
        c.side        = a.str("--side", "buy") == "sell" ? Side::Ask : Side::Bid;
        c.num_slices  = static_cast<int>(a.num("--slices", 20));
        c.total_qty   = order_qty * c.num_slices;
        c.interval_us = static_cast<int64_t>(a.num("--interval-s", 60) * 1e6);
        c.passive     = a.has("--passive");
        return std::make_unique<TwapStrategy>(c);
    }
    (void)inst;
    return nullptr;
}

// Records book_metrics only.
class NullStrategy : public Strategy {
public:
    std::string name() const override { return "None"; }
};

static int run_replay(EventSource& src, const Args& a) {
    const Instrument& inst = src.instrument();
    const std::string strat_name = a.str("--strategy", "");
    std::string out = a.str("--output", "");
    if (out.empty()) out = default_output_dir();

    BacktestConfig cfg;
    cfg.output_dir = out;
    cfg.quiet      = a.has("--quiet");
    cfg.sample_us  = static_cast<int64_t>(a.num("--sample-ms", 1000) * 1000);
    cfg.exchange.fees.maker = a.num("--maker-fee-bps", 2.0) * 1e-4;
    cfg.exchange.fees.taker = a.num("--taker-fee-bps", 5.5) * 1e-4;
    std::string q = a.str("--queue", "power");
    cfg.exchange.queue.kind = (q == "riskaverse") ? QueueModel::Kind::RiskAverse
                            : (q == "log")        ? QueueModel::Kind::ProbLog
                                                  : QueueModel::Kind::ProbPower;
    cfg.exchange.queue.power  = a.num("--queue-power", 2.0);
    cfg.exchange.fill_on_cross = !a.has("--no-cross-fill");
    int64_t lat = static_cast<int64_t>(a.num("--latency-us", 0));
    cfg.latency.feed_us      = static_cast<int64_t>(a.num("--feed-latency-us", static_cast<double>(lat)));
    cfg.latency.entry_us     = static_cast<int64_t>(a.num("--entry-latency-us", static_cast<double>(lat)));
    cfg.latency.response_us  = static_cast<int64_t>(a.num("--response-latency-us", static_cast<double>(lat)));
    cfg.latency.jitter_sigma = a.num("--jitter", 0.0);

    Qty order_qty = std::max<Qty>(1, inst.to_lots(a.num("--order-size", inst.lot_size)));
    Qty max_pos   = std::max<Qty>(order_qty, inst.to_lots(a.num("--max-position", 10 * inst.qty(order_qty))));
    int levels    = static_cast<int>(a.num("--levels", 1));
    // Gateway limit: strategy limit plus room for a full ladder of resting orders.
    cfg.max_position = max_pos + order_qty * levels;

    std::unique_ptr<Strategy> strat;
    if (strat_name.empty() || strat_name == "none") {
        strat = std::make_unique<NullStrategy>();
    } else {
        std::filesystem::create_directories(out);
        strat = make_strategy(strat_name, a, inst, order_qty, max_pos, out);
        if (!strat) { std::cerr << "Unknown strategy: " << strat_name << "\n"; return 1; }
    }

    if (!cfg.quiet) {
        std::printf("Instrument %s tick=%g lot=%g | strategy=%s order=%g max_pos=%g | fees maker=%.2fbp taker=%.2fbp | "
                    "latency feed/entry/resp=%lld/%lld/%lldus | queue=%s%s\n",
                    inst.symbol.c_str(), inst.tick_size, inst.lot_size, strat->name().c_str(),
                    inst.qty(order_qty), inst.qty(max_pos), cfg.exchange.fees.maker * 1e4,
                    cfg.exchange.fees.taker * 1e4, (long long)cfg.latency.feed_us,
                    (long long)cfg.latency.entry_us, (long long)cfg.latency.response_us, q.c_str(),
                    src.has_trades() ? "" : " | NO TRADES: fills from book crosses only");
    }

    Backtester bt(src, *strat, cfg);
    RunSummary s = bt.run();
    if (!cfg.quiet) {
        std::printf("Replayed %zu events (%zu trades) in %.0f ms (%.2fM events/s)\n", s.market_events,
                    s.trade_events, s.wall_ms, s.market_events / std::max(s.wall_ms, 1.0) / 1e3);
        if (strat_name.empty() || strat_name == "none") return 0;
        std::printf("Orders %zu, cancels %zu, rejects %zu, cancel-rejects %zu, refused %zu\n", s.orders_sent,
                    s.cancels_sent, s.rejects, s.cancel_rejects, s.local_refusals);
        std::printf("Fills  maker %zu (%.4f units) / taker %zu (%.4f units), notional $%.0f\n", s.maker_fills,
                    s.maker_volume, s.taker_fills, s.taker_volume, s.notional);
        std::printf("PnL    gross %+.2f  fees %+.2f  net %+.2f  (net after flatten %+.2f)  [%.3f bps of notional]\n",
                    s.gross_pnl, -s.fees, s.net_pnl, s.net_pnl_flat,
                    s.notional > 0 ? s.net_pnl / s.notional * 1e4 : 0.0);
        std::printf("Edge   at fill %+.2f | markout 1s %+.2f 5s %+.2f 60s %+.2f 300s %+.2f\n", s.edge,
                    s.markout[1], s.markout[2], s.markout[4], s.markout[5]);
        std::printf("Risk   max |pos| %.4f, mean |pos| %.4f, final pos %.4f, max DD %.2f\n", s.max_abs_position,
                    s.mean_abs_position, s.final_position, s.max_drawdown);
        if (auto* tw = dynamic_cast<TwapStrategy*>(strat.get())) {
            double arr = tw->arrival_mid(), avg = tw->avg_fill_px();
            double sign = a.str("--side", "buy") == "sell" ? -1.0 : 1.0;
            if (tw->filled() > 0 && arr > 0)
                std::printf("TWAP   filled %.4f units, avg %.4f vs arrival mid %.4f -> shortfall %.3f bps (ex fees)\n",
                            inst.qty(tw->filled()), avg * inst.tick_size, arr * inst.tick_size,
                            sign * (avg - arr) / arr * 1e4);
        }
    }
    return 0;
}

// ── misc modes ───────────────────────────────────────────────────────────────

static void run_load_snapshot(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) { std::cerr << "Cannot open: " << path << "\n"; return; }
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    BookSnapshot snap = Parser::parse_snapshot(content);
    OrderBook book;
    book.load_snapshot(snap);
    auto bb = book.best_bid();
    auto ba = book.best_ask();
    std::cout << "Snapshot loaded. last_update_id=" << snap.last_update_id << "\n"
              << "  Best bid: " << (bb ? bb->price : 0) / TICK_MULTIPLIER << "\n"
              << "  Best ask: " << (ba ? ba->price : 0) / TICK_MULTIPLIER << "\n"
              << "  Spread:   " << book.spread() << " (0.01 units)\n"
              << "  Imbal(5): " << book.imbalance(5) << "\n";
}

int main(int argc, char** argv) {
    if (argc < 2) { print_help(); return 0; }
    Args a = parse_args(argc, argv);
    if (a.has("--help") || a.has("-h")) { print_help(); return 0; }

    try {
        if (a.has("--load-snapshot")) { run_load_snapshot(a.str("--load-snapshot")); return 0; }
        if (a.has("--live-capture")) {
            if (!a.has("--output")) { std::cerr << "--live-capture requires --output <csv>\n"; return 1; }
            run_live_capture(a.str("--live-capture"), static_cast<int>(a.num("--duration-minutes", 60)),
                             a.str("--output"), a.str("--exchange", "bybit"));
            return 0;
        }
        std::string book_path = a.has("--stats") ? a.str("--stats") : a.str("--replay");
        if (book_path.empty()) { print_help(); return 1; }
        auto src = open_event_source(book_path, a.str("--trades"), a.num("--tick-size", 0.0),
                                     a.num("--lot-size", 0.0));
        if (a.has("--stats")) return run_stats(*src, a);
        return run_replay(*src, a);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
