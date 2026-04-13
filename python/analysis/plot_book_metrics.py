#!/usr/bin/env python3
"""
plot_book_metrics.py — visualise mid price, spread, and imbalance.

Usage:
    python plot_book_metrics.py --run data/processed/run_20240405_120000
    python plot_book_metrics.py --run data/processed/run_20240405_120000 --save
"""

import argparse
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.dates as mdates

# Allow running from any directory
sys.path.insert(0, str(Path(__file__).parent))
from load_logs import load_book_metrics


def plot(run_dir: str, save: bool = False):
    df = load_book_metrics(run_dir)
    print(f"Loaded {len(df):,} book snapshots from {run_dir}")

    neg = (df["spread"] < 0).sum()
    if neg > 0:
        print(f"WARNING: filtered {neg} rows with negative spread (transient inconsistent state)")
        df = df[df["spread"] >= 0].copy()

    fig, axes = plt.subplots(3, 1, figsize=(14, 10), sharex=True)
    fig.suptitle(f"Book Metrics — {Path(run_dir).name}", fontsize=14)

    # 1) Mid price
    ax = axes[0]
    ax.plot(df["ts"], df["mid"], linewidth=0.8, color="steelblue")
    ax.set_ylabel("Mid Price")
    ax.set_title("Mid Price over Time")
    ax.grid(True, alpha=0.3)

    # 2) Spread (time series + histogram)
    ax2a = axes[1]
    ax2a.plot(df["ts"], df["spread"], linewidth=0.6, color="darkorange", alpha=0.8)
    ax2a.set_ylabel("Spread")
    ax2a.set_title("Spread over Time")
    ax2a.grid(True, alpha=0.3)

    # 3) Imbalance
    ax3 = axes[2]
    ax3.plot(df["ts"], df["imbalance_5"], linewidth=0.6, color="purple", alpha=0.8)
    ax3.axhline(0, color="black", linewidth=0.5, linestyle="--")
    ax3.axhline(0.6, color="green", linewidth=0.8, linestyle=":", label="threshold +0.6")
    ax3.axhline(-0.6, color="red",   linewidth=0.8, linestyle=":", label="threshold -0.6")
    ax3.set_ylabel("Imbalance (5 levels)")
    ax3.set_title("Order Book Imbalance over Time")
    ax3.legend(fontsize=8)
    ax3.grid(True, alpha=0.3)
    ax3.set_ylim(-1.1, 1.1)

    for ax in axes:
        ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S"))
    axes[-1].set_xlabel("Time (UTC)")

    plt.tight_layout()

    if save:
        out = Path(run_dir) / "book_metrics.png"
        plt.savefig(out, dpi=150)
        print(f"Saved: {out}")
    else:
        plt.show()

    # Print summary stats
    print(f"\nSpread stats:")
    print(df["spread"].describe())
    print(f"\nImbalance stats:")
    print(df["imbalance_5"].describe())


def main():
    parser = argparse.ArgumentParser(description="Plot book metrics from a run directory")
    parser.add_argument("--run", required=True, help="Path to run output directory")
    parser.add_argument("--save", action="store_true", help="Save plot to PNG instead of showing")
    args = parser.parse_args()
    plot(args.run, save=args.save)


if __name__ == "__main__":
    main()
