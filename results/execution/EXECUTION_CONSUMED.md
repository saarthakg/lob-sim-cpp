# Execution study: passive vs aggressive, paired parents

Bybit USDT perpetuals, 5-minute parents cut into 5 slices, 5 ms each-way latency, power-2 queue model, VIP0 fees (maker 2.0 bp, taker 5.5 bp). Cost is bp against the arrival mid, fees included. Nothing was fitted. `saving` is Cross minus the algorithm on the *same parents*; its t-statistic uses one observation per day. **Robustness run:** displayed size is reduced by prints since the last book update (`--consume-prints`).

| symbol | days | parents | algo | filled passively | price shortfall bp | fees bp | total bp | saving vs Cross bp | t (days) | days better |
|---|---|---|---|---|---|---|---|---|---|---|
| BTCUSDT | 50 | 12,000 | cross | 0% | +0.01 | 5.50 | +5.51 | — | — | — |
| BTCUSDT | 50 | 12,000 | join | 78% | +0.70 | 2.77 | +3.46 | +2.05 | 57.5 | 100% |
| BTCUSDT | 50 | 12,000 | peg | 93% | +0.89 | 2.24 | +3.12 | +2.39 | 79.1 | 100% |
| BTCUSDT | 50 | 12,000 | smart | 40% | +0.15 | 4.10 | +4.25 | +1.26 | 102.9 | 100% |
| BTCUSDT | 50 | 12,000 | peg, pessimistic queue | 92% | +0.93 | 2.28 | +3.21 | +2.30 | 64.6 | 100% |
| ATOMUSDT | 50 | 12,000 | cross | 0% | +3.41 | 5.50 | +8.91 | — | — | — |
| ATOMUSDT | 50 | 12,000 | join | 53% | +2.68 | 3.63 | +6.31 | +2.60 | 31.9 | 100% |
| ATOMUSDT | 50 | 12,000 | peg | 74% | +2.42 | 2.92 | +5.34 | +3.58 | 35.7 | 100% |
| ATOMUSDT | 50 | 12,000 | smart | 33% | +2.20 | 4.36 | +6.56 | +2.35 | 66.7 | 100% |
| ATOMUSDT | 50 | 12,000 | peg, pessimistic queue | 73% | +2.48 | 2.94 | +5.42 | +3.49 | 34.0 | 100% |
| XLMUSDT | 50 | 12,000 | cross | 0% | +2.59 | 5.50 | +8.09 | — | — | — |
| XLMUSDT | 50 | 12,000 | join | 61% | +1.91 | 3.38 | +5.29 | +2.80 | 30.5 | 100% |
| XLMUSDT | 50 | 12,000 | peg | 82% | +1.75 | 2.64 | +4.39 | +3.70 | 36.8 | 100% |
| XLMUSDT | 50 | 12,000 | smart | 33% | +1.30 | 4.33 | +5.64 | +2.45 | 77.3 | 100% |
| XLMUSDT | 50 | 12,000 | peg, pessimistic queue | 81% | +1.82 | 2.66 | +4.48 | +3.61 | 34.6 | 100% |
| ADAUSDT | 50 | 12,000 | cross | 0% | +2.33 | 5.50 | +7.83 | — | — | — |
| ADAUSDT | 50 | 12,000 | join | 66% | +1.92 | 3.19 | +5.10 | +2.73 | 31.1 | 100% |
| ADAUSDT | 50 | 12,000 | peg | 87% | +1.94 | 2.46 | +4.40 | +3.43 | 41.7 | 100% |
| ADAUSDT | 50 | 12,000 | smart | 46% | +1.23 | 3.89 | +5.12 | +2.71 | 77.6 | 100% |
| ADAUSDT | 50 | 12,000 | peg, pessimistic queue | 86% | +2.02 | 2.48 | +4.50 | +3.33 | 38.4 | 100% |
