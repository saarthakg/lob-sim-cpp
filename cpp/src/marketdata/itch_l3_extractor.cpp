#include "../../include/marketdata/itch_l3_extractor.hpp"

#include <cstdio>
#include <filesystem>
#include <stdexcept>

int64_t date_to_us(const std::string& d) {
    int y = 0;
    unsigned m = 0, day = 0;
    char extra = 0;
    if (std::sscanf(d.c_str(), "%4d-%2u-%2u%c", &y, &m, &day, &extra) != 3 || m < 1 || m > 12 || day < 1 || day > 31)
        throw std::runtime_error("bad date (want YYYY-MM-DD): " + d);
    // Howard Hinnant's days_from_civil.
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468) * 86'400'000'000LL;
}

ItchL3Extractor::ItchL3Extractor(std::vector<std::string> symbols, const std::string& out_dir,
                                 const std::string& date, int64_t midnight_us, uint64_t end_ns)
    : by_loc_(65536, nullptr), out_dir_(out_dir), date_(date), midnight_us_(midnight_us), end_ns_(end_ns) {
    std::filesystem::create_directories(out_dir);
    for (auto& sym : symbols) {
        auto s = std::make_unique<Sym>();
        s->st.symbol = sym;
        syms_.push_back(std::move(s));
    }
}

void ItchL3Extractor::on_directory(uint16_t loc, const char* stock8) {
    std::string sym(stock8, 8);
    while (!sym.empty() && sym.back() == ' ') sym.pop_back();
    for (auto& s : syms_) {
        if (s->st.symbol != sym) continue;
        by_loc_[loc] = s.get();
        if (s->st.found) return;   // a repeated directory message
        s->st.found = true;
        s->w    = std::make_unique<L3FileWriter>(out_dir_ + "/" + date_ + "_" + sym + ".l3bin", sym, 0.01, 1.0);
        s->book = std::make_unique<L3Book>(16, 1 << 14);
    }
}

void ItchL3Extractor::emit(Sym& s, uint64_t ts, L3Type type, uint64_t id, Side side, Price px, Qty qty) {
    L3Record r{};
    r.ts    = midnight_us_ + static_cast<int64_t>(ts / 1000);
    r.id    = id;
    r.price = px;
    r.qty   = qty;
    r.type  = static_cast<uint8_t>(type);
    r.side  = side == Side::Bid ? 0 : 1;
    s.w->write(r);
}

void ItchL3Extractor::add(Sym& s, uint64_t ts, uint64_t ref, Side side, uint32_t shares, uint32_t price) {
    if (price < 10000) {
        ++s.st.skipped_subdollar;
        s.subdollar[ref] = side == Side::Bid ? 0 : 1;
        return;
    }
    const Price px = price / 100;
    if (!s.book->add(ref, side, px, shares)) return;
    ++s.st.adds;
    emit(s, ts, L3Type::Add, ref, side, px, shares);
}

void ItchL3Extractor::on_add(uint16_t loc, uint64_t ts, uint64_t ref, uint8_t side, uint32_t shares, uint32_t price) {
    if (Sym* s = get(loc, ts)) add(*s, ts, ref, side == 'B' ? Side::Bid : Side::Ask, shares, price);
}

void ItchL3Extractor::reduce(Sym& s, uint64_t ts, const L3Book::Order& o, Qty by) {
    const uint64_t id = o.id;
    const Side side = o.side;
    const Price px = o.price;
    const Qty left = o.qty - by;
    if (left > 0) {
        s.book->modify(id, left);
        emit(s, ts, L3Type::Modify, id, side, px, left);
    } else {
        s.book->cancel(id);
        emit(s, ts, L3Type::Delete, id, side, px, 0);
    }
}

void ItchL3Extractor::on_execute(uint16_t loc, uint64_t ts, uint64_t ref, uint32_t shares) {
    Sym* s = get(loc, ts);
    if (!s) return;
    const L3Book::Order* o = s->book->find(ref);
    if (!o) { s->st.unknown_ref += !s->subdollar.count(ref); return; }
    ++s->st.prints;
    emit(*s, ts, L3Type::Trade, ref, o->side == Side::Bid ? Side::Ask : Side::Bid, o->price, shares);
    reduce(*s, ts, *o, shares);
}

