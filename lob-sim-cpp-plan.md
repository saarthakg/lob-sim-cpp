# lob-sim-cpp — Complete Build Plan

> A C++ event-driven L2 limit order book replay and execution simulator using free Binance market data.
> This document is the canonical reference for Claude Code to follow throughout the project.

---

## Project Goal

Build a correct, performant, recruiter-ready C++ limit order book simulator that:

- Reconstructs a local L2 order book from Binance snapshot + diff-depth incremental updates
- Replays historical Binance futures `bookDepth` data
- Simulates passive and aggressive order execution
- Runs two baseline strategies (market maker, imbalance)
- Exports structured CSV logs for Python analytics

---

## Constraints and Non-Goals

**Do build:**
- L2 order book (price → qty map), not full order-level queue
- Binance snapshot + diff-depth sync (official documented algorithm)
- Replay engine from recorded normalized CSVs
- Market order execution walking visible book levels
- Passive limit order approximation via queue-position tracking
- Maker/taker fees, latency knobs, portfolio PnL
- Market maker strategy + imbalance strategy
- Python analytics scripts (pandas + matplotlib)
- Unit tests (Catch2) + basic benchmark harness

**Do NOT build (skip for now):**
- Per-order FIFO queue within a price level
- Multi-threaded engine
- GUI or web frontend
- ML model training
- Fancy storage (Parquet, Arrow, databases)
- Options or multi-leg instruments

---

## Tech Stack

### C++
- Standard: **C++17** minimum (C++20 optional)
- Build: **CMake 3.20+**
- JSON: **nlohmann/json** (header-only, fetch via CMake FetchContent)
- Testing: **Catch2 v3** (fetch via CMake FetchContent)
- Formatting: **clang-format** (provided config)
- Optional: `fmt` library for string formatting

### Python
- Python 3.10+
- `pandas`, `numpy`, `matplotlib`
- Install via `pip install -r python/requirements.txt`

### Data
- **Live development**: Binance spot/futures diff-depth WebSocket + REST snapshot
- **Historical replay**: Binance public futures `bookDepth` daily files from `data.binance.vision`
- **Symbols**: Start with `BTCUSDT`, add `ETHUSDT` later

---

## Repository Structure

```
lob-sim-cpp/
├── CMakeLists.txt
├── README.md
├── .clang-format
├── .gitignore
├── docs/
│   ├── architecture.md
│   ├── dataset.md
│   ├── assumptions.md
│   └── experiments.md
├── cpp/
│   ├── include/
│   │   ├── core/
│   │   │   ├── types.hpp
│   │   │   ├── side.hpp
│   │   │   ├── level.hpp
│   │   │   ├── order_book.hpp
│   │   │   └── book_snapshot.hpp
│   │   ├── marketdata/
│   │   │   ├── binance_depth_event.hpp
│   │   │   ├── binance_snapshot.hpp
│   │   │   ├── parser.hpp
│   │   │   ├── book_sync.hpp
│   │   │   ├── recorder.hpp
│   │   │   └── replay_engine.hpp
│   │   ├── sim/
│   │   │   ├── sim_order.hpp
│   │   │   ├── execution_report.hpp
│   │   │   ├── exchange_sim.hpp
│   │   │   ├── portfolio.hpp
│   │   │   └── latency_model.hpp
│   │   ├── strategy/
│   │   │   ├── strategy.hpp
│   │   │   ├── market_maker.hpp
│   │   │   └── imbalance_strategy.hpp
│   │   └── util/
│   │       ├── timer.hpp
│   │       ├── csv_writer.hpp
│   │       └── json.hpp
│   ├── src/
│   │   ├── core/
│   │   │   └── order_book.cpp
│   │   ├── marketdata/
│   │   │   ├── parser.cpp
│   │   │   ├── book_sync.cpp
│   │   │   ├── recorder.cpp
│   │   │   └── replay_engine.cpp
│   │   ├── sim/
│   │   │   ├── exchange_sim.cpp
│   │   │   └── portfolio.cpp
│   │   └── strategy/
│   │       ├── market_maker.cpp
│   │       └── imbalance_strategy.cpp
│   ├── app/
│   │   └── main.cpp
│   ├── tests/
│   │   ├── test_order_book.cpp
│   │   ├── test_book_sync.cpp
│   │   ├── test_execution.cpp
│   │   └── test_parser.cpp
│   └── bench/
│       └── bench_replay.cpp
├── python/
│   ├── requirements.txt
│   ├── analysis/
│   │   ├── load_logs.py
│   │   ├── plot_book_metrics.py
│   │   ├── pnl_report.py
│   │   └── fill_quality.py
│   └── notebooks/
│       └── experiment.ipynb
└── data/
    ├── raw/           # downloaded Binance files
    ├── recorded/      # live-captured normalized CSVs
    └── processed/     # outputs from replay
```

