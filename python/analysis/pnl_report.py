#!/usr/bin/env python3
"""
pnl_report.py — equity curve, position, realized vs unrealized PnL.

Usage:
    python pnl_report.py --run data/processed/run_20240405_120000
    python pnl_report.py --run data/processed/run_20240405_120000 --save
"""

import argparse
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.dates as mdates

sys.path.insert(0, str(Path(__file__).parent))
from load_logs import load_pnl


def plot(run_dir: str, save: bool = False):
    df = load_pnl(run_dir)
    print(f"Loaded {len(df):,} PnL snapshots from {run_dir}")

    final_realized = df.iloc[-1]["realized_pnl"]
    has_net = df["net_pnl"].notna().all() and len(df) > 0
    # Note: deliberately no literal "$" here — matplotlib's mathtext parser
    # treats a pair of "$" anywhere in a title as a math expression to render,
    # which mangles the whole string when two independent "$N.NN" values sit
    # in the same title (each contributes one "$", forming an accidental pair).
    title = f"PnL Report — {Path(run_dir).name}  |  Final Realized (gross) PnL: {final_realized:.2f} USD"
    if has_net:
        title += f"  |  Final Net PnL: {df.iloc[-1]['net_pnl']:.2f} USD"
    fig, axes = plt.subplots(3, 1, figsize=(14, 10), sharex=True)
    fig.suptitle(title, fontsize=14)

    # 1) Equity curve
    ax = axes[0]
    ax.plot(df["ts"], df["total_pnl"], linewidth=0.8, color="steelblue", label="Total PnL (gross)")
    ax.plot(df["ts"], df["realized_pnl"], linewidth=0.8, color="green", alpha=0.7, label="Realized (gross)")
    ax.plot(df["ts"], df["unrealized_pnl"], linewidth=0.8, color="orange", alpha=0.7, label="Unrealized")
    if has_net:
        ax.plot(df["ts"], df["net_pnl"], linewidth=1.1, color="crimson", label="Net PnL (post-fee)")
    ax.axhline(0, color="black", linewidth=0.5, linestyle="--")
    ax.set_ylabel("PnL (USD)")
    ax.set_title("Equity Curve")
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.3)

    # 2) Position
    ax = axes[1]
    ax.fill_between(df["ts"], df["position"], 0,
                    where=(df["position"] >= 0), color="green", alpha=0.4, label="Long")
    ax.fill_between(df["ts"], df["position"], 0,
                    where=(df["position"] < 0),  color="red",   alpha=0.4, label="Short")
    ax.axhline(0, color="black", linewidth=0.5)
    ax.set_ylabel("Position (units)")
    ax.set_title("Inventory / Position")
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.3)

    # 3) Mark mid price
    ax = axes[2]
    ax.plot(df["ts"], df["mark_mid"], linewidth=0.7, color="grey")
    ax.set_ylabel("Mid Price")
    ax.set_title("Mark Mid Price")
    ax.grid(True, alpha=0.3)

    for ax in axes:
        ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S"))
    axes[-1].set_xlabel("Time (UTC)")

    plt.tight_layout()

    if save:
        out = Path(run_dir) / "pnl_report.png"
        plt.savefig(out, dpi=150)
        print(f"Saved: {out}")
    else:
        plt.show()

    # Summary
    final = df.iloc[-1]
    print(f"\nFinal realized PnL (gross):   {final['realized_pnl']:.4f}")
    print(f"Final unrealized PnL:         {final['unrealized_pnl']:.4f}")
    print(f"Final total PnL (gross):      {final['total_pnl']:.4f}")
    if has_net:
        print(f"Total fees paid:              {final['fees_paid_cumulative']:.4f}")
        print(f"Final net PnL (post-fee):     {final['net_pnl']:.4f}")
    print(f"Final position:               {final['position']}")


def main():
    parser = argparse.ArgumentParser(description="PnL report for a strategy run")
    parser.add_argument("--run", required=True)
    parser.add_argument("--save", action="store_true")
    args = parser.parse_args()
    plot(args.run, save=args.save)


if __name__ == "__main__":
    main()
