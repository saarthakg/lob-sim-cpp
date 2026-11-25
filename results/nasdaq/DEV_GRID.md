# Nasdaq exact-queue study: development days

10 days (2019-01-30 to 2021-08-13), 14 symbols, 100-share orders, 500-share limit, 50 µs each-way latency, session 09:35-15:55, flattened at the end. In-sample: this stage selects parameters and describes effects.

## Per symbol: touch-joining control vs the best gated ladder

cents per maker share; `edge` = spread captured at the fill, `5 s` = markout.

| symbol | naive edge | naive 5 s | naive net (no rebate) | best ladder | shares/day | edge | 5 s | net no rebate | net base | net top | $/day top | t (days) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SIRI | +0.375 | -0.222 | -0.314 | `levels 3 pull-threshold 0.3 inventory-skew 0.05` | 5,542 | +0.452 | -0.259 | -0.241 | -0.111 | +0.059 | +3 | +0.88 |
| INTC | +0.214 | -0.337 | -0.347 | `levels 3 pull-threshold 0.1 inventory-skew 0` | 25,937 | +0.304 | -0.390 | -0.363 | -0.233 | -0.063 | -16 | -0.37 |
| CSCO | +0.231 | -0.307 | -0.294 | `levels 1 pull-threshold 0 inventory-skew 0.05` | 8,202 | +0.249 | -0.533 | -0.535 | -0.405 | -0.235 | -19 | -1.33 |
| CMCSA | +0.260 | -0.311 | -0.324 | `levels 3 pull-threshold 0 inventory-skew 0.05` | 12,570 | +0.315 | -0.408 | -0.481 | -0.351 | -0.181 | -23 | -1.72 |
| AMD | +0.199 | -0.359 | -0.359 | `levels 1 pull-threshold 0 inventory-skew 0.02` | 67,289 | +0.153 | -0.621 | -0.506 | -0.376 | -0.206 | -138 | -1.39 |
| MU | +0.125 | -0.366 | -0.388 | `levels 3 pull-threshold 0 inventory-skew 0.05` | 45,573 | +0.182 | -0.560 | -0.585 | -0.455 | -0.285 | -130 | -2.12 |
| F | +0.394 | -0.345 | -0.261 | `levels 1 pull-threshold 0 inventory-skew 0.05` | 496 | +0.403 | -0.487 | -0.614 | -0.434 | -0.314 | -2 | -0.61 |
| BAC | +0.325 | -0.330 | -0.344 | `levels 1 pull-threshold 0.1 inventory-skew 0` | 5,411 | +0.343 | -0.349 | +0.339 | +0.519 | +0.639 | +35 | +0.76 |
| PFE | +0.331 | -0.343 | -0.313 | `levels 3 pull-threshold 0 inventory-skew 0` | 13,294 | +0.351 | -0.423 | +0.021 | +0.201 | +0.321 | +43 | +0.93 |
| T | +0.344 | -0.306 | -0.389 | `levels 1 pull-threshold 0.1 inventory-skew 0` | 8,593 | +0.371 | -0.404 | -0.226 | -0.046 | +0.074 | +6 | +0.19 |
| AAPL | +0.287 | -0.522 | -0.523 | `levels 3 pull-threshold 0 inventory-skew 0.02` | 125,178 | +0.189 | -0.681 | -0.237 | -0.107 | +0.063 | +79 | +0.58 |
| MSFT | +0.126 | -0.407 | -0.430 | `levels 3 pull-threshold 0 inventory-skew 0` | 85,280 | +0.197 | -0.459 | -0.470 | -0.340 | -0.170 | -145 | -1.67 |
| QQQ | -0.106 | -0.283 | -0.280 | `levels 1 pull-threshold 0.3 inventory-skew 0` | 139,063 | +0.105 | -0.291 | -0.259 | -0.129 | +0.041 | +57 | +0.55 |
| NVDA | +1.656 | -0.763 | -0.728 | `levels 3 pull-threshold 0.3 inventory-skew 0` | 45,948 | +1.236 | -0.645 | +0.562 | +0.692 | +0.862 | +396 | +1.17 |

## One shared parameter set across all symbols

