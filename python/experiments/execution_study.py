#!/usr/bin/env python3
"""
execution_study.py — what does passive execution actually save?

Every day is cut into back-to-back 5-minute parent orders (alternating
buy/sell, 240 per day), and each parent is worked by four algorithms on the
same data: Cross (aggressive TWAP, the baseline), Join, Peg and Smart (see
cpp/include/strategy/execution_strategy.hpp). Nothing is fitted: one setting
per algorithm, fixed before any result was seen, so every day is usable.

Costs are in bp of notional against the mid at the parent's start, fees
included (Bybit VIP0). Comparisons are paired by parent; standard errors are
clustered by day (the unit of independence is the daily mean difference).

    python experiments/execution_study.py            # all days on disk
"""
import argparse
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "lob_sim"
DATA = ROOT / "data" / "raw" / "bybit"
OUT = ROOT / "results" / "execution"
SIZES = {"BTCUSDT": 0.05, "ATOMUSDT": 100, "XLMUSDT": 1000, "ADAUSDT": 1000}   # per slice
# name -> (lob_sim --algo, extra args). "pessimistic queue" re-runs Peg assuming
# no cancellation ahead of us ever helps; "prints only" drops the rule that the
# opposite quote reaching our price fills us. Together: the lower bound on
# passive fills this data allows.
ALGOS = {"cross": ("cross", []), "join": ("join", []), "peg": ("peg", []), "smart": ("smart", []),
         "peg, pessimistic queue": ("peg", ["--queue", "riskaverse"]),
         "peg, prints only": ("peg", ["--no-cross-fill"]),
         "peg, pessimistic queue, prints only": ("peg", ["--queue", "riskaverse", "--no-cross-fill"])}
SLICES, HORIZON_S, LATENCY_US = 5, 300, 5000


def days_on_disk(sym: str) -> list:
    out = []
    for f in sorted(DATA.glob(f"*_{sym}_ob200.data.zip")):
        day = f.name[:10]
        if (DATA / f"{sym}{day}.csv.gz").exists():
            out.append(day)
    return out


CONSUME = False   # --consume-prints: see ExchangeConfig::consume_prints


def run(job) -> pd.DataFrame:
    sym, day, algo = job
    name, extra = ALGOS[algo]
    out = OUT / "runs" / f"{sym}_{day}_{name}{''.join(extra).replace('--', '_')}{'_consume' if CONSUME else ''}"
    csv = out / "executions.csv"
    if not csv.exists():
        cmd = [str(BIN), "--replay", str(DATA / f"{day}_{sym}_ob200.data.zip"),
               "--trades", str(DATA / f"{sym}{day}.csv.gz"), "--strategy", "exec", "--algo", name, *extra, *(["--consume-prints"] if CONSUME else []),
               "--order-size", str(SIZES[sym]), "--slices", str(SLICES), "--horizon-s", str(HORIZON_S),
               "--latency-us", str(LATENCY_US), "--quiet", "--no-book-metrics", "--sample-ms", "60000",
               "--output", str(out)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            csv.unlink(missing_ok=True)
            raise RuntimeError(f"{job}: {r.stderr[-300:]}")
    df = pd.read_csv(csv)
    df["symbol"], df["day"], df["algo"] = sym, day, algo
    return df


def main() -> int:
    global BIN, CONSUME
    ap = argparse.ArgumentParser()
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 4))
    ap.add_argument("--only", nargs="+", default=list(SIZES))
    ap.add_argument("--bin", default=str(BIN))
    ap.add_argument("--consume-prints", action="store_true",
                    help="subtract prints since the last book update from displayed size (robustness check)")
    a = ap.parse_args()
    BIN, CONSUME = Path(a.bin), a.consume_prints
    jobs = [(s, d, g) for s in a.only for d in days_on_disk(s) for g in ALGOS]
    print(f"{len(jobs)} runs", flush=True)
    with ThreadPoolExecutor(a.jobs) as ex:
        df = pd.concat(ex.map(run, jobs), ignore_index=True)

    md = ["# Execution study: passive vs aggressive, paired parents\n",
          f"Bybit USDT perpetuals, {HORIZON_S // 60}-minute parents cut into {SLICES} slices, {LATENCY_US / 1000:g} ms "
          "each-way latency, power-2 queue model, VIP0 fees (maker 2.0 bp, taker 5.5 bp). Cost is bp against the "
          "arrival mid, fees included. Nothing was fitted. `saving` is Cross minus the algorithm on the *same parents*; "
          "its t-statistic uses one observation per day." +
          (" **Robustness run:** displayed size is reduced by prints since the last book update "
           "(`--consume-prints`)." if CONSUME else "") + "\n",
          "| symbol | days | parents | algo | filled passively | price shortfall bp | fees bp | total bp | saving vs Cross bp | t (days) | days better |",
          "|---|---|---|---|---|---|---|---|---|---|---|"]
    rows = []
    for sym, g in df.groupby("symbol", sort=False):
        piv = g.pivot_table(index=["day", "id"], columns="algo", values="total_bps").dropna()
        for algo in ALGOS:
            x = g[g.algo == algo]
            diff = (piv["cross"] - piv[algo]).groupby("day").mean()
            t = diff.mean() / (diff.std(ddof=1) / np.sqrt(len(diff))) if algo != "cross" else float("nan")
            rows.append({"symbol": sym, "algo": algo, "days": len(diff), "parents": len(piv),
                         "maker_share": x.maker_share.mean(), "shortfall_bps": x.shortfall_bps.mean(),
                         "fee_bps": x.fee_bps.mean(), "total_bps": x.total_bps.mean(),
                         "saving_bps": diff.mean(), "t": t, "days_better": (diff > 0).mean()})
            r = rows[-1]
            tail = "— | — | —" if algo == "cross" else f"{r['saving_bps']:+.2f} | {t:.1f} | {r['days_better']:.0%}"
            md.append(f"| {sym} | {r['days']} | {r['parents']:,} | {algo} | {r['maker_share']:.0%} | "
                      f"{r['shortfall_bps']:+.2f} | {r['fee_bps']:.2f} | {r['total_bps']:+.2f} | {tail} |")
    OUT.mkdir(parents=True, exist_ok=True)
    suffix = "_consumed" if CONSUME else ""
    pd.DataFrame(rows).to_csv(OUT / f"summary{suffix}.csv", index=False)
    (OUT / f"EXECUTION{suffix.upper()}.md").write_text("\n".join(md) + "\n")
    print("\n".join(md))
    return 0


if __name__ == "__main__":
    sys.exit(main())
