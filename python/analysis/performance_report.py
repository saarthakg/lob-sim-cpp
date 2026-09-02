#!/usr/bin/env python3
"""
performance_report.py — quant performance summary for a strategy run:
Sharpe ratio, max drawdown, win rate, fee drag, and turnover.

Usage:
    python performance_report.py --run data/processed/run_20240405_120000
    python performance_report.py --run data/processed/run_20240405_120000 --save

The dataset backing a single run is typically well under an hour of trading,
so "annualized Sharpe" numbers here are illustrative, not statistically
meaningful — treat them as a relative comparison tool across runs (e.g. across
a latency sweep), not a claim about live performance. See docs/assumptions.md.
"""

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates

sys.path.insert(0, str(Path(__file__).parent))
from load_logs import load_fills, load_pnl

SECONDS_PER_YEAR = 365.25 * 24 * 3600


def bucketed_returns(equity: pd.Series, bucket: str = "1min") -> pd.Series:
    """Resample an equity series to fixed time buckets and take first
    differences — this turns a cumulative PnL series into a return-per-bucket
    series, which is what Sharpe/drawdown expect."""
    s = equity.resample(bucket).last().ffill()
    return s.diff().dropna()


def sharpe_ratio(returns: pd.Series, periods_per_year: float) -> float:
    std = returns.std()
    if std == 0 or np.isnan(std):
        return float("nan")
    return (returns.mean() / std) * np.sqrt(periods_per_year)


def max_drawdown(equity: pd.Series) -> tuple:
    """Returns (max_drawdown_abs, max_drawdown_pct_of_peak_notional)."""
    running_max = equity.cummax()
    drawdown = equity - running_max
    dd_abs = drawdown.min()
    peak_at_trough = running_max.loc[drawdown.idxmin()] if len(drawdown) else 0.0
    dd_pct = (dd_abs / peak_at_trough * 100.0) if peak_at_trough not in (0, np.nan) else float("nan")
    return dd_abs, dd_pct


def summarize(run_dir: str) -> dict:
    pnl = load_pnl(run_dir)
    # only_filled=True: "total fills" should count actual trades, not the
    # Cancelled/Rejected rows fills.csv also carries.
    fills = load_fills(run_dir, only_filled=True)

    pnl = pnl.set_index("ts")
    has_net = pnl["net_pnl"].notna().all() and len(pnl) > 0
    # net_pnl (= total_pnl - cumulative fees) is what actually matters for
    # "did this strategy make money" — total_pnl alone is gross of fees and
    # systematically overstates results. Older runs (pre-net_pnl schema)
    # fall back to total_pnl with a clear label change below.
    equity = pnl["net_pnl"] if has_net else pnl["total_pnl"]

    returns_1min = bucketed_returns(equity, "1min")
    bucket_seconds = 60.0
    periods_per_year = SECONDS_PER_YEAR / bucket_seconds

    sharpe = sharpe_ratio(returns_1min, periods_per_year)
    dd_abs, dd_pct = max_drawdown(equity)

    win_rate = float("nan")
    if len(returns_1min) > 0:
        nonzero = returns_1min[returns_1min != 0]
        win_rate = (nonzero > 0).mean() * 100.0 if len(nonzero) else float("nan")

    total_fees = fills["fee"].sum() if not fills.empty else 0.0
    gross_pnl = pnl.iloc[-1]["total_pnl"] if not pnl.empty else 0.0
    fee_drag_pct = (total_fees / gross_pnl * 100.0) if gross_pnl not in (0, np.nan) else float("nan")

    duration_s = (pnl.index[-1] - pnl.index[0]).total_seconds() if len(pnl) > 1 else 0.0
    fills_per_min = (len(fills) / (duration_s / 60.0)) if duration_s > 0 else float("nan")

    return {
        "run_dir": run_dir,
        "equity_is_net": has_net,
        "final_total_pnl": pnl.iloc[-1]["total_pnl"] if not pnl.empty else float("nan"),
        "final_net_pnl": pnl.iloc[-1]["net_pnl"] if has_net else float("nan"),
        "sharpe_1min_annualized": sharpe,
        "max_drawdown_usd": dd_abs,
        "max_drawdown_pct": dd_pct,
        "win_rate_pct_of_minutes": win_rate,
        "total_fills": len(fills),
        "total_fees_usd": total_fees,
        "gross_pnl_usd": gross_pnl,
        "fee_drag_pct_of_gross": fee_drag_pct,
        "duration_s": duration_s,
        "fills_per_minute": fills_per_min,
        "equity": equity,
        "returns_1min": returns_1min,
    }


