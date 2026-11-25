#!/usr/bin/env python3
"""
nasdaq_study.py — market making on Nasdaq with exact queue priority, real
per-share fees and microsecond latency.

Data: .l3bin.gz days from python/data/fetch_itch.py. The order-level feed
removes the crypto study's three caveats: no 100 ms conflation, no modelled
queue, and a published fee schedule instead of an assumed rebate.

Development days (2019-2021) choose parameters and describe effects. The
2025-26 days are for `--confirm` only, under results/nasdaq/PREREGISTRATION.md.

  dev:      python experiments/nasdaq_study.py --stage dev
  confirm:  python experiments/nasdaq_study.py --stage confirm
"""
import argparse
import itertools
import json
import os
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "lob_sim"
DATA = ROOT / "data" / "processed" / "itch_l3"
OUT = ROOT / "results" / "nasdaq"

DEV_DAYS = ["2019-01-30", "2019-03-27", "2019-07-30", "2019-08-30", "2019-10-18", "2019-10-30", "2019-12-30",
            "2020-01-30", "2021-07-13", "2021-08-13"]
CONFIRM_DAYS = ["2025-12-08", "2025-12-09", "2025-12-10", "2025-12-11", "2025-12-12", "2026-05-15", "2026-05-18",
                "2026-06-12"]
SYMBOLS = ["SIRI", "INTC", "CSCO", "CMCSA", "AMD", "MU", "F", "BAC", "PFE", "T", "AAPL", "MSFT", "QQQ", "NVDA"]
NYSE_LISTED = {"F", "BAC", "PFE", "T"}   # Tape A

# $/share, from nasdaqtrader.com's price list (read 2026-11-25). Remove: $0.0030.
# Add (displayed): base "all other firms" tier, and a top volume tier.
TAKER = 0.0030
REBATES = {"none": lambda s: 0.0,
           "base": lambda s: 0.0018 if s in NYSE_LISTED else 0.0013,
           "top": lambda s: 0.0030}

SESSION = "--session-start 09:35:00 --session-end 15:55:00"
BASE = f"--order-size 100 --max-position 500 --maker-fee-per-unit 0 --taker-fee-per-unit {TAKER} {SESSION}"
SELECT_LATENCY_US = 50
LATENCIES = [0, 10, 50, 200, 1000, 10000]
NAIVE = "--strategy naive --quote-on-book"
GRID = {"--levels": [1, 3], "--pull-threshold": [0, 0.1, 0.2, 0.3], "--inventory-skew": [0, 0.02, 0.05]}


def ladder_args(params: dict) -> str:
    return "--strategy ladder --quote-on-book --alpha-micro 1 " + " ".join(f"{k} {v}" for k, v in params.items())


def tag(args: str, lat: int) -> str:
    return (args.replace("--strategy ", "").replace("--quote-on-book", "").replace("--", "")
            .replace("  ", " ").strip().replace(" ", "_") + f"_lat{lat}")


