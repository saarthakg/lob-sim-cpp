# Participation (post hoc, not pre-registered)

Share of Nasdaq's printed session volume that the simulated naive strategy traded. The exchange model assumes our orders change nothing, which is reasonable at a few percent and is not at tens of percent. Dollar totals scale with this volume, so read per-share numbers, and read the high-participation symbols with the most caution. No pre-registered verdict depends on this file.

| latency µs | development | confirmation |
|---|---|---|
| 0 | 8.8% | 14.1% |
| 10 | 8.7% | 14.7% |
| 50 | 10.4% | 21.3% |
| 200 | 11.4% | 24.0% |
| 1,000 | 13.1% | 25.5% |
| 10,000 | 12.7% | 21.4% |

## Confirmation days by symbol

H5 pools by share volume, so it is mostly QQQ and NVDA. By symbol:

| symbol | participation at 50 µs | share of pooled 0 µs volume | net at top rebate, 0 µs (c/share) |
|---|---|---|---|
| SIRI | 21.5% | 1% | -0.055 |
| INTC | 10.6% | 12% | +0.102 |
| CSCO | 22.5% | 5% | -0.072 |
| CMCSA | 4.9% | 2% | -0.085 |
| AMD | 10.4% | 3% | +0.092 |
| MU | 10.1% | 3% | -0.506 |
| F | 3.0% | 1% | -0.062 |
| BAC | 13.9% | 3% | -0.048 |
| PFE | 6.0% | 1% | -0.043 |
| T | 4.3% | 1% | -0.110 |
| AAPL | 18.5% | 8% | -0.021 |
| MSFT | 16.3% | 5% | -0.172 |
| QQQ | 47.0% | 26% | +0.023 |
| NVDA | 25.4% | 31% | +0.054 |

Across symbols: median -0.051, range -0.506 to +0.102, 10 of 14 within ±0.10 c/share, 4 positive.

## Daily naive net at the top rebate, 50 µs (the series behind H1)

| day | $ |
|---|---|
| 2025-12-08 | -22,688 |
| 2025-12-09 | -15,834 |
| 2025-12-10 | -6,547 |
| 2025-12-11 | -10,273 |
| 2025-12-12 | -22,955 |
| 2026-05-15 | -34,747 |
| 2026-05-18 | -90,206 |
| 2026-06-12 | +13,879 |
