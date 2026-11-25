#include "../../include/sim/l3_exchange_sim.hpp"

#include <algorithm>

L3ExchangeSim::L3ExchangeSim(Instrument inst, ExchangeConfig cfg)
    : inst_(inst), cfg_(cfg), taker_(inst, cfg), book_(18, 1 << 16) {
    // Order-level books are never conflated, and taker_ sees no book updates
    // that would reset what it has consumed.
    ExchangeConfig tc = cfg;
    tc.consume_prints = false;
    taker_ = ExchangeSim(inst, tc);
}

double L3ExchangeSim::queue_ahead(OrderId id) const {
    if (!ours_.count(id)) return -1.0;
    return static_cast<double>(book_.queue_ahead(syn(id)));
}

void L3ExchangeSim::resting_sides(bool& has_bid, bool& has_ask) const {
    has_bid = has_ask = false;
    for (const auto& [id, o] : ours_) {
        (void)id;
        (o.req.side == Side::Bid ? has_bid : has_ask) = true;
    }
}

void L3ExchangeSim::on_new_order(const OrderRequest& req, Timestamp ts, const OrderBook& l2, Portfolio& pf,
                                 std::vector<ExecutionReport>& out) {
    if (req.type == OrderType::Market || req.tif == TimeInForce::IOC || req.qty <= 0) {
        taker_.on_new_order(req, ts, l2, pf, out);
        return;
    }
    ExecutionReport base;
    base.order_id   = req.order_id;
    base.ts         = ts;
    base.side       = req.side;
    base.order_type = req.type;
    base.price      = req.price;

    auto bb = l2.best_bid();
    auto ba = l2.best_ask();
    const bool crosses = (req.side == Side::Bid) ? (ba && req.price >= ba->price)
                                                 : (bb && req.price <= bb->price);
    if (crosses && req.tif == TimeInForce::PostOnly) {
        ExecutionReport r = base;
        r.status = OrderStatus::Rejected;
        out.push_back(r);
        return;
    }
    Qty taken = 0;
    if (crosses) {   // GTC: take what crosses, rest the remainder
        OrderRequest ioc = req;
        ioc.tif = TimeInForce::IOC;
        std::vector<ExecutionReport> tmp;
        taker_.on_new_order(ioc, ts, l2, pf, tmp);
        for (auto& r : tmp) {
            if (r.fill_qty <= 0) continue;   // drop the IOC's cancel/reject of the remainder
            taken += r.fill_qty;
            out.push_back(r);
        }
        if (taken >= req.qty) return;
    }
    Ours o;
    o.req    = req;
    o.filled = taken;
    ours_[req.order_id] = o;
    book_.add(syn(req.order_id), req.side, req.price, req.qty - taken);

    ExecutionReport r = base;
    r.status     = OrderStatus::New;
    r.cum_qty    = taken;
    r.leaves_qty = req.qty - taken;
    out.push_back(r);
}

void L3ExchangeSim::on_cancel(OrderId id, Timestamp ts, std::vector<ExecutionReport>& out) {
    ExecutionReport r;
    r.order_id = id;
    r.ts       = ts;
    auto it = ours_.find(id);
    if (it == ours_.end()) {
        r.status = OrderStatus::CancelRejected;
        out.push_back(r);
        return;
    }
    r.side       = it->second.req.side;
    r.order_type = it->second.req.type;
    r.price      = it->second.req.price;
    r.status     = OrderStatus::Cancelled;
    r.cum_qty    = it->second.filled;
    book_.cancel(syn(id));
    ours_.erase(it);
    out.push_back(r);
}

