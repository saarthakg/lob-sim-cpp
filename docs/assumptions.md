# Modelling assumptions

Every backtest rests on a fill model. This page states exactly what ours
assumes, what it can't know, and which way each simplification biases the
results. The [research notes](research_notes.md) give the sources.

## Data: L2 depth + trade prints, exchange-timestamped

- **Book.** Bybit `orderbook.200`, 200 levels per side. The first message is a
  snapshot and the rest are 100 ms *conflated* deltas: each delta gives the
  absolute new size of every level that changed. Any later `snapshot` message
  replaces the book.
- **Trades.** Bybit's public trade file: every print, the aggressor side, and
  timestamps at 0.1 ms resolution.
- **Clock.** Book messages are stamped with `cts`, the matching-engine time,
  not `ts`, the push time (median 32 ms later). Trades use the same engine
  clock. A trade stamped at or before a book update's `cts` is replayed
  *before* that update.
  - We checked this ordering against the data: 88% of prints occur at the
    previous book's touch, and the next update shows that level shrinking 95%
    of the time.
- **RPI trades are dropped.** Retail-Price-Improvement prints match hidden
  retail-only orders that are never in the public book, so they don't consume
  the visible queue we model.
- **Tick and lot are detected, not assumed.** They are the GCD of the price
  grid and of the quantity grid in the first snapshot (BTCUSDT: $0.10 / 0.001).
  One `Price` unit is always one real tick.

What L2 cannot tell you: who cancelled where in the queue, and anything that
happened *inside* a 100 ms window apart from the trades. The fill model below
is built around those two gaps.

## Our orders are an overlay; there is no market impact

Our orders never enter the replayed book. A taker order fills against the
displayed levels, but those levels are not removed for later events. Resting
orders do not change what other participants did.

This is valid for sizes small relative to the touch. The study uses roughly
$200–$900 per order.

## Passive fills: queue position from trades and depth changes

When a resting order arrives, it joins the **back** of the displayed queue at
its price: `queue_ahead = displayed size`. A fresh price level means
`queue_ahead = 0`.

**Fill evidence**, strongest first:

| Event | Effect on our resting order |
|---|---|
| Trade print strictly **through** our price (a sell below our bid) | The aggressor would have hit us first. We fill `min(leaves, print size)`. |
| Trade print **at** our price | Consumes `queue_ahead`. Any excess fills us, so partial fills are allowed. |
| Opposite best quote moves **through** our price (`fill_on_cross`) | Whoever now offers at or below our bid would have traded with us. We fill the full remainder at our price. |

**Cancellations.** A depth decrease at our level that isn't explained by
trades since the last update is treated as cancellations. They are
apportioned between ahead of us and behind us by a queue model, following
[hftbacktest](https://hftbacktest.readthedocs.io/en/latest/order_fill.html):

- `power` (default, n = 2): P(cancel came from behind us) = back^n / (back^n + front^n)
- `log`: the same with ln(1 + x)
- `riskaverse`: every cancel came from behind us, so we never advance on cancels (pessimistic)

Boundary behaviour:
- At the head of the queue, every cancel is behind us. At the tail, every cancel is ahead.
- If the level empties completely, our order **stays** with `queue_ahead = 0`. It is *not* cancelled.

> The original simulator got this backwards in three ways:
> 1. It cancelled our order whenever the level emptied.
> 2. It only filled orders at fresh price levels when someone *else* added size there.
> 3. It never used trade prints at all.
>
> An order that joined an existing level could never fill.

**Bracketing.** `--no-cross-fill` drops the book-cross rule, so only trade
prints fill us. That is the optimistic bound: it assumes we always escape a
level that collapses through cancellations. The study reports both bounds.

## Latency: three channels, deterministic plus optional jitter

| Channel | Effect |
|---|---|
| `feed_us` | The strategy sees each event late. Its local book lags the exchange. |
| `entry_us` | New orders *and cancels* reach the matching engine late. A cancel that arrives after a fill is rejected (`CancelRejected`), so stale quotes get picked off. |
| `response_us` | Fills and acks reach the strategy late, so its position view lags. |

- `--jitter σ` multiplies entry and response delays by a mean-1 lognormal,
  keeping per-channel FIFO order.
- Even at zero configured latency the strategy sees the *book* only every
  100 ms, because that is the data's granularity.

## Fees

- Defaults are Bybit VIP0 USDT perpetuals: maker 2.0 bp, taker 5.5 bp.
- `--maker-fee-bps` may be negative, which models a rebate.
- The study runs three maker-fee tiers:
  - +2.0 bp (VIP0)
  - 0 bp (top VIP tiers)
  - −0.5 bp, the market-maker-programme rebate that the hftbacktest tutorials assume
- Exact programme rebates are negotiated and not public, so treat the −0.5 bp
  tier as a scenario, not a quote.
- Funding payments are not modelled. Positions are held for seconds to
  minutes, and funding settles every 8 h.

## PnL accounting

- **Ground truth.** `Portfolio` runs at exchange time on the true fill stream.
  - `gross_pnl` = realized + unrealized, marked to mid
  - `net_pnl` = gross − fees
  - `net_pnl_flat` additionally charges the cost of closing the final position
    (half spread plus taker fee)
- **Attribution in `summary.json`.** Prices are quote currency; signs are +1
  for buys.
  - `edge` = Σ side·(mid_at_fill − price)·qty, the spread captured at the
    instant of the fill.
  - `markout[h]` is the same quantity measured against the mid h later.
  - `markout[h] − edge` is the adverse selection.
- **Break-even maker fee.** For a passive strategy, `net = gross − fee ×
  notional`, so gross PnL in bp of notional is the maker fee at which the
  strategy breaks even, holding its fill set fixed.

## What is still not modelled

- **Hidden and iceberg liquidity, and self-trade prevention against other participants.**
- **Our own impact on other participants.** For example, our resting size
  deterring someone else's order.
- **Sub-100 ms book dynamics.** A real market maker on Bybit's 10–20 ms feeds
  could pull quotes faster than any strategy here can. This is the main reason
  the pessimistic bound is pessimistic.
- **Exchange rate limits and order-to-trade ratio penalties.** The study's
  strategies send roughly 10–60k messages a day.
