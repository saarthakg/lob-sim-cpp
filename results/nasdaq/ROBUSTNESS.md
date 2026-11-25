# Robustness to impact and the cross-fill rule (exploratory, not pre-registered)

impact  none      a print that fills us also fills the real maker (study default)
          displace  each printed share trades once; displaced makers keep their slot
  cross   on        a real order arriving at a price crossing our quote fills us
                    (study default; pessimistic: it may have been post-only)
          off       only prints fill us (optimistic)

```
    python experiments/nasdaq_robustness.py --bin build/lob_sim

```


## Development days: naive, cents per maker share

| impact | cross-fills | latency µs | fills from cross rule | share of market volume | edge | 5 s markout | net before rebate | net top rebate |
|---|---|---|---|---|---|---|---|---|
| none | on | 0 | 3% | 8.8% | +0.594 | -0.326 | -0.333 | -0.033 |
| none | off | 0 | 0% | 8.6% | +0.624 | -0.322 | -0.330 | -0.030 |
| displace | on | 0 | 2% | 8.4% | +0.585 | -0.320 | -0.325 | -0.025 |
| displace | off | 0 | 0% | 4.1% | +0.664 | -0.329 | -0.307 | -0.007 |
| none | on | 10 | 8% | 8.7% | +0.538 | -0.351 | -0.360 | -0.060 |
| none | off | 10 | 0% | 7.9% | +0.629 | -0.346 | -0.355 | -0.055 |
| displace | on | 10 | 8% | 8.3% | +0.530 | -0.356 | -0.364 | -0.064 |
| displace | off | 10 | 0% | 3.9% | +0.671 | -0.368 | -0.343 | -0.043 |
| none | on | 50 | 37% | 10.4% | +0.210 | -0.385 | -0.390 | -0.090 |
| none | off | 50 | 0% | 6.5% | +0.638 | -0.394 | -0.401 | -0.101 |
| displace | on | 50 | 38% | 10.2% | +0.192 | -0.387 | -0.393 | -0.093 |
| displace | off | 50 | 0% | 3.5% | +0.679 | -0.423 | -0.406 | -0.106 |
| none | on | 10,000 | 58% | 12.7% | -0.058 | -0.451 | -0.459 | -0.159 |
| none | off | 10,000 | 0% | 5.6% | +0.544 | -0.551 | -0.549 | -0.249 |
| displace | on | 10,000 | 59% | 12.6% | -0.072 | -0.454 | -0.461 | -0.161 |
| displace | off | 10,000 | 0% | 3.3% | +0.536 | -0.612 | -0.553 | -0.253 |

## Confirmation days: naive, cents per maker share

| impact | cross-fills | latency µs | fills from cross rule | share of market volume | edge | 5 s markout | net before rebate | net top rebate |
|---|---|---|---|---|---|---|---|---|
| none | on | 0 | 2% | 14.1% | +1.196 | -0.329 | -0.296 | +0.004 |
| none | off | 0 | 0% | 13.8% | +1.234 | -0.333 | -0.302 | -0.002 |
| displace | on | 0 | 2% | 12.9% | +1.129 | -0.319 | -0.283 | +0.017 |
| displace | off | 0 | 0% | 5.8% | +1.698 | -0.370 | -0.306 | -0.006 |
| none | on | 10 | 10% | 14.7% | +1.087 | -0.389 | -0.365 | -0.065 |
| none | off | 10 | 0% | 13.1% | +1.287 | -0.393 | -0.376 | -0.076 |
| displace | on | 10 | 11% | 13.6% | +1.028 | -0.389 | -0.351 | -0.051 |
| displace | off | 10 | 0% | 5.9% | +1.753 | -0.488 | -0.417 | -0.117 |
| none | on | 50 | 48% | 21.3% | +0.447 | -0.445 | -0.452 | -0.152 |
| none | off | 50 | 0% | 11.0% | +1.374 | -0.554 | -0.556 | -0.256 |
| displace | on | 50 | 49% | 20.4% | +0.371 | -0.437 | -0.431 | -0.131 |
| displace | off | 50 | 0% | 5.5% | +1.745 | -0.689 | -0.660 | -0.360 |
| none | on | 10,000 | 64% | 21.4% | +0.075 | -0.757 | -0.775 | -0.475 |
| none | off | 10,000 | 0% | 9.0% | +1.054 | -1.161 | -1.183 | -0.883 |
| displace | on | 10,000 | 65% | 21.0% | +0.008 | -0.765 | -0.772 | -0.472 |
| displace | off | 10,000 | 0% | 5.3% | +1.205 | -1.475 | -1.443 | -1.143 |

## The pre-registered statistics recomputed under each assumption (confirmation days)

Same statistics and thresholds as PREREGISTRATION.md, whose verdicts stand on the first row only.

| impact | cross-fills | H1 t (naive $/day, top) | H2 t (ladder $/day, top) | H3 c/share (t) | H4 c/share (t) | H5 c/share | H5 symbols within ±0.10 |
|---|---|---|---|---|---|---|---|
| none | on | -2.20 | -2.03 | -0.239 (-4.84) | +0.525 (+2.69) | +0.004 | 10 of 14 |
| none | off | -1.99 | -1.76 | -0.342 (-5.97) | +0.786 (+4.85) | -0.002 | 11 of 14 |
| displace | on | -1.92 | -2.17 | -0.308 (-6.39) | +0.547 (+2.65) | +0.017 | 10 of 14 |
| displace | off | -2.08 | -2.11 | -0.320 (-3.63) | +0.931 (+4.50) | -0.006 | 9 of 14 |
