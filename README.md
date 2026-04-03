# lob-sim-cpp

A C++ event-driven **L2 limit order book replay and execution simulator** using free Binance market data.

---

## Features

- **L2 Order Book** — integer-tick `std::map<Price, Qty>` with O(log N) updates
- **Binance sync algorithm** — exact snapshot + diff-depth state machine per the official Binance docs
- **Replay engine** — reads normalized `depth_events.csv`, fires per-event callbacks
- **Execution simulator** — market orders walk the visible book; passive limit orders approximate queue position from L2 data
- **Two strategies** — Market Maker (inventory-skewed quoting) + Imbalance (aggressive directional)
- **Latency model** — configurable fixed market-data, decision, and order-entry delays
- **Structured CSV exports** — `book_metrics.csv`, `fills.csv`, `pnl.csv`
- **Python analytics** — pandas + matplotlib scripts for PnL, fill quality, book metrics
- **Unit tests** — Catch2, 95 assertions across 30 test cases
- **Benchmarked** — 5.2M events/sec (book only), 2.3M events/sec (with MarketMaker strategy)

---

## Quickstart

### Prerequisites

```bash
brew install cmake ninja
```

Requires: macOS with Xcode Command Line Tools, or Linux with GCC/Clang.

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
# All tests passed (95 assertions in 30 test cases)
```

### Synthetic demo

```bash
./build/lob_sim --synthetic-demo
```

### Replay with strategy

```bash
# Convert Binance data (see docs/dataset.md)
python python/analysis/convert_binance_bookdepth.py \
    --input  data/raw/BTCUSDT-bookDepth-2024-04-05.csv \
    --output data/recorded/btc-20240405.csv

# Run market maker strategy
./build/lob_sim --strategy mm \
    --replay data/recorded/btc-20240405.csv \
    --output data/processed/mm_run

# Analyze
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
./lob_sim --strategy mm        --replay <csv> [--output <dir>]
./lob_sim --strategy imbalance --replay <csv> [--output <dir>]
```

---

## Benchmark Results

Measured on Apple M-series, Release build (`-O3`), 1 million synthetic events:

| Mode                      | Throughput       |
|---------------------------|------------------|
| Book-only replay          | **5.2M events/s** |
| Replay + MarketMaker      | **2.3M events/s** |

Target from design doc: 500k/s book-only, 100k/s with strategy. Achieved 10×.

---

## Architecture

See [`docs/architecture.md`](docs/architecture.md) for a full component diagram and event flow.

```
ReplayEngine → BinanceDepthEvent → OrderBook → ExchangeSim → Strategy → Portfolio → CSV
```

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

- **L2 only** — no per-order queue visibility
- **Passive fill approximation** — `qty_ahead` decremented as visible qty at a price level shrinks
- **Market impact ignored** — our orders don't move the book (valid for small sizes)
- **Fixed latency** — no jitter modelling

---

## Dataset

See [`docs/dataset.md`](docs/dataset.md) for how to download Binance historical futures data.

Free daily bookDepth files available at:
```
https://data.binance.vision/?prefix=data/futures/um/daily/bookDepth/BTCUSDT/
```

---

## Extensions

- [ ] L3 order-level simulation (requires ITCH or Binance OMS data)
- [ ] Multi-threaded engine with lock-free book updates
- [ ] Live Binance WebSocket capture (`--live` mode)
- [ ] Additional strategies (TWAP, momentum, cross-venue arbitrage)
- [ ] More realistic latency modelling (log-normal distribution)
- [ ] Parquet output for large-scale analysis
