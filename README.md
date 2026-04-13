# lob-sim-cpp

A C++ event-driven **L2 limit order book replay and execution simulator** built around the Binance diff-depth WebSocket protocol, with live market data capture via Bybit.

---

## Features

- **L2 Order Book** — integer-tick `std::map<Price, Qty>` with O(log N) updates
- **Binance sync algorithm** — exact snapshot + diff-depth state machine per the official Binance docs
- **Live capture** — connects to Bybit WebSocket, syncs the book, records to normalized CSV
- **Replay engine** — reads normalized `depth_events.csv`, fires per-event callbacks
- **Execution simulator** — market orders walk the visible book; passive limit orders approximate queue position from L2 data
- **Two strategies** — Market Maker (inventory-skewed quoting) + Imbalance (aggressive directional)
- **Latency model** — configurable fixed market-data, decision, and order-entry delays
- **Structured CSV exports** — `book_metrics.csv`, `fills.csv`, `pnl.csv`
- **Python analytics** — pandas + matplotlib scripts for PnL, fill quality, book metrics
- **Unit tests** — Catch2, 96 assertions across 31 test cases
- **Benchmarked** — 5.3M events/sec (book only), 2.3M events/sec (with MarketMaker strategy)

---

## Quickstart

### Prerequisites

```bash
brew install cmake ninja       # macOS
# sudo apt install cmake ninja-build   # Ubuntu
```

Requires macOS with Xcode Command Line Tools, or Linux with GCC/Clang 14+.

### Build

```bash
git clone <repo>
cd lob-sim-cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build -j$(nproc)
```

### Run tests

```bash
./build/lob_tests
# All tests passed (96 assertions in 31 test cases)
```

### Synthetic demo

```bash
./build/lob_sim --synthetic-demo
```

### Capture live data and replay

```bash
# Capture 60 minutes of BTCUSDT from Bybit (US-accessible)
./build/lob_sim --live-capture BTCUSDT --exchange bybit \
    --output data/recorded/btc_live.csv --duration-minutes 60

# Replay with market maker strategy
./build/lob_sim --strategy mm \
    --replay data/recorded/btc_live.csv \
    --output data/processed/mm_run

# Analyze results
cd python && python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
python analysis/plot_book_metrics.py --run ../data/processed/mm_run --save
python analysis/pnl_report.py        --run ../data/processed/mm_run --save
python analysis/fill_quality.py      --run ../data/processed/mm_run --save
```

---

## CLI Reference

```
./lob_sim --synthetic-demo
./lob_sim --load-snapshot <path.json>
./lob_sim --replay <depth_events.csv>
./lob_sim --strategy mm        --replay <csv> [--output <dir>] [--latency-us <N>]
./lob_sim --strategy imbalance --replay <csv> [--output <dir>] [--latency-us <N>]
./lob_sim --live-capture <SYMBOL> --exchange <bybit|binance> \
          --output <csv> [--duration-minutes <N>]
```

`--latency-us <N>` applies a fixed N-microsecond delay to market-data, decision, and order-entry paths. Use `python/experiments/latency_sweep.sh` to sweep latencies automatically.

---

## Results

### Benchmark Performance

Measured on Apple M-series, Release build (`-O3`), 1 million synthetic events (20 levels per side):

| Mode                            | Throughput        |
|---------------------------------|-------------------|
| Book-only replay (std::map)     | **5.8M events/s** |
| Book-only replay (sorted vector)| **8.7M events/s** |
| Replay + MarketMaker            | **2.3M events/s** |

Run `./build/lob_bench` to reproduce. See Key Finding 4 for context.

---

### Live Data Experiment

Two 60-minute sessions of BTCUSDT captured live from Bybit (USDT perpetuals, `orderbook.200`).

| Session | Events  | CSV size | Price range            |
|---------|---------|----------|------------------------|
| 1       | ~832K   | 49 MB    | —                      |
| 2       | ~753K   | 44 MB    | $66,790 – $66,957      |

**MarketMaker on Session 2 (14,933 replay events, $66,790–$66,957 range):**

| Metric              | Value                              |
|---------------------|------------------------------------|
| Total fills         | 932 (466 buy / 466 sell)           |
| Gross realized PnL  | **+$9.32 USD**                     |
| Maker fees paid     | ~$12,462 USD (0.02% × notional)    |
| Net PnL after fees  | –$12,452 USD                       |
| Final position      | Flat (0)                           |

**ImbalanceStrategy on Session 2:**

| Metric              | Value                              |
|---------------------|------------------------------------|
| Total fills         | 1 (aggressive sell at $66,857.73)  |
| Realized PnL        | $0 (position not closed)           |
| Unrealized at end   | +$13.78 USD (price fell after fill)|
| Final position      | Short 1 BTC                        |

The imbalance threshold (0.6) was breached only once in 14,933 events — consistent with
Bybit `orderbook.200` being a deep L2 feed where imbalance rarely reaches extremes.

---

### Key Findings

