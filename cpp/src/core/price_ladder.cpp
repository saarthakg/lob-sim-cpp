#include "../../include/core/price_ladder.hpp"

#include <algorithm>

PriceLadder::PriceLadder(bool descending, int log2_window)
    : desc_(descending), W_(int64_t{1} << std::max(log2_window, 12)) {
    slots_.assign(static_cast<size_t>(W_), NIL);
    words_.assign(static_cast<size_t>(W_ >> 6), 0);
    summary_.assign(static_cast<size_t>(W_ >> 12), 0);
}

void PriceLadder::set_bit(int64_t i) {
    const int64_t w = i >> 6;
    words_[w] |= uint64_t{1} << (i & 63);
    summary_[w >> 6] |= uint64_t{1} << (w & 63);
}

void PriceLadder::clear_bit(int64_t i) {
    const int64_t w = i >> 6;
    words_[w] &= ~(uint64_t{1} << (i & 63));
    if (words_[w] == 0) summary_[w >> 6] &= ~(uint64_t{1} << (w & 63));
}

int64_t PriceLadder::next_set(int64_t i) const {
    const int64_t j = i + 1;
    if (j >= W_) return -1;
    int64_t w = j >> 6;
    uint64_t bits = words_[w] & (~uint64_t{0} << (j & 63));
    if (bits) return (w << 6) + __builtin_ctzll(bits);
    const int64_t w2 = w + 1;
    const int64_t nwords = W_ >> 6;
    if (w2 >= nwords) return -1;
    int64_t s = w2 >> 6;
    uint64_t sbits = summary_[s] & (~uint64_t{0} << (w2 & 63));
    const int64_t nsum = static_cast<int64_t>(summary_.size());
    while (!sbits) {
        if (++s >= nsum) return -1;
        sbits = summary_[s];
    }
    w = (s << 6) + __builtin_ctzll(sbits);
    return (w << 6) + __builtin_ctzll(words_[w]);
}

int64_t PriceLadder::prev_set(int64_t i) const {
    if (i <= 0) return -1;
    const int64_t j = i - 1;
    int64_t w = j >> 6;
    const int b = static_cast<int>(j & 63);
    uint64_t mask = (b == 63) ? ~uint64_t{0} : ((uint64_t{1} << (b + 1)) - 1);
    uint64_t bits = words_[w] & mask;
    if (bits) return (w << 6) + 63 - __builtin_clzll(bits);
    if (w == 0) return -1;
    const int64_t w2 = w - 1;
    int64_t s = w2 >> 6;
    const int sb = static_cast<int>(w2 & 63);
    uint64_t smask = (sb == 63) ? ~uint64_t{0} : ((uint64_t{1} << (sb + 1)) - 1);
    uint64_t sbits = summary_[s] & smask;
    while (!sbits) {
        if (s == 0) return -1;
        sbits = summary_[--s];
    }
    w = (s << 6) + 63 - __builtin_clzll(sbits);
    return (w << 6) + 63 - __builtin_clzll(words_[w]);
}

uint32_t PriceLadder::find(Price p) const {
    if (in_window(p)) return slots_[static_cast<size_t>(p - base_)];
    auto it = overflow_.find(p);
    return it == overflow_.end() ? NIL : it->second;
}

void PriceLadder::insert(Price p, uint32_t level) {
    if (!based_) {
        base_  = p - W_ / 2;
        based_ = true;
    }
    if (in_window(p)) {
        const int64_t i = p - base_;
        slots_[static_cast<size_t>(i)] = level;
        set_bit(i);
    } else {
        overflow_[p] = level;
    }
    if (++count_ == 1 || better(p, best_)) best_ = p;
    if (!in_window(best_)) recenter(best_);
}

void PriceLadder::erase(Price p) {
    if (in_window(p)) {
        const int64_t i = p - base_;
        slots_[static_cast<size_t>(i)] = NIL;
        clear_bit(i);
    } else {
        overflow_.erase(p);
    }
    if (--count_ == 0) return;
    if (p == best_) {
        next_worse(p, best_);
        if (!in_window(best_)) recenter(best_);
    }
}

bool PriceLadder::next_worse(Price p, Price& out) const {
    bool found = false;
    Price cand = 0;
    if (based_) {
        const int64_t idx = p - base_;
        int64_t r = desc_ ? prev_set(std::clamp<int64_t>(idx, 0, W_))
                          : next_set(std::clamp<int64_t>(idx, -1, W_ - 1));
        if (r >= 0) {
            cand  = base_ + r;
            found = true;
        }
    }
    if (!overflow_.empty()) {
        if (desc_) {
            auto it = overflow_.lower_bound(p);
            if (it != overflow_.begin()) {
                --it;
                if (!found || it->first > cand) { cand = it->first; found = true; }
            }
        } else {
            auto it = overflow_.upper_bound(p);
            if (it != overflow_.end() && (!found || it->first < cand)) {
                cand  = it->first;
                found = true;
            }
        }
    }
    if (found) out = cand;
    return found;
}

void PriceLadder::recenter(Price center) {
    std::vector<std::pair<Price, uint32_t>> all;
    all.reserve(count_);
    for (int64_t w = 0; w < static_cast<int64_t>(words_.size()); ++w) {
        uint64_t bits = words_[w];
        while (bits) {
            const int64_t i = (w << 6) + __builtin_ctzll(bits);
            all.emplace_back(base_ + i, slots_[static_cast<size_t>(i)]);
            slots_[static_cast<size_t>(i)] = NIL;
            bits &= bits - 1;
        }
        words_[w] = 0;
    }
    std::fill(summary_.begin(), summary_.end(), 0);
    for (const auto& kv : overflow_) all.push_back(kv);
    overflow_.clear();

    base_ = center - W_ / 2;
    for (const auto& [p, lvl] : all) {
        if (in_window(p)) {
            slots_[static_cast<size_t>(p - base_)] = lvl;
            set_bit(p - base_);
        } else {
            overflow_[p] = lvl;
        }
    }
    ++recenters_;
}

void PriceLadder::clear() {
    if (based_) {
        std::fill(slots_.begin(), slots_.end(), NIL);
        std::fill(words_.begin(), words_.end(), 0);
        std::fill(summary_.begin(), summary_.end(), 0);
    }
    overflow_.clear();
    count_ = 0;
    based_ = false;
}
