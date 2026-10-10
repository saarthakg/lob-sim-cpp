// L3 order book benchmark: L3Book vs the textbook std::map/std::list book.
//
// A synthetic feed is generated once (so RNG and bookkeeping are outside the
// timed region) with the shape of a real order-level feed: mostly adds and
// cancels clustered near a drifting touch, a steady-state resting book of
// ~20k orders, and occasional executions and aggressive sweeps. The same
// stream is then replayed into each book.
//
// Latency is measured per batch of 32 operations and reported as ns/op
// percentiles: macOS's steady_clock ticks at ~42 ns on Apple Silicon, too
// coarse to time a single 10–50 ns operation honestly.
#include "../include/core/l3_book.hpp"
#include "../include/core/reference_l3_book.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

namespace {

enum class Op : uint8_t { Add, Cancel, Modify, Execute, Match };

struct Msg {
    Op       op;
    Side     side;
    uint64_t id;
    Price    price;
    Qty      qty;
};

std::vector<Msg> make_stream(size_t n, uint64_t seed) {
    std::mt19937_64 rng(seed);
    auto rnd = [&](uint64_t m) { return rng() % m; };
    ReferenceL3Book shadow;     // keeps the generator consistent with book state
    std::vector<uint64_t> live;
    std::vector<Msg> out;
    out.reserve(n);
    uint64_t next = 1;
    std::vector<std::tuple<uint64_t, Price, Qty>> fills;

    for (size_t i = 0; i < n; ++i) {
        const Price mid = 10'000'000 + static_cast<Price>((i / 5000) % 400) - 200;   // slow drift
        const uint64_t r = rnd(100);
        const bool want_add = live.size() < 20000 ? r < 60 : r < 45;
        if (want_add || live.empty()) {
            Side s = rnd(2) ? Side::Bid : Side::Ask;
            // Geometric-ish distance from the touch: most activity at 0-5 ticks.
            Price d = static_cast<Price>(std::min<uint64_t>(rnd(8) * rnd(8), 200));
            Price p = s == Side::Bid ? mid - 1 - d : mid + 1 + d;
            Qty q = 1 + static_cast<Qty>(rnd(500));
            uint64_t id = (next++) * 0x9E3779B97F4A7C15ULL;
            shadow.add(id, s, p, q);
            live.push_back(id);
            out.push_back({Op::Add, s, id, p, q});
        } else if (r < 90) {
            size_t k = rnd(live.size());
            uint64_t id = live[k];
            live[k] = live.back();
            live.pop_back();
            if (!shadow.where.count(id)) { --i; continue; }   // already filled
            shadow.cancel(id);
            out.push_back({Op::Cancel, Side::Bid, id, 0, 0});
        } else if (r < 94) {
            uint64_t id = live[rnd(live.size())];
            if (!shadow.where.count(id)) { --i; continue; }
            Qty q = 1 + static_cast<Qty>(rnd(400));
            shadow.modify(id, q);
            out.push_back({Op::Modify, Side::Bid, id, 0, q});
        } else if (r < 98) {
            uint64_t id = live[rnd(live.size())];
            if (!shadow.where.count(id)) { --i; continue; }
            Qty q = 1 + static_cast<Qty>(rnd(200));
            shadow.execute(id, q);
            out.push_back({Op::Execute, Side::Bid, id, 0, q});
        } else {
            Side a = rnd(2) ? Side::Bid : Side::Ask;
            Price lim = a == Side::Bid ? mid + 3 : mid - 3;
            Qty q = 1 + static_cast<Qty>(rnd(2000));
            fills.clear();
            shadow.match(a, lim, q, fills);
            out.push_back({Op::Match, a, 0, lim, q});
        }
    }
    return out;
}

template <typename Book, typename Apply>
void run(const char* name, const std::vector<Msg>& msgs, Apply apply) {
    Book book;
    constexpr size_t B = 32;
    std::vector<double> per_op;
    per_op.reserve(msgs.size() / B + 1);
    volatile Qty sink = 0;

    auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < msgs.size(); i += B) {
        auto b0 = std::chrono::steady_clock::now();
        const size_t e = std::min(msgs.size(), i + B);
        for (size_t j = i; j < e; ++j) sink = sink + apply(book, msgs[j]);
        auto b1 = std::chrono::steady_clock::now();
        per_op.push_back(std::chrono::duration<double, std::nano>(b1 - b0).count() / static_cast<double>(e - i));
    }
    auto t1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();
    std::sort(per_op.begin(), per_op.end());
    auto pct = [&](double q) { return per_op[static_cast<size_t>(q * static_cast<double>(per_op.size() - 1))]; };
    std::printf("%-26s %8.2f M msg/s   ns/op p50 %6.1f  p90 %6.1f  p99 %6.1f  p99.9 %7.1f\n", name,
                static_cast<double>(msgs.size()) / secs / 1e6, pct(0.50), pct(0.90), pct(0.99), pct(0.999));
}

}  // namespace

int main() {
    constexpr size_t N = 5'000'000;
    std::printf("Generating %zu-message order-level stream...\n", N);
    auto msgs = make_stream(N, 7);
    size_t counts[5] = {};
    for (auto& m : msgs) ++counts[static_cast<int>(m.op)];
    std::printf("mix: add %.1f%%  cancel %.1f%%  modify %.1f%%  execute %.1f%%  match %.1f%%\n\n",
                100.0 * counts[0] / N, 100.0 * counts[1] / N, 100.0 * counts[2] / N,
                100.0 * counts[3] / N, 100.0 * counts[4] / N);

    run<L3Book>("L3Book (pool+ladder)", msgs, [](L3Book& b, const Msg& m) -> Qty {
        switch (m.op) {
            case Op::Add:     return b.add(m.id, m.side, m.price, m.qty);
            case Op::Cancel:  return b.cancel(m.id);
            case Op::Modify:  return b.modify(m.id, m.qty);
            case Op::Execute: return b.execute(m.id, m.qty);
            case Op::Match:   return b.match(m.side, m.price, m.qty, [](uint64_t, Price, Qty) {});
        }
        return 0;
    });
    run<ReferenceL3Book>("std::map/std::list book", msgs, [](ReferenceL3Book& b, const Msg& m) -> Qty {
        static std::vector<std::tuple<uint64_t, Price, Qty>> fills;
        switch (m.op) {
            case Op::Add:     return b.add(m.id, m.side, m.price, m.qty);
            case Op::Cancel:  return b.cancel(m.id);
            case Op::Modify:  return b.modify(m.id, m.qty);
            case Op::Execute: return b.execute(m.id, m.qty);
            case Op::Match:   fills.clear(); return b.match(m.side, m.price, m.qty, fills);
        }
        return 0;
    });
    return 0;
}
