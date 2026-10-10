# Taker / execution strategies (test days 2026-10-02..05)

## ImbalanceStrategy (cross the spread on touch imbalance, exit after 5 s)

| symbol | threshold | trades | gross bp | net bp (5.5 bp taker) | net $ total |
|---|---|---|---|---|---|
| ATOMUSDT | 0.6 | 115,682 | -2.85 | -8.35 | -14,225 |
| ATOMUSDT | 0.8 | 92,823 | -2.96 | -8.46 | -10,871 |
| BTCUSDT | 0.6 | 116,545 | +0.24 | -5.26 | -46,324 |
| BTCUSDT | 0.8 | 95,562 | +0.28 | -5.22 | -34,776 |

## TWAP: buy 1 BTC in 20 slices over 19 min, BTCUSDT

Cost vs arrival mid (positive = paid more). Fees: maker 2 bp, taker 5.5 bp.

| mode | price shortfall bp | fee bp | total cost bp | share filled passively |
|---|---|---|---|---|
| aggr | -2.49 | 5.50 | +3.01 | 0% |
| passive | -1.72 | 2.96 | +1.24 | 73% |

Per day:

| day | mode | shortfall bp | fee bp | total bp |
|---|---|---|---|---|
| 2026-10-02 | aggr | -4.26 | 5.50 | +1.24 |
| 2026-10-03 | aggr | -2.42 | 5.50 | +3.08 |
| 2026-10-04 | aggr | +7.16 | 5.50 | +12.66 |
| 2026-10-05 | aggr | -10.44 | 5.50 | -4.94 |
| 2026-10-02 | passive | -2.98 | 3.05 | +0.07 |
| 2026-10-03 | passive | -1.44 | 2.88 | +1.43 |
| 2026-10-04 | passive | +7.70 | 3.40 | +11.10 |
| 2026-10-05 | passive | -10.16 | 2.53 | -7.63 |
