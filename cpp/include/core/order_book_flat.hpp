#pragma once

#include "types.hpp"
#include "side.hpp"
#include "level.hpp"
#include "book_snapshot.hpp"

#include <optional>
#include <vector>

/**
 * OrderBookFlat — same public interface as OrderBook, alternative implementation.
 *
 * Internals: sorted std::vector<Level> instead of std::map.
 *   Bids: descending by price (front = best bid)
 *   Asks: ascending  by price (front = best ask)
 *
 * Trade-offs vs std::map:
 *   apply_update : O(log N) binary search + O(N) vector shift  (worse for deep books)
 *   best_bid/ask : O(1) front access                           (better than map begin())
 *   cache perf   : contiguous memory — faster iteration for small N (< ~30 levels)
 *   deep books   : std::map wins at depth > ~30 levels (less cache-friendly shifting)
 *
 * Bybit orderbook.200 has up to 200 levels, so std::map is expected to win on throughput.
 * The benchmark confirms or contradicts this empirically.
 */
class OrderBookFlat {
public:
    void clear();
    void load_snapshot(const BookSnapshot& snapshot);
    void apply_update(Side side, Level level);  // qty == 0 → delete
    void apply_update(Side side, Price price, Qty qty);

    std::optional<Level> best_bid() const;
    std::optional<Level> best_ask() const;

    double mid_price() const;
    double spread()    const;

    std::vector<Level> top_n_bids(int n) const;
    std::vector<Level> top_n_asks(int n) const;

    UpdateId update_id() const;
    void set_update_id(UpdateId id);

private:
    // bids_: sorted descending (highest price at front)
    // asks_: sorted ascending  (lowest  price at front)
    std::vector<Level> bids_;
    std::vector<Level> asks_;
    UpdateId           update_id_ = 0;

    // Returns iterator to level with given price, or end() if not found.
    // Comparator must match the vector's sort order.
    template <typename Comp>
    static std::vector<Level>::iterator find_price(
        std::vector<Level>& vec, Price price, Comp cmp);
};