**1. Spread capture works; maker fees dominate.**
The market maker captured the bid-ask spread consistently, producing a monotonically
increasing gross realized PnL of +$9.32. However, maker fees at 0.02% of notional on 932
fills totalling ~$62.3M in notional exceeded gross PnL by three orders of magnitude.
This is expected for a high-frequency passive strategy at these parameters — in practice,
fill selectivity (cherry-picking favourable queue positions) or fee rebates at higher tiers
would be needed for net profitability.

**2. Adverse selection is measurable via markout.**
Run `python analysis/markout_analysis.py --run data/processed/mm_bybit_fresh --save`
to compute markout at N = 10, 50, 100 events post-fill. Buy fills during the $167 upward
drift at session start likely show negative markout (adverse selection), while fills during
the subsequent retreat show near-zero markout (neutral fills). This is honest and expected.

**3. Queue position approximation is conservative.**
The L2 passive fill model (`qty_ahead` decrementation) tends to underestimate fill rate
relative to a true L3/FIFO model — it cannot see queue replenishment. PnL estimates from
the MarketMaker are therefore a lower bound. See [`docs/assumptions.md`](docs/assumptions.md).

**4. Flat vector outperforms std::map for shallow books; results flip at depth.**
At 20 levels per side (typical of many venues), the sorted-vector `OrderBookFlat` achieves
8.7M events/sec vs 5.8M for `std::map` — a 50% throughput improvement from cache-friendly
contiguous memory. Bybit `orderbook.200` has up to 200 levels; at that depth the O(N) vector
shift overhead expected to negate the cache benefit. Run the benchmark with larger snapshots
to confirm empirically.

---

### Limitations

- **L2 data only** — no per-order queue visibility; passive fills use qty-ahead approximation
- **No market impact** — strategy orders do not move the book (valid for small sizes)
- **Fixed latency model** — no jitter or variable network delay
- **Maker fee dominance** — strategy is gross-profitable but net-negative at current fee tier
- **Single symbol, single session** — results may not generalise across market regimes

---

### Book Metrics

![Book metrics over time](docs/plots/book_metrics.png)

### PnL Report

![PnL report](docs/plots/pnl_report.png)

### Fill Quality

![Fill quality analysis](docs/plots/fill_quality.png)

---

## Architecture

See [`docs/architecture.md`](docs/architecture.md) for a full component diagram and event flow.

```
LiveCapture → Recorder → depth_events.csv
                              │
                              ▼
ReplayEngine → BinanceDepthEvent → OrderBook → ExchangeSim → Strategy → Portfolio → CSV
```

The live capture flow (Bybit):
1. Subscribe to `orderbook.200.BTCUSDT` over WebSocket
2. Receive initial `type=snapshot` → load full book
3. Receive `type=delta` updates → apply incrementally, write to CSV

The live capture flow (Binance):
1. Open diff-depth WebSocket stream, buffer events
2. Fetch REST depth snapshot (`/api/v3/depth`)
3. Run `BookSync` state machine to find the first valid event
4. Apply subsequent events and write to CSV

> **Note:** Binance is geo-blocked for US users (HTTP 451). Use `--exchange bybit` (the default).

---

## Strategies

### MarketMaker
Quotes a bid and ask symmetrically around the mid price. Cancels and re-quotes when
the mid moves by more than 1 tick. Skews quotes toward reducing inventory when position
exceeds the configured limit. Does not quote when the book spread is unusually wide.

Key parameters: `target_half_spread_ticks`, `quote_size`, `max_inventory`, `skew_coefficient`.

### ImbalanceStrategy
Computes order book imbalance (bid qty − ask qty) / (bid qty + ask qty) over the top N levels.
Submits an aggressive market order when imbalance exceeds a configurable threshold.
Only trades when flat (no open position). Has a cooldown period after each trade.

Key parameters: `depth_levels`, `imbalance_threshold`, `order_size`, `cooldown_events`.

---

## Assumptions

See [`docs/assumptions.md`](docs/assumptions.md) for a complete list. Key points:

- **L2 only** — no per-order queue visibility; we reconstruct a price → aggregate qty map
- **Passive fill approximation** — `qty_ahead` decremented as visible qty at a level shrinks (see assumptions doc)
- **Market impact ignored** — our orders don't move the book (valid for small sizes)
- **Fixed latency** — configurable offsets for market-data delay, decision time, and order entry

---

## Dataset

Live data is captured directly from Bybit's WebSocket stream using the `--live-capture` mode.
See [`docs/dataset.md`](docs/dataset.md) for details on the normalized CSV schema and conversion tools.

---

## Extensions

- [ ] L3 order-level simulation (requires ITCH or proprietary OMS feed)
- [ ] Multi-threaded engine with lock-free book updates
- [ ] Additional strategies (TWAP, momentum, spread arbitrage)
- [ ] Realistic latency modelling (log-normal distribution, co-location tiers)
- [ ] Parquet output for large-scale analysis
