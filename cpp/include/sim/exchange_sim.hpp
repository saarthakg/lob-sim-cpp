#pragma once
#include "../core/order_book.hpp"
#include "../marketdata/market_event.hpp"
#include "execution_report.hpp"
#include "portfolio.hpp"
#include "sim_order.hpp"
#include <unordered_map>
#include <vector>

// Fee rates as a fraction of notional. Negative = rebate.
// Defaults: Bybit USDT perpetual VIP0 (maker 0.02%, taker 0.055%).
struct FeeSchedule {
    double maker = 0.0002;
    double taker = 0.00055;
};

// How the queue ahead of a resting order advances when the displayed size at
// its price shrinks for reasons other than trades (i.e. cancellations).
// See docs/assumptions.md and hftbacktest's queue models, which these follow.
struct QueueModel {
    enum class Kind {
        RiskAverse,  // every cancel is assumed to come from *behind* us (pessimistic)
        ProbPower,   // P(cancel is behind us) = back^n / (back^n + front^n)
        ProbLog      // P(cancel is behind us) = ln(1+back) / (ln(1+back) + ln(1+front))
    };
    Kind   kind  = Kind::ProbPower;
    double power = 2.0;

    // Probability that a cancellation at our level came from behind us.
    double prob_behind(double front, double back) const;
};

struct ExchangeConfig {
    FeeSchedule fees;
    QueueModel  queue;
    // If true (default), an order whose price the opposite side's best quote
    // has moved *through* is filled even without a matching trade print —
    // the only fill evidence available when no trade feed is loaded.
    bool fill_on_cross = true;
};

// Simulated matching engine for *our* orders against a replayed market.
//
// The replayed book is the real market and our orders are an overlay on top
// of it: they never alter the replayed book (no market impact). Passive fill
// evidence, in order of strength:
//   1. A trade print strictly through our price (a sell below our bid): the
//      aggressor would have hit us first → fill up to the print's size.
//   2. A trade print at our price: consumes the queue ahead of us; whatever
//      exceeds it fills us (partial fills allowed).
//   3. The opposite best quote moving through our price (fill_on_cross).
// Depth decreases at our level not explained by trades are cancellations,
// apportioned between the queue ahead of and behind us by QueueModel.
class ExchangeSim {
public:
    struct Resting {
        OrderRequest req;
        Qty    filled      = 0;
        double queue_ahead = 0.0;   // estimated displayed qty ahead of us (lots)
        double level_qty   = 0.0;   // displayed qty at our price at the last depth update
        double traded_since = 0.0;  // trade volume at our price since that update
        Qty leaves() const { return req.qty - filled; }
    };

    ExchangeSim(Instrument inst, ExchangeConfig cfg = {});

    // Order arrives at the exchange at `ts` (after entry latency), matched
    // against the replayed book as it is at that moment.
    void on_new_order(const OrderRequest& req, Timestamp ts, const OrderBook& book,
                      Portfolio& pf, std::vector<ExecutionReport>& out);
    // Cancel arrives at `ts`. If the order already filled, a CancelRejected is
    // reported — cancel latency is a real source of adverse fills.
    void on_cancel(OrderId id, Timestamp ts, std::vector<ExecutionReport>& out);

    // Market data, in exchange time. `book` must already reflect `ev` for depth
    // events (trades don't change the replayed L2 book).
    void on_trade(const TradePrint& tr, Timestamp ts, Portfolio& pf,
                  std::vector<ExecutionReport>& out);
    void on_depth(const MarketEvent& ev, const OrderBook& book, Portfolio& pf,
                  std::vector<ExecutionReport>& out);

    size_t open_order_count() const { return resting_.size(); }
    const Resting* find(OrderId id) const;
    const std::unordered_map<OrderId, Resting>& resting() const { return resting_; }
    const ExchangeConfig& config() const { return cfg_; }

private:
    Instrument     inst_;
    ExchangeConfig cfg_;
    std::unordered_map<OrderId, Resting> resting_;
    std::unordered_map<Price, Qty> scratch_bids_, scratch_asks_;
    std::vector<OrderId> scratch_ids_;

    void fill_passive(Resting& r, Qty qty, Price px, Timestamp ts, Portfolio& pf,
                      std::vector<ExecutionReport>& out, FillCause cause, double queue_ahead);
    // Sweeps the book as a taker, up to `limit` (inclusive). Returns qty filled.
    Qty take_liquidity(const OrderRequest& req, Qty qty, Price limit, bool has_limit, Timestamp ts,
                       const OrderBook& book, Portfolio& pf, Qty already_filled,
                       std::vector<ExecutionReport>& out);
    ExecutionReport base_report(const OrderRequest& req, Timestamp ts) const;
};
