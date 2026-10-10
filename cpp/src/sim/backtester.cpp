#include "../../include/sim/backtester.hpp"
#include "../../include/sim/l3_exchange_sim.hpp"
#include "../../include/util/csv_writer.hpp"
#include "../../include/util/json.hpp"
#include "../../include/util/timer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

int64_t mono_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

RunSummary::Pctl percentiles(std::vector<int64_t>& v) {
    RunSummary::Pctl p;
    p.n = v.size();
    if (v.empty()) return p;
    std::sort(v.begin(), v.end());
    auto at = [&](double q) { return static_cast<double>(v[static_cast<size_t>(q * static_cast<double>(v.size() - 1))]); };
    p.p50 = at(0.5); p.p90 = at(0.9); p.p99 = at(0.99); p.p999 = at(0.999);
    p.max = static_cast<double>(v.back());
    return p;
}

constexpr Timestamp T_INF = std::numeric_limits<Timestamp>::max();

void apply_to_book(OrderBook& book, const MarketEvent& ev) {
    if (ev.type == EventType::Trade) return;
    if (ev.type == EventType::Snapshot) book.clear();
    for (const auto& l : ev.bids) book.apply_update(Side::Bid, l.price, l.qty);
    for (const auto& l : ev.asks) book.apply_update(Side::Ask, l.price, l.qty);
}

const char* cause_name(FillCause c) {
    switch (c) {
        case FillCause::Taker:        return "taker";
        case FillCause::TradeAtPrice: return "trade_at_price";
        case FillCause::TradeThrough: return "trade_through";
        case FillCause::BookCross:    return "book_cross";
        default:                      return "none";
    }
}

std::string fmt(double v, int prec = 6) {
    if (std::isnan(v)) return "";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", prec, v);
    return buf;
}

}  // namespace

struct Backtester::Writers {
    std::unique_ptr<CsvWriter> equity;
    std::unique_ptr<CsvWriter> book;
    double buy_vol = 0.0, sell_vol = 0.0;  // trade volume since last book sample
};

Backtester::Backtester(EventSource& source, Strategy& strategy, BacktestConfig cfg)
    : src_(source), strat_(strategy), cfg_(std::move(cfg)), inst_(source.instrument()),
      pf_(source.instrument()), rng_(cfg_.latency.seed), w_(std::make_unique<Writers>()) {
    px_prec_ = std::max(0, static_cast<int>(std::ceil(-std::log10(inst_.tick_size) - 1e-9)));
    // Without a trade feed, the only fill evidence is the book crossing our
    // price — force that on so a depth-only replay can fill at all.
    if (!src_.has_trades()) cfg_.exchange.fill_on_cross = true;
    const bool l3 = cfg_.exchange_model == ExchangeModel::L3Exact ||
                    (cfg_.exchange_model == ExchangeModel::Auto && src_.has_l3());
    if (l3 && !src_.has_l3()) throw std::runtime_error("L3 exchange model requires an order-level source");
    if (l3) exch_ = std::make_unique<L3ExchangeSim>(inst_, cfg_.exchange);
    else    exch_ = std::make_unique<ExchangeSim>(inst_, cfg_.exchange);
}

Backtester::~Backtester() = default;

int64_t Backtester::jittered(int64_t base) {
    if (base <= 0 || cfg_.latency.jitter_sigma <= 0.0) return base;
    const double s = cfg_.latency.jitter_sigma;
    std::normal_distribution<double> n(-0.5 * s * s, s);
    return static_cast<int64_t>(static_cast<double>(base) * std::exp(n(rng_)));
}

// ── StrategyContext: order gateway ───────────────────────────────────────────

