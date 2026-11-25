# The engine: order-level book, ITCH replay, live runtime

Moved from the README. See [architecture.md](architecture.md) for components and threading.

## Order-level (L3) engine

`L3Book` is a full order-level book with strict price-time priority, built
for replaying an exchange's raw order feed.

```
 order id ──FlatHashMap──▶ pool index ──▶ Order {prev,next,level}  ◀── intrusive FIFO per price level
                                                 │
 price ──PriceLadder (bitmap window + overflow)──▶ Level {head,tail,total,count}
```

| Design choice | Why |
|---|---|
| Orders in an index-based `ObjectPool`, linked into intrusive doubly linked FIFOs | O(1) add, cancel, modify and execute, with no search inside a level; 32-bit links keep orders dense; no malloc once warmed up |
| `FlatHashMap` (open addressing, linear probing, backward-shift deletion) for order id → order | One or two cache lines per lookup; no tombstones, so probe lengths don't degrade under real feeds where most orders are cancelled |
| `PriceLadder`: a dense price window with a two-level occupancy bitmap, plus a map for far prices | Finding the next best price after the touch empties is a couple of `ctz`/`clz` instructions, not a tree walk; the window recentres when the market drifts |

**Correctness.** A 300k-operation randomized differential test checks every
step against a `std::map`/`std::list` reference book, including identical
maker fill sequences from `match()`. Tests: `cpp/tests/test_l3_book.cpp`.

**Speed** (`lob_bench_l3`, 5M-message order-level stream: 50% add, 40%
cancel, executes and sweeps; ~20k resting orders; Apple M-series):

| Book | Throughput | p50 | p90 | p99 | p99.9 |
|---|---|---|---|---|---|
| **L3Book** | **50.1M msg/s** | **18 ns** | **23 ns** | **46 ns** | **143 ns** |
| `std::map` + `std::list` + `std::unordered_map` | 10.4M msg/s | 91 ns | 109 ns | 189 ns | 323 ns |

**Real exchange data.**
- `python/data/capture_bitstamp_l3.py` records Bitstamp's public order-level
  feed; `lob_sim --convert-bitstamp-l3` turns it into a fixed-record binary
  format (`.l3bin`).
- `lob_sim --replay-l3` replays it.
- 150 minutes of BTC/USD (954k events) replays at **24M records/s**, with zero
  unknown order references.

**Exact-queue exchange.** On `.l3bin` data the backtester uses
`L3ExchangeSim`. Our orders are inserted into the real FIFO, so every later
market order queues behind us and every cancel ahead moves us up. A trade
print fills us exactly when the matching engine would have reached our order
before the real maker. Scored against the independent L3 ground truth
(`python/analysis/queue_model_validation.py`):

| BTC/USD, 60 s horizon (63% of fills queue-driven) | Fill rate (true 0.305) | Accuracy | Precision |
|---|---|---|---|
| **Exact-queue L3 exchange** | **0.300** | **0.996** | **1.000** |
| Best L2 queue model (Power-2) | 0.296 | 0.965 | 0.957 |
| Log queue model | 0.392 | 0.889 | 0.747 |

**Checked against real Nasdaq orders.** A real order's fate is in the feed, so
it can referee the simulator. For 288,295 real displayed orders (14 stocks, 9
days in 2019 and 2025-26), a one-share simulated clone is placed right behind the real one.
Price-time priority then brackets the clone's fill: never before the order
ahead finished, never after the next order behind it traded. The bounds are
computed in Python from the raw records, independently of the C++ book
([results/itch/CLONE_VALIDATION.md](../results/itch/CLONE_VALIDATION.md)).

| Model (same market) | Impossible fills (jumped the queue) | Missed fills |
|---|---|---|
| **Exact-queue L3 exchange** | **0 of 288,295** | **2 of 79,713** (see below) |
| L2 Power-2 (default) | 13,096 (4.5%) | 1,400 (1.8%) |
| L2 RiskAverse | 735 (0.3%) | 11,410 (14.3%) |
| L2 Log | 20,947 (7.3%) | 3,264 (4.1%) |

