#include <catch2/catch_test_macros.hpp>
#include "../include/marketdata/itch_book_builder.hpp"

#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

// Builds ITCH 5.0 messages (big-endian fields, 2-byte length framing).
struct Writer {
    std::vector<uint8_t> buf;
    std::vector<uint8_t> msg;

    void u8(uint8_t v) { msg.push_back(v); }
    void u16(uint16_t v) { msg.push_back(static_cast<uint8_t>(v >> 8)); msg.push_back(static_cast<uint8_t>(v)); }
    void u32(uint32_t v) { for (int s = 24; s >= 0; s -= 8) msg.push_back(static_cast<uint8_t>(v >> s)); }
    void u48(uint64_t v) { for (int s = 40; s >= 0; s -= 8) msg.push_back(static_cast<uint8_t>(v >> s)); }
    void u64(uint64_t v) { for (int s = 56; s >= 0; s -= 8) msg.push_back(static_cast<uint8_t>(v >> s)); }
    void str8(const std::string& s) { for (size_t i = 0; i < 8; ++i) msg.push_back(i < s.size() ? s[i] : ' '); }
    void head(char type, uint16_t loc) { msg.clear(); u8(static_cast<uint8_t>(type)); u16(loc); u16(0); u48(34200000000000ULL); }
    void end() {
        buf.push_back(static_cast<uint8_t>(msg.size() >> 8));
        buf.push_back(static_cast<uint8_t>(msg.size()));
        buf.insert(buf.end(), msg.begin(), msg.end());
    }

    void directory(uint16_t loc, const std::string& sym) { head('R', loc); str8(sym); for (int i = 0; i < 20; ++i) u8(0); end(); }
    void add(uint16_t loc, uint64_t ref, char side, uint32_t shares, uint32_t price) {
        head('A', loc); u64(ref); u8(static_cast<uint8_t>(side)); u32(shares); str8("X"); u32(price); end();
    }
    void execute(uint16_t loc, uint64_t ref, uint32_t shares) { head('E', loc); u64(ref); u32(shares); u64(1); end(); }
    void cancel(uint16_t loc, uint64_t ref, uint32_t shares) { head('X', loc); u64(ref); u32(shares); end(); }
    void del(uint16_t loc, uint64_t ref) { head('D', loc); u64(ref); end(); }
    void replace(uint16_t loc, uint64_t old_ref, uint64_t new_ref, uint32_t shares, uint32_t price) {
        head('U', loc); u64(old_ref); u64(new_ref); u32(shares); u32(price); end();
    }
};

}  // namespace

TEST_CASE("ITCH 5.0: messages build the expected order-level book", "[itch]") {
    Writer w;
    w.directory(7, "AAPL");
    w.add(7, 1, 'B', 100, 1500000);   // $150.00
    w.add(7, 2, 'B', 200, 1500000);
    w.add(7, 3, 'S', 50, 1500100);    // $150.01
    w.execute(7, 1, 40);              // 60 left, still first in queue
    w.cancel(7, 2, 50);               // partial cancel keeps priority: 150 left
    w.replace(7, 3, 4, 70, 1500200);  // ask moves to $150.02, new ref
    w.add(7, 5, 'B', 10, 1499900);
    w.del(7, 5);
    w.del(7, 999);                    // unknown ref is counted, not fatal

    ItchBookBuilder b;
    REQUIRE(itch::parse_itch(w.buf.data(), w.buf.size(), b) == w.buf.size());
    REQUIRE(b.symbol(7) == "AAPL");
    REQUIRE(b.find_symbol("AAPL") == 7);
    const L3Book* bk = b.book(7);
    REQUIRE(bk != nullptr);
    REQUIRE(bk->best_price(Side::Bid) == ItchBookBuilder::to_ticks(1500000));
    REQUIRE(ItchBookBuilder::to_usd(bk->best_price(Side::Bid)) == 150.0);
    REQUIRE(bk->best_qty(Side::Bid) == 210);
    REQUIRE(bk->queue_ahead(2) == 60);                // order 1 still ahead of 2
    REQUIRE(ItchBookBuilder::to_usd(bk->best_price(Side::Ask)) == 150.02);
    REQUIRE_FALSE(bk->contains(3));
    REQUIRE(bk->find(4)->qty == 70);
    REQUIRE(b.stats().unknown_ref == 1);
    REQUIRE(b.stats().subpenny_violations == 0);
    REQUIRE(b.stats().messages == 10);
}