OrderId Backtester::submit_limit(Side side, Price px, Qty qty, TimeInForce tif) {
    if (qty <= 0 || px <= 0) { ++sum_.local_refusals; return 0; }
    Qty same_side = 0;
    for (const auto& [id, lo] : local_orders_) {
        (void)id;
        if (lo.side == side) { same_side += lo.leaves(); continue; }
        // Self-cross prevention against our own opposite-side orders.
        if (lo.type == OrderType::Limit &&
            ((side == Side::Bid && px >= lo.price) || (side == Side::Ask && px <= lo.price))) {
            ++sum_.local_refusals;
            return 0;
        }
    }
    if (cfg_.max_position > 0) {
        Qty worst = (side == Side::Bid) ? local_pos_ + same_side + qty : local_pos_ - same_side - qty;
        if (std::llabs(worst) > cfg_.max_position) { ++sum_.local_refusals; return 0; }
    }

    OrderRequest req;
    req.order_id = next_id_++;
    req.side     = side;
    req.type     = OrderType::Limit;
    req.tif      = tif;
    req.price    = px;
    req.qty      = qty;

    LocalOrder lo;
    lo.id = req.order_id; lo.side = side; lo.type = OrderType::Limit;
    lo.price = px; lo.qty = qty; lo.sent_ts = local_now_;
    local_orders_[lo.id] = lo;

    Timestamp arrive = std::max(local_now_ + jittered(cfg_.latency.entry_us), last_entry_arrival_);
    last_entry_arrival_ = arrive;
    to_exchange_.push_back({arrive, false, req});
    ++sum_.orders_sent;
    note_submit();
    return req.order_id;
}

OrderId Backtester::submit_market(Side side, Qty qty) {
    if (qty <= 0) { ++sum_.local_refusals; return 0; }
    if (cfg_.max_position > 0) {
        Qty worst = local_pos_ + (side == Side::Bid ? qty : -qty);
        if (std::llabs(worst) > cfg_.max_position) { ++sum_.local_refusals; return 0; }
    }
    OrderRequest req;
    req.order_id = next_id_++;
    req.side     = side;
    req.type     = OrderType::Market;
    req.tif      = TimeInForce::IOC;
    req.qty      = qty;

    LocalOrder lo;
    lo.id = req.order_id; lo.side = side; lo.type = OrderType::Market;
    lo.qty = qty; lo.sent_ts = local_now_;
    local_orders_[lo.id] = lo;

    Timestamp arrive = std::max(local_now_ + jittered(cfg_.latency.entry_us), last_entry_arrival_);
    last_entry_arrival_ = arrive;
    to_exchange_.push_back({arrive, false, req});
    ++sum_.orders_sent;
    note_submit();
    return req.order_id;
}

void Backtester::cancel(OrderId id) {
    auto it = local_orders_.find(id);
    if (it == local_orders_.end() || it->second.cancel_sent) return;
    it->second.cancel_sent = true;
    OrderRequest req;
    req.order_id = id;
    Timestamp arrive = std::max(local_now_ + jittered(cfg_.latency.entry_us), last_entry_arrival_);
    last_entry_arrival_ = arrive;
    to_exchange_.push_back({arrive, true, req});
    ++sum_.cancels_sent;
    note_submit();
}

void Backtester::note_submit() {
    if (react_ns_ <= 0) return;
    const int64_t now = mono_ns();
    lat_t2t_.push_back(now - react_ns_);
    lat_e2t_.push_back(now - react_pop_ns_);
}

// ── event plumbing ───────────────────────────────────────────────────────────

void Backtester::handle_exchange_reports(Timestamp t) {
    for (const auto& rep : scratch_reports_) {
        if (rep.status == OrderStatus::New) {
            const double q = exch_->queue_ahead(rep.order_id);
            if (q >= 0.0) queue_at_entry_[rep.order_id] = q;
        }
        if (rep.fill_qty > 0) record_fill(rep);
        Timestamp arrive = std::max(t + jittered(cfg_.latency.response_us), last_resp_arrival_);
        last_resp_arrival_ = arrive;
        to_strategy_.push_back({arrive, rep});
    }
    scratch_reports_.clear();
}

void Backtester::deliver_report(const ExecutionReport& rep) {
    auto it = local_orders_.find(rep.order_id);
    switch (rep.status) {
        case OrderStatus::New:
            if (it != local_orders_.end()) it->second.acked = true;
            break;
        case OrderStatus::PartiallyFilled:
        case OrderStatus::Filled:
            local_pos_ += (rep.side == Side::Bid) ? rep.fill_qty : -rep.fill_qty;
            if (it != local_orders_.end()) {
                it->second.filled = rep.cum_qty;
                if (rep.status == OrderStatus::Filled) local_orders_.erase(it);
            }
            break;
        case OrderStatus::Cancelled:
            if (it != local_orders_.end()) local_orders_.erase(it);
            break;
        case OrderStatus::Rejected:
            ++sum_.rejects;
            if (it != local_orders_.end()) local_orders_.erase(it);
            break;
        case OrderStatus::CancelRejected:
            ++sum_.cancel_rejects;
            break;
    }
    strat_.on_order_update(*this, rep);
}

