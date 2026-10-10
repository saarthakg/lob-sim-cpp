#pragma once
#include <cstdint>
#include <vector>

// Index-based object pool: objects live in one contiguous vector and are
// addressed by 32-bit index, recycled through a free list. Indices stay valid
// across growth (unlike pointers), are half the size of pointers (denser
// intrusive links), and steady-state allocation is a pop from the free list,
// with no malloc on the hot path once the pool has warmed up to its peak size.
template <typename T>
class ObjectPool {
public:
    static constexpr uint32_t NIL = ~uint32_t{0};

    explicit ObjectPool(size_t reserve = 0) {
        items_.reserve(reserve);
        free_.reserve(reserve);
    }

    uint32_t alloc() {
        if (!free_.empty()) {
            uint32_t i = free_.back();
            free_.pop_back();
            items_[i] = T{};
            ++live_;
            return i;
        }
        items_.emplace_back();
        ++live_;
        return static_cast<uint32_t>(items_.size() - 1);
    }

    void release(uint32_t i) {
        free_.push_back(i);
        --live_;
    }

    T&       operator[](uint32_t i)       { return items_[i]; }
    const T& operator[](uint32_t i) const { return items_[i]; }

    size_t live() const { return live_; }
    size_t capacity() const { return items_.size(); }
    void clear() { items_.clear(); free_.clear(); live_ = 0; }

private:
    std::vector<T> items_;
    std::vector<uint32_t> free_;
    size_t live_ = 0;
};
