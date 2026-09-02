# Assumptions and Simplifications

This document explicitly lists the modelling assumptions made in `lob-sim-cpp`.
Recruiters and interviewers: these are deliberate, documented trade-offs — not oversights.

---

## L2 Only (Price → Quantity Map)

We reconstruct a **price → aggregate quantity** map per side. We have no visibility
into individual orders within a level, and no trade-print (execution) stream — only
displayed-depth changes. This is standard for L2 market data and is what Binance
diff-depth and Bybit `orderbook.200` provide.

---

## Passive Order Queue Position Approximation

**This is the most important approximation in the simulator.**

We do not have L3 (order-level) data, so we cannot know exactly where in the queue
a passive limit order sits. The approximation:

1. When a limit order is placed at price `P`, record `qty_ahead = visible_qty_at(P)`.
   This is the total quantity at that level when our order was placed. If `P` currently
   has no displayed depth at all (a fresh price — common when quoting inside the spread),
   `qty_ahead` starts at `0`.
2. **A passive order is only re-evaluated on activity at its own exact price `P`.**
   If a market event doesn't report a change at `P`, the order is left untouched —
   it does not fill, and it does not get cancelled, no matter what else happens
   elsewhere in the book.
3. When an event *does* report a change at `P`: if the new displayed qty is `0`, the
   level is gone and the order is cancelled (we assume it was pulled or fully consumed
   by others ahead of us with no fill evidence). Otherwise, if the new qty is *lower*
   than our tracked `qty_ahead`, we snap `qty_ahead` down to it.
4. Once `qty_ahead <= 0`, the order fills in full at its limit price.

> **Fixed bug (previously fired on unrelated events):** an earlier version of this
> check ran unconditionally for *every* resting order on *every* market event, not
> just events that touched that order's own price. Since a fresh price starts with
> `qty_ahead == 0`, this meant a freshly-quoted order filled on the very next
> *unrelated* tick anywhere in the book — in practice, MarketMaker's bid and ask filled
> simultaneously on almost every quote cycle, a risk-free spread capture that never
> actually held inventory. Gating step 2 above on genuine activity at the order's own
> price fixed this; see the top-level README's Key Findings for the before/after on
> real captured data.

**Known bias, still present:** we assume our order is at the *front* of any reduction
in visible qty at its own price. In reality, cancellations at the same level (not just
fills) also reduce visible qty without us actually getting filled, and existing queue
members ahead of us filling would make room for us before the *displayed* qty at the
level necessarily changes at all. Whether this nets out to an over- or under-estimate
of true fill rate depends on the mix of cancellation vs. trade-through activity at a
given price and isn't something an L2-only feed can disambiguate — treat PnL and fill
counts from the passive fill model as directionally informative, not a precise forecast
of live performance. This is a standard simplification in L2-only simulators; L3 data
(ITCH, proprietary feeds) or a trade-print stream would be required to do better.

---

## Inside-Spread Quoting

A strategy computing prices relative to the book's mid (rather than relative to the
real best bid/ask) breaks down when the prevailing spread is wider than the strategy's
intended aggressiveness: e.g. a 10-tick spread with a 1-tick "distance from mid" target
quotes 4 ticks *inside* best_bid/best_ask — a synthetic price with no resting depth and
no realistic chance any real order ever trades through it under the L2 approximation
above (see "Fixed bug" note). `MarketMaker` clamps price improvement over the real touch
to `target_half_spread_ticks`, so it never quotes further inside the spread than that,
regardless of how wide the spread happens to be.

Separately, a venue's real minimum price increment can be coarser than the raw data's
tick precision — this project's Bybit BTCUSDT capture only ever shows resting depth on
multiples of 10 raw ticks ($0.10), never on odd ticks, even though prices are stored at
$0.01 precision (`TICK_MULTIPLIER = 100`). A quote that isn't a multiple of the real
tick size can never coincide with genuine market activity. `MarketMaker::Config::quote_tick_size`
snaps the final quoted price to the real grid (rounding the bid down, the ask up, so
neither becomes more aggressive than intended); the CLI sets it to 10 for this project's
captured data. A production system would read the real tick size from the venue's
exchange-info API rather than hardcoding it per dataset.

