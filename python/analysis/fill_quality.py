#!/usr/bin/env python3
"""
fill_quality.py — fill price vs mid, markout, fee breakdown.

Usage:
    python fill_quality.py --run data/processed/run_20240405_120000
    python fill_quality.py --run data/processed/run_20240405_120000 --save
"""

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

sys.path.insert(0, str(Path(__file__).parent))
from load_logs import load_fills, load_book_metrics


def plot(run_dir: str, save: bool = False):
    # only_filled=True: exclude Cancelled/Rejected rows, which have
    # fill_price == 0 and would otherwise corrupt slippage as a huge outlier.
    fills = load_fills(run_dir, only_filled=True)
    metrics = load_book_metrics(run_dir)

    if fills.empty:
        print("No fills found in this run.")
        return

    print(f"Loaded {len(fills):,} fill events from {run_dir}")

    # Merge fills with nearest book snapshot to get contemporaneous mid
    fills_sorted   = fills.sort_values("ts_us")
    metrics_sorted = metrics.sort_values("ts_us")

    fills_with_mid = pd.merge_asof(
        fills_sorted,
        metrics_sorted[["ts_us", "mid"]].rename(columns={"mid": "mid_at_fill"}),
        on="ts_us",
        direction="nearest"
    )

    # Slippage: fill_price - mid (for buys, positive = paid above mid)
    fills_with_mid["slippage"] = fills_with_mid.apply(
        lambda r: (r["fill_price"] - r["mid_at_fill"]) * (1 if r["side"] == "B" else -1),
        axis=1
    )

    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle(f"Fill Quality — {Path(run_dir).name}", fontsize=14)

    # 1) Fill price vs mid
    ax = axes[0, 0]
    buys  = fills_with_mid[fills_with_mid["side"] == "B"]
    sells = fills_with_mid[fills_with_mid["side"] == "A"]
    ax.scatter(buys["ts"],  buys["fill_price"],  s=20, color="green", alpha=0.7, label="Buy fill")
    ax.scatter(sells["ts"], sells["fill_price"], s=20, color="red",   alpha=0.7, label="Sell fill")
    ax.plot(metrics["ts"], metrics["mid"], linewidth=0.6, color="grey", alpha=0.5, label="Mid")
    ax.set_title("Fill Price vs Mid")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)

    # 2) Slippage histogram
    ax = axes[0, 1]
    ax.hist(fills_with_mid["slippage"], bins=30, color="steelblue", edgecolor="white")
    ax.axvline(0, color="black", linewidth=1, linestyle="--")
    mean_slip = fills_with_mid["slippage"].mean()
    ax.axvline(mean_slip, color="red", linewidth=1.5,
               linestyle="--", label=f"Mean={mean_slip:.4f}")
    ax.set_title("Slippage Distribution (fill - mid, sign-adjusted)")
    ax.set_xlabel("Slippage (price units)")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)

    # 3) Fee breakdown
    ax = axes[1, 0]
    by_type = fills_with_mid.groupby("order_type")["fee"].sum()
    ax.bar(by_type.index, by_type.values, color=["steelblue", "darkorange"][:len(by_type)])
    ax.set_title("Total Fees by Order Type")
    ax.set_ylabel("Total Fee")
    ax.grid(True, alpha=0.3, axis="y")

    # 4) Fill qty distribution
    ax = axes[1, 1]
    ax.hist(fills_with_mid["fill_qty"], bins=20, color="purple", edgecolor="white")
    ax.set_title("Fill Qty Distribution")
    ax.set_xlabel("Fill Qty")
    ax.grid(True, alpha=0.3)

    plt.tight_layout()

    if save:
        out = Path(run_dir) / "fill_quality.png"
        plt.savefig(out, dpi=150)
        print(f"Saved: {out}")
    else:
        plt.show()

    print(f"\nFill summary:")
    print(f"  Total fills:    {len(fills):,}")
    print(f"  Total fees:     {fills['fee'].sum():.4f}")
    print(f"  Mean slippage:  {mean_slip:.4f}")
    print(f"  Side breakdown: {fills['side'].value_counts().to_dict()}")


def main():
    parser = argparse.ArgumentParser(description="Fill quality analysis")
    parser.add_argument("--run", required=True)
    parser.add_argument("--save", action="store_true")
    args = parser.parse_args()
    plot(args.run, save=args.save)


if __name__ == "__main__":
    main()
