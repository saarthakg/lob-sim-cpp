#include "../../include/marketdata/itch_book_builder.hpp"

ItchBookBuilder::ItchBookBuilder(int log2_window)
    : log2_window_(log2_window), books_(65536), scale_(65536, 0), symbols_(65536) {}

L3Book& ItchBookBuilder::get(uint16_t loc, uint32_t first_price) {
    auto& b = books_[loc];
    if (!b) {
        b = std::make_unique<L3Book>(log2_window_, 64);
        scale_[loc] = first_price >= 10000 ? 100 : 1;
    }
    return *b;
}

Price ItchBookBuilder::to_px(uint16_t loc, uint32_t price) {
    const uint32_t s = scale_[loc];
    if (s == 100 && price % 100 != 0) ++stats_.subpenny_violations;
    return static_cast<Price>(s == 100 ? (price + 50) / 100 : price);
}

void ItchBookBuilder::on_add(uint16_t loc, uint64_t, uint64_t ref, uint8_t side, uint32_t shares, uint32_t price) {
    ++stats_.adds;
    L3Book& b = get(loc, price);
    if (!b.add(ref, side == 'B' ? Side::Bid : Side::Ask, to_px(loc, price), shares)) ++stats_.duplicate_add;
}

void ItchBookBuilder::on_execute(uint16_t loc, uint64_t, uint64_t ref, uint32_t shares) {
    ++stats_.executes;
    L3Book* b = books_[loc].get();
    if (!b || !b->contains(ref)) { ++stats_.unknown_ref; return; }
    if (b->execute(ref, shares) < static_cast<Qty>(shares)) ++stats_.over_execute;
}

void ItchBookBuilder::on_cancel(uint16_t loc, uint64_t, uint64_t ref, uint32_t shares) {
    ++stats_.cancels;
    L3Book* b = books_[loc].get();
    const L3Book::Order* o = b ? b->find(ref) : nullptr;
    if (!o) { ++stats_.unknown_ref; return; }
    if (static_cast<Qty>(shares) > o->qty) ++stats_.over_execute;
    b->modify(ref, o->qty - static_cast<Qty>(shares));   // partial cancel keeps priority
}

void ItchBookBuilder::on_delete(uint16_t loc, uint64_t, uint64_t ref) {
    ++stats_.deletes;
    L3Book* b = books_[loc].get();
    if (!b || !b->cancel(ref)) ++stats_.unknown_ref;
}

void ItchBookBuilder::on_replace(uint16_t loc, uint64_t, uint64_t old_ref, uint64_t new_ref, uint32_t shares,
                                 uint32_t price) {
    ++stats_.replaces;
    L3Book* b = books_[loc].get();
    const L3Book::Order* o = b ? b->find(old_ref) : nullptr;
    if (!o) { ++stats_.unknown_ref; return; }
    const Side side = o->side;   // a replace keeps the side; it loses time priority
    b->cancel(old_ref);
    if (!b->add(new_ref, side, to_px(loc, price), shares)) ++stats_.duplicate_add;
}

void ItchBookBuilder::on_directory(uint16_t loc, const char* stock8) {
    std::string s(stock8, 8);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    symbols_[loc] = s;
}

size_t ItchBookBuilder::book_count() const {
    size_t n = 0;
    for (const auto& b : books_) n += (b != nullptr);
    return n;
}

size_t ItchBookBuilder::resting_orders() const {
    size_t n = 0;
    for (const auto& b : books_) if (b) n += b->order_count();
    return n;
}

int ItchBookBuilder::find_symbol(const std::string& ticker) const {
    for (size_t i = 0; i < symbols_.size(); ++i)
        if (symbols_[i] == ticker) return static_cast<int>(i);
    return -1;
}