Both "misses" are the test competing with itself: a one-share print went to
another clone at a better price, as price priority requires.

The clones follow orders that were *not* cancelled, so the L2 rows show how
often a model jumps a queue that really stayed in place. They are not
unconditional error rates.

Every strategy runs unchanged on both data paths, and `--exchange-model l2`
scores the queue model on the same L3 market for a direct comparison
([results/l2_vs_l3/COMPARISON.md](../results/l2_vs_l3/COMPARISON.md)). Running
naive, ladder and GLFT strategies on 150 minutes of Bitstamp BTC, ETH and XRP
under both exchanges:

- **Fill counts.** The queue model's fills are −4% to +48% off the
  exact-queue fills. XRP touch-joining strategies are over-filled by about
  45%.
- **Gross edge.** It is misstated by up to 0.8–1.5 bp in either direction,
  the same order of magnitude as maker fees and as the edge being measured.
- **What survives.** Every configuration is negative under both exchanges, so
  the conclusions hold. But the size of a market maker's edge can only be
  trusted with exact queue priority.


## Nasdaq ITCH 5.0 replay

`itch::parse_itch` is a zero-copy decoder for Nasdaq TotalView-ITCH 5.0,
the binary order-level feed of every Nasdaq-traded stock.

- **Decoding.** Length-framed, big-endian messages are decoded in place and
  dispatched to a templated handler: no virtual calls and no copies.
- **Books.** `ItchBookBuilder` keeps one `L3Book` per symbol.
- **Prices** use an exact, order-preserving piecewise scale: 1/10,000 USD
  below $1, cents at or above $1. Sub-penny quotes are legal only below $1
  (Reg NMS Rule 612), and the rule depends on the *order's* price, not the
  stock's, so a $0.0001 stub bid on a $12 stock is kept exactly.

**Nasdaq's free 2019-12-30 sample day** (8.25 GB uncompressed,
[results/itch/2019-12-30_replay.txt](../results/itch/2019-12-30_replay.txt)):

| | |
|---|---|
| Messages | 268.7M: 118.6M adds, 114.4M deletes, 21.6M replaces, 5.8M executions, 2.8M partial cancels |
| Books | 8,892 symbols, every one empty at end of feed (as it should be after the close) |
| Consistency | 0 unknown order refs, 0 duplicate adds, 0 over-executions, 0 sub-penny prices ≥ $1 |
| Compute (decode + book updates) | 21.7 s, i.e. **12.4M msgs/s (81 ns/msg)** |
| End to end incl. disk | 24.0 s (11.2M msgs/s) |

The full-market rate is below the single-book benchmark (18 ns/op). The
profile shows the time spread across the book operations rather than any one
hotspot: each message lands on a random one of ~9k books, so the working set
far exceeds the CPU caches and the cost is memory latency.

The results file includes a 10:30 snapshot of the AAPL, MSFT, AMZN, TSLA and
QQQ books, for example AAPL $286.37 × $286.40 and MSFT $157.11 × $157.12:
plausible levels for those names in late December 2019, built purely from
order messages.


## Live paper-trading runtime

`lob_sim --live bybit --symbol BTCUSDT --strategy <s> --quote-on-book` runs
any strategy against Bybit's **real-time** public feed, with orders matched by
the simulated exchange. It needs no account and no API keys, and it uses the
same `Backtester`, strategies and exchange as a backtest; only the
`EventSource` changes.

```
 WebSocket thread                                    engine thread
 frame ─▶ stamp recv time ─▶ fixed-point parse ─▶ SpscRing ─▶ exchange sim ─▶ strategy ─▶ order
                                                 lock-free       ↑ recv→engine    ↑ engine→trade
                                                 SPSC ring       └──────── tick→trade ────────┘
```

| Piece | Design |
|---|---|
| Hand-off | Lock-free SPSC ring: cache-line-separated head and tail, each side caching the other's index, acquire/release only. ThreadSanitizer-clean under a 5M-message two-thread test. |
| Parsing | The same allocation-light fixed-point parser as the historical archive (the archive stores the WebSocket messages verbatim). |
| Flow control | Backpressure, never drops, since a lost delta corrupts the book. Reconnects resubscribe and arrive as a fresh snapshot. |
| Measurement | Every frame is stamped on arrival. Receive→engine, engine→trade and tick→trade are recorded for every order or cancel sent *in reaction to* an event, with a 1 s start-up warm-up excluded. |

