# Architecture

## Component Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                          lob-sim-cpp                            │
│                                                                 │
│  ┌──────────────┐    ┌────────────────┐    ┌────────────────┐  │
│  │  MarketData  │    │   Core Book    │    │   Simulation   │  │
│  │              │    │                │    │                │  │
│  │  Parser      │───▶│  OrderBook     │───▶│  ExchangeSim   │  │
│  │  BookSync    │    │  (map<P,Q>)    │    │  Portfolio     │  │
│  │  ReplayEngine│    │                │    │  LatencyModel  │  │
│  │  Recorder    │    └────────────────┘    └───────┬────────┘  │
│  └──────────────┘                                  │            │
│                                                    ▼            │
│                                         ┌──────────────────┐   │
│                                         │   Strategies     │   │
│                                         │                  │   │
│                                         │  MarketMaker     │   │
│                                         │  ImbalanceStrat  │   │
│                                         │  TwapStrategy    │   │
│                                         └──────────────────┘   │
│                                                  │              │
│                                                  ▼              │
│                                         ┌──────────────────┐   │
│                                         │  Output CSVs     │   │
│                                         │  book_metrics    │   │
│                                         │  fills           │   │
│                                         │  pnl             │   │
│                                         └──────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## Event Flow

```
Binance diff-depth CSV
        │
        ▼
   ReplayEngine
   (reads CSV, groups rows into events, fires callback)
        │
        ▼
   BinanceDepthEvent
        │
   ┌────┴──────────┐
   │               │
   ▼               ▼
OrderBook      ExchangeSim.on_market_event()
(apply update)  └─ advance passive order queue
                └─ emit ExecutionReports
        │               │
        ▼               ▼
   Strategy.on_market_event()
   └─ read book state (mid, spread, imbalance)
   └─ submit_market_order() or submit_limit_order()
        │
        ▼
   Portfolio.on_fill()
   (update position, cash, PnL)
        │
        ▼
   CSV Writers
   (book_metrics, fills, pnl)
```

## Key Design Decisions

### Integer Prices
All prices are stored as `int64_t` ticks (price × TICK_MULTIPLIER). This avoids
floating-point equality bugs in map lookups and makes comparisons exact.

### std::map for Book Levels
`bids_` uses `std::map<Price, Qty, std::greater<Price>>` (descending) so `begin()`
is always the best bid. `asks_` uses ascending order. This gives O(log N) updates
and O(1) best bid/ask access, which is acceptable for L2 simulation.

### Callback-Based Replay
`ReplayEngine` uses `std::function<void(const BinanceDepthEvent&)>` callbacks,
making it easy to compose: plug in any combination of book update, strategy, and
output logging without changing the engine.

### Passive Queue Approximation
See `docs/assumptions.md` for the full explanation. The key insight is that L2 data
doesn't carry per-order identity, so queue position is estimated from changes in
aggregate displayed quantity — and only ever re-evaluated on activity at the order's
own exact price, never on unrelated book activity.

### Order-Arrival Latency
`ExchangeSim` treats `LatencyModel::total_us()` as a single delay between order
submission and the order actually being matched against the book. At the default
(zero latency) it's synchronous; with latency configured, orders queue in
`pending_market_orders_` / `pending_limit_orders_` and are resolved against a later
book state during a subsequent `on_market_event()` call. See `docs/assumptions.md`.

## File Map

```
cpp/include/core/         — types, side, level, book_snapshot, order_book (header)
cpp/include/marketdata/   — binance event structs, parser, book_sync, recorder, replay_engine
cpp/include/sim/          — sim_order, execution_report, latency_model, portfolio, exchange_sim
cpp/include/strategy/     — strategy interface, market_maker, imbalance_strategy, twap_strategy
cpp/include/util/         — csv_writer, timer, json (nlohmann re-export)
cpp/src/                  — implementations for all of the above
cpp/app/main.cpp          — CLI dispatcher
cpp/tests/                — Catch2 unit tests (order book, book sync, execution, parser, strategy, portfolio)
cpp/bench/                — 1M-event benchmark
python/analysis/          — pandas/matplotlib analytics scripts, incl. performance_report.py (Sharpe/drawdown/win-rate)
data/                     — raw, recorded, processed subdirs
```