def run(job):
    sym, day, args, lat = job
    src = DATA / f"{day}_{sym}.l3bin.gz"
    if not src.exists():
        return None
    out = OUT / "runs" / f"{day}_{sym}_{tag(args, lat)}"
    f = out / "summary.json"
    if not f.exists():
        cmd = [str(BIN), "--replay", str(src), *shlex.split(BASE), *shlex.split(args), "--latency-us", str(lat),
               "--quiet", "--no-book-metrics", "--sample-ms", "10000", "--output", str(out)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            f.unlink(missing_ok=True)
            raise RuntimeError(f"{job}: {r.stderr[-300:]}")
    s = json.loads(f.read_text())
    # Fees in these runs are the taker fee alone (rebate 0); a rebate only
    # rescales the maker shares, since no strategy here quotes off the fee.
    row = {"symbol": sym, "day": day, "args": args, "latency_us": lat, "gross": s["gross_pnl"], "taker_fees": s["fees"],
           "maker_shares": s["maker_volume"], "taker_shares": s["taker_volume"], "fills": s["maker_fills"],
           "edge": s["edge"], "mk_100ms": s["markout"]["100ms"], "mk_1s": s["markout"]["1s"], "mk_5s": s["markout"]["5s"],
           "final_position": s["final_position"], "max_dd": s["max_drawdown"], "orders": s["orders_sent"]}
    for name, reb in REBATES.items():
        row[f"net_{name}"] = s["gross_pnl"] - s["fees"] + reb(sym) * s["maker_volume"]
    return row


def sweep(jobs, n) -> pd.DataFrame:
    with ThreadPoolExecutor(n) as ex:
        rows = [r for r in ex.map(run, jobs) if r]
    return pd.DataFrame(rows)


def tstat(x) -> float:
    x = np.asarray(x, dtype=float)
    s = x.std(ddof=1) if len(x) > 1 else 0.0
    return float(x.mean() / (s / np.sqrt(len(x)))) if s > 0 else float("nan")


def portfolio(df: pd.DataFrame, col: str) -> pd.Series:
    return df.groupby("day")[col].sum()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", choices=["dev", "confirm"], required=True)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    a = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    if a.stage == "dev":
        return dev(a.jobs)
    import nasdaq_confirm
    return nasdaq_confirm.main(a.jobs)


def dev(jobs: int) -> int:
    grid = [ladder_args(dict(zip(GRID, v))) for v in itertools.product(*GRID.values())]
    df = sweep([(s, d, g, SELECT_LATENCY_US) for s in SYMBOLS for d in DEV_DAYS for g in [NAIVE] + grid], jobs)
    df.to_csv(OUT / "dev_grid.csv", index=False)
    md = ["# Nasdaq exact-queue study: development days\n",
          f"{df.day.nunique()} days ({df.day.min()} to {df.day.max()}), {df.symbol.nunique()} symbols, 100-share orders, "
          f"500-share limit, {SELECT_LATENCY_US} µs each-way latency, session 09:35-15:55, flattened at the end. "
          "In-sample: this stage selects parameters and describes effects.\n",
          "## Per symbol: touch-joining control vs the best gated ladder\n",
          "cents per maker share; `edge` = spread captured at the fill, `5 s` = markout.\n",
          "| symbol | naive edge | naive 5 s | naive net (no rebate) | best ladder | shares/day | edge | 5 s | net no rebate | net base | net top | $/day top | t (days) |",
          "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for sym, g in df.groupby("symbol", sort=False):
        nv = g[g.args == NAIVE]
        lad = g[g.args != NAIVE]
        best = lad.groupby("args")["net_top"].mean().idxmax()
        b = lad[lad.args == best]
        c = lambda x, col: x[col].sum() / max(x.maker_shares.sum(), 1) * 100
        md.append(f"| {sym} | {c(nv, 'edge'):+.3f} | {c(nv, 'mk_5s'):+.3f} | {c(nv, 'net_none'):+.3f} | "
                  f"`{best.split('--alpha-micro 1 ')[1].replace('--', '')}` | {b.maker_shares.mean():,.0f} | "
                  f"{c(b, 'edge'):+.3f} | {c(b, 'mk_5s'):+.3f} | {c(b, 'net_none'):+.3f} | {c(b, 'net_base'):+.3f} | "
                  f"{c(b, 'net_top'):+.3f} | {b.net_top.mean():+.0f} | {tstat(b.net_top):+.2f} |")
    md += ["\n## One shared parameter set across all symbols\n",
           "| params | $/day no rebate | t | $/day base | t | $/day top | t | shares/day |", "|---|---|---|---|---|---|---|---|"]
    tab = []
    for args, g in df.groupby("args"):
        p = {k: portfolio(g, f"net_{k}") for k in REBATES}
        tab.append((p["top"].mean(), args, p, g.groupby("day").maker_shares.sum().mean()))
    for _, args, p, sh in sorted(tab, key=lambda x: -x[0]):
        md.append(f"| `{args.replace('--strategy ', '').replace('--quote-on-book ', '').replace('--', '')}` | "
                  + " | ".join(f"{p[k].mean():+,.0f} | {tstat(p[k]):+.2f}" for k in REBATES) + f" | {sh:,.0f} |")
    (OUT / "DEV_GRID.md").write_text("\n".join(md) + "\n")
    print("\n".join(md))
    return 0


if __name__ == "__main__":
    sys.exit(main())
