#pragma once
#include "../core/l3_book.hpp"
#include "exchange.hpp"
#include "exchange_sim.hpp"

#include <deque>
#include <unordered_map>
#include <vector>

// Exact-queue exchange for order-level (L3) replay.
//
// The replayed market's orders and ours live in one L3Book. Our orders get
// synthetic ids (top bit set) and join the back of the *real* FIFO at their
// price, so queue position is exact rather than modelled: every market order
// that arrives later queues behind us, and every cancel ahead of us moves us
// up, exactly as on the venue.
//
// Fill rules:
//   * Trade print (maker M, price P, size Q): the matching engine reached M,
//     so any of our orders with priority over M — a better price, or ahead of
//     M in P's FIFO — would have been hit first. They fill, in priority order,
//     up to Q. The market book itself is left to the venue's own order events
//     (no market impact). If M is already gone when its print arrives (venues
//     can publish the delete first), only strictly better-priced orders fill:
//     whether we were ahead of M at P is then unknowable, so assume not.
//   * Resting cross: a market order that rests at a price crossing ours,
//     while the real book is *not* crossed, only rested because we weren't
//     there — on the venue it would have traded with us. Fill up to its size.
//     This is the pessimistic reading: a post-only order would instead have
//     been repriced or rejected, and the feed cannot tell the two apart.
//     fill_on_cross = false is the optimistic one (only prints fill us).
//     (A real book that is crossed means an incoming taker, whose trades
//     follow as prints and are handled by the first rule.)
//
// Market and IOC orders, and the crossing part of a GTC limit, take
// liquidity from the L2 view exactly as in ExchangeSim.
class L3ExchangeSim : public Exchange {
public:
    L3ExchangeSim(Instrument inst, ExchangeConfig cfg);

    void on_new_order(const OrderRequest& req, Timestamp ts, const OrderBook& l2, Portfolio& pf,
                      std::vector<ExecutionReport>& out) override;
    void on_cancel(OrderId id, Timestamp ts, std::vector<ExecutionReport>& out) override;
    void on_market(const MarketEvent& ev, const OrderBook& l2, Portfolio& pf,
                   std::vector<ExecutionReport>& out) override;

    size_t open_order_count() const override { return ours_.size(); }
    double queue_ahead(OrderId id) const override;
    void   resting_sides(bool& has_bid, bool& has_ask) const override;

    const L3Book& book() const { return book_; }
    size_t prints_with_missing_maker() const { return missing_maker_; }
    // Volume accounting for displace_volume (lots): what our orders took from
    // prints, and what previously displaced size absorbed.
    Qty our_print_volume() const { return our_print_volume_; }
    Qty displaced_absorbed() const { return displaced_absorbed_; }
    size_t phantom_orders() const { return phantoms_.size(); }

private:
    static constexpr uint64_t OURS = uint64_t{1} << 63;
    struct Ours {
        OrderRequest req;
        Qty filled = 0;
        Qty leaves() const { return req.qty - filled; }
    };

    Instrument   inst_;
    ExchangeConfig cfg_;
    ExchangeSim  taker_;     // L2 sweeps for market/IOC/crossing orders
    L3Book       book_;
    std::unordered_map<OrderId, Ours> ours_;
    size_t       missing_maker_ = 0;

    // ── volume displacement (cfg_.displace_volume) ──
    // A real maker that still rests and owns `extra` lots more than the feed
    // says, because our orders (or displaced size ahead of it) took prints
    // the feed says it filled.
    struct Deferred {
        Qty extra   = 0;
        Qty real    = 0;   // its size in the feed, after the last book event
        Qty pending = 0;   // printed against it since that event
    };
    // Displaced size whose real owner has been fully executed in the feed.
    struct Phantom { Side side; Price price; };
    std::unordered_map<uint64_t, Deferred> deferred_;
    std::unordered_map<uint64_t, Phantom>  phantoms_;       // by the owner's order id; size lives in book_
    std::deque<std::pair<Timestamp, uint64_t>> expiry_;
    std::unordered_map<uint64_t, bool> shrunk_;             // incoming orders partly executed on arrival by us
    Qty our_print_volume_ = 0, displaced_absorbed_ = 0;

    void expire_phantoms(Timestamp ts);
    void apply_modify(const L3Record& r);
    void apply_delete(const L3Record& r, Timestamp ts);
    void defer(uint64_t maker, Qty taken, Qty printed);

    struct Cand {
        OrderId  id;            // ours
        Price    price;
        size_t   rank;
        uint64_t phantom = 0;   // non-zero: displaced size instead of one of ours
    };
    std::vector<Cand>     cands_;
    std::vector<uint64_t> level_ids_;

    static uint64_t syn(OrderId id) { return id | OURS; }
    void on_print(const L3Record& r, Timestamp ts, Portfolio& pf, std::vector<ExecutionReport>& out);
    void on_resting_cross(const L3Record& r, Side side, Price px, Timestamp ts, const OrderBook& l2,
                          Portfolio& pf, std::vector<ExecutionReport>& out);
    // Fills our orders in `cands_` (already in priority order) up to `qty`.
    // Returns the quantity taken.
    Qty allocate(Qty qty, Timestamp ts, FillCause cause_at, Price at_price, Portfolio& pf,
                  std::vector<ExecutionReport>& out);
    void fill(Ours& o, Qty q, Timestamp ts, FillCause cause, Portfolio& pf, std::vector<ExecutionReport>& out);
};
