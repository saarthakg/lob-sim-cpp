# Pre-registered test: touch-joining and gated market making on Nasdaq

Committed **before** any 2025-26 ITCH day was downloaded. The git timestamp of
this file is the record. Development: 10 days, 2019-01-30 to 2021-08-13
(`DEV_GRID.md`), 14 symbols, exact-queue exchange.

## What development suggested

- A passive order joining the touch loses about as much to adverse selection as
  Nasdaq's best rebate pays: -0.033 c/share at zero latency after the $0.0030 rebate.
- Latency costs money monotonically: -0.33 c/share before rebates at 0 µs, -0.46 at 10 ms.
- Gating on touch imbalance makes each fill *more* adverse, not less.
- The best shared ladder parameters (of 24 tried) still lose money at 50 µs.

## Frozen

- Strategies: **naive** (`--strategy naive --quote-on-book`) and **ladder**
  (`--strategy ladder --quote-on-book --alpha-micro 1 --levels 3 --pull-threshold 0 --inventory-skew 0`,
  the best shared development parameters by net PnL at the top tier, 50 µs).
- Everything else as in `python/experiments/nasdaq_study.py` at this commit: 14 symbols, 100-share
  orders, 500-share limit, 09:35-15:55, flatten at the end, taker fee $0.0030, rebates none / base
  ($0.0013 Tape C, $0.0018 Tape A) / top ($0.0030), L3 exchange, latency 50 µs unless stated.
- The fee schedule is the one published on 2026-11-25, applied to every day.

## Data

2025-12-08, 09, 10, 11, 12, 2026-05-15, 2026-05-18 and 2026-06-12: every full ITCH 5.0 day of 2025-26
that was on Nasdaq's server on 2026-11-25. A symbol-day is excluded only if it is missing from the
feed, has a half-penny tick, or had a trading halt in session (the extractor reports all three).

## Hypotheses (unit of observation: one day, all symbols pooled)

| | Prediction | Supported if |
|---|---|---|
| H1 | Naive touch-joining loses money even at the top rebate | portfolio daily net_top, t < -3.0 |
| H2 | The frozen ladder makes money at the top rebate | portfolio daily net_top, t > 3.0 and over 50% of days positive |
| H3 | Imbalance gating worsens adverse selection | daily (ladder - naive) 5 s markout per share, t < -3.0 |
| H4 | Faster is better | daily naive net per share before rebates, 10 µs minus 10 ms, t > 3.0 |
| H5 | Touch-joining is near zero-profit at zero latency | pooled naive net_top at 0 µs within ±0.10 c/share |

t = 3.0 with 7 degrees of freedom is one-sided p = 0.01, i.e. Bonferroni for five tests at 0.05. H2 is
expected to fail. Every outcome is reported either way, in `CONFIRMATION.md`.
