#pragma once
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <vector>

// Open-addressing hash map from 64-bit keys to 32-bit values, specialised for
// order-id → pool-index lookup on the order book's hot path.
//
//  * Linear probing over a power-of-two table: one cache line usually holds
//    the whole probe sequence.
//  * Backward-shift deletion: no tombstones, so probe lengths don't degrade
//    under the add/cancel churn of a real order feed (most orders are
//    cancelled, not filled).
//  * Keys are mixed with the splitmix64 finalizer, since exchange order ids
//    are often sequential or share low bits.
//  * One key value (EMPTY) is reserved.
class FlatHashMap {
public:
    static constexpr uint64_t EMPTY = ~uint64_t{0};
    static constexpr uint32_t NOT_FOUND = ~uint32_t{0};

    explicit FlatHashMap(size_t expected = 1024) { rehash(capacity_for(expected)); }

    size_t size() const { return size_; }
    size_t capacity() const { return keys_.size(); }

    // Inserts or overwrites.
    void insert(uint64_t key, uint32_t value) {
        if ((size_ + 1) * 2 > keys_.size()) rehash(keys_.size() * 2);
        size_t i = slot(key);
        while (keys_[i] != EMPTY) {
            if (keys_[i] == key) { vals_[i] = value; return; }
            i = (i + 1) & mask_;
        }
        keys_[i] = key;
        vals_[i] = value;
        ++size_;
    }

    uint32_t find(uint64_t key) const {
        size_t i = slot(key);
        while (keys_[i] != EMPTY) {
            if (keys_[i] == key) return vals_[i];
            i = (i + 1) & mask_;
        }
        return NOT_FOUND;
    }

    bool erase(uint64_t key) {
        size_t i = slot(key);
        while (keys_[i] != key) {
            if (keys_[i] == EMPTY) return false;
            i = (i + 1) & mask_;
        }
        // Backward-shift: pull later entries of the probe chain into the hole
        // when their home slot is at or before it, so lookups never need a
        // tombstone to keep probing.
        size_t hole = i;
        size_t j = i;
        while (true) {
            j = (j + 1) & mask_;
            if (keys_[j] == EMPTY) break;
            size_t home = slot(keys_[j]);
            // Is `home` cyclically outside (hole, j]? Then the entry may move.
            bool movable = (hole <= j) ? (home <= hole || home > j) : (home <= hole && home > j);
            if (movable) {
                keys_[hole] = keys_[j];
                vals_[hole] = vals_[j];
                hole = j;
            }
        }
        keys_[hole] = EMPTY;
        --size_;
        return true;
    }

    void clear() {
        std::fill(keys_.begin(), keys_.end(), EMPTY);
        size_ = 0;
    }

private:
    std::vector<uint64_t> keys_;
    std::vector<uint32_t> vals_;
    size_t mask_ = 0;
    size_t size_ = 0;

    static size_t capacity_for(size_t n) {
        size_t c = 16;
        while (c < n * 2) c <<= 1;
        return c;
    }
    static uint64_t mix(uint64_t x) {
        x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27; x *= 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }
    size_t slot(uint64_t key) const { return static_cast<size_t>(mix(key)) & mask_; }

    void rehash(size_t new_cap) {
        std::vector<uint64_t> old_k = std::move(keys_);
        std::vector<uint32_t> old_v = std::move(vals_);
        keys_.assign(new_cap, EMPTY);
        vals_.assign(new_cap, 0);
        mask_ = new_cap - 1;
        size_ = 0;
        for (size_t i = 0; i < old_k.size(); ++i)
            if (old_k[i] != EMPTY) insert(old_k[i], old_v[i]);
    }
};