| params | $/day no rebate | t | $/day base | t | $/day top | t | shares/day |
|---|---|---|---|---|---|---|---|
| `ladder alpha-micro 1 levels 3 pull-threshold 0 inventory-skew 0` | -1,843 | -5.30 | -1,277 | -3.84 | -562 | -1.74 | 427,250 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0 inventory-skew 0.02` | -1,999 | -6.20 | -1,381 | -4.67 | -602 | -2.22 | 465,754 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0 inventory-skew 0.05` | -2,227 | -6.24 | -1,557 | -4.73 | -712 | -2.34 | 504,879 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0 inventory-skew 0` | -1,880 | -4.55 | -1,403 | -3.58 | -803 | -2.16 | 359,207 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0 inventory-skew 0.02` | -1,985 | -4.01 | -1,462 | -3.11 | -804 | -1.82 | 393,473 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.1 inventory-skew 0` | -2,711 | -7.21 | -1,870 | -5.43 | -812 | -2.53 | 633,236 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0 inventory-skew 0.05` | -2,211 | -4.10 | -1,648 | -3.21 | -939 | -1.94 | 423,975 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.1 inventory-skew 0.02` | -3,284 | -7.22 | -2,311 | -5.88 | -1,086 | -3.36 | 732,803 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.1 inventory-skew 0` | -2,843 | -4.61 | -2,090 | -3.54 | -1,145 | -2.02 | 565,990 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.1 inventory-skew 0.05` | -3,577 | -7.47 | -2,519 | -5.99 | -1,188 | -3.29 | 796,300 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.2 inventory-skew 0` | -4,243 | -8.04 | -2,905 | -6.43 | -1,227 | -3.29 | 1,005,381 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.1 inventory-skew 0.02` | -3,348 | -4.80 | -2,469 | -3.77 | -1,365 | -2.23 | 660,862 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.1 inventory-skew 0.05` | -3,735 | -5.55 | -2,784 | -4.48 | -1,589 | -2.79 | 715,313 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.2 inventory-skew 0` | -4,409 | -5.95 | -3,159 | -4.59 | -1,594 | -2.50 | 938,344 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.2 inventory-skew 0.02` | -5,118 | -7.78 | -3,571 | -6.34 | -1,633 | -3.56 | 1,161,720 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.3 inventory-skew 0` | -6,620 | -8.74 | -4,454 | -7.06 | -1,751 | -3.48 | 1,623,186 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.2 inventory-skew 0.05` | -5,660 | -7.60 | -3,980 | -6.18 | -1,875 | -3.51 | 1,261,766 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.2 inventory-skew 0.02` | -5,210 | -6.21 | -3,755 | -4.92 | -1,936 | -2.82 | 1,091,342 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.2 inventory-skew 0.05` | -5,693 | -6.41 | -4,117 | -5.16 | -2,146 | -3.06 | 1,182,612 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.3 inventory-skew 0.02` | -7,933 | -8.63 | -5,436 | -7.14 | -2,323 | -3.96 | 1,870,199 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.3 inventory-skew 0` | -7,009 | -7.59 | -4,929 | -6.01 | -2,337 | -3.22 | 1,557,197 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.3 inventory-skew 0.05` | -8,504 | -8.73 | -5,831 | -7.26 | -2,498 | -4.06 | 2,001,770 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.3 inventory-skew 0.02` | -8,140 | -7.64 | -5,734 | -6.17 | -2,739 | -3.46 | 1,800,232 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.3 inventory-skew 0.05` | -8,691 | -8.02 | -6,117 | -6.55 | -2,912 | -3.73 | 1,926,266 |
| `naive quote-on-book` | -20,592 | -9.34 | -13,494 | -7.84 | -4,763 | -3.74 | 5,276,510 |

## Latency

All symbols pooled, cents per maker share, each-way latency (feed, entry and response).

| latency µs | naive: shares/day | edge | 5 s markout | net no rebate | net top | ladder: shares/day | edge | 5 s markout | net no rebate | net top |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 4,477,493 | +0.594 | -0.326 | -0.333 | -0.033 | 206,976 | +0.589 | -0.375 | -0.015 | +0.285 |
| 10 | 4,402,332 | +0.538 | -0.351 | -0.360 | -0.060 | 307,074 | +0.667 | -0.473 | -0.222 | +0.078 |
| 50 | 5,276,510 | +0.210 | -0.385 | -0.390 | -0.090 | 427,250 | +0.349 | -0.589 | -0.431 | -0.131 |
| 200 | 5,816,155 | +0.103 | -0.410 | -0.410 | -0.110 | 594,716 | +0.203 | -0.648 | -0.529 | -0.229 |
| 1,000 | 6,647,708 | -0.017 | -0.430 | -0.430 | -0.130 | 1,151,467 | -0.013 | -0.589 | -0.545 | -0.245 |
| 10,000 | 6,454,062 | -0.058 | -0.451 | -0.459 | -0.159 | 1,715,965 | -0.085 | -0.587 | -0.537 | -0.237 |
