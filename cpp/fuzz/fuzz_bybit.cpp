// libFuzzer target: arbitrary text through the Bybit message and decimal
// parsers. Build with -DLOBSIM_FUZZ=ON (Clang).
#include "../include/marketdata/bybit_parse.hpp"
#include "../include/marketdata/event_source.hpp"

#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::string s(reinterpret_cast<const char*>(data), size);
    bybit::BookMsg book;
    if (bybit::parse_book(s.c_str(), book)) {
        const bybit::Grid g = bybit::detect_grid(book, 0.0, 0.0);
        for (const auto& l : book.bids) { (void)g.px(l.px); (void)g.qty(l.qty); }
    }
    std::vector<bybit::TradeMsg> trades;
    bybit::parse_trades(s.c_str(), trades);
    decimal::parse_scaled(s);
    return 0;
}
