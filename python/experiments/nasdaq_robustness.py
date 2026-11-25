#!/usr/bin/env python3
"""
nasdaq_robustness.py — do the Nasdaq results survive the simulator's two
unobservable assumptions? EXPLORATORY: run after the confirmation results were
committed, on the same days. It cannot confirm or overturn a pre-registered
verdict; it says which of them depend on modelling choices.

  impact  none      a print that fills us also fills the real maker (study default)
          displace  each printed share trades once; displaced makers keep their slot
  cross   on        a real order arriving at a price crossing our quote fills us
                    (study default; pessimistic: it may have been post-only)
          off       only prints fill us (optimistic)

    python experiments/nasdaq_robustness.py --bin build/lob_sim
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

from nasdaq_confirm import LADDER
from nasdaq_study import BASE, CONFIRM_DAYS, DATA, DEV_DAYS, NAIVE, OUT, REBATES, ROOT, SYMBOLS, tag, tstat

LATENCIES = [0, 10, 50, 10000]
IMPACT = {"none": "", "displace": "--impact displace"}
CROSS = {"on": "", "off": "--no-cross-fill"}


def run(job):
    binary, sym, day, args, lat, imp, cross = job
    out = OUT / "runs" / "robust" / f"{day}_{sym}_{tag(args, lat)}_{imp}_{cross}"
    f = out / "summary.json"
    if not f.exists():
        cmd = [binary, "--replay", str(DATA / f"{day}_{sym}.l3bin.gz"), *shlex.split(BASE), *shlex.split(args),
               *shlex.split(IMPACT[imp]), *shlex.split(CROSS[cross]), "--latency-us", str(lat), "--quiet",
               "--summary-only", "--sample-ms", "10000", "--output", str(out)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            f.unlink(missing_ok=True)
            raise RuntimeError(f"{job}: {r.stderr[-300:]}")
    s = json.loads(f.read_text())
    row = {"symbol": sym, "day": day, "strategy": "naive" if args == NAIVE else "ladder", "latency_us": lat,
           "impact": imp, "cross": cross, "net_none": s["gross_pnl"] - s["fees"], "maker_shares": s["maker_volume"],
           "edge": s["edge"], "mk_5s": s["markout"]["5s"],
           "cross_shares": s.get("by_cause", {}).get("book_cross", {}).get("volume", 0.0)}
    row["net_top"] = row["net_none"] + REBATES["top"](sym) * row["maker_shares"]
    return row


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=str(ROOT / "build" / "lob_sim"))
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    a = ap.parse_args()
    jobs = [(a.bin, s, d, g, l, i, c) for d in DEV_DAYS + CONFIRM_DAYS for s in SYMBOLS for g in (NAIVE, LADDER)
            for l, i, c in itertools.product(LATENCIES, IMPACT, CROSS)]
    with ThreadPoolExecutor(a.jobs) as ex:
        df = pd.DataFrame(ex.map(run, jobs))
    df.to_csv(OUT / "robustness.csv", index=False)
    vol = pd.read_csv(OUT / "session_volume.csv")
    df = df.merge(vol, on=["day", "symbol"])
    df["period"] = df.day.map(lambda d: "development" if d in DEV_DAYS else "confirmation")
    ps = lambda g, col: g[col].sum() / max(g.maker_shares.sum(), 1) * 100

    md = ["# Robustness to impact and the cross-fill rule (exploratory, not pre-registered)\n",
          (__doc__ or "").split("\n\n")[1].strip() + "\n", "```\n" + (__doc__ or "").split("\n\n")[2] + "\n```\n"]
    for period in ("development", "confirmation"):
        md += [f"\n## {period.capitalize()} days: naive, cents per maker share\n",
               "| impact | cross-fills | latency µs | fills from cross rule | share of market volume | edge | 5 s markout | "
               "net before rebate | net top rebate |", "|---|---|---|---|---|---|---|---|---|"]
        x = df[(df.period == period) & (df.strategy == "naive")]
        for (i, c, l), g in x.groupby(["impact", "cross", "latency_us"], sort=False):
            md.append(f"| {i} | {c} | {l:,} | {g.cross_shares.sum() / max(g.maker_shares.sum(), 1):.0%} | "
                      f"{g.maker_shares.sum() / g.volume.sum():.1%} | {ps(g, 'edge'):+.3f} | {ps(g, 'mk_5s'):+.3f} | "
                      f"{ps(g, 'net_none'):+.3f} | {ps(g, 'net_top'):+.3f} |")
    md += ["\n## The pre-registered statistics recomputed under each assumption (confirmation days)\n",
           "Same statistics and thresholds as PREREGISTRATION.md, whose verdicts stand on the first row only.\n",
           "| impact | cross-fills | H1 t (naive $/day, top) | H2 t (ladder $/day, top) | H3 c/share (t) | H4 c/share (t) | H5 c/share | "
           "H5 symbols within ±0.10 |", "|---|---|---|---|---|---|---|---|"]
    c = df[df.period == "confirmation"]
    daily = lambda g, col: g.groupby("day")[col].sum() / g.groupby("day").maker_shares.sum() * 100
    for (i, x), g in c.groupby(["impact", "cross"], sort=False):
        pick = lambda st, l: g[(g.strategy == st) & (g.latency_us == l)]
        n50, l50 = pick("naive", 50), pick("ladder", 50)
        h3 = daily(l50, "mk_5s") - daily(n50, "mk_5s")
        h4 = daily(pick("naive", 10), "net_none") - daily(pick("naive", 10000), "net_none")
        z = pick("naive", 0)
        per = z.groupby("symbol")[["net_top", "maker_shares"]].sum()
        per = per.net_top / per.maker_shares * 100
        md.append(f"| {i} | {x} | {tstat(n50.groupby('day').net_top.sum()):+.2f} | {tstat(l50.groupby('day').net_top.sum()):+.2f} | "
                  f"{h3.mean():+.3f} ({tstat(h3):+.2f}) | {h4.mean():+.3f} ({tstat(h4):+.2f}) | {ps(z, 'net_top'):+.3f} | "
                  f"{int((per.abs() <= 0.10).sum())} of {len(per)} |")
    (OUT / "ROBUSTNESS.md").write_text("\n".join(md) + "\n")
    print("\n".join(md))
    return 0


if __name__ == "__main__":
    sys.exit(main())
