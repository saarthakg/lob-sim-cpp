#!/usr/bin/env python3
"""
run_report.py — one-page report for a single lob_sim run.

  1. net / gross PnL and fees over time
  2. position over time
  3. markout curve (bp) by fill cause — where the spread captured at the fill
     goes: positive at 0 s, decaying = adverse selection
  4. text panel: the summary.json attribution

Usage:
    python analysis/run_report.py --run ../data/processed/my_run [--save]
"""
import argparse
import sys
from pathlib import Path

import matplotlib.pyplot as plt

sys.path.insert(0, str(Path(__file__).parent))
import style  # noqa: E402
from load_logs import load_equity, load_fills, load_summary, markout_bps  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", required=True)
    ap.add_argument("--save", action="store_true")
    a = ap.parse_args()
    style.apply()
    s = load_summary(a.run)
    eq = load_equity(a.run)
    fills = load_fills(a.run)

    fig, ax = plt.subplots(2, 2, figsize=(13, 7.5))
    fig.suptitle(f"{s['strategy']} on {s['symbol']} — {Path(a.run).name}", fontsize=12, fontweight="bold")

    x = ax[0, 0]
    x.plot(eq["time"], eq["net_pnl"], label="Net PnL", color=style.SERIES[0])
    x.plot(eq["time"], eq["gross_pnl"], label="Gross (before fees)", color=style.SERIES[1], linewidth=1.2)
    x.plot(eq["time"], -eq["fees"], label="−Fees", color=style.SERIES[2], linewidth=1.2)
    x.axhline(0, color=style.TEXT_2, linewidth=0.6)
    x.set_title("PnL (quote ccy)")
    x.legend(loc="best")

    x = ax[0, 1]
    x.plot(eq["time"], eq["position"], color=style.SERIES[0], linewidth=1.0)
    x.axhline(0, color=style.TEXT_2, linewidth=0.6)
    x.set_title("Position (base units)")

    x = ax[1, 0]
    if len(fills):
        for i, (cause, g) in enumerate(fills.groupby("cause")):
            mk = markout_bps(g)
            x.plot(mk["horizon_s"] + 0.05, mk["bps"], marker="o", markersize=4,
                   label=f"{cause} ({g['qty'].sum() / fills['qty'].sum():.0%} of volume)",
                   color=style.SERIES[i])
        fee_bps = s["fees"] / s["notional"] * 1e4 if s["notional"] else 0
        x.axhline(fee_bps, color=style.TEXT_2, linewidth=0.8, linestyle="--", label=f"avg fee {fee_bps:.2f} bp")
        x.axhline(0, color=style.TEXT_2, linewidth=0.6)
        x.set_xscale("log")
        x.legend(loc="best")
    x.set_title("Markout by fill cause (bp, qty-weighted)")
    x.set_xlabel("seconds after fill (log; 0 s = edge at fill)")

    x = ax[1, 1]
    x.axis("off")
    lines = [
        f"Fills: {s['maker_fills']:,} maker / {s['taker_fills']:,} taker   notional ${s['notional']:,.0f}",
        f"Gross PnL       {s['gross_pnl']:+,.2f}",
        f"Fees            {-s['fees']:+,.2f}",
        f"Net PnL         {s['net_pnl']:+,.2f}   ({s['net_bps_of_notional']:+.2f} bp of notional)",
        f"Net after flatten {s['net_pnl_flat']:+,.2f}",
        "",
        f"Edge at fill    {s['edge']:+,.2f}",
        f"Markout 1s      {s['markout']['1s']:+,.2f}",
        f"Markout 60s     {s['markout']['60s']:+,.2f}",
        f"Max drawdown    {s['max_drawdown']:,.2f}",
        f"Max |position|  {s['max_abs_position']:,.4g}",
        f"Orders / cancels {s['orders_sent']:,} / {s['cancels_sent']:,}",
    ]
    x.text(0.0, 1.0, "\n".join(lines), va="top", family="monospace", fontsize=9, color=style.TEXT)

    fig.tight_layout()
    if a.save:
        out = Path(a.run) / "run_report.png"
        fig.savefig(out, dpi=140)
        print(f"saved {out}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
