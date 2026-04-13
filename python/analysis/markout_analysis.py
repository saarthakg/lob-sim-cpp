#!/usr/bin/env python3
"""
markout_analysis.py — adverse selection / fill quality via markout.

Markout measures how the mid price moves after a fill:
  markout_N = sign(side) * (mid_at_t_plus_N - mid_at_fill)

Positive markout = price moved in your favour (good passive fill).
Negative markout = adverse selection (market moved against you).

Usage:
    python analysis/markout_analysis.py --run data/processed/mm_bybit --save
"""

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

sys.path.insert(0, str(Path(__file__).parent))
from load_logs import load_fills, load_book_metrics

HORIZONS = [10, 50, 100]


def compute_markouts(fills: pd.DataFrame, metrics: pd.DataFrame) -> pd.DataFrame:
    """
    For each fill compute markout at each horizon N in HORIZONS.

    metrics must be sorted by ts (load_book_metrics guarantees this).
    We use pd.merge_asof to match each fill to the nearest prior book snapshot,
    then look forward N rows in metrics to get the future mid.
    """
    metrics = metrics.sort_values("ts").reset_index(drop=True)
    fills   = fills.sort_values("ts").reset_index(drop=True)

    # Add a sequential row index so we can do forward lookups
    metrics["row_idx"] = np.arange(len(metrics))

    # Merge fills with book metrics to get mid_at_fill
    merged = pd.merge_asof(
        fills,
        metrics[["ts", "mid", "row_idx"]].rename(columns={"ts": "ts_book", "mid": "mid_at_fill"}),
        left_on="ts",
        right_on="ts_book",
        direction="backward"
    )

    # sign: +1 for buy (B), -1 for sell (A)
    merged["sign"] = merged["side"].map({"B": 1, "A": -1})

    # Slippage = sign * (fill_price - mid_at_fill)  [in USD]
    merged["slippage"] = merged["sign"] * (merged["fill_price"] - merged["mid_at_fill"])

    # For each horizon, look up mid N rows ahead
    for N in HORIZONS:
        future = metrics[["row_idx", "mid"]].copy()
        future["row_idx"] = future["row_idx"] - N   # shift: row r maps to "mid N rows later"
        future = future.rename(columns={"mid": f"mid_plus_{N}"})
        merged = merged.merge(future, on="row_idx", how="left")
        merged[f"markout_{N}"] = merged["sign"] * (merged[f"mid_plus_{N}"] - merged["mid_at_fill"])

    return merged


def plot(run_dir: str, save: bool = False):
    fills   = load_fills(run_dir)
    metrics = load_book_metrics(run_dir)

    if fills.empty:
        print(f"No fills found in {run_dir}. Nothing to plot.")
        return

    print(f"Loaded {len(fills):,} fills and {len(metrics):,} book snapshots")

    df = compute_markouts(fills, metrics)

    # ── Summary stats ─────────────────────────────────────────────────────────
    print(f"\nMarkout summary (N=50, in USD):")
    for side_label, side_code in [("Buy fills (B)", "B"), ("Sell fills (A)", "A")]:
        sub = df[df["side"] == side_code]["markout_50"].dropna()
        if len(sub):
            print(f"  {side_label}: mean={sub.mean():.4f}  std={sub.std():.4f}  n={len(sub)}")

    print(f"\nMean markout by horizon:")
    for N in HORIZONS:
        col = f"markout_{N}"
        buys  = df[df["side"] == "B"][col].mean()
        sells = df[df["side"] == "A"][col].mean()
        print(f"  N={N:3d}: buy={buys:+.4f}  sell={sells:+.4f}")

    # ── Plots ─────────────────────────────────────────────────────────────────
    fig, axes = plt.subplots(1, 3, figsize=(18, 5))
    fig.suptitle(f"Markout / Adverse Selection — {Path(run_dir).name}", fontsize=13)

    # 1) Distribution of markout at N=50
    ax = axes[0]
    buys  = df[df["side"] == "B"]["markout_50"].dropna()
    sells = df[df["side"] == "A"]["markout_50"].dropna()
    bins = np.linspace(
        min(buys.min() if len(buys) else 0, sells.min() if len(sells) else 0),
        max(buys.max() if len(buys) else 0, sells.max() if len(sells) else 0),
        40
    )
    if len(buys):
        ax.hist(buys,  bins=bins, alpha=0.6, color="green", label="Buy fills")
        ax.axvline(buys.mean(),  color="darkgreen", linestyle="--", linewidth=1.5,
                   label=f"Buy mean={buys.mean():.3f}")
    if len(sells):
        ax.hist(sells, bins=bins, alpha=0.6, color="red",   label="Sell fills")
        ax.axvline(sells.mean(), color="darkred",   linestyle="--", linewidth=1.5,
                   label=f"Sell mean={sells.mean():.3f}")
    ax.axvline(0, color="black", linewidth=0.8, linestyle=":")
    ax.set_xlabel("Markout at N=50 (USD)")
    ax.set_ylabel("Fill count")
    ax.set_title("Markout Distribution (N=50)")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)

    # 2) Mean markout vs horizon
    ax = axes[1]
    buy_means  = [df[df["side"] == "B"][f"markout_{N}"].mean() for N in HORIZONS]
    sell_means = [df[df["side"] == "A"][f"markout_{N}"].mean() for N in HORIZONS]
    ax.plot(HORIZONS, buy_means,  marker="o", color="green", label="Buy fills")
    ax.plot(HORIZONS, sell_means, marker="o", color="red",   label="Sell fills")
    ax.axhline(0, color="black", linewidth=0.8, linestyle=":")
    ax.set_xlabel("Horizon N (events)")
    ax.set_ylabel("Mean markout (USD)")
    ax.set_title("Mean Markout vs Horizon")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)

    # 3) Scatter: slippage vs markout at N=50
    ax = axes[2]
    valid = df[["slippage", "markout_50", "side"]].dropna()
    for side_code, color, label in [("B", "green", "Buy"), ("A", "red", "Sell")]:
        sub = valid[valid["side"] == side_code]
        ax.scatter(sub["slippage"], sub["markout_50"], alpha=0.3, s=8,
                   color=color, label=label)
    ax.axhline(0, color="black", linewidth=0.5, linestyle=":")
    ax.axvline(0, color="black", linewidth=0.5, linestyle=":")
    ax.set_xlabel("Fill slippage (USD, sign-adjusted)")
    ax.set_ylabel("Markout at N=50 (USD)")
    ax.set_title("Slippage vs Markout (N=50)")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)

    plt.tight_layout()

    if save:
        out = Path(run_dir) / "markout_analysis.png"
        plt.savefig(out, dpi=150)
        print(f"\nSaved: {out}")
    else:
        plt.show()


def main():
    parser = argparse.ArgumentParser(description="Markout / adverse selection analysis")
    parser.add_argument("--run", required=True, help="Path to run output directory")
    parser.add_argument("--save", action="store_true", help="Save PNG instead of showing")
    args = parser.parse_args()
    plot(args.run, save=args.save)


if __name__ == "__main__":
    main()
