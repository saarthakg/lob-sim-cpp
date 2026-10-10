#pragma once
#include "level.hpp"
#include "price_ladder.hpp"
#include "side.hpp"
#include "types.hpp"
#include "../util/flat_hash_map.hpp"
#include "../util/object_pool.hpp"

#include <cstdint>
#include <vector>

// Order-level (L3) limit order book with strict price-time priority.
//
// Layout, chosen for the hot path of replaying a full exchange feed:
//   * Orders live in an ObjectPool and are linked into one intrusive doubly
//     linked FIFO per price level (prev/next are 32-bit pool indices), so
//     cancelling or executing any order is O(1) — no search within the level.
//   * order id → pool index through FlatHashMap (open addressing, no
//     tombstones), so the per-message lookup is one or two cache lines.
//   * Levels live in their own pool; each side's price → level map is a
//     PriceLadder (bitmap-indexed dense window + overflow), so finding the
//     next best price after the touch empties is a few bit operations.
//   * Nothing on the add/cancel/execute path allocates once the pools have
//     grown to the book's working size.
//
// All operations take the venue's order ids, so a raw exchange feed (Bitstamp
// live_orders, Nasdaq ITCH, Coinbase full) drives it directly.
class L3Book {
public:
    struct Order {
        uint64_t id    = 0;
        Price    price = 0;
        Qty      qty   = 0;
        Side     side  = Side::Bid;
        uint32_t prev  = ObjectPool<Order>::NIL;
        uint32_t next  = ObjectPool<Order>::NIL;
        uint32_t level = ObjectPool<Order>::NIL;
    };
    struct LevelNode {
        Price    price = 0;
        Qty      total = 0;
        uint32_t count = 0;
        uint32_t head  = ObjectPool<Order>::NIL;
        uint32_t tail  = ObjectPool<Order>::NIL;
    };
    static constexpr uint32_t NIL = ObjectPool<Order>::NIL;

    explicit L3Book(int log2_window = 18, size_t reserve_orders = 1 << 16);

    // Adds at the back of its price level. False if the id already exists or
    // qty <= 0.
    bool add(uint64_t id, Side side, Price price, Qty qty);
    // Removes the order. False if unknown.
    bool cancel(uint64_t id);
    // Changes size. A decrease keeps queue priority (partial cancel); an
    // increase loses it (re-queued at the back), as on most venues.
    // new_qty <= 0 cancels. False if unknown.
    bool modify(uint64_t id, Qty new_qty);
    // A resting order traded `qty` as the maker. Returns the qty actually
    // executed (capped at the order's size); removes the order when filled.
    Qty execute(uint64_t id, Qty qty);

    // Matches an incoming aggressive order against the book in price-time
    // priority up to `limit` (inclusive), calling on_fill(maker_id, price,
    // qty) for each fill. Returns the filled quantity. Unfilled remainder is
    // not rested (callers decide).
    template <typename OnFill>
    Qty match(Side aggressor, Price limit, Qty qty, OnFill&& on_fill);

    bool         contains(uint64_t id) const { return ids_.find(id) != FlatHashMap::NOT_FOUND; }
    const Order* find(uint64_t id) const;

    bool  empty(Side s) const { return ladder(s).empty(); }
    Price best_price(Side s) const { return ladder(s).best(); }   // requires !empty(s)
    Qty   best_qty(Side s) const;
    Qty   level_qty(Side s, Price p) const;
    uint32_t level_orders(Side s, Price p) const;
    // Total displayed quantity ahead of the order in its level's FIFO. O(position).
    Qty   queue_ahead(uint64_t id) const;

    size_t order_count() const { return orders_.live(); }
    size_t level_count(Side s) const { return ladder(s).size(); }

    // Aggregated (L2) view of the top n levels, best first.
    void top_levels(Side s, size_t n, std::vector<Level>& out) const;

    // Visit the orders of one level front-to-back: f(const Order&).
    template <typename F>
    void for_each_in_level(Side s, Price p, F&& f) const;

    void clear();

private:
    ObjectPool<Order>     orders_;
    ObjectPool<LevelNode> levels_;
    FlatHashMap           ids_;
    PriceLadder           bids_;
    PriceLadder           asks_;

    PriceLadder&       ladder(Side s)       { return s == Side::Bid ? bids_ : asks_; }
    const PriceLadder& ladder(Side s) const { return s == Side::Bid ? bids_ : asks_; }

    void unlink(uint32_t oi);   // remove order from its level (and the level if empty) and free it
    void append(uint32_t oi);   // link order at the back of its price level, creating the level
};

// ── template definitions ─────────────────────────────────────────────────────

template <typename OnFill>
Qty L3Book::match(Side aggressor, Price limit, Qty qty, OnFill&& on_fill) {
    const Side passive = (aggressor == Side::Bid) ? Side::Ask : Side::Bid;
    PriceLadder& lad = ladder(passive);
    Qty filled = 0;
    // One fill per iteration; best() and find() are O(1), so re-reading the
    // touch after each fill costs nothing and keeps the loop obviously correct
    // when a fill empties a level.
    while (filled < qty && !lad.empty()) {
        const Price px = lad.best();
        if (aggressor == Side::Bid ? px > limit : px < limit) break;
        const uint32_t li = lad.find(px);
        const uint32_t oi = levels_[li].head;
        Order& o = orders_[oi];
        const Qty f = (o.qty < qty - filled) ? o.qty : (qty - filled);
        const uint64_t maker = o.id;
        if (f == o.qty) {
            unlink(oi);
        } else {
            o.qty -= f;
            levels_[li].total -= f;
        }
        filled += f;
        on_fill(maker, px, f);
    }
    return filled;
}

template <typename F>
void L3Book::for_each_in_level(Side s, Price p, F&& f) const {
    const uint32_t li = ladder(s).find(p);
    if (li == PriceLadder::NIL) return;
    for (uint32_t oi = levels_[li].head; oi != NIL; oi = orders_[oi].next) f(orders_[oi]);
}