---

## CMake Setup

`CMakeLists.txt` at root should:

1. Set `CMAKE_CXX_STANDARD 17`
2. Use `FetchContent` to pull `nlohmann_json` and `Catch2`
3. Define a library target `lob_core` from all `cpp/src/**/*.cpp`
4. Define executable `lob_sim` from `cpp/app/main.cpp` linked to `lob_core`
5. Define test executable `lob_tests` from `cpp/tests/*.cpp` linked to `lob_core` and `Catch2`
6. Define bench executable `lob_bench` from `cpp/bench/*.cpp` linked to `lob_core`
7. Enable `clang-format` target

Build commands:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/lob_sim --help
./build/lob_tests
./build/lob_bench
```

---

## Complete Type Definitions

### `cpp/include/core/types.hpp`

```cpp
#pragma once
#include <cstdint>

using Price     = int64_t;   // integer ticks (e.g., price * 100 for 2dp)
using Qty       = int64_t;   // integer lot units or scaled quantity
using Timestamp = int64_t;   // microseconds since epoch
using UpdateId  = uint64_t;  // Binance update sequence ID
using OrderId   = uint64_t;  // internal simulator order ID
```

**Important:** Never use `double` for prices internally. Convert Binance string prices to integer ticks at parse time using a configurable tick size (e.g., multiply by 100 for 2 decimal places, or 10 for 1). Store the tick size as a global constant or config parameter.

### `cpp/include/core/side.hpp`

```cpp
#pragma once
enum class Side { Bid, Ask };
```

### `cpp/include/core/level.hpp`

```cpp
#pragma once
#include "types.hpp"

struct Level {
    Price price;
    Qty   qty;    // qty == 0 means "delete this level" in Binance updates
};
```

### `cpp/include/core/book_snapshot.hpp`

```cpp
#pragma once
#include "types.hpp"
#include "level.hpp"
#include <vector>

struct BookSnapshot {
    UpdateId          last_update_id;
    std::vector<Level> bids;
    std::vector<Level> asks;
};
```

---

## Order Book Implementation

### `cpp/include/core/order_book.hpp`

```cpp
#pragma once
#include "types.hpp"
#include "side.hpp"
#include "level.hpp"
#include "book_snapshot.hpp"
#include "../marketdata/binance_depth_event.hpp"
#include <map>
#include <vector>
#include <optional>
#include <functional>

class OrderBook {
public:
    void clear();
    void load_snapshot(const BookSnapshot& snapshot);
    void apply_update(Side side, Price price, Qty qty);  // qty==0 → delete level

    std::optional<Level> best_bid() const;
    std::optional<Level> best_ask() const;

    double mid_price() const;     // (best_bid + best_ask) / 2.0
    double spread() const;        // best_ask - best_bid in ticks
    double imbalance(int depth_levels = 5) const;
    // imbalance = (bid_qty - ask_qty) / (bid_qty + ask_qty)
    // summed over top N levels

    Qty visible_qty_at_price(Side side, Price px) const;
    std::vector<Level> top_n_bids(int n) const;
    std::vector<Level> top_n_asks(int n) const;

    UpdateId update_id() const;
    void set_update_id(UpdateId id);

private:
    std::map<Price, Qty, std::greater<Price>> bids_;  // descending
    std::map<Price, Qty, std::less<Price>>    asks_;  // ascending
    UpdateId update_id_ = 0;
};
```

### Implementation rules for `order_book.cpp`

- `apply_update`: if `qty == 0`, erase the price level if it exists (ignore if not present). Otherwise, set `map[price] = qty`.
- `load_snapshot`: call `clear()`, then apply all bid and ask levels, then set `update_id_`.
- `imbalance`: iterate only the first `depth_levels` entries in each map. Return 0.0 if both sides are empty.
- All map operations are O(log N) which is acceptable for this project.

---

## Binance Market Data Structures

### `cpp/include/marketdata/binance_depth_event.hpp`

```cpp
#pragma once
#include "../core/types.hpp"
#include "../core/level.hpp"
#include <vector>
#include <string>