---

## Market Order Execution

Market orders immediately walk the full visible book depth on the contra side, in price
order, until filled or the book is exhausted. Partial fills occur if book liquidity is
insufficient.

**Ignored effects:**
- **Market impact:** Our orders don't move the book. Acceptable for small sizes.
- **Slippage from latency:** see "Latency Model" below — latency now genuinely delays
  when an order is matched, which can itself produce slippage as the book moves in the
  interim, but there's no additional random jitter on top of that.

---

## Latency Model

`--latency-us <N>` sets `market_data_us = decision_us = order_entry_us = N`, and
`ExchangeSim` treats their sum as a single **order-arrival delay**: an order submitted
at decision time `T` is not matched against the book until `T + total_us()`, using the
book as it exists *at arrival*, not at decision time. Concretely:

- A market order submitted with nonzero total latency is queued and executed later
  (during a subsequent market event whose timestamp has reached the arrival time),
  against whatever the book looks like then — which may have moved since the decision
  was made, producing realistic latency-driven slippage.
- A passive limit order's `qty_ahead` is captured from the book *at arrival*, not at
  submission — under latency, you're queuing behind whatever was ahead of you by the
  time your order actually reached the exchange, not what was ahead of you when you
  decided to send it.
- At the default `--latency-us 0`, orders are matched synchronously and immediately,
  exactly as before this was implemented.

No jitter or variable latency is modelled — every order submitted at a given moment
gets exactly the same delay. Real latency has high variance and tail events that matter
significantly for HFT; a log-normal jitter distribution and co-location tiers remain a
documented open extension (see the top-level README).

---

## Self-Crossing Prevention

`ExchangeSim::submit_limit_order` rejects (returns order ID `0`) a new limit order that
would cross one of the *same strategy's own* resting or in-flight orders on the opposite
side — e.g. a new bid at or above an already-resting ask from the same `strategy_name`.
This only guards against a strategy crossing itself; it does not model the exchange
preventing wash trades between two different market participants.

---

## Pre-Trade Risk Limits

`ExchangeSim` accepts an optional `max_abs_position` (0 = disabled). Both market and
limit order submission are checked against the position the order *could* result in if
fully filled; an order that would push `abs(position)` past the limit is rejected before
it ever reaches the book. This is a pre-trade check only — a limit order accepted while
within the limit is not re-checked at fill time.

---

## Fees

- Taker fee: **0.04%** (0.0004) — applied to market orders
- Maker fee: **0.02%** (0.0002) — applied to passive limit fills
- Configurable via `ExchangeSim` constructor
- Fees are deducted from the portfolio's cash balance, and separately accumulated in
  `Portfolio::total_fees_paid()`

No rebates, no tiered fees, no funding rates.

**Gross vs. net PnL:** `Portfolio::realized_pnl()` and `total_pnl()` are trading PnL
only — fees never touch them, only `cash_`. Use `Portfolio::net_pnl()` (= `total_pnl()`
`- total_fees_paid()`) for "did this strategy actually make money"; both are written to
`pnl.csv` (`total_pnl` and `net_pnl` columns) so downstream analysis doesn't have to
recompute it by hand.

---

## No Funding or Settlement

Binance/Bybit futures funding rates are not modelled. PnL is measured purely from
fill prices and fees.

---

## Integer Tick Arithmetic

All prices are stored as integer ticks (`int64_t`) scaled by `TICK_MULTIPLIER = 100`.
So $65,000.12 → 6,500,012 ticks. This eliminates floating-point rounding errors
in price comparisons and map lookups. Note this is the *raw* data precision, not
necessarily the venue's real tradeable tick size — see "Inside-Spread Quoting" above.
