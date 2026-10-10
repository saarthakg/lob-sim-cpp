# Same market, two exchange models: exact L3 queue vs L2 queue model

Bitstamp order-level capture (150 min each pair), maker fee 0. L3 = our orders in the real FIFO; L2 = queue position estimated from depth changes and prints (power-2 model).

| pair | strategy | fills L3 | fills L2 | L2/L3 fills | volume L3 | volume L2 | edge at fill bp L3 / L2 | 5s markout bp L3 / L2 | gross bp L3 / L2 | gross $ L3 / L2 |
|---|---|---|---|---|---|---|---|---|---|---|
| btcusd | naive (join touch) | 523 | 502 | 0.96 | 0.2936 | 0.3351 | +0.00 / -0.13 | -0.63 / -0.81 | -0.83 / -0.97 | -2.02 / -2.68 |
| btcusd | ladder (3 levels) | 606 | 687 | 1.13 | 0.3726 | 0.5359 | -0.03 / -0.21 | -0.79 / -0.92 | -1.04 / -1.15 | -3.21 / -5.07 |
| btcusd | glft | 141 | 134 | 0.95 | 0.109 | 0.1057 | -0.15 / -0.14 | -0.50 / -0.53 | -0.75 / -0.73 | -0.67 / -0.64 |
| ethusd | naive (join touch) | 111 | 125 | 1.13 | 1.991 | 2.455 | +0.13 / -0.07 | -0.80 / -1.24 | -0.68 / -1.13 | -0.34 / -0.69 |
| ethusd | ladder (3 levels) | 136 | 173 | 1.27 | 2.542 | 3.4 | +0.21 / -0.00 | -1.21 / -1.54 | -0.72 / -1.46 | -0.46 / -1.24 |
| ethusd | glft | 197 | 168 | 0.85 | 3.892 | 3.321 | -0.21 / -0.22 | -0.51 / -0.38 | -0.67 / -0.49 | -0.65 / -0.41 |
| xrpusd | naive (join touch) | 156 | 224 | 1.44 | 6751 | 9829 | +0.08 / -0.27 | -1.07 / -1.62 | -4.15 / -2.66 | -3.93 / -3.67 |
| xrpusd | ladder (3 levels) | 187 | 276 | 1.48 | 8249 | 1.253e+04 | +0.13 / -0.35 | -1.22 / -1.96 | -3.51 / -2.73 | -4.07 / -4.81 |
| xrpusd | glft | 406 | 408 | 1.00 | 1.996e+04 | 2.006e+04 | -0.57 / -0.57 | -0.69 / -0.69 | -0.67 / -0.67 | -1.87 / -1.88 |