void Backtester::deliver_local(const MarketEvent& ev, Timestamp local_t) {
    local_now_ = local_t;
    apply_to_book(local_book_, ev);
    // Orders sent from these callbacks are reactions to `ev`: time them
    // (after the warm-up, so the backlog queued during start-up doesn't count).
    react_ns_     = (ev.local_ns > 0 && ev.ts >= latency_from_ts_) ? ev.local_ns : 0;
    react_pop_ns_ = pop_ns_;
    if (ev.type == EventType::Trade) {
        strat_.on_trade(*this, ev.trade);
    } else if (!local_book_.empty() && !local_book_.crossed()) {
        strat_.on_book(*this);
    }
    react_ns_ = 0;
}

void Backtester::drain_until(Timestamp t, bool inclusive) {
    const int64_t timer_iv = strat_.timer_interval_us();
    while (true) {
        Timestamp t_ex = to_exchange_.empty() ? T_INF : to_exchange_.front().t;
        Timestamp t_rp = to_strategy_.empty() ? T_INF : to_strategy_.front().t;
        Timestamp t_fd = feed_.empty() ? T_INF : feed_.front().t;
        Timestamp t_tm = (timer_iv > 0 && next_timer_ > 0) ? next_timer_ : T_INF;
        Timestamp tmin = std::min({t_ex, t_rp, t_fd, t_tm});
        if (tmin == T_INF) break;
        if (inclusive ? (tmin > t) : (tmin >= t)) break;

        if (t_ex == tmin) {
            OrderMsg m = to_exchange_.front();
            to_exchange_.pop_front();
            ex_now_ = m.t;
            if (m.is_cancel) exch_->on_cancel(m.req.order_id, m.t, scratch_reports_);
            else             exch_->on_new_order(m.req, m.t, ex_book_, pf_, scratch_reports_);
            handle_exchange_reports(m.t);
        } else if (t_rp == tmin) {
            ReportMsg m = to_strategy_.front();
            to_strategy_.pop_front();
            local_now_ = m.t;
            deliver_report(m.rep);
        } else if (t_fd == tmin) {
            FeedMsg m = std::move(feed_.front());
            feed_.pop_front();
            deliver_local(m.ev, m.t);
        } else {
            local_now_ = next_timer_;
            next_timer_ += timer_iv;
            if (!local_book_.empty()) strat_.on_timer(*this);
        }
    }
}

void Backtester::exchange_process(const MarketEvent& ev) {
    if (ev.type == EventType::Trade) {
        ++sum_.trade_events;
        (ev.trade.aggressor == Side::Bid ? w_->buy_vol : w_->sell_vol) += inst_.qty(ev.trade.qty);
        exch_->on_market(ev, ex_book_, pf_, scratch_reports_);
    } else {
        apply_to_book(ex_book_, ev);
        exch_->on_market(ev, ex_book_, pf_, scratch_reports_);
        if (!ex_book_.empty() && !ex_book_.crossed()) update_markouts();
    }
    handle_exchange_reports(ev.ts);
}

void Backtester::record_fill(const ExecutionReport& rep) {
    FillRecord f;
    f.ts        = rep.ts;
    f.order_id  = rep.order_id;
    f.side      = rep.side;
    f.liquidity = rep.liquidity;
    f.price     = rep.fill_price;
    f.qty       = rep.fill_qty;
    f.fee       = rep.fee;
    f.mid_at_fill = ex_book_.mid_price();
    auto q = queue_at_entry_.find(rep.order_id);
    f.queue_ahead_at_entry = (q != queue_at_entry_.end()) ? q->second : 0.0;
    f.position_after = pf_.position();
    f.queue_ahead_at_fill = rep.queue_ahead;
    f.cause = rep.cause;
    f.mid_after.fill(std::numeric_limits<double>::quiet_NaN());
    fills_.push_back(f);

    const double units = inst_.qty(rep.fill_qty);
    if (rep.liquidity == Liquidity::Maker) { ++sum_.maker_fills; sum_.maker_volume += units; }
    else                                   { ++sum_.taker_fills; sum_.taker_volume += units; }
}

