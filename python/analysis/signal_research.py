#!/usr/bin/env python3
"""
signal_research.py — does order-book imbalance predict short-horizon price
moves, and by how much? Fits the alpha coefficients GlftMarketMaker uses.

Input: one or more book_metrics.csv files from `lob_sim --replay ... --sample-ms 100`
(no strategy needed). Features, all computed by the C++ book at each sample:

  micro  = weighted_mid - mid              (ticks)   size-weighted mid offset
           (Stoikov 2018's micro-price starts from this and shrinks it)
  obi    = depth_imbalance_5bp             [-1, 1]   (Qb - Qa)/(Qb + Qa) within 5 bp of mid
  imb1   = imbalance_1                     [-1, 1]   touch-only imbalance

Target: future mid change in ticks at horizons 1 s, 5 s, 30 s.

For each file (= day) reports Pearson IC and the OLS coefficients of
Δmid_h ~ micro + obi. Coefficients are averaged across *training* days only
(--train N: the first N files) and evaluated out-of-sample on the rest, so
the numbers fed to the strategy are never fitted on the days they're tested on.

Usage:
    python analysis/signal_research.py --tick 0.1 --train 4 \
        --files ../data/processed/sig_d1/book_metrics.csv ../data/processed/sig_d2/book_metrics.csv ...
"""

import argparse
import json
from pathlib import Path

import numpy as np
import pandas as pd

HORIZONS_S = [1, 5, 30]
FEATURES = ["micro", "obi"]


def load(path: str, tick: float) -> pd.DataFrame:
    df = pd.read_csv(path)
    df = df.sort_values("ts").reset_index(drop=True)
    step_us = int(np.median(np.diff(df["ts"].values[:1000])))
    df["mid_t"] = df["mid"] / tick
    df["micro"] = (df["weighted_mid"] - df["mid"]) / tick
    df["obi"] = df["depth_imbalance_5bp"]
    df["imb1"] = df["imbalance_1"]
    for h in HORIZONS_S:
        k = max(1, int(round(h * 1e6 / step_us)))
        df[f"ret_{h}s"] = df["mid_t"].shift(-k) - df["mid_t"]
    return df.dropna()


def ols(X: np.ndarray, y: np.ndarray) -> np.ndarray:
    X1 = np.column_stack([np.ones(len(X)), X])
    beta, *_ = np.linalg.lstsq(X1, y, rcond=None)
    return beta  # [intercept, coefs...]


def r2(y: np.ndarray, yhat: np.ndarray) -> float:
    ss = ((y - y.mean()) ** 2).sum()
    return 1 - ((y - yhat) ** 2).sum() / ss if ss > 0 else float("nan")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--files", nargs="+", required=True)
    ap.add_argument("--tick", type=float, required=True)
    ap.add_argument("--train", type=int, default=None, help="first N files are training days")
    ap.add_argument("--out", default=None, help="write fitted coefficients as JSON")
    args = ap.parse_args()

    days = [load(f, args.tick) for f in args.files]
    n_train = args.train if args.train is not None else len(days)
    rows = []
    for i, df in enumerate(days):
        for h in HORIZONS_S:
            y = df[f"ret_{h}s"].values
            r = {"day": Path(args.files[i]).parent.name, "h": h, "split": "train" if i < n_train else "test",
                 "n": len(df), "sd_ret_ticks": y.std()}
            for f in FEATURES + ["imb1"]:
                r[f"ic_{f}"] = np.corrcoef(df[f].values, y)[0, 1]
            b = ols(df[FEATURES].values, y)
            r.update({f"b_{f}": b[j + 1] for j, f in enumerate(FEATURES)})
            r["r2"] = r2(y, b[0] + df[FEATURES].values @ b[1:])
            rows.append(r)
    res = pd.DataFrame(rows)
    pd.set_option("display.width", 200)
    print(res.round(4).to_string(index=False))

    print("\nOut-of-sample check: coefficients averaged over training days, applied to test days")
    fitted = {}
    for h in HORIZONS_S:
        tr = res[(res.h == h) & (res.split == "train")]
        coef = np.array([tr[f"b_{f}"].mean() for f in FEATURES])
        fitted[f"{h}s"] = dict(zip(FEATURES, coef.tolist()))
        for i in range(n_train, len(days)):
            df = days[i]
            y = df[f"ret_{h}s"].values
            yhat = df[FEATURES].values @ coef
            print(f"  h={h:>2}s {Path(args.files[i]).parent.name}: OOS R^2={r2(y, yhat - yhat.mean() + y.mean()):.4f} "
                  f"corr={np.corrcoef(yhat, y)[0, 1]:.4f}  coefs={np.round(coef, 3).tolist()}")
    print("\nTraining-day mean coefficients (ticks of expected move per unit feature):")
    print(json.dumps(fitted, indent=2))
    if args.out:
        Path(args.out).write_text(json.dumps(fitted, indent=2))


if __name__ == "__main__":
    main()
