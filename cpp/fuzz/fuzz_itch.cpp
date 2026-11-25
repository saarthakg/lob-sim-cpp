// libFuzzer target: arbitrary bytes through the ITCH decoder, the book builder
// and the L3 extractor's mapping. Build with -DLOBSIM_FUZZ=ON (Clang).
#include "../include/marketdata/itch_book_builder.hpp"

#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    ItchBookBuilder b(12, 8);   // few locates: keeps the fuzzer fast
    if (itch::parse_itch(data, size, b) > size) std::abort();
    for (int loc = 0; loc < 8; ++loc) {
        const L3Book* bk = b.book(static_cast<uint16_t>(loc));
        if (!bk) continue;
        for (Side s : {Side::Bid, Side::Ask}) {
            std::vector<Level> lv;
            bk->top_levels(s, 64, lv);
            for (const Level& l : lv) {
                Qty sum = 0;
                bk->for_each_in_level(s, l.price, [&](const L3Book::Order& o) { sum += o.qty; });
                if (sum != l.qty || sum <= 0) std::abort();   // aggregate out of step with its orders
            }
        }
    }
    return 0;
}
