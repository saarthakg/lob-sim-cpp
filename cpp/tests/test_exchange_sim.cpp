#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "../include/sim/exchange_sim.hpp"

using Catch::Approx;

namespace {

Instrument inst() {
    Instrument i;
    i.symbol = "TEST";
    i.tick_size = 0.1;
    i.lot_size = 0.001;
    return i;
}

// bids 1000 x 50, 999 x 80 | asks 1001 x 40, 1002 x 60   (ticks / lots)
OrderBook make_book() {
    OrderBook b;
    b.apply_update(Side::Bid, 1000, 50);
    b.apply_update(Side::Bid, 999, 80);
    b.apply_update(Side::Ask, 1001, 40);
    b.apply_update(Side::Ask, 1002, 60);
    return b;
}

OrderRequest limit(OrderId id, Side s, Price px, Qty q, TimeInForce tif = TimeInForce::PostOnly) {
    OrderRequest r;
    r.order_id = id; r.side = s; r.type = OrderType::Limit; r.tif = tif; r.price = px; r.qty = q;
    return r;
}

TradePrint trade(Side aggressor, Price px, Qty q) { return TradePrint{aggressor, px, q}; }

MarketEvent delta(std::vector<Level> bids, std::vector<Level> asks, Timestamp ts = 10) {
    MarketEvent e;
    e.ts = ts; e.type = EventType::Delta; e.bids = std::move(bids); e.asks = std::move(asks);
    return e;
}

void apply(OrderBook& b, const MarketEvent& e) {
    for (auto& l : e.bids) b.apply_update(Side::Bid, l.price, l.qty);
    for (auto& l : e.asks) b.apply_update(Side::Ask, l.price, l.qty);
}

Qty filled(const std::vector<ExecutionReport>& reps) {
    Qty q = 0;
    for (auto& r : reps) q += r.fill_qty;
    return q;
}

}  // namespace

TEST_CASE("Post-only order that would cross is rejected", "[exchange]") {
    ExchangeSim ex(inst());
    Portfolio pf(inst());
    auto book = make_book();
    std::vector<ExecutionReport> out;
    ex.on_new_order(limit(1, Side::Bid, 1001, 5), 0, book, pf, out);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].status == OrderStatus::Rejected);
    REQUIRE(ex.open_order_count() == 0);
}

TEST_CASE("Joining a level queues behind its displayed size", "[exchange]") {
    ExchangeSim ex(inst());
    Portfolio pf(inst());
    auto book = make_book();
    std::vector<ExecutionReport> out;
    ex.on_new_order(limit(1, Side::Bid, 1000, 10), 0, book, pf, out);
    REQUIRE(out.back().status == OrderStatus::New);
    REQUIRE(ex.find(1)->queue_ahead == Approx(50));

    out.clear();
    ex.on_trade(trade(Side::Ask, 1000, 30), 1, pf, out);  // sell hits bid: 30 of 50 ahead
    REQUIRE(out.empty());
    REQUIRE(ex.find(1)->queue_ahead == Approx(20));

    ex.on_trade(trade(Side::Ask, 1000, 24), 2, pf, out);  // 20 ahead, 4 excess fill us
    REQUIRE(filled(out) == 4);
    REQUIRE(out.back().status == OrderStatus::PartiallyFilled);
    REQUIRE(out.back().liquidity == Liquidity::Maker);
    REQUIRE(pf.position() == 4);
}

TEST_CASE("A trade through our price fills us, limited by the print's size", "[exchange]") {
    ExchangeSim ex(inst());
    Portfolio pf(inst());
    auto book = make_book();
    std::vector<ExecutionReport> out;
    ex.on_new_order(limit(1, Side::Bid, 1000, 10), 0, book, pf, out);
    out.clear();
    ex.on_trade(trade(Side::Ask, 999, 3), 1, pf, out);  // sell printed below our bid
    REQUIRE(filled(out) == 3);
    ex.on_trade(trade(Side::Ask, 999, 50), 2, pf, out);
    REQUIRE(filled(out) == 10);
    REQUIRE(out.back().status == OrderStatus::Filled);
    REQUIRE(ex.open_order_count() == 0);
}