TEST_CASE("ITCH 5.0: a partial trailing message is left for the next buffer", "[itch]") {
    Writer w;
    w.add(1, 1, 'S', 5, 5000);        // $0.50: sub-dollar stock keeps 1/10,000 units
    w.add(1, 2, 'S', 5, 4999);
    ItchBookBuilder b;
    const size_t cut = w.buf.size() - 3;
    const size_t used = itch::parse_itch(w.buf.data(), cut, b);
    REQUIRE(used < cut);
    REQUIRE(b.stats().adds == 1);
    itch::parse_itch(w.buf.data() + used, w.buf.size() - used, b);
    REQUIRE(b.stats().adds == 2);
    REQUIRE(b.book(1)->best_price(Side::Ask) == 4999);   // sub-dollar: exact 1/10,000 units
}

TEST_CASE("ITCH price map is exact and order-preserving across $1", "[itch]") {
    // A $0.0001 stub bid on a $12 stock is legal: sub-penny is allowed below $1.
    REQUIRE(ItchBookBuilder::to_ticks(1) < ItchBookBuilder::to_ticks(9999));
    REQUIRE(ItchBookBuilder::to_ticks(9999) < ItchBookBuilder::to_ticks(10000));
    REQUIRE(ItchBookBuilder::to_ticks(10000) + 1 == ItchBookBuilder::to_ticks(10100));
    for (uint32_t p : {1u, 9999u, 10000u, 123400u, 99999900u}) REQUIRE(ItchBookBuilder::to_usd(ItchBookBuilder::to_ticks(p)) * 1e4 == p);
}

TEST_CASE("ITCH 5.0: messages shorter than their spec length are counted, never decoded", "[itch]") {
    Writer w;
    w.directory(7, "AAPL");
    const std::vector<uint8_t> good = w.buf;
    // Zero-length frame, a 1-byte 'D', and an add cut off before its price.
    std::vector<uint8_t> buf = {0, 0, 0, 1, 'D'};
    w.buf.clear();
    w.add(7, 1, 'B', 100, 1500000);
    buf.push_back(0);
    buf.push_back(30);
    buf.insert(buf.end(), w.buf.begin() + 2, w.buf.begin() + 32);
    buf.insert(buf.end(), good.begin(), good.end());   // the stream resynchronises
    // Copy to an exactly-sized heap block so ASan sees any overread.
    std::unique_ptr<uint8_t[]> exact(new uint8_t[buf.size()]);
    std::copy(buf.begin(), buf.end(), exact.get());

    ItchBookBuilder b;
    REQUIRE(itch::parse_itch(exact.get(), buf.size(), b) == buf.size());
    REQUIRE(b.stats().malformed == 3);
    REQUIRE(b.stats().adds == 0);
    REQUIRE(b.symbol(7) == "AAPL");
}

TEST_CASE("ITCH 5.0: random and bit-flipped streams never read out of bounds or corrupt the book", "[itch][fuzz]") {
    std::mt19937_64 rng(1);
    for (int trial = 0; trial < 200; ++trial) {
        Writer w;
        w.directory(1, "X");
        for (uint64_t i = 1; i < 60; ++i) {
            w.add(1, i, rng() % 2 ? 'B' : 'S', 1 + static_cast<uint32_t>(rng() % 500), 100000 + 100 * static_cast<uint32_t>(rng() % 50));
            if (rng() % 3 == 0) w.execute(1, 1 + rng() % i, 10);
            if (rng() % 3 == 0) w.cancel(1, 1 + rng() % i, 10);
            if (rng() % 4 == 0) w.del(1, 1 + rng() % i);
            if (rng() % 4 == 0) w.replace(1, 1 + rng() % i, 1000 + i, 77, 100000);
        }
        for (int k = 0; k < 40; ++k) w.buf[rng() % w.buf.size()] = static_cast<uint8_t>(rng());
        std::unique_ptr<uint8_t[]> exact(new uint8_t[w.buf.size()]);
        std::copy(w.buf.begin(), w.buf.end(), exact.get());
        ItchBookBuilder b;
        REQUIRE(itch::parse_itch(exact.get(), w.buf.size(), b) <= w.buf.size());
        // Aggregates stay consistent with the orders they summarise.
        for (int loc = 0; loc < 4; ++loc) {
            const L3Book* bk = b.book(static_cast<uint16_t>(loc));
            if (!bk) continue;
            for (Side s : {Side::Bid, Side::Ask}) {
                std::vector<Level> lv;
                bk->top_levels(s, 1000, lv);
                for (const Level& l : lv) {
                    Qty sum = 0;
                    bk->for_each_in_level(s, l.price, [&](const L3Book::Order& o) { sum += o.qty; });
                    REQUIRE(sum == l.qty);
                    REQUIRE(sum > 0);
                }
            }
        }
    }
}