void Backtester::update_markouts() {
    const double mid = ex_book_.mid_price();
    for (int h = 0; h < N_MARKOUTS; ++h) {
        size_t& c = markout_cursor_[h];
        while (c < fills_.size() && fills_[c].ts + MARKOUT_US[h] <= ex_now_) {
            fills_[c].mid_after[h] = mid;
            ++c;
        }
    }
}

void Backtester::sample(Timestamp t) {
    if (ex_book_.empty()) return;
    const double mid = ex_book_.mid_price();
    const double net = pf_.net_pnl(mid);
    if (samples_ == 0) peak_equity_ = net;
    peak_equity_     = std::max(peak_equity_, net);
    sum_.max_drawdown = std::max(sum_.max_drawdown, peak_equity_ - net);
    const double abs_pos = std::fabs(pf_.position_units());
    abs_pos_acc_ += abs_pos;
    sum_.max_abs_position = std::max(sum_.max_abs_position, abs_pos);
    bool has_bid = false, has_ask = false;
    exch_->resting_sides(has_bid, has_ask);
    bid_up_ += has_bid;
    ask_up_ += has_ask;
    ++samples_;

    if (w_->equity) {
        w_->equity->write_row({
            std::to_string(t), fmt(mid * inst_.tick_size, px_prec_),
            fmt(pf_.position_units(), 6), fmt(pf_.realized_pnl(), 4), fmt(pf_.unrealized_pnl(mid), 4),
            fmt(pf_.total_pnl(mid), 4), fmt(pf_.total_fees_paid(), 4), fmt(net, 4),
            std::to_string(fills_.size()), std::to_string(exch_->open_order_count())});
    }
    if (w_->book) {
        auto bb = ex_book_.best_bid();
        auto ba = ex_book_.best_ask();
        w_->book->write_row({
            std::to_string(t), fmt(inst_.price(bb->price), px_prec_), fmt(inst_.qty(bb->qty), 6),
            fmt(inst_.price(ba->price), px_prec_), fmt(inst_.qty(ba->qty), 6),
            std::to_string(ba->price - bb->price), fmt(mid * inst_.tick_size, px_prec_ + 1),
            fmt(ex_book_.weighted_mid() * inst_.tick_size, px_prec_ + 2),
            fmt(ex_book_.imbalance(1), 5), fmt(ex_book_.imbalance(5), 5),
            fmt(ex_book_.depth_imbalance(static_cast<Price>(std::llround(mid * 0.0005))), 5),
            fmt(w_->buy_vol, 6), fmt(w_->sell_vol, 6)});
    }
    w_->buy_vol = w_->sell_vol = 0.0;
}