void L3ExchangeSim::on_market(const MarketEvent& ev, const OrderBook& l2, Portfolio& pf,
                              std::vector<ExecutionReport>& out) {
    if (!ev.has_l3) return;
    const L3Record& r = ev.l3;
    if (!phantoms_.empty()) expire_phantoms(ev.ts);
    switch (static_cast<L3Type>(r.type)) {
        case L3Type::Add: {
            const Side side = l3_side(r.side);
            book_.add(r.id, side, r.price, r.qty);
            if (cfg_.fill_on_cross && (!ours_.empty() || !phantoms_.empty())) on_resting_cross(r, side, r.price, ev.ts, l2, pf, out);
            break;
        }
        case L3Type::Delete:
            if (deferred_.empty()) book_.cancel(r.id);
            else apply_delete(r, ev.ts);
            if (!shrunk_.empty()) shrunk_.erase(r.id);
            break;
        case L3Type::Modify:
            if (deferred_.empty() && shrunk_.empty()) book_.modify(r.id, r.qty);
            else apply_modify(r);
            break;
        case L3Type::Trade:
            if (!ours_.empty() || !phantoms_.empty()) on_print(r, ev.ts, pf, out);
            else if (auto it = deferred_.find(r.id); it != deferred_.end()) it->second.pending += r.qty;
            break;
    }
}

void L3ExchangeSim::apply_modify(const L3Record& r) {
    auto it = deferred_.find(r.id);
    if (it != deferred_.end()) {
        it->second.real    = r.qty;
        it->second.pending = 0;
        book_.modify(r.id, r.qty + it->second.extra);   // never an increase: keeps priority
        return;
    }
    if (shrunk_.count(r.id)) {
        const L3Book::Order* o = book_.find(r.id);
        if (o) book_.modify(r.id, std::min<Qty>(o->qty, r.qty));
        return;
    }
    book_.modify(r.id, r.qty);
}

void L3ExchangeSim::apply_delete(const L3Record& r, Timestamp ts) {
    auto it = deferred_.find(r.id);
    if (it == deferred_.end()) {
        book_.cancel(r.id);
        return;
    }
    const Deferred d = it->second;
    deferred_.erase(it);
    const L3Book::Order* o = book_.find(r.id);
    if (!o) return;
    if (d.pending < d.real) {   // cancelled, not executed: its owner would have pulled the extra too
        book_.cancel(r.id);
        return;
    }
    const Phantom ph{o->side, o->price};
    if (book_.modify(r.id, d.extra) && d.extra > 0) {
        add_phantom(r.id, ph);
        if (cfg_.displaced_ttl_us > 0) expiry_.push_back({ts + cfg_.displaced_ttl_us, r.id});
    }
}

void L3ExchangeSim::add_phantom(uint64_t id, Phantom ph) {
    phantoms_[id] = ph;
    phantom_levels_[ph.side == Side::Bid ? 0 : 1][ph.price] += 1;
}

bool L3ExchangeSim::erase_phantom(uint64_t id) {
    auto it = phantoms_.find(id);
    if (it == phantoms_.end()) return false;
    auto& levels = phantom_levels_[it->second.side == Side::Bid ? 0 : 1];
    auto lv = levels.find(it->second.price);
    if (--lv->second == 0) levels.erase(lv);
    phantoms_.erase(it);
    return true;
}

void L3ExchangeSim::walk(Side side, Price limit, Qty qty, uint64_t stop_before, bool front_only,
                         bool include_limit) {
    cands_.clear();
    auto better = [&](Price a, Price b) { return side == Side::Bid ? a > b : a < b; };
    walk_prices_.clear();
    for (const auto& [id, o] : ours_) {
        (void)id;
        if (o.req.side == side && (better(o.req.price, limit) || (include_limit && o.req.price == limit)))
            walk_prices_.push_back(o.req.price);
    }
    const auto& lv = phantom_levels_[side == Side::Bid ? 0 : 1];
    auto visit = [&](Price p) {
        if (!better(p, limit) && !(include_limit && p == limit)) return false;
        walk_prices_.push_back(p);
        return true;
    };
    if (side == Side::Bid) { for (auto it = lv.rbegin(); it != lv.rend() && visit(it->first); ++it) {} }
    else                   { for (auto it = lv.begin(); it != lv.end() && visit(it->first); ++it) {} }
    std::sort(walk_prices_.begin(), walk_prices_.end(), better);
    walk_prices_.erase(std::unique(walk_prices_.begin(), walk_prices_.end()), walk_prices_.end());

    Qty found = 0;
    for (Price p : walk_prices_) {
        book_.for_each_in_level_while(side, p, [&](const L3Book::Order& o) {
            const bool mine = (o.id & OURS) != 0, displaced = !mine && phantoms_.count(o.id) != 0;
            if (p == limit && (o.id == stop_before || (front_only && !mine && !displaced))) return false;
            if (mine || displaced) {
                cands_.push_back({mine ? o.id & ~OURS : 0, p, cands_.size(), displaced ? o.id : 0});
                found += o.qty;
            }
            return found < qty;
        });
        if (found >= qty) break;
    }
}

