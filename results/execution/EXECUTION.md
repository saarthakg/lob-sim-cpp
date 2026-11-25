# Execution study: passive vs aggressive, paired parents

Bybit USDT perpetuals, 5-minute parents cut into 5 slices, 5 ms each-way latency, power-2 queue model, VIP0 fees (maker 2.0 bp, taker 5.5 bp). Cost is bp against the arrival mid, fees included. Nothing was fitted. `saving` is Cross minus the algorithm on the *same parents*; its t-statistic uses one observation per day.

| symbol | days | parents | algo | filled passively | price shortfall bp | fees bp | total bp | saving vs Cross bp | t (days) | days better |
|---|---|---|---|---|---|---|---|---|---|---|
| BTCUSDT | 50 | 12,000 | cross | 0% | +0.01 | 5.50 | +5.51 | — | — | — |
| BTCUSDT | 50 | 12,000 | join | 78% | +0.70 | 2.77 | +3.46 | +2.04 | 57.9 | 100% |
| BTCUSDT | 50 | 12,000 | peg | 93% | +0.89 | 2.24 | +3.12 | +2.38 | 79.8 | 100% |
| BTCUSDT | 50 | 12,000 | smart | 40% | +0.14 | 4.10 | +4.24 | +1.27 | 100.6 | 100% |
| BTCUSDT | 50 | 12,000 | peg, pessimistic queue | 92% | +0.93 | 2.28 | +3.21 | +2.30 | 65.0 | 100% |
| ATOMUSDT | 50 | 12,000 | cross | 0% | +3.40 | 5.50 | +8.90 | — | — | — |
| ATOMUSDT | 50 | 12,000 | join | 53% | +2.68 | 3.63 | +6.31 | +2.59 | 32.0 | 100% |
| ATOMUSDT | 50 | 12,000 | peg | 74% | +2.42 | 2.92 | +5.34 | +3.56 | 35.8 | 100% |
| ATOMUSDT | 50 | 12,000 | smart | 33% | +2.17 | 4.36 | +6.53 | +2.37 | 67.3 | 100% |
| ATOMUSDT | 50 | 12,000 | peg, pessimistic queue | 73% | +2.48 | 2.94 | +5.42 | +3.48 | 34.1 | 100% |
| XLMUSDT | 50 | 12,000 | cross | 0% | +2.58 | 5.50 | +8.08 | — | — | — |
| XLMUSDT | 50 | 12,000 | join | 61% | +1.91 | 3.38 | +5.29 | +2.79 | 30.6 | 100% |
| XLMUSDT | 50 | 12,000 | peg | 82% | +1.75 | 2.64 | +4.39 | +3.69 | 36.9 | 100% |
| XLMUSDT | 50 | 12,000 | smart | 33% | +1.27 | 4.33 | +5.61 | +2.47 | 76.1 | 100% |
| XLMUSDT | 50 | 12,000 | peg, pessimistic queue | 81% | +1.82 | 2.66 | +4.48 | +3.60 | 34.7 | 100% |
| ADAUSDT | 50 | 12,000 | cross | 0% | +2.32 | 5.50 | +7.82 | — | — | — |
| ADAUSDT | 50 | 12,000 | join | 66% | +1.91 | 3.19 | +5.10 | +2.72 | 31.1 | 100% |
| ADAUSDT | 50 | 12,000 | peg | 87% | +1.94 | 2.46 | +4.40 | +3.42 | 41.6 | 100% |
| ADAUSDT | 50 | 12,000 | smart | 46% | +1.16 | 3.89 | +5.05 | +2.77 | 77.4 | 100% |
| ADAUSDT | 50 | 12,000 | peg, pessimistic queue | 86% | +2.02 | 2.48 | +4.50 | +3.32 | 38.4 | 100% |