struct BinanceDepthEvent {
    Timestamp           event_time;
    std::string         symbol;
    UpdateId            first_update_id;   // "U" field in Binance JSON
    UpdateId            final_update_id;   // "u" field in Binance JSON
    std::vector<Level>  bid_updates;
    std::vector<Level>  ask_updates;
};
```

### `cpp/include/marketdata/binance_snapshot.hpp`

Same as `BookSnapshot` but with parsing helpers. Can be a typedef or a separate struct with a static `parse(nlohmann::json)` factory.

---

## Parser

### `cpp/include/marketdata/parser.hpp`

```cpp
#pragma once
#include "binance_depth_event.hpp"
#include "../core/book_snapshot.hpp"
#include <string>

class Parser {
public:
    // Parse a single Binance diff-depth WebSocket JSON message
    static BinanceDepthEvent parse_depth_event(const std::string& json_str);

    // Parse a Binance REST /api/v3/depth snapshot response
    static BookSnapshot parse_snapshot(const std::string& json_str);

    // Parse a single row from the normalized depth_events.csv format
    // Returns multiple Level updates (one row per price level change)
    static BinanceDepthEvent parse_csv_row(const std::string& line);
};
```

### Price tick conversion

In `parser.cpp`, define a constant:
```cpp
static constexpr double TICK_DIVISOR = 100.0; // or configure per symbol
```

Convert: `Price tick_price = static_cast<Price>(std::round(double_price * TICK_DIVISOR));`

---

## Binance Book Synchronization State Machine

This is the most critical correctness component. Implement it exactly per [Binance docs](https://developers.binance.com/docs/binance-spot-api-docs/web-socket-streams).

### Official Binance sync algorithm

1. Open diff-depth WebSocket stream **before** fetching the snapshot
2. **Buffer** all incoming diff-depth events (do not apply yet)
3. Fetch REST depth snapshot → get `lastUpdateId`
4. Discard all buffered events where `event.final_update_id <= snapshot.last_update_id`
5. The **first valid event** must satisfy: `event.first_update_id <= snapshot.last_update_id + 1` AND `event.final_update_id >= snapshot.last_update_id + 1`
6. Apply the snapshot to the book, set `local_update_id = snapshot.last_update_id`
7. For each subsequent event: verify `event.first_update_id == local_update_id + 1`; if not → **restart** (gap detected)
8. Apply each event and update `local_update_id = event.final_update_id`

### `cpp/include/marketdata/book_sync.hpp`

```cpp
#pragma once
#include "binance_depth_event.hpp"
#include "../core/book_snapshot.hpp"
#include "../core/order_book.hpp"
#include <vector>

enum class SyncState {
    WaitingForSnapshot,
    Initialized,
    Error       // gap detected, needs restart
};

class BookSync {
public:
    BookSync();

    // Call for every incoming WebSocket event (even before snapshot)
    void buffer_event(const BinanceDepthEvent& event);

    // Call once when the REST snapshot has been fetched
    // Returns true if initialization succeeded
    bool initialize_from_snapshot(const BookSnapshot& snapshot, OrderBook& book);

    // Call for each subsequent event after initialization
    // Returns false if a gap is detected (caller should restart)
    bool apply_event(const BinanceDepthEvent& event, OrderBook& book);

    SyncState state() const;
    void reset();

private:
    SyncState state_;
    UpdateId  local_update_id_;
    std::vector<BinanceDepthEvent> buffer_;
};
```

### Tests required for `BookSync`

Write Catch2 tests covering:
- Stale buffered event correctly discarded
- Valid first event passes the U/u check
- Gap detection triggers `SyncState::Error`
- Normal sequential events apply correctly
- `reset()` returns to `WaitingForSnapshot`

---

## Recorder

### `cpp/include/marketdata/recorder.hpp`

Writes incoming events to the normalized CSV schema for later replay.

```cpp
#pragma once
#include "binance_depth_event.hpp"
#include <fstream>
#include <string>

class Recorder {
public:
    explicit Recorder(const std::string& output_path);
    ~Recorder();

