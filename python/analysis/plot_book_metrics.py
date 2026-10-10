#!/usr/bin/env python3
"""
plot_book_metrics.py — mid price, spread (ticks) and touch imbalance over a run.

Usage:
    python analysis/plot_book_metrics.py --run ../data/processed/my_run [--save]
"""
import argparse
import sys
from pathlib import Path

import matplotlib.pyplot as plt

sys.path.insert(0, str(Path(__file__).parent))
import style  # noqa: E402
from load_logs import load_book_metrics  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", required=True)
    ap.add_argument("--save", action="store_true")
    a = ap.parse_args()
    style.apply()
    df = load_book_metrics(a.run)
    fig, ax = plt.subplots(3, 1, figsize=(12, 8), sharex=True)
    ax[0].plot(df["time"], df["mid"], color=style.SERIES[0], linewidth=1.0)
    ax[0].set_title("Mid price")
    ax[1].plot(df["time"], df["spread_ticks"], color=style.SERIES[1], linewidth=0.8)
    ax[1].set_title("Spread (ticks)")
    ax[2].plot(df["time"], df["imbalance_1"].rolling(60, min_periods=1).mean(), color=style.SERIES[2], linewidth=0.8)
    ax[2].axhline(0, color=style.TEXT_2, linewidth=0.6)
    ax[2].set_title("Touch imbalance (Qb − Qa)/(Qb + Qa), 60-sample rolling mean")
    fig.tight_layout()
    if a.save:
        out = Path(a.run) / "book_metrics.png"
        fig.savefig(out, dpi=140)
        print(f"saved {out}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