TEST_CASE("Trades on the other side or behind our price don't fill", "[exchange]") {
    ExchangeSim ex(inst());
    Portfolio pf(inst());
    auto book = make_book();
    std::vector<ExecutionReport> out;
    ex.on_new_order(limit(1, Side::Bid, 999, 10), 0, book, pf, out);
    out.clear();
    ex.on_trade(trade(Side::Bid, 1001, 100), 1, pf, out);  // buy aggressor: lifts asks
    ex.on_trade(trade(Side::Ask, 1000, 100), 1, pf, out);  // sell at better bid than ours
    REQUIRE(out.empty());
}

TEST_CASE("Depth decrease already explained by trades is not double counted", "[exchange]") {
    ExchangeConfig cfg;
    cfg.queue.kind = QueueModel::Kind::ProbPower;
    ExchangeSim ex(inst(), cfg);
    Portfolio pf(inst());
    auto book = make_book();
    std::vector<ExecutionReport> out;
    ex.on_new_order(limit(1, Side::Bid, 1000, 10), 0, book, pf, out);
    ex.on_trade(trade(Side::Ask, 1000, 30), 1, pf, out);   // ahead 50 -> 20
    auto ev = delta({{1000, 20}}, {});                       // level 50 -> 20: all from the trade
    apply(book, ev);
    ex.on_depth(ev, book, pf, out);
    REQUIRE(ex.find(1)->queue_ahead == Approx(20));
}

TEST_CASE("Queue models apportion cancellations differently", "[exchange]") {
    for (auto kind : {QueueModel::Kind::RiskAverse, QueueModel::Kind::ProbPower}) {
        ExchangeConfig cfg;
        cfg.queue.kind = kind;
        ExchangeSim ex(inst(), cfg);
        Portfolio pf(inst());
        auto book = make_book();
        std::vector<ExecutionReport> out;
        ex.on_new_order(limit(1, Side::Bid, 1000, 10), 0, book, pf, out);   // ahead 50
        auto grow = delta({{1000, 150}}, {});                                 // 100 join behind us
        apply(book, grow);
        ex.on_depth(grow, book, pf, out);
        REQUIRE(ex.find(1)->queue_ahead == Approx(50));
        auto shrink = delta({{1000, 110}}, {}, 20);                           // 40 cancelled somewhere
        apply(book, shrink);
        ex.on_depth(shrink, book, pf, out);
        double ahead = ex.find(1)->queue_ahead;
        if (kind == QueueModel::Kind::RiskAverse) {
            REQUIRE(ahead == Approx(50));            // assume all cancels behind us
        } else {
            // p_behind = 100^2/(100^2+50^2) = 0.8 → 8 of the 40 from ahead
            REQUIRE(ahead == Approx(42));
        }
    }
}

TEST_CASE("QueueModel boundary conditions", "[exchange]") {
    QueueModel m;
    REQUIRE(m.prob_behind(0, 10) == Approx(1.0));   // at the front: every cancel is behind
    REQUIRE(m.prob_behind(10, 0) == Approx(0.0));   // at the back: every cancel is ahead
    m.kind = QueueModel::Kind::ProbLog;
    REQUIRE(m.prob_behind(5, 5) == Approx(0.5));
}

TEST_CASE("A level emptying out leaves our order at the front, not cancelled", "[exchange]") {
    ExchangeSim ex(inst());
    Portfolio pf(inst());
    auto book = make_book();
    std::vector<ExecutionReport> out;
    ex.on_new_order(limit(1, Side::Bid, 1000, 10), 0, book, pf, out);
    out.clear();
    auto ev = delta({{1000, 0}}, {});
    apply(book, ev);
    ex.on_depth(ev, book, pf, out);
    REQUIRE(out.empty());
    REQUIRE(ex.find(1) != nullptr);
    REQUIRE(ex.find(1)->queue_ahead == Approx(0));
    ex.on_trade(trade(Side::Ask, 1000, 4), 2, pf, out);
    REQUIRE(filled(out) == 4);
}

