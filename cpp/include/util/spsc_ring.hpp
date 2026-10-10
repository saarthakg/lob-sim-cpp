#pragma once
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

// Bounded lock-free single-producer / single-consumer ring buffer — the
// hand-off between the market-data thread and the strategy thread.
//
//  * Capacity is a power of two; indices are free-running counters masked on
//    access, so full vs empty needs no extra flag.
//  * head_ (consumer-owned) and tail_ (producer-owned) sit on separate cache
//    lines, so the two threads never write the same line.
//  * Each side keeps a *cached* copy of the other side's index and only
//    re-reads the shared atomic when the cache says the ring looks full
//    (producer) or empty (consumer). In steady state a push or pop touches no
//    cache line the other thread is writing.
//  * Only acquire/release ordering: the producer's release store of tail_
//    publishes the slot it just wrote; the consumer's release store of head_
//    hands the slot back.
template <typename T>
class SpscRing {
    static_assert(std::is_move_assignable_v<T>, "SpscRing element must be move-assignable");
    // 128 rather than std::hardware_destructive_interference_size: Apple
    // Silicon lines are 128 bytes, x86 prefetches line pairs, and GCC warns
    // that the standard constant is ABI-unstable.
    static constexpr size_t kLine = 128;

public:
    explicit SpscRing(size_t capacity_pow2) : slots_(round_up(capacity_pow2)), mask_(slots_.size() - 1) {}

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    size_t capacity() const { return slots_.size(); }

    // Producer thread only. Returns false if full (caller decides: drop,
    // spin, or count an overrun).
    template <typename U>
    bool try_push(U&& v) {
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (t - head_cache_ == slots_.size()) {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (t - head_cache_ == slots_.size()) return false;
        }
        slots_[t & mask_] = std::forward<U>(v);
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    // Consumer thread only. Returns false if empty.
    bool try_pop(T& out) {
        const size_t h = head_.load(std::memory_order_relaxed);
        if (h == tail_cache_) {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (h == tail_cache_) return false;
        }
        out = std::move(slots_[h & mask_]);
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    // Approximate; exact only when called from one of the two threads with
    // the other quiescent.
    size_t size_approx() const {
        return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire);
    }

private:
    static size_t round_up(size_t n) {
        size_t c = 2;
        while (c < n) c <<= 1;
        return c;
    }

    std::vector<T> slots_;
    const size_t mask_;
    alignas(kLine) std::atomic<size_t> head_{0};   // written by consumer
    alignas(kLine) size_t tail_cache_ = 0;          // consumer's view of tail_
    alignas(kLine) std::atomic<size_t> tail_{0};   // written by producer
    alignas(kLine) size_t head_cache_ = 0;          // producer's view of head_
};
