# Nasdaq exact-queue study: development days

3 days (2019-08-30 to 2019-12-30), 14 symbols, 100-share orders, 500-share limit, 50 µs each-way latency, session 09:35-15:55, flattened at the end. In-sample: this stage selects parameters and describes effects.

## Per symbol: touch-joining control vs the best gated ladder

cents per maker share; `edge` = spread captured at the fill, `5 s` = markout.

| symbol | naive edge | naive 5 s | naive net (no rebate) | best ladder | shares/day | edge | 5 s | net no rebate | net base | net top | $/day top | t (days) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SIRI | +0.402 | -0.271 | -0.396 | `levels 1 pull-threshold 0.3 inventory-skew 0.05` | 2,904 | +0.444 | -0.291 | -0.371 | -0.241 | -0.071 | -2 | -0.22 |
| INTC | +0.194 | -0.293 | -0.329 | `levels 3 pull-threshold 0.1 inventory-skew 0.05` | 33,691 | +0.263 | -0.376 | -0.352 | -0.222 | -0.052 | -18 | -0.42 |
| CSCO | +0.260 | -0.293 | -0.286 | `levels 1 pull-threshold 0.2 inventory-skew 0` | 33,704 | +0.274 | -0.418 | -0.074 | +0.056 | +0.226 | +76 | +2.36 |
| CMCSA | +0.272 | -0.275 | -0.313 | `levels 1 pull-threshold 0 inventory-skew 0` | 7,433 | +0.258 | -0.718 | +0.114 | +0.244 | +0.414 | +31 | +0.44 |
| AMD | +0.190 | -0.388 | -0.388 | `levels 1 pull-threshold 0 inventory-skew 0.02` | 46,486 | +0.196 | -0.751 | -0.384 | -0.254 | -0.084 | -39 | -0.42 |
| MU | +0.105 | -0.319 | -0.315 | `levels 3 pull-threshold 0 inventory-skew 0` | 21,421 | +0.081 | -0.638 | -0.819 | -0.689 | -0.519 | -111 | -0.98 |
| F | +0.391 | -0.337 | -0.253 | `levels 1 pull-threshold 0.1 inventory-skew 0.02` | 774 | +0.431 | -0.493 | +0.660 | +0.840 | +0.960 | +7 | +1.90 |
| BAC | +0.310 | -0.318 | -0.290 | `levels 3 pull-threshold 0 inventory-skew 0.05` | 3,389 | +0.464 | -0.295 | -0.368 | -0.188 | -0.068 | -2 | -0.18 |
| PFE | +0.295 | -0.339 | -0.305 | `levels 3 pull-threshold 0.2 inventory-skew 0` | 16,834 | +0.406 | -0.437 | +0.401 | +0.581 | +0.701 | +118 | +0.92 |
| T | +0.340 | -0.332 | -0.411 | `levels 1 pull-threshold 0.1 inventory-skew 0` | 7,718 | +0.357 | -0.502 | +0.419 | +0.599 | +0.719 | +55 | +0.82 |
| AAPL | +0.427 | -0.708 | -0.603 | `levels 3 pull-threshold 0 inventory-skew 0.02` | 115,145 | +0.163 | -1.020 | +0.077 | +0.207 | +0.377 | +434 | +2.06 |
| MSFT | +0.089 | -0.338 | -0.340 | `levels 3 pull-threshold 0 inventory-skew 0` | 77,958 | +0.128 | -0.424 | -0.316 | -0.186 | -0.016 | -13 | -0.06 |
| QQQ | -0.084 | -0.243 | -0.233 | `levels 1 pull-threshold 0.3 inventory-skew 0.05` | 134,381 | +0.094 | -0.254 | -0.238 | -0.108 | +0.062 | +83 | +0.85 |
| NVDA | +1.579 | -0.685 | -0.825 | `levels 3 pull-threshold 0.3 inventory-skew 0.02` | 21,046 | +1.158 | -0.999 | -0.023 | +0.107 | +0.277 | +58 | +0.23 |

