#include <catch2/catch_test_macros.hpp>
#include "../include/marketdata/itch_book_builder.hpp"

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
    REQUIRE(b.units_per_usd(7) == 100.0);             // cents book for a $150 stock
    REQUIRE(bk->best_price(Side::Bid) == 15000);
    REQUIRE(bk->best_qty(Side::Bid) == 210);
    REQUIRE(bk->queue_ahead(2) == 60);                // order 1 still ahead of 2
    REQUIRE(bk->best_price(Side::Ask) == 15002);
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
    REQUIRE(b.units_per_usd(1) == 10000.0);
    REQUIRE(b.book(1)->best_price(Side::Ask) == 4999);
}