**10 minutes live, BTCUSDT** (`orderbook.50` + `publicTrade`, laptop, no core
pinning; 20k events, zero ring overruns):

| Stage | p50 | p90 | p99 | p99.9 |
|---|---|---|---|---|
| Parse (WebSocket thread) | 1.6 µs | — | 6.4 µs | — |
| Receive → engine (through the ring) | 2.6 µs | 6.1 µs | 37.5 µs | 818 µs |
| Engine → order (exchange sim + strategy) | 3.2 µs | 6.5 µs | 12.9 µs | 14.1 µs |
| **Tick → trade** (frame received → order sent) | **8.2 µs** | **14.8 µs** | **22.2 µs** | **25.4 µs** |

**Profiling.** `--measure-latency` replays an archive while timing the same
engine path, so it can be profiled reproducibly. The profile showed the
`std::map` L2 book plus a per-event hash map dominating. Rebuilding the L2
book on the bitmap price ladder cut engine compute on a full BTCUSDT day from
**25.7 µs to 3.1 µs at p50** and from **103 µs to 8.9 µs at p99**, and live
tick-to-trade from 16.4 µs to 8.2 µs, with backtest results bit-identical.

The remaining p99.9 tail on receive→engine is OS scheduling on a laptop.
Core pinning and a busy-polling socket would be the next steps on a
dedicated host.


## How the simulator works

```
 Bybit ob200 (.zip) ─┐                      exchange time                         local (strategy) time
 Bybit trades (.gz) ─┼─▶ EventSource ─▶ book ─▶ ExchangeSim ─┬─ feed_us ────▶ local book ─▶ Strategy
 v2 live capture  ───┘   merge by       (true)   queue model  │                              │
                         engine time             fills, fees  ◀── entry_us (orders, cancels) ─┘
                                                 Portfolio   ── response_us (fills, acks) ──▶ local position
```

| Concern | What the simulator does |
|---|---|
| **Data** | Bybit `orderbook.200`: a snapshot, then 100 ms deltas. The book is stamped with matching-engine time (`cts`), and trade prints are merged in *before* the book update that reflects them. Tick and lot are auto-detected. RPI prints are dropped. [docs/dataset.md](dataset.md) |
| **Passive fills** | Our order joins the back of the displayed queue. It fills from prints at its price (only after the queue ahead is consumed; partial fills allowed), from prints *through* its price, or when the opposite quote crosses it. Depth decreases not explained by trades are cancellations, split between ahead of and behind us by a RiskAverse / Power(n) / Log queue model, after [hftbacktest](https://hftbacktest.readthedocs.io/en/latest/order_fill.html). [docs/assumptions.md](assumptions.md) |
| **Latency** | Three channels. **Feed:** the strategy sees a delayed book. **Entry:** orders *and cancels* land late, so stale quotes get picked off and late cancels come back `CancelRejected`. **Response:** the strategy's view of its own position lags. Optional lognormal jitter. |
| **Orders** | Post-only, GTC, IOC and market. A local gateway refuses self-crosses and position-limit breaches. Maker and taker fees are separate; negative fees are rebates. |
| **Attribution** | Every fill records the mid at fill and at +0.1 s, 1 s, 5 s, 30 s, 60 s and 300 s. `edge` is the spread captured at the fill instant; markout minus edge is adverse selection; gross PnL in bp of notional is the break-even maker fee. Also reported: net PnL after the cost of flattening, drawdown, inventory and quote uptime. |
| **Speed** | L2 book updates: 33M/s (20 levels) and 62M/s (200 levels) on the bitmap price ladder, versus 12M/s and 8M/s for the original `std::map` book. Full backtest loop with a strategy: 5.7–6.2M events/s (`lob_bench`, synthetic). A real BTCUSDT day (860k book updates, 320k trades) replays in about 2.5 s from the compressed files; decompression and parsing are now the bottleneck. |

