# Assumptions and Simplifications

This document explicitly lists the modelling assumptions made in `lob-sim-cpp`.
Recruiters and interviewers: these are deliberate, documented trade-offs — not oversights.

---

## L2 Only (Price → Quantity Map)

We reconstruct a **price → aggregate quantity** map per side. We have no visibility
into individual orders within a level. This is standard for L2 market data and is
what Binance diff-depth provides.

---

## Passive Order Queue Position Approximation

**This is the most important approximation in the simulator.**

We do not have L3 (order-level) data, so we cannot know exactly where in the queue
a passive limit order sits. The approximation:

1. When a limit order is placed at price `P`, record `qty_ahead = visible_qty_at(P)`.
   This is the total quantity at that level when our order was placed.
2. On each subsequent market event, if the displayed qty at `P` decreases (because
   trades executed or cancellations occurred at that level), we snap our `qty_ahead`
   to the new lower visible qty.
3. When `qty_ahead ≤ 0`, the order begins filling.
4. If the price level disappears (qty goes to 0 in a diff-depth event), the order
   is cancelled (the level has been exhausted or pulled).

**Known bias:** This overestimates fill probability for passive orders because:
- We assume our order is at the *front* of any reduction in visible qty.
- In reality, cancellations at the same level would also reduce visible qty without
  us getting a fill.

This is a standard simplification in L2-only simulators. L3 data (ITCH, proprietary
feeds) would be required for accurate queue simulation.

---

## Market Order Execution

Market orders immediately walk the visible book levels in order. Partial fills occur
if book liquidity is insufficient.

**Ignored effects:**
- **Market impact:** Our orders don't move the book. Acceptable for small sizes.
- **Slippage from latency:** We apply fixed latency offsets, not a distribution.

---

## Latency Model

Fixed latency offsets are applied:
- `market_data_us`: delay before strategy sees a market event
- `decision_us`: strategy computation time
- `order_entry_us`: delay from decision to order reaching the simulator

No jitter or variable latency is modelled. Real latency has high variance and
tail events that matter significantly for HFT, but are beyond the scope of this project.

---

## No Self-Crossing Prevention

The simulator does not prevent strategies from crossing their own orders. Strategies
should implement their own logic to avoid placing a bid above their own ask.
The MarketMaker strategy cancels old quotes before placing new ones, which naturally
avoids this.

---

## Fees

- Taker fee: **0.04%** (0.0004) — applied to market orders
- Maker fee: **0.02%** (0.0002) — applied to passive limit fills
- Configurable via `ExchangeSim` constructor
- Fees are deducted from the portfolio's cash balance

No rebates, no tiered fees, no funding rates.

---

## No Funding or Settlement

Binance futures funding rates are not modelled. PnL is measured purely from
fill prices and fees.

---

## Integer Tick Arithmetic

All prices are stored as integer ticks (`int64_t`) scaled by `TICK_MULTIPLIER = 100`.
So $65,000.12 → 6,500,012 ticks. This eliminates floating-point rounding errors
in price comparisons and map lookups.

---

## Passive Fill Rate is a Lower Bound

The L2 passive fill model (`qty_ahead` approximation) tends to **underestimate fill rate**
compared to a true FIFO L3 model. When visible quantity at our price level decreases,
we snap `qty_ahead` to the new visible qty — but this ignores replenishment from new orders
joining behind us. In reality, a passive order sitting at a stable level would see a higher
fill rate as existing queue members fill and the order moves forward. The PnL estimates
from the MarketMaker strategy should therefore be interpreted as a **conservative lower bound**
on what a real passive strategy would achieve with equivalent parameters.
