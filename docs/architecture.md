# Architecture

```
 Bybit ob200 .data.zip ─┐                               ┌──────────────── Backtester ────────────────┐
 Bybit trades .csv.gz ──┼─▶ EventSource ─▶ MarketEvent ─┤                                             │
 live-capture v2 CSV ───┘   (merge by       (Snapshot/  │  exchange time            local time        │
                             engine time)    Delta/     │  ┌──────────┐  feed_us   ┌──────────────┐   │
                                             Trade)     │  │ ex book  │──────────▶ │ local book   │   │
                                                        │  │ExchangeSim│           │ Strategy     │   │
                                                        │  │ queue/fill│◀─entry_us─│  (ctx.submit │   │
                                                        │  │ Portfolio │──resp_us─▶│   ctx.cancel)│   │
                                                        │  └──────────┘            └──────────────┘   │
                                                        │   fills ─▶ markouts, equity samples, summary│
                                                        └─────────────────────────────────────────────┘
```

## Three ways to drive the same strategy code

| Mode | Market data | Exchange | Entry point |
|---|---|---|---|
| L2 backtest | Bybit ob200 archive + trades (`BybitHistoricalSource`) | `ExchangeSim`: queue-model fills | `lob_sim --replay x.data.zip --trades y.csv.gz` |
| L3 backtest | order-level capture (`L3EventSource`, `.l3bin`) | `L3ExchangeSim`: our orders in the real FIFO | `lob_sim --replay x.l3bin` |
| Live paper trading | Bybit WebSocket (`LiveBybitSource`) | `ExchangeSim`, matching in real time | `lob_sim --live bybit --symbol BTCUSDT` |

All three go through the same `Backtester` and `Strategy` interfaces; only
the `EventSource` and the `Exchange` change.

## Live threading model

```
 WebSocket thread (IXWebSocket)                 engine thread
 ───────────────────────────────                ─────────────────────────────────────────
 frame → local_ns = steady_clock                Backtester::run()
       → bybit::parse_book / parse_trades         next(): spin-pop SpscRing (yield after 4k spins)
       → MarketEvent{ts = matching-engine time}   → exchange: fills/acks (simulated venue)
       → SpscRing::try_push (backpressure)        → strategy callback → submit/cancel
                                                   ↑ recv→engine        ↑ engine→trade, tick→trade
```

- **One producer, one consumer, no locks.** Head and tail live on separate
  cache lines, and each side caches the other's index.
- **Backpressure instead of dropping.** A lost book delta would corrupt the
  book, so a full ring makes the producer wait.
- **Event timestamps are exchange time,** so strategy timers and latency
  channels behave exactly as in a backtest. Latency *measurements* use the
  local monotonic clock.

## Order-level book internals

```
 order id ──FlatHashMap──▶ pool index ──▶ Order {id, price, qty, prev, next, level}
                                               │ intrusive FIFO per level
 price ──PriceLadder──▶ level index ──▶ Level {price, total, count, head, tail}
          dense window [base, base + 2^k) + 2-level occupancy bitmap; std::map overflow
```

## Components

| Path | Role |
|---|---|
| `core/order_book` | `std::map` L2 book in integer ticks/lots. Provides mid, weighted mid, touch and depth-band imbalance. `order_book_flat` is a sorted-vector variant kept for the benchmark. |
| `core/l3_book`, `core/price_ladder`, `util/flat_hash_map`, `util/object_pool` | Order-level book: pooled orders in intrusive FIFOs, open-addressing id map, bitmap-indexed price ladder. `core/reference_l3_book` is the textbook version used as test oracle and benchmark baseline. |
| `marketdata/l3_format`, `marketdata/l3_record` | Fixed 48-byte binary order-level records (`.l3bin`); Bitstamp converter. |
| `marketdata/bybit_parse` | Fixed-point parsers for Bybit book and trade messages, shared by the archive reader and the live feed. |
| `sim/exchange`, `sim/l3_exchange_sim` | `Exchange` interface; exact-queue exchange for order-level replay. |
| `live/live_bybit_source`, `util/spsc_ring` | Real-time feed on its own thread, handed to the engine through a lock-free SPSC ring (`lob_live` library, keeping networking out of `lob_core`). |
| `marketdata/event_source` | `BybitHistoricalSource` is a hand-rolled parser for the JSON-lines book and the trade CSV. It merges trades before book updates with the same or later `cts` and auto-detects tick and lot. `NormalizedCsvSource` reads v2 and legacy v1 CSVs. `VectorSource` is used by tests. |
| `marketdata/recorder`, `app/live_capture.hpp` | v2 capture with snapshots and trades. `book_sync` and `parser` handle the Binance U/u handshake and JSON. |
| `sim/exchange_sim` | Matching for *our* orders against the replay: post-only, IOC and market orders; queue model; three fill rules; cancel-after-fill rejects; maker/taker fees including rebates. |
| `sim/backtester` | Discrete-event loop. Three FIFO message channels (to exchange, to strategy, feed) plus a strategy timer, merged by timestamp. A local risk gateway refuses self-crosses and position-limit breaches. Records fills with markouts at 0.1/1/5/30/60/300 s and writes `summary.json`, `fills.csv`, `equity.csv`, `book_metrics.csv`. |
| `strategy/glft_market_maker` | Guéant–Lehalle–Fernandez-Tapia quotes with online A, k, σ calibration, fee-aware depth, imbalance/microprice alpha, a grid, and a depletion guard. |
| `strategy/ladder_market_maker` | Queue-priority ladder for large-tick books, with alpha-gated touch quotes and a depletion guard. |
| `strategy/naive_market_maker` | Touch-joining baseline. |
| `strategy/imbalance_strategy` | Taker on touch imbalance with a hold time and an exit. |
| `strategy/twap_strategy` | TWAP, aggressive or passive-then-sweep, reporting implementation shortfall. |
| `python/data/download_bybit.py` | Fetches public data. |
| `python/analysis/signal_research.py` | IC and OLS of 1/5/30 s mid moves on imbalance features. Coefficients are fitted on train days and checked out of sample. |
| `python/experiments/run_backtests.py`, `study.py`, `summarize_study.py` | Parallel grid search, walk-forward selection, and sensitivity sweeps, producing the README tables and plots. |
| `python/analysis/run_report.py`, `plot_book_metrics.py` | Per-run figures. |

## Event ordering within one timestamp

1. Order and cancel messages, reports, and delayed feed deliveries due
   *strictly before* the event's timestamp.
2. The exchange applies the event:
   - a trade runs fill checks;
   - a depth update applies to the book, then runs queue updates and cross checks.
3. Reports due at that same timestamp. At zero latency, the strategy learns of
   a fill before it sees the event that caused it.
4. The strategy sees the event (immediately, or `feed_us` later).

Orders the strategy sends in reaction to an event are matched against the
book *as of that event*. They can never see the next update before it
happens: there is no look-ahead.