    void record_event(const BinanceDepthEvent& event);
    void flush();

private:
    std::ofstream file_;
    void write_header();
};
```

Output format (one row per price-level change, multiple rows per event):
```
ts,event_type,first_update_id,final_update_id,side,price_ticks,qty
```

---

## Replay Engine

### `cpp/include/marketdata/replay_engine.hpp`

```cpp
#pragma once
#include "binance_depth_event.hpp"
#include <string>
#include <functional>

using EventCallback = std::function<void(const BinanceDepthEvent&)>;

class ReplayEngine {
public:
    explicit ReplayEngine(const std::string& csv_path);

    // Replay all events, calling callback for each
    void replay(EventCallback callback);

    // Replay up to max_events events
    void replay_n(size_t max_events, EventCallback callback);

    size_t total_events() const;   // after a full scan or replay
};
```

The replay engine reads the normalized CSV row by row, reconstructs `BinanceDepthEvent` objects (grouping rows with the same `first_update_id`/`final_update_id`), and fires the callback in order.

---

## Simulation Layer

### `cpp/include/sim/sim_order.hpp`

```cpp
#pragma once
#include "../core/types.hpp"
#include "../core/side.hpp"

enum class OrderType   { Limit, Market };
enum class OrderStatus { New, PartiallyFilled, Filled, Cancelled, Rejected };

struct SimOrder {
    OrderId    order_id;
    Timestamp  ts;
    Side       side;
    OrderType  type;
    Price      price;    // ignored for Market orders
    Qty        qty;      // total requested quantity
    std::string strategy_name;
};
```

### `cpp/include/sim/execution_report.hpp`

```cpp
#pragma once
#include "../core/types.hpp"
#include "../core/side.hpp"
#include "sim_order.hpp"

struct ExecutionReport {
    OrderId     order_id;
    Timestamp   ts;
    Side        side;
    OrderStatus status;
    Price       fill_price;    // weighted average if multi-level
    Qty         fill_qty;
    Qty         remaining_qty;
    double      fee_paid;
    std::string strategy_name;
};
```

### `cpp/include/sim/latency_model.hpp`

```cpp
#pragma once
#include "../core/types.hpp"

struct LatencyModel {
    int64_t market_data_us  = 0;   // delay before strategy sees an event
    int64_t decision_us     = 0;   // strategy processing time
    int64_t order_entry_us  = 0;   // delay from decision to order reaching sim exchange
    // Total effective latency = market_data_us + decision_us + order_entry_us
};
```

### `cpp/include/sim/portfolio.hpp`

```cpp
#pragma once
#include "../core/types.hpp"
#include "../core/side.hpp"

class Portfolio {
public:
    void on_fill(Side side, Price px, Qty qty, double fee);

    double realized_pnl()                   const;
    double unrealized_pnl(double mark_price) const;
    Qty    position()                        const;  // positive = long, negative = short
    double cash()                            const;  // in quote currency
    double total_pnl(double mark_price)     const;

    void log_state(Timestamp ts, double mark_price) const;  // prints to stdout

private:
    Qty    position_   = 0;
    double cash_       = 0.0;
    double avg_cost_   = 0.0;   // average entry price
    double realized_   = 0.0;
};
```

### `cpp/include/sim/exchange_sim.hpp`

```cpp
#pragma once
#include "../core/order_book.hpp"
#include "sim_order.hpp"
#include "execution_report.hpp"
#include "portfolio.hpp"
#include "latency_model.hpp"
#include <vector>
#include <unordered_map>

struct PassiveOrderState {
    SimOrder order;
    Qty      qty_ahead;         // estimated queue position
    Price    level_qty_at_entry; // visible qty at this level when order was placed
};

class ExchangeSim {
public:
    explicit ExchangeSim(LatencyModel latency, double taker_fee = 0.0004, double maker_fee = 0.0002);

    // Execute a market order immediately against visible book levels
    // Walks multiple levels if needed (partial fill if book is thin)
    std::vector<ExecutionReport> submit_market_order(
        const SimOrder& order,
        const OrderBook& book,
        Portfolio& portfolio);

    // Place a passive limit order; tracked internally
    OrderId submit_limit_order(const SimOrder& order, const OrderBook& book);

    void cancel_order(OrderId order_id);

    // Call on every market event to advance passive order queue simulation
    std::vector<ExecutionReport> on_market_event(
        const BinanceDepthEvent& event,
        const OrderBook& book,
        Portfolio& portfolio);

    size_t active_order_count() const;

