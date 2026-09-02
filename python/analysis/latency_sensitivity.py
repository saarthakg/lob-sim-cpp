#!/usr/bin/env python3
"""
latency_sensitivity.py — visualise how PnL, fill count, and slippage change with latency.

Run latency_sweep.sh first to generate the run directories, then:

    python analysis/latency_sensitivity.py \
        --runs data/processed/latency_0us data/processed/latency_1000us \
               data/processed/latency_5000us data/processed/latency_10000us \
               data/processed/latency_50000us data/processed/latency_100000us \
        --save

Latency is inferred from the directory name (latency_<N>us) unless --labels is given.
"""

import argparse
import re
import sys
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

sys.path.insert(0, str(Path(__file__).parent))
from load_logs import load_fills, load_pnl, load_book_metrics


def parse_latency_us(run_dir: str) -> int:
    """Extract latency value from directory name like 'latency_10000us'."""
    m = re.search(r"latency_(\d+)us", Path(run_dir).name)
    if m:
        return int(m.group(1))
    raise ValueError(
        f"Cannot infer latency from directory name: {run_dir!r}. "
        "Use --labels to provide explicit labels."
    )


def load_run(run_dir: str):
    """Return (latency_us, final_realized_pnl, fill_count, mean_slippage)."""
    latency_us = parse_latency_us(run_dir)

    pnl_df = load_pnl(run_dir)
    final_pnl = pnl_df.iloc[-1]["realized_pnl"] if not pnl_df.empty else float("nan")

    # only_filled=True: exclude Cancelled/Rejected rows (fill_price == 0),
    # which would otherwise both inflate fill_count and corrupt mean_slippage.
    fills = load_fills(run_dir, only_filled=True)
    fill_count = len(fills)

    mean_slippage = float("nan")
    if not fills.empty:
        try:
            metrics = load_book_metrics(run_dir)
            merged = pd.merge_asof(
                fills.sort_values("ts"),
                metrics[["ts", "mid"]].rename(columns={"ts": "ts_book", "mid": "mid_at_fill"}),
                left_on="ts",
                right_on="ts_book",
                direction="backward"
            )
            sign = merged["side"].map({"B": 1, "A": -1})
            slippage = sign * (merged["fill_price"] - merged["mid_at_fill"])
            mean_slippage = slippage.mean()
        except Exception:
            pass

    return latency_us, final_pnl, fill_count, mean_slippage


def plot(run_dirs: list, save: bool = False):
    rows = []
    for d in run_dirs:
        try:
            lat, pnl, fills, slip = load_run(d)
            rows.append({"latency_us": lat, "realized_pnl": pnl,
                         "fill_count": fills, "mean_slippage": slip})
            print(f"  {Path(d).name}: latency={lat}µs  pnl=${pnl:.4f}  fills={fills}  slippage={slip:.4f}")
        except Exception as e:
            print(f"  WARNING: skipping {d}: {e}")

    if not rows:
        print("No runs loaded. Exiting.")
        return

    df = pd.DataFrame(rows).sort_values("latency_us").reset_index(drop=True)
    latency_ms = df["latency_us"] / 1000.0

    fig, axes = plt.subplots(1, 3, figsize=(18, 5))
    fig.suptitle("Latency Sensitivity — MarketMaker Strategy", fontsize=13)

    # 1) PnL vs latency
    ax = axes[0]
    ax.plot(latency_ms, df["realized_pnl"], marker="o", color="steelblue")
    ax.set_xlabel("Simulated latency (ms)")
    ax.set_ylabel("Final Realized PnL (USD)")
    ax.set_title("PnL vs Latency")
    ax.grid(True, alpha=0.3)
    for _, row in df.iterrows():
        ax.annotate(f"${row['realized_pnl']:.2f}",
                    (row["latency_us"] / 1000.0, row["realized_pnl"]),
                    textcoords="offset points", xytext=(0, 8), ha="center", fontsize=7)

    # 2) Fill count vs latency
    ax = axes[1]
    ax.bar(latency_ms, df["fill_count"], width=latency_ms.diff().median() * 0.4 or 0.3,
           color="darkorange", alpha=0.8)
    ax.set_xlabel("Simulated latency (ms)")
    ax.set_ylabel("Total fill count")
    ax.set_title("Fill Count vs Latency")
    ax.grid(True, alpha=0.3, axis="y")

    # 3) Mean slippage vs latency
    ax = axes[2]
    ax.plot(latency_ms, df["mean_slippage"], marker="o", color="crimson")
    ax.axhline(0, color="black", linewidth=0.8, linestyle=":")
    ax.set_xlabel("Simulated latency (ms)")
    ax.set_ylabel("Mean slippage (USD, sign-adjusted)")
    ax.set_title("Mean Slippage vs Latency")
    ax.grid(True, alpha=0.3)

    plt.tight_layout()

    if save:
        # Save in each run dir and also next to the first run dir
        out = Path(run_dirs[0]).parent / "latency_sensitivity.png"
        plt.savefig(out, dpi=150)
        print(f"\nSaved: {out}")
    else:
        plt.show()


def main():
    parser = argparse.ArgumentParser(description="Latency sensitivity analysis")
    parser.add_argument("--runs", nargs="+", required=True,
                        help="Run directories (latency_<N>us naming convention)")
    parser.add_argument("--save", action="store_true", help="Save PNG instead of showing")
    args = parser.parse_args()
    plot(args.runs, save=args.save)


if __name__ == "__main__":
    main()
