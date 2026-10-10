#!/usr/bin/env python3
"""
run_backtests.py — parallel parameter sweeps and walk-forward evaluation over
many days of Bybit data, driving the C++ `lob_sim` binary.

Each (day, parameter set, scenario) is one independent `lob_sim --replay` run
writing summary.json; results are collected into a CSV.

Walk-forward protocol (--train-days N):
  * the first N days are the *training* set: every grid point is run on them
    and the one with the best mean daily net PnL is selected;
  * the selected parameters are then run, untouched, on the remaining
    *test* days, and only those numbers are reported as performance.
Signal coefficients (alpha) should likewise come from training days only
(analysis/signal_research.py --train N).

Example:
    python experiments/run_backtests.py --symbol BTCUSDT \
        --days 2026-09-28 2026-09-29 2026-09-30 2026-10-01 2026-10-02 2026-10-03 \
        --train-days 3 --strategy glft \
        --fixed "--order-size 0.01 --max-position 0.1 --maker-fee-bps -0.5" \
        --grid '{"--gamma": [0.01, 0.05], "--alpha-micro": [0, 41.8]}' \
        --out ../results/btc_glft
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

import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "lob_sim"


def day_files(data_dir: Path, symbol: str, day: str):
    books = sorted(data_dir.glob(f"{day}_{symbol}_ob*.data*"))
    if not books:
        raise FileNotFoundError(f"no order book file for {symbol} {day} in {data_dir}")
    trades = data_dir / f"{symbol}{day}.csv.gz"
    return books[0], (trades if trades.exists() else None)


def run_one(job: dict) -> dict:
    out = Path(job["out"])
    summary = out / "summary.json"
    if not summary.exists() or job.get("force"):
        cmd = [str(BIN), "--replay", str(job["book"]), "--strategy", job["strategy"],
               # Sweeps only need summary.json/fills.csv; per-second samples
               # across thousands of runs filled ~90 GB of disk.
               "--output", str(out), "--quiet", "--sample-ms", "10000", "--no-book-metrics"]
        if job["trades"]:
            cmd += ["--trades", str(job["trades"])]
        cmd += job["args"]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0 or not summary.exists():
            return {**job["tags"], "error": (r.stderr or r.stdout)[-500:]}
    s = json.loads(summary.read_text())
    s.pop("markout", None)
    m = json.loads(summary.read_text()).get("markout", {})
    row = {**job["tags"], **s}
    for k, v in m.items():
        row[f"markout_{k}"] = v
    return row


def expand_grid(grid: dict):
    if not grid:
        yield {}
        return
    keys = list(grid)
    for vals in itertools.product(*(grid[k] for k in keys)):
        yield dict(zip(keys, vals))


def params_to_args(p: dict):
    args = []
    for k, v in p.items():
        if isinstance(v, bool):
            if v:
                args.append(k)
        else:
            args += [k, str(v)]
    return args


def param_label(p: dict) -> str:
    return " ".join(f"{k.lstrip('-')}={v}" for k, v in p.items()) or "default"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--symbol", required=True)
    ap.add_argument("--days", nargs="+", required=True)
    ap.add_argument("--data-dir", default=str(ROOT / "data" / "raw" / "bybit"))
    ap.add_argument("--strategy", required=True)
    ap.add_argument("--fixed", default="", help="args passed to every run")
    ap.add_argument("--grid", default="{}", help='JSON: {"--flag": [values...]}')
    ap.add_argument("--train-days", type=int, default=0, help="walk-forward: first N days select params")
    ap.add_argument("--select-by", default="net_pnl", help="summary field maximised on training days")
    ap.add_argument("--out", required=True)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()

    if not BIN.exists():
        print(f"build lob_sim first: {BIN}", file=sys.stderr)
        return 1
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    data_dir = Path(args.data_dir)
    fixed = shlex.split(args.fixed)
    grid = list(expand_grid(json.loads(args.grid)))
    train = args.days[: args.train_days] if args.train_days else []
    test = args.days[args.train_days:] if args.train_days else args.days

    def jobs_for(days, params_list, split):
        jobs = []
        for day in days:
            book, trades = day_files(data_dir, args.symbol, day)
            for i, p in enumerate(params_list):
                label = param_label(p)
                tag = f"{split}_p{grid.index(p) if p in grid else i}_{day}"
                jobs.append({"book": book, "trades": trades, "strategy": args.strategy,
                             "args": fixed + params_to_args(p), "out": out / "runs" / tag,
                             "force": args.force,
                             "tags": {"day": day, "split": split, "params": label,
                                      "param_id": grid.index(p) if p in grid else i}})
        return jobs

    def execute(jobs):
        with ThreadPoolExecutor(max_workers=args.jobs) as ex:
            rows = list(ex.map(run_one, jobs))
        df = pd.DataFrame(rows)
        if "error" in df and df["error"].notna().any():
            print(df[df["error"].notna()][["day", "params", "error"]].to_string(), file=sys.stderr)
        return df

    frames = []
    if train:
        print(f"[train] {len(grid)} parameter sets x {len(train)} days")
        tr = execute(jobs_for(train, grid, "train"))
        frames.append(tr)
        agg = tr.groupby(["param_id", "params"])[args.select_by].agg(["mean", "std", "min", "count"])
        agg = agg.sort_values("mean", ascending=False)
        print(agg.head(10).round(3).to_string())
        best_id = int(agg.index[0][0])
        chosen = [grid[best_id]]
        print(f"[select] {param_label(chosen[0])}")
    else:
        chosen = grid
    print(f"[test] {len(chosen)} parameter sets x {len(test)} days")
    te = execute(jobs_for(test, chosen, "test"))
    frames.append(te)

    res = pd.concat(frames, ignore_index=True)
    res.to_csv(out / "results.csv", index=False)
    cols = ["day", "split", "params", "maker_fills", "taker_fills", "notional", "gross_pnl", "fees", "net_pnl",
            "net_pnl_flat", "net_bps_of_notional", "edge", "markout_5s", "max_drawdown", "max_abs_position"]
    cols = [c for c in cols if c in res]
    print(res[res.split == "test"][cols].round(2).to_string(index=False))
    t = res[res.split == "test"]
    if len(t):
        g = t.groupby("params")["net_pnl"]
        print("\nTest-day net PnL by parameter set: mean / std / % days positive / total")
        print(pd.DataFrame({"mean": g.mean(), "std": g.std(), "pct_pos": g.apply(lambda s: (s > 0).mean() * 100),
                            "total": g.sum()}).round(2).to_string())
    print(f"\nwrote {out / 'results.csv'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
