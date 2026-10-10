#include "../../include/core/l3_book.hpp"

L3Book::L3Book(int log2_window, size_t reserve_orders)
    : orders_(reserve_orders), levels_(reserve_orders / 8 + 64), ids_(reserve_orders),
      bids_(true, log2_window), asks_(false, log2_window) {}

const L3Book::Order* L3Book::find(uint64_t id) const {
    const uint32_t oi = ids_.find(id);
    return oi == FlatHashMap::NOT_FOUND ? nullptr : &orders_[oi];
}

void L3Book::append(uint32_t oi) {
    Order& o = orders_[oi];
    PriceLadder& lad = ladder(o.side);
    uint32_t li = lad.find(o.price);
    if (li == PriceLadder::NIL) {
        li = levels_.alloc();
        levels_[li].price = o.price;
        lad.insert(o.price, li);
    }
    LevelNode& lv = levels_[li];
    o.level = li;
    o.prev  = lv.tail;
    o.next  = NIL;
    if (lv.tail != NIL) orders_[lv.tail].next = oi;
    else                lv.head = oi;
    lv.tail = oi;
    lv.total += o.qty;
    ++lv.count;
}

void L3Book::unlink(uint32_t oi) {
    Order& o = orders_[oi];
    LevelNode& lv = levels_[o.level];
    if (o.prev != NIL) orders_[o.prev].next = o.next;
    else               lv.head = o.next;
    if (o.next != NIL) orders_[o.next].prev = o.prev;
    else               lv.tail = o.prev;
    lv.total -= o.qty;
    if (--lv.count == 0) {
        ladder(o.side).erase(lv.price);
        levels_.release(o.level);
    }
    ids_.erase(o.id);
    orders_.release(oi);
}

bool L3Book::add(uint64_t id, Side side, Price price, Qty qty) {
    if (qty <= 0 || ids_.find(id) != FlatHashMap::NOT_FOUND) return false;
    const uint32_t oi = orders_.alloc();
    Order& o = orders_[oi];
    o.id    = id;
    o.side  = side;
    o.price = price;
    o.qty   = qty;
    ids_.insert(id, oi);
    append(oi);
    return true;
}

bool L3Book::cancel(uint64_t id) {
    const uint32_t oi = ids_.find(id);
    if (oi == FlatHashMap::NOT_FOUND) return false;
    unlink(oi);
    return true;
}

bool L3Book::modify(uint64_t id, Qty new_qty) {
    const uint32_t oi = ids_.find(id);
    if (oi == FlatHashMap::NOT_FOUND) return false;
    Order& o = orders_[oi];
    if (new_qty <= 0) {
        unlink(oi);
        return true;
    }
    if (new_qty <= o.qty) {
        levels_[o.level].total -= (o.qty - new_qty);
        o.qty = new_qty;
        return true;
    }
    // Size increase: loses priority — move to the back of the level.
    const Side side = o.side;
    const Price px = o.price;
    unlink(oi);
    return add(id, side, px, new_qty);
}

Qty L3Book::execute(uint64_t id, Qty qty) {
    const uint32_t oi = ids_.find(id);
    if (oi == FlatHashMap::NOT_FOUND || qty <= 0) return 0;
    Order& o = orders_[oi];
    if (qty >= o.qty) {
        const Qty q = o.qty;
        unlink(oi);
        return q;
    }
    o.qty -= qty;
    levels_[o.level].total -= qty;
    return qty;
}

Qty L3Book::best_qty(Side s) const {
    const PriceLadder& lad = ladder(s);
    return lad.empty() ? 0 : levels_[lad.find(lad.best())].total;
}

Qty L3Book::level_qty(Side s, Price p) const {
    const uint32_t li = ladder(s).find(p);
    return li == PriceLadder::NIL ? 0 : levels_[li].total;
}

uint32_t L3Book::level_orders(Side s, Price p) const {
    const uint32_t li = ladder(s).find(p);
    return li == PriceLadder::NIL ? 0 : levels_[li].count;
}

Qty L3Book::queue_ahead(uint64_t id) const {
    const uint32_t oi = ids_.find(id);
    if (oi == FlatHashMap::NOT_FOUND) return 0;
    Qty ahead = 0;
    for (uint32_t p = orders_[oi].prev; p != NIL; p = orders_[p].prev) ahead += orders_[p].qty;
    return ahead;
}

void L3Book::top_levels(Side s, size_t n, std::vector<Level>& out) const {
    out.clear();
    ladder(s).for_each_best(n, [&](Price p, uint32_t li) { out.push_back({p, levels_[li].total}); });
}

void L3Book::clear() {
    orders_.clear();
    levels_.clear();
    ids_.clear();
    bids_.clear();
    asks_.clear();
}
