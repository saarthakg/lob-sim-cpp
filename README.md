# lob-sim-cpp

A C++ event-driven **L2 limit order book replay and execution simulator** built around the Binance diff-depth WebSocket protocol.

---

## Features

- **L2 Order Book** — integer-tick `std::map<Price, Qty>` with O(log N) updates
- **Binance sync algorithm** — exact snapshot + diff-depth state machine per the official Binance docs
- **Live capture** — connects to Bybit/Binance WebSocket, syncs the book, records to normalized CSV
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
# All tests passed (95 assertions in 30 test cases)
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
./lob_sim --strategy mm        --replay <csv> [--output <dir>]
./lob_sim --strategy imbalance --replay <csv> [--output <dir>]
./lob_sim --live-capture <SYMBOL> --exchange <bybit|binance> \
          --output <csv> [--duration-minutes <N>]
```

---

## Benchmark Results

Measured on Apple M-series, Release build (`-O3`), 1 million synthetic events:

| Mode                      | Throughput        |
|---------------------------|-------------------|
| Book-only replay          | **5.2M events/s** |
| Replay + MarketMaker      | **2.3M events/s** |

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

Live data is captured directly from exchange WebSocket streams using the `--live-capture` mode.
See [`docs/dataset.md`](docs/dataset.md) for details on the normalized CSV schema and conversion tools.

---

## Extensions

- [ ] L3 order-level simulation (requires ITCH or proprietary OMS feed)
- [ ] Multi-threaded engine with lock-free book updates
- [ ] Additional strategies (TWAP, momentum, spread arbitrage)
- [ ] Realistic latency modelling (log-normal distribution, co-location tiers)
- [ ] Parquet output for large-scale analysis