void L3ExchangeSim::expire_phantoms(Timestamp ts) {
    while (!expiry_.empty() && expiry_.front().first <= ts) {
        if (erase_phantom(expiry_.front().second)) book_.cancel(expiry_.front().second);
        expiry_.pop_front();
    }
}

// `taken` lots of a `printed`-lot print naming `maker` went to us or to
// displaced size ahead of it, so in our world it still owns them.
void L3ExchangeSim::defer(uint64_t maker, Qty taken, Qty printed) {
    const L3Book::Order* o = book_.find(maker);
    if (!o || phantoms_.count(maker)) return;
    auto [it, fresh] = deferred_.try_emplace(maker);
    Deferred& d = it->second;
    if (fresh) d.real = o->qty;
    d.extra   += std::min(taken, printed);
    d.pending += printed;
}

void L3ExchangeSim::on_print(const L3Record& r, Timestamp ts, Portfolio& pf, std::vector<ExecutionReport>& out) {
    const Side passive = (l3_side(r.side) == Side::Bid) ? Side::Ask : Side::Bid;
    const Price P = r.price;
    const L3Book::Order* maker = book_.find(r.id);
    const bool maker_at_p = maker && maker->side == passive && maker->price == P;
    const bool hidden = (r.id == 0);   // executed against a non-displayed order
    if (!maker && !hidden) ++missing_maker_;

    if (cfg_.displace_volume) {
        walk(passive, P, r.qty, r.id, hidden, hidden || maker_at_p);
        const Qty taken = cands_.empty() ? 0 : allocate(r.qty, ts, FillCause::TradeAtPrice, P, pf, out);
        if (hidden) return;
        if (taken > 0) defer(r.id, taken, r.qty);
        else if (auto it = deferred_.find(r.id); it != deferred_.end()) it->second.pending += r.qty;
        return;
    }

    // Rank of each order in P's FIFO, computed only if we rest at P.
    bool level_loaded = false;
    size_t maker_rank = 0;
    auto load_level = [&] {
        level_ids_.clear();
        book_.for_each_in_level(passive, P, [&](const L3Book::Order& o) { level_ids_.push_back(o.id); });
        maker_rank = static_cast<size_t>(std::find(level_ids_.begin(), level_ids_.end(), r.id) - level_ids_.begin());
        level_loaded = true;
    };

    cands_.clear();
    for (const auto& [id, o] : ours_) {
        if (o.req.side != passive) continue;
        const Price px = o.req.price;
        const bool better = (passive == Side::Bid) ? px > P : px < P;
        if (better) {
            cands_.push_back({id, px, 0});
        } else if (px == P && hidden) {
            // Displayed orders have priority over hidden ones at the same
            // price, so the aggressor reaches us first — but only if no real
            // displayed order is ahead (one that was still there would have
            // traded instead of the hidden order).
            if (book_.queue_ahead(syn(id)) == 0) cands_.push_back({id, px, 0});
        } else if (px == P && maker_at_p) {
            if (!level_loaded) load_level();
            const size_t rank = static_cast<size_t>(
                std::find(level_ids_.begin(), level_ids_.end(), syn(id)) - level_ids_.begin());
            if (rank < maker_rank) cands_.push_back({id, px, rank});
        }
    }
    if (cands_.empty()) return;
    std::sort(cands_.begin(), cands_.end(), [&](const Cand& a, const Cand& b) {
        if (a.price != b.price) return passive == Side::Bid ? a.price > b.price : a.price < b.price;
        return a.rank < b.rank;
    });
    allocate(r.qty, ts, FillCause::TradeAtPrice, P, pf, out);
}