    OrderId next_order_id();

private:
    LatencyModel latency_;
    double taker_fee_;
    double maker_fee_;
    OrderId next_id_ = 1;
    std::unordered_map<OrderId, PassiveOrderState> passive_orders_;

    void advance_passive_orders(
        const BinanceDepthEvent& event,
        const OrderBook& book,
        Portfolio& portfolio,
        std::vector<ExecutionReport>& reports);
};
```

#### Passive order queue approximation (important — document this clearly)

This is an **L2 approximation**. We do not have per-order queue data. The logic is:

1. When a limit order is placed at price `P`, record `qty_ahead = visible_qty_at(P)` at that moment.
2. On each subsequent market event, if the displayed qty at `P` decreases by `delta`, decrement `qty_ahead -= delta`.
3. When `qty_ahead <= 0`, begin filling the order.
4. If the price level disappears from the book, cancel or keep tracking depending on strategy preference.

This logic must be clearly documented in `docs/assumptions.md` as an approximation.

---

## Strategy Interface

### `cpp/include/strategy/strategy.hpp`

```cpp
#pragma once
#include "../core/order_book.hpp"
#include "../core/types.hpp"
#include "../sim/exchange_sim.hpp"
#include "../sim/portfolio.hpp"

class Strategy {
public:
    virtual ~Strategy() = default;

    virtual void on_market_event(
        Timestamp         ts,
        const OrderBook&  book,
        ExchangeSim&      exchange,
        Portfolio&        portfolio) = 0;

    virtual std::string name() const = 0;
};
```

### `cpp/include/strategy/market_maker.hpp`

Market maker logic:

- Quote a bid and ask around midprice
- Target spread: configurable in ticks (e.g., 2 ticks)
- Quote size: configurable
- Max inventory: configurable; if exceeded, skew quotes to reduce position
- Inventory skew: shift quotes by `skew_coeff * position` ticks
- Cancel and re-quote when mid moves by more than 1 tick
- Do not quote if spread is wider than `max_spread_ticks` (market is unstable)

```cpp
class MarketMaker : public Strategy {
public:
    struct Config {
        int    target_half_spread_ticks = 1;
        Qty    quote_size               = 100;
        Qty    max_inventory            = 500;
        double skew_coefficient         = 0.5;  // ticks of skew per unit of inventory
        int    max_spread_ticks         = 20;   // don't quote if spread exceeds this
    };

    explicit MarketMaker(Config config);

    void on_market_event(Timestamp ts, const OrderBook& book,
                         ExchangeSim& exchange, Portfolio& portfolio) override;
    std::string name() const override { return "MarketMaker"; }

private:
    Config   config_;
    OrderId  bid_order_id_ = 0;
    OrderId  ask_order_id_ = 0;
    Price    last_mid_      = 0;
};
```

### `cpp/include/strategy/imbalance_strategy.hpp`

Imbalance strategy logic:

- Compute order book imbalance over top N levels
- If imbalance > threshold (e.g., 0.6), place aggressive buy
- If imbalance < -threshold, place aggressive sell
- Only trade if no open position (simple version)
- Configurable cooldown between trades (e.g., skip next 50 events after a trade)

```cpp
class ImbalanceStrategy : public Strategy {
public:
    struct Config {
        int    depth_levels       = 5;
        double imbalance_threshold = 0.6;
        Qty    order_size         = 100;
        int    cooldown_events    = 50;  // events to skip after a trade
    };

    explicit ImbalanceStrategy(Config config);

    void on_market_event(Timestamp ts, const OrderBook& book,
                         ExchangeSim& exchange, Portfolio& portfolio) override;
    std::string name() const override { return "ImbalanceStrategy"; }

private:
    Config config_;
    int    cooldown_remaining_ = 0;
};
```

---

## Utility Classes

### `cpp/include/util/csv_writer.hpp`

Simple RAII CSV file writer. Constructor takes output path and column headers. `write_row()` takes a vector of strings. Flushes on destruction.

### `cpp/include/util/timer.hpp`

High-resolution wall-clock timer using `std::chrono::steady_clock`. Methods: `start()`, `stop()`, `elapsed_us()`, `elapsed_ms()`.

### `cpp/include/util/json.hpp`

Thin wrapper or re-export of `nlohmann/json.hpp`. Keeps includes centralized.

---

## Main Application (`cpp/app/main.cpp`)

Parse command-line arguments and dispatch to modes:

```
./lob_sim --synthetic-demo
    Generate 1000 synthetic depth events, run book, print top of book.