def print_summary(s: dict):
    label = "Net PnL (post-fee)" if s["equity_is_net"] else "Total PnL (gross — no net_pnl column in this run)"
    print(f"\nPerformance summary — {Path(s['run_dir']).name}")
    print("=" * 60)
    print(f"  Duration:              {s['duration_s'] / 60.0:.1f} min")
    print(f"  Total fills:           {s['total_fills']:,}  ({s['fills_per_minute']:.2f}/min)")
    print(f"  Gross PnL (pre-fee):   ${s['gross_pnl_usd']:.4f}")
    print(f"  Total fees:            ${s['total_fees_usd']:.4f}  ({s['fee_drag_pct_of_gross']:.1f}% of gross)")
    if s["equity_is_net"]:
        print(f"  Final net PnL:         ${s['final_net_pnl']:.4f}")
    print(f"  Max drawdown ({label}): ${s['max_drawdown_usd']:.4f}  ({s['max_drawdown_pct']:.1f}% of peak)")
    print(f"  Win rate (by minute):  {s['win_rate_pct_of_minutes']:.1f}%")
    print(f"  Sharpe (1min, ann.):   {s['sharpe_1min_annualized']:.2f}  "
          f"[illustrative — short sample, see docstring]")
    print("=" * 60)


def plot(s: dict, save: bool):
    equity_label = "Net PnL" if s["equity_is_net"] else "Total PnL (gross)"
    fig, axes = plt.subplots(1, 3, figsize=(16, 4.5))
    fig.suptitle(f"Performance Report — {Path(s['run_dir']).name}", fontsize=13)

    ax = axes[0]
    ax.plot(s["equity"].index, s["equity"].values, color="steelblue", linewidth=0.9)
    ax.axhline(0, color="black", linewidth=0.5, linestyle="--")
    ax.set_title(f"Equity Curve ({equity_label})")
    ax.set_ylabel("USD")
    ax.grid(True, alpha=0.3)
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S"))

    ax = axes[1]
    running_max = s["equity"].cummax()
    drawdown = s["equity"] - running_max
    ax.fill_between(drawdown.index, drawdown.values, 0, color="crimson", alpha=0.4)
    ax.set_title("Drawdown")
    ax.set_ylabel("USD below peak")
    ax.grid(True, alpha=0.3)
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S"))
    for label in ax.get_xticklabels():
        label.set_rotation(30)
    for label in axes[0].get_xticklabels():
        label.set_rotation(30)

    ax = axes[2]
    if len(s["returns_1min"]) > 0:
        ax.hist(s["returns_1min"].values, bins=30, color="darkorange", edgecolor="white")
    ax.axvline(0, color="black", linewidth=1, linestyle="--")
    ax.set_title("Per-Minute PnL Change Distribution")
    ax.set_xlabel("USD / min")
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    if save:
        out = Path(s["run_dir"]) / "performance_report.png"
        plt.savefig(out, dpi=150)
        print(f"Saved: {out}")
    else:
        plt.show()


def main():
    parser = argparse.ArgumentParser(description="Quant performance summary for a strategy run")
    parser.add_argument("--run", required=True)
    parser.add_argument("--save", action="store_true")
    parser.add_argument("--no-plot", action="store_true", help="Print summary only, skip plotting")
    args = parser.parse_args()

    s = summarize(args.run)
    print_summary(s)
    if not args.no_plot:
        plot(s, save=args.save)


if __name__ == "__main__":
    main()
