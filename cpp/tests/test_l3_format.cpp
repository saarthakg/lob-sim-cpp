#include <catch2/catch_test_macros.hpp>
#include "../include/core/l3_book.hpp"
#include "../include/marketdata/event_source.hpp"
#include "../include/marketdata/l3_format.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

TEST_CASE("Bitstamp L3 capture converts and replays into the expected book", "[l3format]") {
    const std::string dir = fs::temp_directory_path().string();
    const std::string cap = dir + "/bitstamp_testusd_x.l3.jsonl";
    const std::string bin = dir + "/lobsim_test.l3bin";
    {
        std::ofstream f(cap);
        // Snapshot: two bids at 100.00 (ids give priority), one ask.
        f << R"({"recv_us":0,"snapshot":{"microtimestamp":"1000","bids":[["100.00","0.50000000","20"],["100.00","0.25000000","10"]],"asks":[["100.05","1.00000000","30"]]}})" << "\n";
        // Stale event (before the snapshot) must be skipped.
        f << R"({"recv_us":0,"msg":{"event":"order_deleted","data":{"id":10,"order_type":0,"microtimestamp":"999","price_str":"100.00","amount_str":"0.25000000"}}})" << "\n";
        f << R"({"recv_us":0,"msg":{"event":"order_created","data":{"id":40,"order_type":1,"microtimestamp":"1100","price_str":"100.04","amount_str":"0.10000000"}}})" << "\n";
        f << R"({"recv_us":0,"msg":{"event":"trade","data":{"type":0,"buy_order_id":99,"sell_order_id":40,"microtimestamp":"1200","price_str":"100.04","amount_str":"0.04000000"}}})" << "\n";
        f << R"({"recv_us":0,"msg":{"event":"order_changed","data":{"id":40,"order_type":1,"microtimestamp":"1200","price_str":"100.04","amount_str":"0.06000000"}}})" << "\n";
        f << R"({"recv_us":0,"msg":{"event":"order_deleted","data":{"id":20,"order_type":0,"microtimestamp":"1300","price_str":"100.00","amount_str":"0.50000000"}}})" << "\n";
    }
    REQUIRE(convert_bitstamp_l3(cap, bin) == 7);   // 3 snapshot adds + 4 live events

    L3FileReader rd(bin);
    REQUIRE(std::string(rd.header().symbol) == "testusd");
    REQUIRE(rd.header().tick_size == 0.01);
    REQUIRE(rd.header().lot_size == 1e-8);

    L3Book book(12);
    size_t trades = 0;
    while (const L3Record* r = rd.next()) {
        switch (static_cast<L3Type>(r->type)) {
            case L3Type::Add:    REQUIRE(book.add(r->id, l3_side(r->side), r->price, r->qty)); break;
            case L3Type::Delete: REQUIRE(book.cancel(r->id)); break;
            case L3Type::Modify: REQUIRE(book.modify(r->id, r->qty)); break;
            case L3Type::Trade:
                ++trades;
                REQUIRE(r->id == 40);          // maker
                REQUIRE(r->aux == 99);         // aggressor
                REQUIRE(r->side == 0);         // buyer-initiated
                break;
        }
    }
    REQUIRE(trades == 1);
    // Trade did not mutate the book; the order_changed did (0.10 -> 0.06).
    REQUIRE(book.find(40)->qty == 6'000'000);
    REQUIRE(book.best_price(Side::Ask) == 10004);
    // Order 20 deleted; order 10 (earlier id, so first in queue) remains.
    REQUIRE(book.best_price(Side::Bid) == 10000);
    REQUIRE(book.level_orders(Side::Bid, 10000) == 1);
    REQUIRE(book.contains(10));
    std::remove(cap.c_str());
    std::remove(bin.c_str());
}

TEST_CASE("L3EventSource emits per-level deltas and trades with the raw record attached", "[l3format]") {
    const std::string bin = (fs::temp_directory_path() / "lobsim_src_test.l3bin").string();
    {
        L3FileWriter w(bin, "X", 0.01, 1e-8);
        auto rec = [](int64_t ts, L3Type t, uint64_t id, uint8_t side, Price p, Qty q) {
            L3Record r{};
            r.ts = ts; r.type = static_cast<uint8_t>(t); r.id = id; r.side = side; r.price = p; r.qty = q;
            return r;
        };
        w.write(rec(1, L3Type::Add, 1, 0, 100, 5));
        w.write(rec(2, L3Type::Add, 2, 0, 100, 7));
        w.write(rec(3, L3Type::Modify, 1, 0, 100, 2));
        w.write(rec(4, L3Type::Trade, 2, 1, 100, 3));   // sell aggressor hits order 2
        w.write(rec(5, L3Type::Delete, 1, 0, 100, 0));
    }
    auto src = open_event_source(bin, "");
    REQUIRE(src->has_l3());
    REQUIRE(src->instrument().tick_size == 0.01);
    MarketEvent ev;
    std::vector<Qty> level_after;
    while (src->next(ev)) {
        REQUIRE(ev.has_l3);
        if (ev.type == EventType::Trade) {
            REQUIRE(ev.trade.aggressor == Side::Ask);
            REQUIRE(ev.l3.id == 2);
        } else {
            REQUIRE(ev.bids.size() == 1);
            level_after.push_back(ev.bids[0].qty);
        }
    }
    // 5, 12, 9 (modify 5->2), then 7 after deleting order 1
    REQUIRE(level_after == std::vector<Qty>{5, 12, 9, 7});
    std::remove(bin.c_str());
}