RunSummary Backtester::run() {
    Timer timer;
    timer.start();

    sum_.strategy = strat_.name();
    sum_.symbol   = inst_.symbol;

    if (!cfg_.output_dir.empty()) {
        std::filesystem::create_directories(cfg_.output_dir);
        w_->equity = std::make_unique<CsvWriter>(cfg_.output_dir + "/equity.csv",
            std::vector<std::string>{"ts", "mid", "position", "realized_pnl", "unrealized_pnl",
                                     "gross_pnl", "fees", "net_pnl", "n_fills", "open_orders"});
        if (cfg_.write_book_metrics) {
            w_->book = std::make_unique<CsvWriter>(cfg_.output_dir + "/book_metrics.csv",
                std::vector<std::string>{"ts", "best_bid", "best_bid_qty", "best_ask", "best_ask_qty",
                                         "spread_ticks", "mid", "weighted_mid", "imbalance_1",
                                         "imbalance_5", "depth_imbalance_5bp", "buy_volume",
                                         "sell_volume"});
        }
    }

    strat_.on_start(*this);

    MarketEvent ev;
    bool first = true;
    const int64_t feed = cfg_.latency.feed_us;
    while (src_.next(ev)) {
        pop_ns_ = mono_ns();
        if (first) latency_from_ts_ = ev.ts + 1'000'000;   // 1 s warm-up
        if (ev.local_ns > 0 && ev.ts >= latency_from_ts_) lat_recv_.push_back(pop_ns_ - ev.local_ns);
        if (first) {
            first = false;
            sum_.start_ts = ev.ts;
            next_sample_  = ev.ts + cfg_.sample_us;
            if (strat_.timer_interval_us() > 0) next_timer_ = ev.ts + strat_.timer_interval_us();
            local_now_ = ev.ts;
        }
        drain_until(ev.ts, false);
        ex_now_ = ev.ts;
        exchange_process(ev);
        // Zero-latency reports produced by this event reach the strategy
        // before it sees the event itself (that's what zero latency means).
        drain_until(ev.ts, true);
        if (sum_.mid_start == 0.0 && !ex_book_.empty() && !ex_book_.crossed())
            sum_.mid_start = ex_book_.mid_price() * inst_.tick_size;
        if (feed == 0) deliver_local(ev, ev.ts);
        else           feed_.push_back({ev.ts + feed, ev});
        while (ev.ts >= next_sample_) {
            sample(next_sample_);
            next_sample_ += cfg_.sample_us;
        }
        ++sum_.market_events;
        sum_.end_ts = ev.ts;
    }
    // Let in-flight messages land, but don't run the strategy past the data.
    drain_until(ex_now_, true);
    strat_.on_finish(*this);

    timer.stop();
    sum_.wall_ms = timer.elapsed_ms();
    finalize();
    return sum_;
}

void Backtester::finalize() {
    const double mid = ex_book_.empty() ? 0.0 : ex_book_.mid_price();
    const double tick = inst_.tick_size;
    sum_.mid_end = mid * tick;

    for (auto& f : fills_) {
        const double s = (f.side == Side::Bid) ? 1.0 : -1.0;
        const double u = inst_.qty(f.qty);
        sum_.edge += s * (f.mid_at_fill - static_cast<double>(f.price)) * tick * u;
        for (int h = 0; h < N_MARKOUTS; ++h) {
            // Fills too close to the end of the data are marked at the last mid.
            double m = std::isnan(f.mid_after[h]) ? mid : f.mid_after[h];
            sum_.markout[h] += s * (m - static_cast<double>(f.price)) * tick * u;
        }
        sum_.avg_queue_ahead_at_fill += f.queue_ahead_at_entry;
    }
    if (!fills_.empty()) sum_.avg_queue_ahead_at_fill /= static_cast<double>(fills_.size());

    sum_.notional       = pf_.traded_notional();
    sum_.gross_pnl      = pf_.total_pnl(mid);
    sum_.realized_pnl   = pf_.realized_pnl();
    sum_.fees           = pf_.total_fees_paid();
    sum_.net_pnl        = pf_.net_pnl(mid);
    sum_.final_position = pf_.position_units();
    if (!ex_book_.empty()) {
        const double half_spread = 0.5 * ex_book_.spread() * tick;
        sum_.flatten_cost = std::fabs(sum_.final_position) * (half_spread + cfg_.exchange.fees.taker * mid * tick);
    }
    sum_.net_pnl_flat = sum_.net_pnl - sum_.flatten_cost;
    sum_.recv_to_engine = percentiles(lat_recv_);
    sum_.tick_to_trade  = percentiles(lat_t2t_);
    sum_.engine_to_trade = percentiles(lat_e2t_);
    if (samples_ > 0) {
        sum_.mean_abs_position = abs_pos_acc_ / static_cast<double>(samples_);
        sum_.uptime_bid = static_cast<double>(bid_up_) / static_cast<double>(samples_);
        sum_.uptime_ask = static_cast<double>(ask_up_) / static_cast<double>(samples_);
    }

    if (cfg_.output_dir.empty()) return;
    {
        CsvWriter fw(cfg_.output_dir + "/fills.csv",
            {"ts", "order_id", "side", "liquidity", "cause", "price", "qty", "fee", "mid_at_fill",
             "queue_ahead_at_entry", "queue_ahead_at_fill", "position_after", "mid_100ms", "mid_1s", "mid_5s", "mid_30s",
             "mid_60s", "mid_300s"});
        for (const auto& f : fills_) {
            std::vector<std::string> row = {
                std::to_string(f.ts), std::to_string(f.order_id), f.side == Side::Bid ? "B" : "A",
                f.liquidity == Liquidity::Maker ? "maker" : "taker", cause_name(f.cause),
                fmt(inst_.price(f.price), px_prec_), fmt(inst_.qty(f.qty), 6), fmt(f.fee, 6),
                fmt(f.mid_at_fill * tick, px_prec_ + 1), fmt(f.queue_ahead_at_entry * inst_.lot_size, 6),
                fmt(f.queue_ahead_at_fill * inst_.lot_size, 6),
                fmt(inst_.qty(f.position_after), 6)};
            for (int h = 0; h < N_MARKOUTS; ++h) row.push_back(fmt(f.mid_after[h] * tick, px_prec_ + 1));
            fw.write_row(row);
        }
    }
    write_summary_json(sum_, cfg_.output_dir + "/summary.json");
    if (!cfg_.quiet) std::cout << "Output written to: " << cfg_.output_dir << "\n";
}