## One shared parameter set across all symbols

| params | $/day no rebate | t | $/day base | t | $/day top | t | shares/day |
|---|---|---|---|---|---|---|---|
| `ladder alpha-micro 1 levels 1 pull-threshold 0 inventory-skew 0.02` | -1,287 | -2.27 | -877 | -1.43 | -356 | -0.53 | 310,514 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0 inventory-skew 0` | -1,498 | -2.66 | -1,051 | -1.87 | -483 | -0.86 | 338,404 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0 inventory-skew 0.05` | -1,483 | -2.11 | -1,043 | -1.39 | -485 | -0.60 | 332,865 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0 inventory-skew 0.02` | -1,622 | -6.47 | -1,132 | -4.26 | -507 | -1.74 | 371,581 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0 inventory-skew 0` | -1,363 | -4.11 | -993 | -2.77 | -523 | -1.31 | 280,071 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0 inventory-skew 0.05` | -1,881 | -5.91 | -1,345 | -3.84 | -663 | -1.69 | 405,902 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.1 inventory-skew 0.02` | -2,363 | -3.10 | -1,684 | -2.07 | -823 | -0.93 | 513,408 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.1 inventory-skew 0.02` | -2,591 | -20.59 | -1,831 | -19.96 | -867 | -7.20 | 574,462 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.1 inventory-skew 0` | -2,353 | -4.62 | -1,701 | -3.14 | -876 | -1.50 | 492,367 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.1 inventory-skew 0.05` | -2,551 | -2.96 | -1,819 | -2.01 | -893 | -0.92 | 552,665 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.1 inventory-skew 0` | -2,207 | -3.08 | -1,630 | -2.08 | -901 | -1.04 | 435,280 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.2 inventory-skew 0` | -3,326 | -15.97 | -2,294 | -12.29 | -992 | -4.58 | 778,043 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.2 inventory-skew 0.02` | -3,575 | -5.55 | -2,457 | -3.49 | -1,048 | -1.30 | 842,226 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.2 inventory-skew 0.05` | -3,787 | -6.18 | -2,582 | -4.01 | -1,063 | -1.47 | 908,161 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.2 inventory-skew 0` | -3,230 | -3.24 | -2,272 | -2.15 | -1,065 | -0.93 | 721,399 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.1 inventory-skew 0.05` | -3,008 | -23.12 | -2,184 | -30.82 | -1,140 | -10.82 | 622,761 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.2 inventory-skew 0.02` | -4,057 | -11.11 | -2,860 | -11.03 | -1,350 | -9.16 | 902,476 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.2 inventory-skew 0.05` | -4,311 | -12.93 | -3,015 | -14.68 | -1,378 | -23.21 | 977,558 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.3 inventory-skew 0.05` | -6,312 | -13.56 | -4,252 | -14.10 | -1,667 | -8.60 | 1,548,265 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.3 inventory-skew 0.02` | -6,129 | -21.45 | -4,196 | -37.33 | -1,770 | -18.69 | 1,452,713 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.3 inventory-skew 0.02` | -6,091 | -8.38 | -4,236 | -5.42 | -1,909 | -2.10 | 1,394,243 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.3 inventory-skew 0.05` | -6,477 | -9.33 | -4,501 | -6.21 | -2,021 | -2.42 | 1,485,237 |
| `ladder alpha-micro 1 levels 3 pull-threshold 0.3 inventory-skew 0` | -5,846 | -7.71 | -4,169 | -6.94 | -2,060 | -4.92 | 1,261,994 |
| `ladder alpha-micro 1 levels 1 pull-threshold 0.3 inventory-skew 0` | -5,836 | -7.55 | -4,233 | -5.61 | -2,220 | -2.76 | 1,205,323 |
| `naive quote-on-book` | -15,033 | -51.16 | -9,552 | -51.05 | -2,790 | -21.49 | 4,081,007 |
