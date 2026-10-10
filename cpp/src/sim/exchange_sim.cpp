#include "../../include/sim/exchange_sim.hpp"

#include <algorithm>
#include <cmath>

double QueueModel::prob_behind(double front, double back) const {
    front = std::max(front, 0.0);
    back  = std::max(back, 0.0);
    double fb = 0.0, ff = 0.0;
    switch (kind) {
        case Kind::RiskAverse: return 1.0;
        case Kind::ProbPower:
            fb = std::pow(back, power);
            ff = std::pow(front, power);
            break;
        case Kind::ProbLog:
            fb = std::log1p(back);
            ff = std::log1p(front);
            break;
    }
    if (fb + ff <= 0.0) return 1.0;
    return fb / (fb + ff);
}

ExchangeSim::ExchangeSim(Instrument inst, ExchangeConfig cfg)
    : inst_(std::move(inst)), cfg_(cfg) {}

void ExchangeSim::on_market(const MarketEvent& ev, const OrderBook& book, Portfolio& pf,
                            std::vector<ExecutionReport>& out) {
    if (ev.type == EventType::Trade) on_trade(ev.trade, ev.ts, pf, out);
    else if (!book.empty() && !book.crossed()) on_depth(ev, book, pf, out);
}

double ExchangeSim::queue_ahead(OrderId id) const {
    const Resting* r = find(id);
    return r ? r->queue_ahead : -1.0;
}

void ExchangeSim::resting_sides(bool& has_bid, bool& has_ask) const {
    has_bid = has_ask = false;
    for (const auto& [id, rs] : resting_) {
        (void)id;
        (rs.req.side == Side::Bid ? has_bid : has_ask) = true;
    }
}

const ExchangeSim::Resting* ExchangeSim::find(OrderId id) const {
    auto it = resting_.find(id);
    return it == resting_.end() ? nullptr : &it->second;
}

ExecutionReport ExchangeSim::base_report(const OrderRequest& req, Timestamp ts) const {
    ExecutionReport r;
    r.order_id   = req.order_id;
    r.ts         = ts;
    r.side       = req.side;
    r.order_type = req.type;
    r.price      = req.price;
    return r;
}

Qty ExchangeSim::take_liquidity(const OrderRequest& req, Qty qty, Price limit, bool has_limit,
                                Timestamp ts, const OrderBook& book, Portfolio& pf,
                                Qty already_filled, std::vector<ExecutionReport>& out) {
    Qty filled = 0;
    // Buying walks the asks, selling walks the bids, best price first.
    book.for_each_level(req.side == Side::Bid ? Side::Ask : Side::Bid, [&](Price px, Qty lvl_qty) {
        if (filled >= qty) return false;
        if (has_limit) {
            if (req.side == Side::Bid && px > limit) return false;
            if (req.side == Side::Ask && px < limit) return false;
        }
        Qty f = std::min(qty - filled, lvl_qty);
        if (f <= 0) return true;
        filled += f;
        double fee = inst_.notional(px, f) * cfg_.fees.taker;
        pf.on_fill(req.side, px, f, fee);
        ExecutionReport r = base_report(req, ts);
        r.fill_price = px;
        r.fill_qty   = f;
        r.cum_qty    = already_filled + filled;
        r.leaves_qty = req.qty - r.cum_qty;
        r.fee        = fee;
        r.liquidity  = Liquidity::Taker;
        r.cause      = FillCause::Taker;
        r.status     = (r.leaves_qty == 0) ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
        out.push_back(r);
        return true;
    });
    return filled;
}

