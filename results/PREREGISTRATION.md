# Pre-registered confirmation test: wide-quote GLFT

Committed **before** any of the confirmation data below was downloaded or
inspected. The git timestamp of this file is the record.

## Why a confirmation test

After 16 out-of-sample days (results/study/EXTRA_OOS.md), the wide-quote
GLFT configurations from stage 2 (half-spread 2–8× the GLFT value) had
positive mean daily PnL in 6 of 8 symbol × fee cells. ADAUSDT reached
t = 2.28 (VIP0) and t = 2.35 (rebate).

About 28 configurations had been examined by then, so one or two |t| > 2
cells are expected by chance. The hypothesis is economically plausible —
quoting far from mid means providing liquidity only into short-term
dislocations, which tend to revert (Nagel 2012, *Evaporating Liquidity*) —
but it is a hypothesis, not a result.

## Hypothesis

H1: the frozen wide-quote GLFT configurations earn positive net PnL on unseen days.

## Frozen configurations

There is no re-fitting of any kind.

| Symbol | Maker fee | Parameters |
|---|---|---|
| ADAUSDT | +2.0 bp (VIP0) | alpha-scale=1 levels=3 half-spread-mult=5 depletion-pull=0.3 |
| ADAUSDT | −0.5 bp | alpha-scale=0 levels=1 half-spread-mult=8 depletion-pull=0.3 |
| ATOMUSDT | +2.0 bp | alpha-scale=1 levels=3 half-spread-mult=3 depletion-pull=0.3 |
| ATOMUSDT | −0.5 bp | alpha-scale=1 levels=3 half-spread-mult=5 depletion-pull=0.3 |
| XLMUSDT | +2.0 bp | alpha-scale=0 levels=3 half-spread-mult=8 depletion-pull=0.3 |
| XLMUSDT | −0.5 bp | alpha-scale=1 levels=3 half-spread-mult=2 depletion-pull=0.3 |
| BTCUSDT | +2.0 bp | alpha-scale=0 levels=1 half-spread-mult=5 depletion-pull=0.3 |
| BTCUSDT | −0.5 bp | alpha-scale=0 levels=3 half-spread-mult=8 depletion-pull=0.3 |

Everything else is held fixed:
- **Alpha coefficients:** `results/signals/<SYMBOL>.json`, 5 s horizon, fitted on 2026-09-28 → 10-01.
- **Order sizes:** as in `python/experiments/study.py`.
- **Fill model:** taker fee 5.5 bp, default power-2 queue model, cross-fills on, zero latency.

## Confirmation data

Bybit public ob200 and trades, **2026-08-17 → 2026-09-15**: 30 days, none
used before.

## Pass/fail criteria (fixed now)

1. **Primary, retail tier.** Form a daily portfolio net PnL: the sum of the
   ADA, ATOM and XLM VIP0 configurations. H1 is **supported at VIP0** if
   the one-sample t-statistic of the 30 daily values is **> 2.0** *and* more
   than 50% of days are positive.
2. **Secondary, rebate tier.** The same rule on the −0.5 bp configurations.
3. **Per-configuration claims.** No single configuration is called
   profitable unless its t-statistic exceeds **2.7**, which is
   Bonferroni-corrected for 8 configurations at one-sided α = 0.05 with
   29 degrees of freedom.
4. **Reporting.** Every result is reported whether it passes or fails.