TEST_CASE("Book moving through our price fills us (fill_on_cross)", "[exchange]") {
    ExchangeSim ex(inst());
    Portfolio pf(inst());
    auto book = make_book();
    std::vector<ExecutionReport> out;
    ex.on_new_order(limit(1, Side::Ask, 1001, 10), 0, book, pf, out);  // join best ask
    out.clear();
    // Bids jump to 1002: anyone bidding 1002 would have traded with our 1001 ask.
    auto ev = delta({{1002, 5}, {1001, 5}}, {{1001, 0}, {1002, 0}, {1003, 20}});
    apply(book, ev);
    ex.on_depth(ev, book, pf, out);
    REQUIRE(filled(out) == 10);
    REQUIRE(out.back().fill_price == 1001);
    REQUIRE(pf.position() == -10);
}

TEST_CASE("Cancel that arrives after a fill is rejected", "[exchange]") {
    ExchangeSim ex(inst());
    Portfolio pf(inst());
    auto book = make_book();
    std::vector<ExecutionReport> out;
    ex.on_new_order(limit(1, Side::Bid, 1000, 10), 0, book, pf, out);
    ex.on_trade(trade(Side::Ask, 999, 100), 1, pf, out);
    out.clear();
    ex.on_cancel(1, 2, out);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].status == OrderStatus::CancelRejected);
}

TEST_CASE("Market order sweeps levels with one report per level", "[exchange]") {
    ExchangeSim ex(inst());
    Portfolio pf(inst());
    auto book = make_book();
    std::vector<ExecutionReport> out;
    OrderRequest m;
    m.order_id = 7; m.side = Side::Bid; m.type = OrderType::Market; m.qty = 70;
    ex.on_new_order(m, 0, book, pf, out);
    REQUIRE(out.size() == 2);
    REQUIRE(out[0].fill_price == 1001);
    REQUIRE(out[0].fill_qty == 40);
    REQUIRE(out[1].fill_price == 1002);
    REQUIRE(out[1].fill_qty == 30);
    REQUIRE(out[1].status == OrderStatus::Filled);
    REQUIRE(out[0].liquidity == Liquidity::Taker);
    // taker fee 5.5 bp on 0.04 * 100.1 + 0.03 * 100.2
    REQUIRE(pf.total_fees_paid() == Approx((0.04 * 100.1 + 0.03 * 100.2) * 0.00055));
}

TEST_CASE("Market order larger than the book: remainder cancelled", "[exchange]") {
    ExchangeSim ex(inst());
    Portfolio pf(inst());
    auto book = make_book();
    std::vector<ExecutionReport> out;
    OrderRequest m;
    m.order_id = 7; m.side = Side::Ask; m.type = OrderType::Market; m.qty = 1000;
    ex.on_new_order(m, 0, book, pf, out);
    REQUIRE(filled(out) == 130);
    REQUIRE(out.back().status == OrderStatus::Cancelled);
}

TEST_CASE("Negative maker fee is a rebate", "[exchange]") {
    ExchangeConfig cfg;
    cfg.fees.maker = -0.0001;
    ExchangeSim ex(inst(), cfg);
    Portfolio pf(inst());
    auto book = make_book();
    std::vector<ExecutionReport> out;
    ex.on_new_order(limit(1, Side::Ask, 1005, 1000), 0, book, pf, out);  // fresh level, front of queue
    ex.on_trade(trade(Side::Bid, 1005, 1000), 1, pf, out);
    REQUIRE(out.back().fee == Approx(-100.5 * 1.0 * 0.0001));
    REQUIRE(pf.net_pnl(1005) > pf.total_pnl(1005));
}