./lob_sim --load-snapshot <path>
    Load a Binance JSON snapshot file, print best bid/ask/spread/imbalance.

./lob_sim --replay <csv_path>
    Replay a normalized depth_events.csv with no strategy. Export book_metrics.csv.

./lob_sim --strategy mm --replay <csv_path> [--output <dir>]
    Run MarketMaker strategy over replay. Export fills.csv, pnl.csv, book_metrics.csv.

./lob_sim --strategy imbalance --replay <csv_path> [--output <dir>]
    Run ImbalanceStrategy over replay. Same exports.

./lob_sim --live BTCUSDT [--record <output_csv>]
    Subscribe to Binance live diff-depth stream, maintain local book, optionally record.
    (Optional / advanced feature)
```

Use `argc/argv` parsing; no external CLI library needed for this scope.

---

## Normalized CSV Schemas

All output files go to the `--output <dir>` directory (default: `data/processed/run_<timestamp>/`).

### `depth_events.csv` (recorder output / replay input)

```
ts,event_type,first_update_id,final_update_id,side,price_ticks,qty
1712331000123456,depth,123456789,123456790,B,650000000,1200
1712331000123456,depth,123456789,123456790,A,650010000,900
```

- One row per price-level change within an event
- `side`: `B` = Bid, `A` = Ask
- `price_ticks`: integer (double price × tick multiplier)
- `qty = 0` means delete this level

### `book_metrics.csv` (per-event snapshot of top of book)

```
ts,best_bid_ticks,best_bid_qty,best_ask_ticks,best_ask_qty,spread_ticks,mid_ticks,imbalance_5
```

### `fills.csv` (per fill from ExchangeSim)

```
ts,order_id,strategy_name,side,order_type,fill_price_ticks,fill_qty,fee,status
```

### `pnl.csv` (per-event portfolio snapshot)

```
ts,position,cash,realized_pnl,unrealized_pnl,mark_mid_ticks,total_pnl
```

---

## Unit Tests (Catch2)

### `test_order_book.cpp`

- Load snapshot → verify best bid and ask
- Apply update (qty > 0) → verify level updated
- Apply update (qty == 0) → verify level deleted
- Multiple levels → verify top-N ordering
- Imbalance calculation for known inputs
- Spread and mid calculation

### `test_book_sync.cpp`

- Stale buffered events (final_update_id ≤ snapshot's last_update_id) are discarded
- Valid first event after snapshot is accepted
- Gap detection: first_update_id ≠ local_update_id + 1 → Error state
- Normal sequential application works correctly
- Reset clears all state

### `test_execution.cpp`

- Market buy walks ask levels correctly
- Market buy partial fill (insufficient book liquidity)
- Taker fee applied correctly
- Passive order placed and filled when qty_ahead exhausted
- Cancel removes order from active set

### `test_parser.cpp`

- Parse a sample Binance diff-depth JSON string → correct BinanceDepthEvent
- Parse a sample Binance snapshot JSON → correct BookSnapshot
- Price tick conversion is correct

---

## Benchmark (`cpp/bench/bench_replay.cpp`)

Goals:
- Replay 1 million depth events
- Measure wall-clock time
- Report: events/sec, p50/p95 per-event processing time (use Timer)
- Measure peak active passive orders

Target on a modern laptop: >500k events/sec for book-only replay, >100k events/sec with strategy.

Print results to stdout in a structured format that can be copy-pasted into the README.

---

## Python Analytics

### `python/requirements.txt`

```
pandas
numpy
matplotlib
```

### `python/analysis/load_logs.py`

Helper module: functions to load each CSV file into a pandas DataFrame with correct column types and timestamp parsing. Used by all other scripts.

### `python/analysis/plot_book_metrics.py`

Plots:
- Mid price over time
- Spread over time (histogram + time series)
- Order book imbalance over time

### `python/analysis/pnl_report.py`

Plots:
- Equity curve (total PnL over time)
- Position (inventory) over time
- Realized vs unrealized PnL

### `python/analysis/fill_quality.py`

Plots:
- Fill price vs mid price at fill time
- Markout: mid price change after N events post-fill (adverse selection estimate)
- Fee breakdown

All scripts accept `--run <directory>` argument to point at an output directory.

---

## Documented Assumptions (`docs/assumptions.md`)

The following must be documented explicitly in the repo:

### L2 only
We reconstruct a price → quantity map. We have no visibility into individual orders within a level. This is standard for L2 data.

### Passive order queue approximation
We do not have L3 (order-level) data. Queue position is approximated as:
- `qty_ahead = visible_qty_at_level` when the order is placed
- Decremented as displayed qty at the level decreases in future updates
- Order begins filling once `qty_ahead ≤ 0`

This is a known simplification. It tends to overestimate fill probability for passive orders.

### Market order execution
Market orders immediately walk the visible book. Partial fills occur if book liquidity is insufficient. This ignores market impact (our orders don't move the book), which is acceptable for small order sizes.

### Latency model
Fixed latency offsets (market data delay, decision delay, order entry delay) are applied. Jitter and variable latency are not modeled.

### No self-crossing
The simulator does not prevent strategies from crossing their own orders. Strategies should implement their own logic to avoid this.

### Fees
Taker fee: 0.04% (0.0004). Maker fee: 0.02% (0.0002). Configurable. Applied at fill time.

### No funding or settlement
Futures funding rates are not modeled.

---

## Dataset Instructions (`docs/dataset.md`)

### Download historical Binance futures bookDepth data

```bash
# Browse available files
# https://data.binance.vision/?prefix=data/futures/um/daily/bookDepth/BTCUSDT/

