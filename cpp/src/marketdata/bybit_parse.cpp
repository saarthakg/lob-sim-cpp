#include "../../include/marketdata/bybit_parse.hpp"
#include "../../include/marketdata/event_source.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace bybit {
namespace {

const char* find_after(const char* hay, const char* needle, const char* end = nullptr) {
    const char* p = std::strstr(hay, needle);
    if (!p || (end && p >= end)) return nullptr;
    return p + std::strlen(needle);
}

int64_t parse_int(const char* p) {
    int64_t v = 0;
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    return v;
}

// `[["px","qty"],["px","qty"],...]` starting at the first '['.
void parse_levels(const char* p, std::vector<RawLevel>& out) {
    out.clear();
    if (*p != '[') return;
    ++p;
    while (*p == '[') {
        ++p;
        if (*p == '"') ++p;
        const int64_t px = decimal::parse_scaled(p);
        if (*p == '"') ++p;
        if (*p == ',') ++p;
        if (*p == '"') ++p;
        const int64_t q = decimal::parse_scaled(p);
        if (*p == '"') ++p;
        if (*p == ']') ++p;
        out.push_back({px, q});
        if (*p == ',') ++p;
    }
}

}  // namespace

bool parse_book(const char* s, BookMsg& out) {
    const char* t = find_after(s, "\"type\":\"");
    const char* data = find_after(s, "\"data\":");
    if (!t || !data) return false;
    out.snapshot = (std::strncmp(t, "snapshot", 8) == 0);
    out.bids.clear();
    out.asks.clear();
    if (const char* b = find_after(data, "\"b\":")) parse_levels(b, out.bids);
    if (const char* a = find_after(data, "\"a\":")) parse_levels(a, out.asks);
    // Prefer matching-engine time (cts) — the clock trade prints use.
    const char* cts = find_after(s, "\"cts\":");
    const char* ts  = find_after(s, "\"ts\":");
    out.ts = (cts ? parse_int(cts) : (ts ? parse_int(ts) : 0)) * 1000;
    return true;
}

size_t parse_trades(const char* s, std::vector<TradeMsg>& out) {
    const char* data = find_after(s, "\"data\":[");
    if (!data) return 0;
    size_t n = 0;
    const char* p = data;
    while (const char* open = std::strchr(p, '{')) {
        const char* close = std::strchr(open, '}');
        if (!close) break;
        TradeMsg m;
        if (const char* T = find_after(open, "\"T\":", close)) m.ts = parse_int(T) * 1000;
        if (const char* S = find_after(open, "\"S\":\"", close)) m.aggressor = (*S == 'B') ? Side::Bid : Side::Ask;
        if (const char* v = find_after(open, "\"v\":\"", close)) m.qty = decimal::parse_scaled(v);
        if (const char* pp = find_after(open, "\"p\":\"", close)) m.px = decimal::parse_scaled(pp);
        if (const char* r = find_after(open, "\"RPI\":", close)) m.rpi = (*r == 't' || *r == '1');
        out.push_back(m);
        ++n;
        p = close + 1;
        if (*p != ',') break;
    }
    return n;
}

Grid detect_grid(const BookMsg& snap, double tick, double lot) {
    Grid g;
    if (tick > 0) g.tick_scaled = static_cast<int64_t>(std::llround(tick * decimal::SCALE));
    if (lot > 0)  g.lot_scaled  = static_cast<int64_t>(std::llround(lot * decimal::SCALE));
    if (g.tick_scaled <= 0) {
        std::vector<int64_t> px;
        for (const auto& l : snap.bids) px.push_back(l.px);
        for (const auto& l : snap.asks) px.push_back(l.px);
        std::sort(px.begin(), px.end());
        int64_t gcd = 0;
        for (size_t i = 1; i < px.size(); ++i) gcd = std::gcd(gcd, px[i] - px[i - 1]);
        g.tick_scaled = gcd > 0 ? gcd : decimal::SCALE / 100;
    }
    if (g.lot_scaled <= 0) {
        int64_t gcd = 0;
        for (const auto& l : snap.bids) gcd = std::gcd(gcd, l.qty);
        for (const auto& l : snap.asks) gcd = std::gcd(gcd, l.qty);
        g.lot_scaled = gcd > 0 ? gcd : decimal::SCALE / 1000;
    }
    return g;
}

}  // namespace bybit