void ItchL3Extractor::on_execute_with_price(uint16_t loc, uint64_t ts, uint64_t ref, uint32_t shares, bool printable,
                                            uint32_t price) {
    Sym* s = get(loc, ts);
    if (!s) return;
    const L3Book::Order* o = s->book->find(ref);
    if (!o) { s->st.unknown_ref += !s->subdollar.count(ref); return; }
    if (!printable) {
        ++s->st.nonprintable;
    } else if (price % 100 != 0) {
        ++s->st.subpenny_prints;
    } else {
        ++s->st.prints;
        emit(*s, ts, L3Type::Trade, ref, o->side == Side::Bid ? Side::Ask : Side::Bid, price / 100, shares);
    }
    reduce(*s, ts, *o, shares);
}

void ItchL3Extractor::on_cancel(uint16_t loc, uint64_t ts, uint64_t ref, uint32_t shares) {
    Sym* s = get(loc, ts);
    if (!s) return;
    const L3Book::Order* o = s->book->find(ref);
    if (!o) { s->st.unknown_ref += !s->subdollar.count(ref); return; }
    reduce(*s, ts, *o, shares);
}

void ItchL3Extractor::on_delete(uint16_t loc, uint64_t ts, uint64_t ref) {
    Sym* s = get(loc, ts);
    if (!s) return;
    const L3Book::Order* o = s->book->find(ref);
    if (!o) { s->st.unknown_ref += !s->subdollar.erase(ref); return; }
    reduce(*s, ts, *o, o->qty);
}

void ItchL3Extractor::on_replace(uint16_t loc, uint64_t ts, uint64_t old_ref, uint64_t new_ref, uint32_t shares,
                                 uint32_t price) {
    Sym* s = get(loc, ts);
    if (!s) return;
    Side side;
    if (const L3Book::Order* o = s->book->find(old_ref)) {
        side = o->side;   // a replace keeps the side
        reduce(*s, ts, *o, o->qty);
    } else if (auto it = s->subdollar.find(old_ref); it != s->subdollar.end()) {
        side = it->second == 0 ? Side::Bid : Side::Ask;
        s->subdollar.erase(it);
    } else {
        ++s->st.unknown_ref;
        return;
    }
    add(*s, ts, new_ref, side, shares, price);
}

void ItchL3Extractor::on_hidden_trade(uint16_t loc, uint64_t ts, uint32_t shares, uint32_t price) {
    Sym* s = get(loc, ts);
    if (!s || shares == 0) return;
    if (price % 100 != 0) { ++s->st.subpenny_prints; return; }
    const L3Book& b = *s->book;
    if (b.empty(Side::Bid) || b.empty(Side::Ask)) return;
    // ITCH's side field on hidden trades is always 'B' since 2014; infer the
    // aggressor from where the print sits relative to the displayed mid.
    const Price px = price / 100;
    const Price mid2 = b.best_price(Side::Bid) + b.best_price(Side::Ask);
    if (2 * px == mid2) { ++s->st.subpenny_prints; return; }   // mid print: no side, no displayed queue
    ++s->st.hidden_prints;
    emit(*s, ts, L3Type::Trade, 0, 2 * px > mid2 ? Side::Bid : Side::Ask, px, shares);
}

void ItchL3Extractor::on_cross(uint16_t loc, uint64_t ts, uint64_t shares, uint32_t) {
    Sym* s = get(loc, ts);
    if (s && s->st.open_cross_shares == 0) s->st.open_cross_shares = shares;
}

void ItchL3Extractor::on_trading_action(uint16_t loc, uint64_t ts, uint8_t state) {
    Sym* s = get(loc, ts);
    if (s && state != 'T' && ts >= 34'200'000'000'000ULL) ++s->st.halts_in_session;
}

void ItchL3Extractor::on_system_event(uint64_t ts, uint8_t) {
    if (ts >= end_ns_) done_ = true;
}

std::vector<ItchL3Extractor::SymbolStats> ItchL3Extractor::finish() {
    std::vector<SymbolStats> out;
    for (auto& s : syms_) {
        if (s->w) {
            s->w->close();
            s->st.records = s->w->count();
        }
        out.push_back(s->st);
    }
    return out;
}