# Example: download a specific day
wget https://data.binance.vision/data/futures/um/daily/bookDepth/BTCUSDT/BTCUSDT-bookDepth-2024-04-05.zip
unzip BTCUSDT-bookDepth-2024-04-05.zip -d data/raw/
```

### Convert to normalized CSV

Write a script or C++ tool to convert the Binance bookDepth format to the normalized `depth_events.csv` format the replay engine expects.

### Live capture for development

If implementing live mode:
- Connect to `wss://stream.binance.com:9443/ws/btcusdt@depth`
- Fetch snapshot from `https://api.binance.com/api/v3/depth?symbol=BTCUSDT&limit=1000`
- Run BookSync state machine
- Record to `data/recorded/btcusdt_depth_<timestamp>.csv`

---

## README Outline

```md
# lob-sim-cpp

C++ event-driven L2 limit order book replay and execution simulator.

## Features
## Architecture
[insert diagram]
## Datasets
## Quickstart
## Benchmark Results
## Strategy Results
## Assumptions
## Extensions
## Limitations
```

Include actual numbers from the benchmark run and actual PnL plots.

---

## Build Order Recommendation for Claude Code

Implement in this order to keep the project compilable at every step:

1. `types.hpp`, `side.hpp`, `level.hpp`, `book_snapshot.hpp` — plain structs, no logic
2. `order_book.hpp` + `order_book.cpp` + `test_order_book.cpp` — core data structure
3. `binance_depth_event.hpp`, `parser.hpp` + `parser.cpp` + `test_parser.cpp` — parse real data
4. `book_sync.hpp` + `book_sync.cpp` + `test_book_sync.cpp` — synchronization state machine
5. `recorder.hpp` + `recorder.cpp` — write normalized CSVs
6. `replay_engine.hpp` + `replay_engine.cpp` — read normalized CSVs and fire callbacks
7. `CMakeLists.txt` fully wired, `main.cpp` with `--synthetic-demo` and `--replay` modes working
8. `portfolio.hpp` + `portfolio.cpp` — PnL tracking
9. `latency_model.hpp` — plain struct
10. `exchange_sim.hpp` + `exchange_sim.cpp` + `test_execution.cpp` — market + passive execution
11. `strategy.hpp` — abstract interface
12. `market_maker.hpp` + `market_maker.cpp` — first strategy
13. `imbalance_strategy.hpp` + `imbalance_strategy.cpp` — second strategy
14. `csv_writer.hpp`, `timer.hpp` — utilities
15. `bench_replay.cpp` — benchmark
16. Python analytics scripts
17. `README.md`, `docs/`, architecture diagram

---

## Interview Pitch

> I built a C++ event-driven limit order book simulator that reconstructs local L2 market depth from Binance incremental updates using the official snapshot-and-diff synchronization algorithm. The system replays historical order book data, simulates passive and aggressive execution with latency assumptions and a queue-position approximation for passive fills, and exports structured logs for Python analysis of PnL, fill quality, and adverse selection. I focused on correctness, clean event-driven architecture, and performance — benchmarking at over 500k events/second on a laptop.
