#pragma once
#include "../core/order_book.hpp"
#include "../marketdata/event_source.hpp"
#include "../strategy/strategy.hpp"
#include "exchange_sim.hpp"
#include "portfolio.hpp"

#include <array>
#include <deque>
#include <memory>
#include <random>
#include <string>
#include <vector>

// Three one-way delays, modelled separately because they hurt in different
// ways:
//   feed_us     exchange event → strategy sees it (stale book, late signals)
//   entry_us    strategy sends → order/cancel reaches the matching engine
//               (stale quotes get picked off while a cancel is in flight)
//   response_us exchange report → strategy learns of the fill/ack
//               (strategy's position view lags reality)
// jitter_sigma > 0 multiplies entry/response delays by a lognormal factor
// exp(N(-σ²/2, σ²)) (mean 1), keeping per-channel FIFO order.
struct LatencyConfig {
    int64_t  feed_us      = 0;
    int64_t  entry_us     = 0;
    int64_t  response_us  = 0;
    double   jitter_sigma = 0.0;
    uint64_t seed         = 42;
};

// Which simulated venue our orders trade against.
//   Auto     — exact-queue L3 exchange when the source carries order-level
//              records, otherwise the L2 queue-model exchange.
//   L2Queue  — always the queue-model ExchangeSim (also on L3 data, to compare).
//   L3Exact  — L3ExchangeSim; requires an order-level source.
enum class ExchangeModel { Auto, L2Queue, L3Exact };

struct BacktestConfig {
    ExchangeConfig exchange;
    ExchangeModel  exchange_model = ExchangeModel::Auto;
    LatencyConfig  latency;
    Qty            max_position = 0;      // lots; 0 = no gateway position limit
    int64_t        sample_us    = 1'000'000;  // equity/book sampling interval
    std::string    output_dir;            // empty = no files written
    bool           write_book_metrics = true;
    bool           quiet = false;
};

static constexpr int N_MARKOUTS = 6;
static constexpr std::array<int64_t, N_MARKOUTS> MARKOUT_US = {
    100'000, 1'000'000, 5'000'000, 30'000'000, 60'000'000, 300'000'000};

struct FillRecord {
    Timestamp ts = 0;
    OrderId   order_id = 0;
    Side      side = Side::Bid;
    Liquidity liquidity = Liquidity::Maker;
    Price     price = 0;
    Qty       qty = 0;
    double    fee = 0.0;
    double    mid_at_fill = 0.0;       // exchange mid (ticks) at fill time
    double    queue_ahead_at_entry = 0.0;
    double    queue_ahead_at_fill  = 0.0;
    FillCause cause = FillCause::None;
    Qty       position_after = 0;
    std::array<double, N_MARKOUTS> mid_after{};  // NaN if horizon beyond data
};

struct RunSummary {
    std::string strategy;
    std::string symbol;
    Timestamp   start_ts = 0, end_ts = 0;
    size_t      market_events = 0, trade_events = 0;
    double      wall_ms = 0.0;

    size_t orders_sent = 0, cancels_sent = 0, rejects = 0, cancel_rejects = 0, local_refusals = 0;
    size_t maker_fills = 0, taker_fills = 0;
    double maker_volume = 0.0, taker_volume = 0.0;   // base units
    double notional = 0.0;                            // quote ccy

    double gross_pnl = 0.0, fees = 0.0, net_pnl = 0.0;
    double realized_pnl = 0.0;
    double final_position = 0.0;                      // base units
    double flatten_cost = 0.0;                        // est. cost to close at taker fee + half spread
    double net_pnl_flat = 0.0;                        // net_pnl - flatten_cost

    // Attribution (quote ccy). edge = Σ side·(mid_at_fill − price)·qty is the
    // spread captured vs mid at the instant of the fill; markout_h is the same
    // against the mid h later. markout_h − edge is the adverse selection.
    double edge = 0.0;
    std::array<double, N_MARKOUTS> markout{};

    double max_drawdown = 0.0;   // on sampled net equity
    double max_abs_position = 0.0;
    double mean_abs_position = 0.0;
    double avg_queue_ahead_at_fill = 0.0;
    double mid_start = 0.0, mid_end = 0.0;            // quote ccy
    double uptime_bid = 0.0, uptime_ask = 0.0;        // fraction of samples with a resting quote
};

class Backtester : private StrategyContext {
public:
    Backtester(EventSource& source, Strategy& strategy, BacktestConfig cfg);
    ~Backtester() override;

    RunSummary run();

    const std::vector<FillRecord>& fills() const { return fills_; }
    const Portfolio& portfolio() const { return pf_; }

    static void write_summary_json(const RunSummary& s, const std::string& path);

private:
    // StrategyContext
    Timestamp         now() const override { return local_now_; }
    const OrderBook&  book() const override { return local_book_; }
    const Instrument& instrument() const override { return inst_; }
    Qty               position() const override { return local_pos_; }
    double            maker_fee() const override { return cfg_.exchange.fees.maker; }
    double            taker_fee() const override { return cfg_.exchange.fees.taker; }
    const std::unordered_map<OrderId, LocalOrder>& orders() const override { return local_orders_; }
    OrderId submit_limit(Side side, Price px, Qty qty, TimeInForce tif) override;
    OrderId submit_market(Side side, Qty qty) override;
    void    cancel(OrderId id) override;

    struct OrderMsg {
        Timestamp t;
        bool      is_cancel;
        OrderRequest req;
    };
    struct ReportMsg {
        Timestamp t;
        ExecutionReport rep;
    };
    struct FeedMsg {
        Timestamp t;
        MarketEvent ev;
    };

    EventSource&   src_;
    Strategy&      strat_;
    BacktestConfig cfg_;
    Instrument     inst_;

    OrderBook   ex_book_;      // exchange-time book
    OrderBook   local_book_;   // what the strategy sees (feed-delayed)
    std::unique_ptr<Exchange> exch_;
    Portfolio   pf_;           // ground-truth accounting at exchange time

    int       px_prec_   = 2;   // decimals needed to print a price exactly
    Timestamp ex_now_    = 0;
    Timestamp local_now_ = 0;
    Qty       local_pos_ = 0;
    OrderId   next_id_   = 1;
    std::unordered_map<OrderId, LocalOrder> local_orders_;

    std::deque<OrderMsg>  to_exchange_;
    std::deque<ReportMsg> to_strategy_;
    std::deque<FeedMsg>   feed_;
    Timestamp next_timer_ = 0;
    Timestamp last_entry_arrival_ = 0, last_resp_arrival_ = 0;
    std::mt19937_64 rng_;

    std::vector<ExecutionReport> scratch_reports_;
    std::vector<FillRecord> fills_;
    std::array<size_t, N_MARKOUTS> markout_cursor_{};
    std::unordered_map<OrderId, double> queue_at_entry_;
    RunSummary sum_;

    // sampling
    Timestamp next_sample_ = 0;
    double peak_equity_ = 0.0;
    double abs_pos_acc_ = 0.0;
    size_t samples_ = 0, bid_up_ = 0, ask_up_ = 0;

    struct Writers;
    std::unique_ptr<Writers> w_;

    int64_t jittered(int64_t base);
    void drain_until(Timestamp t, bool inclusive);
    void exchange_process(const MarketEvent& ev);
    void deliver_local(const MarketEvent& ev, Timestamp local_t);
    void handle_exchange_reports(Timestamp t);
    void deliver_report(const ExecutionReport& rep);
    void record_fill(const ExecutionReport& rep);
    void update_markouts();
    void sample(Timestamp t);
    void finalize();
};