void ExchangeSim::on_new_order(const OrderRequest& req, Timestamp ts, const OrderBook& book,
                               Portfolio& pf, std::vector<ExecutionReport>& out) {
    if (req.qty <= 0) {
        ExecutionReport r = base_report(req, ts);
        r.status = OrderStatus::Rejected;
        out.push_back(r);
        return;
    }

    if (req.type == OrderType::Market) {
        Qty f = take_liquidity(req, req.qty, 0, false, ts, book, pf, 0, out);
        if (f < req.qty) {
            // Unfilled remainder of a market order is cancelled (IOC semantics).
            ExecutionReport r = base_report(req, ts);
            r.status     = (f == 0) ? OrderStatus::Rejected : OrderStatus::Cancelled;
            r.cum_qty    = f;
            r.leaves_qty = 0;
            out.push_back(r);
        }
        return;
    }

    auto bb = book.best_bid();
    auto ba = book.best_ask();
    bool crosses = (req.side == Side::Bid) ? (ba && req.price >= ba->price)
                                           : (bb && req.price <= bb->price);

    if (crosses && req.tif == TimeInForce::PostOnly) {
        ExecutionReport r = base_report(req, ts);
        r.status     = OrderStatus::Rejected;
        r.leaves_qty = 0;
        out.push_back(r);
        return;
    }

    Qty taken = 0;
    if (crosses) taken = take_liquidity(req, req.qty, req.price, true, ts, book, pf, 0, out);

    if (req.tif == TimeInForce::IOC) {
        if (taken < req.qty) {
            ExecutionReport r = base_report(req, ts);
            r.status     = (taken == 0) ? OrderStatus::Rejected : OrderStatus::Cancelled;
            r.cum_qty    = taken;
            r.leaves_qty = 0;
            out.push_back(r);
        }
        return;
    }
    if (taken >= req.qty) return;

    // Rest the remainder. We join the back of the displayed queue.
    Resting rs;
    rs.req          = req;
    rs.filled       = taken;
    rs.level_qty    = static_cast<double>(book.visible_qty_at_price(req.side, req.price));
    rs.queue_ahead  = rs.level_qty;
    rs.traded_since = 0.0;
    resting_[req.order_id] = rs;

    ExecutionReport r = base_report(req, ts);
    r.status     = OrderStatus::New;
    r.cum_qty    = taken;
    r.leaves_qty = req.qty - taken;
    out.push_back(r);
}

void ExchangeSim::on_cancel(OrderId id, Timestamp ts, std::vector<ExecutionReport>& out) {
    auto it = resting_.find(id);
    ExecutionReport r;
    r.order_id = id;
    r.ts       = ts;
    if (it == resting_.end()) {
        r.status = OrderStatus::CancelRejected;
        out.push_back(r);
        return;
    }
    r = base_report(it->second.req, ts);
    r.status     = OrderStatus::Cancelled;
    r.cum_qty    = it->second.filled;
    r.leaves_qty = 0;
    out.push_back(r);
    resting_.erase(it);
}

void ExchangeSim::fill_passive(Resting& rs, Qty qty, Price px, Timestamp ts, Portfolio& pf,
                               std::vector<ExecutionReport>& out, FillCause cause, double queue_ahead) {
    qty = std::min(qty, rs.leaves());
    if (qty <= 0) return;
    rs.filled += qty;
    double fee = inst_.notional(px, qty) * cfg_.fees.maker;
    pf.on_fill(rs.req.side, px, qty, fee);
    ExecutionReport r = base_report(rs.req, ts);
    r.fill_price = px;
    r.fill_qty   = qty;
    r.cum_qty    = rs.filled;
    r.leaves_qty = rs.leaves();
    r.fee        = fee;
    r.liquidity  = Liquidity::Maker;
    r.status     = (r.leaves_qty == 0) ? OrderStatus::Filled : OrderStatus::PartiallyFilled;
    r.cause       = cause;
    r.queue_ahead = queue_ahead;
    out.push_back(r);
}

