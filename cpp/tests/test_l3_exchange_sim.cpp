#include <catch2/catch_test_macros.hpp>
#include "../include/sim/l3_exchange_sim.hpp"

namespace {

Instrument inst() {
    Instrument i;
    i.symbol = "T";
    i.tick_size = 0.01;
    i.lot_size = 1e-8;
    return i;
}

// Drives an L3ExchangeSim and a matching market-only L2 book together, the
// way L3EventSource + Backtester would.
struct Rig {
    L3ExchangeSim ex{inst(), ExchangeConfig{}};
    OrderBook l2;
    Portfolio pf{inst()};
    std::vector<ExecutionReport> out;
    std::unordered_map<uint64_t, std::tuple<Side, Price, Qty>> mkt;   // market orders, to keep l2 in sync

    void push(L3Type t, uint64_t id, Side s, Price p, Qty q, Timestamp ts = 1) {
        L3Record r{};
        r.ts = ts; r.type = static_cast<uint8_t>(t); r.id = id; r.side = s == Side::Bid ? 0 : 1;
        r.price = p; r.qty = q;
        MarketEvent ev;
        ev.ts = ts; ev.has_l3 = true; ev.l3 = r;
        if (t == L3Type::Trade) {
            ev.type = EventType::Trade;
            ev.trade = TradePrint{s, p, q};
        } else {
            ev.type = EventType::Delta;
            if (t == L3Type::Add) mkt[id] = {s, p, q};
            else if (t == L3Type::Delete) mkt.erase(id);
            else std::get<2>(mkt[id]) = q;
            auto [ms, mp, mq] = t == L3Type::Delete ? std::make_tuple(s, p, Qty{0}) : mkt[id];
            Qty lvl = 0;
            for (auto& [oid, v] : mkt) if (std::get<0>(v) == ms && std::get<1>(v) == mp) lvl += std::get<2>(v);
            l2.apply_update(ms, mp, lvl);
            (void)mq;
        }
        ex.on_market(ev, l2, pf, out);
    }
    void order(OrderId id, Side s, Price p, Qty q) {
        OrderRequest r;
        r.order_id = id; r.side = s; r.type = OrderType::Limit; r.tif = TimeInForce::PostOnly; r.price = p; r.qty = q;
        ex.on_new_order(r, 1, l2, pf, out);
    }
    Qty filled() const {
        Qty f = 0;
        for (auto& r : out) f += r.fill_qty;
        return f;
    }
};

}  // namespace

TEST_CASE("L3 exchange: we queue behind earlier orders and ahead of later ones", "[l3ex]") {
    Rig g;
    g.push(L3Type::Add, 10, Side::Bid, 100, 5);     // A, ahead of us
    g.push(L3Type::Add, 20, Side::Ask, 105, 5);
    g.order(1, Side::Bid, 100, 3);                  // us
    g.push(L3Type::Add, 11, Side::Bid, 100, 4);     // B, behind us
    REQUIRE(g.ex.queue_ahead(1) == 5);
    g.push(L3Type::Trade, 10, Side::Ask, 100, 5);   // sell hits A: A was ahead, no fill
    REQUIRE(g.filled() == 0);
    g.push(L3Type::Delete, 10, Side::Bid, 100, 0);  // A fully filled, removed
    g.push(L3Type::Trade, 11, Side::Ask, 100, 2);   // sell hits B — but we are ahead of B
    REQUIRE(g.filled() == 2);
    REQUIRE(g.out.back().cause == FillCause::TradeAtPrice);
    REQUIRE(g.pf.position() == 2);
}

TEST_CASE("L3 exchange: a cancel ahead of us moves us up exactly", "[l3ex]") {
    Rig g;
    g.push(L3Type::Add, 10, Side::Bid, 100, 5);
    g.push(L3Type::Add, 20, Side::Ask, 105, 5);
    g.order(1, Side::Bid, 100, 3);
    g.push(L3Type::Add, 11, Side::Bid, 100, 4);
    g.push(L3Type::Delete, 10, Side::Bid, 100, 0);  // A cancels
    REQUIRE(g.ex.queue_ahead(1) == 0);
    g.push(L3Type::Trade, 11, Side::Ask, 100, 10);  // hits B: we were first, fill all 3
    REQUIRE(g.filled() == 3);
    REQUIRE(g.ex.open_order_count() == 0);
}

TEST_CASE("L3 exchange: better-priced order fills before the real maker", "[l3ex]") {
    Rig g;
    g.push(L3Type::Add, 10, Side::Bid, 100, 5);
    g.push(L3Type::Add, 20, Side::Ask, 105, 5);
    g.order(1, Side::Bid, 102, 3);                  // improves the bid
    g.push(L3Type::Trade, 10, Side::Ask, 100, 2);   // print at 100 means the sweep passed 102
    REQUIRE(g.filled() == 2);
    REQUIRE(g.out.back().cause == FillCause::TradeThrough);
}

TEST_CASE("L3 exchange: print whose maker is already gone fills only better prices", "[l3ex]") {
    Rig g;
    g.push(L3Type::Add, 20, Side::Ask, 105, 5);
    g.push(L3Type::Add, 10, Side::Bid, 100, 5);
    g.order(1, Side::Bid, 100, 3);
    g.push(L3Type::Delete, 10, Side::Bid, 100, 0);  // venue publishes delete before the print
    g.push(L3Type::Trade, 10, Side::Ask, 100, 5);
    REQUIRE(g.filled() == 0);
    REQUIRE(g.ex.prints_with_missing_maker() == 1);
}

TEST_CASE("L3 exchange: a resting order crossing ours trades with us", "[l3ex]") {
    Rig g;
    g.push(L3Type::Add, 10, Side::Bid, 99, 5);
    g.push(L3Type::Add, 20, Side::Ask, 105, 5);
    g.order(1, Side::Bid, 101, 3);
    // Seller posts at 100: rests in the real book (real best bid 99), but it
    // would have matched our 101 bid.
    g.push(L3Type::Add, 21, Side::Ask, 100, 2);
    REQUIRE(g.filled() == 2);
    REQUIRE(g.out.back().cause == FillCause::BookCross);
}

TEST_CASE("L3 exchange: a crossing taker in a crossed real book waits for its prints", "[l3ex]") {
    Rig g;
    g.push(L3Type::Add, 10, Side::Bid, 100, 5);
    g.push(L3Type::Add, 20, Side::Ask, 105, 5);
    g.order(1, Side::Bid, 100, 3);
    g.push(L3Type::Add, 30, Side::Ask, 99, 4);      // taker sell, transiently crossing the real bid
    REQUIRE(g.filled() == 0);                       // no double count: prints decide
    g.push(L3Type::Trade, 10, Side::Ask, 100, 4);   // engine hit A (ahead of us)
    REQUIRE(g.filled() == 0);
}

TEST_CASE("L3 exchange: post-only rejects and cancel races", "[l3ex]") {
    Rig g;
    g.push(L3Type::Add, 10, Side::Bid, 100, 5);
    g.push(L3Type::Add, 20, Side::Ask, 105, 5);
    g.order(1, Side::Bid, 105, 1);
    REQUIRE(g.out.back().status == OrderStatus::Rejected);
    g.order(2, Side::Ask, 104, 3);
    g.push(L3Type::Trade, 20, Side::Bid, 105, 5);   // buy sweep passed our 104 ask
    REQUIRE(g.filled() == 3);
    g.out.clear();
    g.ex.on_cancel(2, 2, g.out);
    REQUIRE(g.out.back().status == OrderStatus::CancelRejected);
}