void L3ExchangeSim::on_resting_cross(const L3Record& r, Side side, Price px, Timestamp ts, const OrderBook& l2,
                                     Portfolio& pf, std::vector<ExecutionReport>& out) {
    // A crossed real book means an incoming taker; its trades arrive as prints.
    if (!l2.empty() && l2.crossed()) return;
    const Side ours_side = (side == Side::Bid) ? Side::Ask : Side::Bid;
    if (cfg_.displace_volume) {
        walk(ours_side, px, r.qty, 0, false, true);
        const Qty taken = cands_.empty() ? 0 : allocate(r.qty, ts, FillCause::BookCross, px, pf, out);
        if (taken > 0) {
            // Those lots of the incoming order traded on arrival and never rested.
            shrunk_[r.id] = true;
            book_.modify(r.id, r.qty - taken);
        }
        return;
    }
    cands_.clear();
    for (const auto& [id, o] : ours_) {
        if (o.req.side != ours_side) continue;
        const bool crossed = (ours_side == Side::Bid) ? o.req.price >= px : o.req.price <= px;
        if (crossed) cands_.push_back({id, o.req.price, static_cast<size_t>(book_.queue_ahead(syn(id)))});
    }
    if (cands_.empty()) return;
    std::sort(cands_.begin(), cands_.end(), [&](const Cand& a, const Cand& b) {
        if (a.price != b.price) return ours_side == Side::Bid ? a.price > b.price : a.price < b.price;
        return a.rank < b.rank;
    });
    allocate(r.qty, ts, FillCause::BookCross, px, pf, out);
}

Qty L3ExchangeSim::allocate(Qty qty, Timestamp ts, FillCause cause_at, Price at_price, Portfolio& pf,
                             std::vector<ExecutionReport>& out) {
    Qty remaining = qty;
    for (const Cand& c : cands_) {
        if (remaining <= 0) break;
        if (c.phantom) {
            const L3Book::Order* o = book_.find(c.phantom);
            if (!o) { erase_phantom(c.phantom); continue; }
            const Qty f = book_.execute(c.phantom, remaining);
            if (!book_.contains(c.phantom)) erase_phantom(c.phantom);
            displaced_absorbed_ += f;
            remaining -= f;
            continue;
        }
        auto it = ours_.find(c.id);
        if (it == ours_.end()) continue;
        const Qty f = std::min(it->second.leaves(), remaining);
        if (f <= 0) continue;
        const FillCause cause = (cause_at == FillCause::BookCross) ? FillCause::BookCross
                              : (c.price == at_price)            ? FillCause::TradeAtPrice
                                                                 : FillCause::TradeThrough;
        fill(it->second, f, ts, cause, pf, out);
        if (cause_at != FillCause::BookCross) our_print_volume_ += f;
        remaining -= f;
        if (it->second.leaves() <= 0) ours_.erase(it);
    }
    return qty - remaining;
}

void L3ExchangeSim::fill(Ours& o, Qty q, Timestamp ts, FillCause cause, Portfolio& pf,
                         std::vector<ExecutionReport>& out) {
    const OrderRequest& req = o.req;
    const double ahead = static_cast<double>(book_.queue_ahead(syn(req.order_id)));
    book_.execute(syn(req.order_id), q);
    o.filled += q;
    const double fee = cfg_.fees.maker_fee(inst_.notional(req.price, q), inst_.qty(q));
    pf.on_fill(req.side, req.price, q, fee);

    ExecutionReport r;
    r.order_id    = req.order_id;
    r.ts          = ts;
    r.side        = req.side;
    r.order_type  = req.type;
    r.price       = req.price;
    r.fill_price  = req.price;
    r.fill_qty    = q;
    r.cum_qty     = o.filled;
    r.leaves_qty  = o.leaves();
    r.fee         = fee;
    r.liquidity   = Liquidity::Maker;
    r.status      = r.leaves_qty == 0 ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
    r.cause       = cause;
    r.queue_ahead = ahead;
    out.push_back(r);
}