void ExchangeSim::on_trade(const TradePrint& tr, Timestamp ts, Portfolio& pf,
                           std::vector<ExecutionReport>& out) {
    if (resting_.empty() || tr.qty <= 0) return;
    // A buy aggressor lifts asks; a sell aggressor hits bids.
    const Side passive = (tr.aggressor == Side::Bid) ? Side::Ask : Side::Bid;

    scratch_ids_.clear();
    for (auto& [id, rs] : resting_)
        if (rs.req.side == passive) scratch_ids_.push_back(id);
    if (scratch_ids_.empty()) return;
    // Price priority among our own orders: best-priced first.
    std::sort(scratch_ids_.begin(), scratch_ids_.end(), [&](OrderId a, OrderId b) {
        const auto& ra = resting_[a].req;
        const auto& rb = resting_[b].req;
        if (ra.price != rb.price) return passive == Side::Bid ? ra.price > rb.price : ra.price < rb.price;
        return a < b;
    });

    double remaining = static_cast<double>(tr.qty);  // aggressor size left to allocate to us
    for (OrderId id : scratch_ids_) {
        Resting& rs = resting_[id];
        const bool through = (passive == Side::Bid) ? (tr.price < rs.req.price)
                                                    : (tr.price > rs.req.price);
        if (through) {
            Qty f = std::min<Qty>(rs.leaves(), static_cast<Qty>(remaining));
            if (f > 0) {
                fill_passive(rs, f, rs.req.price, ts, pf, out, FillCause::TradeThrough, rs.queue_ahead);
                remaining -= static_cast<double>(f);
            }
            // Level was swept: nobody is left ahead of whatever remains of us.
            rs.queue_ahead = 0.0;
        } else if (tr.price == rs.req.price) {
            rs.traded_since += static_cast<double>(tr.qty);
            const double ahead_before = rs.queue_ahead;
            rs.queue_ahead  -= static_cast<double>(tr.qty);
            if (rs.queue_ahead < 0.0) {
                double excess = -rs.queue_ahead;
                rs.queue_ahead = 0.0;
                Qty f = static_cast<Qty>(std::floor(std::min(excess, remaining) + 1e-9));
                f = std::min(f, rs.leaves());
                if (f > 0) {
                    fill_passive(rs, f, rs.req.price, ts, pf, out, FillCause::TradeAtPrice, ahead_before);
                    remaining -= static_cast<double>(f);
                }
            }
        }
        if (remaining <= 0.0) break;
    }
    for (OrderId id : scratch_ids_) {
        auto it = resting_.find(id);
        if (it != resting_.end() && it->second.leaves() <= 0) resting_.erase(it);
    }
}

void ExchangeSim::on_depth(const MarketEvent& ev, const OrderBook& book, Portfolio& pf,
                           std::vector<ExecutionReport>& out) {
    if (resting_.empty()) return;

    const bool snapshot = (ev.type == EventType::Snapshot);

    auto bb = book.best_bid();
    auto ba = book.best_ask();
    scratch_ids_.clear();

    for (auto& [id, rs] : resting_) {
        const auto& req = rs.req;
        bool touched = snapshot;
        double new_qty = 0.0;
        if (snapshot) {
            new_qty = static_cast<double>(book.visible_qty_at_price(req.side, req.price));
        } else {
            // A handful of resting orders against tens of changed levels: a
            // linear scan beats building a hash map of the delta every event.
            const auto& lv = (req.side == Side::Bid) ? ev.bids : ev.asks;
            for (const auto& l : lv) {
                if (l.price == req.price) {
                    touched = true;
                    new_qty = static_cast<double>(l.qty);   // last update for a price wins
                }
            }
        }

        if (touched) {
            const double prev     = rs.level_qty;
            const double net_chg  = prev - new_qty - rs.traded_since;  // >0: cancels
            const double level_now = std::max(prev - rs.traded_since, 0.0);
            rs.traded_since = 0.0;
            if (net_chg > 0.0) {
                const double front = rs.queue_ahead;
                const double back  = std::max(level_now - front, 0.0);
                const double p     = cfg_.queue.prob_behind(front, back);
                double est = front - (1.0 - p) * net_chg + std::min(back - p * net_chg, 0.0);
                rs.queue_ahead = std::max(est, 0.0);
            }
            rs.queue_ahead = std::min(rs.queue_ahead, new_qty);
            rs.level_qty   = new_qty;
        }

        // The opposite side trading through our price is unambiguous fill
        // evidence even with no trade print (e.g. depth-only data).
        if (cfg_.fill_on_cross) {
            bool crossed = (req.side == Side::Bid) ? (ba && ba->price <= req.price)
                                                   : (bb && bb->price >= req.price);
            if (crossed) {
                fill_passive(rs, rs.leaves(), req.price, ev.ts, pf, out, FillCause::BookCross, rs.queue_ahead);
                scratch_ids_.push_back(id);
            }
        }
    }
    for (OrderId id : scratch_ids_) resting_.erase(id);
}