void Backtester::write_summary_json(const RunSummary& s, const std::string& path) {
    nlohmann::json j;
    j["strategy"] = s.strategy;
    j["symbol"]   = s.symbol;
    j["start_ts"] = s.start_ts;
    j["end_ts"]   = s.end_ts;
    j["hours"]    = static_cast<double>(s.end_ts - s.start_ts) / 3.6e9;
    j["market_events"] = s.market_events;
    j["trade_events"]  = s.trade_events;
    j["wall_ms"] = s.wall_ms;
    j["orders_sent"] = s.orders_sent;
    j["cancels_sent"] = s.cancels_sent;
    j["rejects"] = s.rejects;
    j["cancel_rejects"] = s.cancel_rejects;
    j["local_refusals"] = s.local_refusals;
    j["maker_fills"] = s.maker_fills;
    j["taker_fills"] = s.taker_fills;
    j["maker_volume"] = s.maker_volume;
    j["taker_volume"] = s.taker_volume;
    j["notional"] = s.notional;
    j["gross_pnl"] = s.gross_pnl;
    j["realized_pnl"] = s.realized_pnl;
    j["fees"] = s.fees;
    j["net_pnl"] = s.net_pnl;
    j["final_position"] = s.final_position;
    j["flatten_cost"] = s.flatten_cost;
    j["net_pnl_flat"] = s.net_pnl_flat;
    j["edge"] = s.edge;
    const char* names[N_MARKOUTS] = {"100ms", "1s", "5s", "30s", "60s", "300s"};
    for (int h = 0; h < N_MARKOUTS; ++h) j["markout"][names[h]] = s.markout[h];
    j["max_drawdown"] = s.max_drawdown;
    j["max_abs_position"] = s.max_abs_position;
    j["mean_abs_position"] = s.mean_abs_position;
    j["avg_queue_ahead_at_fill_lots"] = s.avg_queue_ahead_at_fill;
    j["mid_start"] = s.mid_start;
    j["mid_end"] = s.mid_end;
    j["uptime_bid"] = s.uptime_bid;
    j["uptime_ask"] = s.uptime_ask;
    j["net_bps_of_notional"] = s.notional > 0 ? s.net_pnl / s.notional * 1e4 : 0.0;
    auto pj = [](const RunSummary::Pctl& p) {
        return nlohmann::json{{"n", p.n}, {"p50_ns", p.p50}, {"p90_ns", p.p90}, {"p99_ns", p.p99},
                              {"p999_ns", p.p999}, {"max_ns", p.max}};
    };
    if (s.recv_to_engine.n) j["latency"]["recv_to_engine"] = pj(s.recv_to_engine);
    if (s.tick_to_trade.n)  j["latency"]["tick_to_trade"]  = pj(s.tick_to_trade);
    if (s.engine_to_trade.n) j["latency"]["engine_to_trade"] = pj(s.engine_to_trade);
    std::ofstream(path) << j.dump(2) << "\n";
}
