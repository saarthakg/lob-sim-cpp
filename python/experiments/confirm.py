#!/usr/bin/env python3
"""
confirm.py — run the pre-registered confirmation test (results/PREREGISTRATION.md).

Runs the eight frozen wide-quote GLFT configurations on the confirmation
window and applies exactly the pass/fail rules fixed in the pre-registration.

    python experiments/confirm.py
"""
import json
import subprocess
import sys
from datetime import date, timedelta
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).parent))
from study import FEES, PY, RUNNER, SYMBOLS, alpha_args  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "results" / "confirmation"

FROZEN = {  # copied verbatim from results/PREREGISTRATION.md
    ("ADAUSDT", "vip0"): "--alpha-scale 1 --levels 3 --half-spread-mult 5 --depletion-pull 0.3",
    ("ADAUSDT", "rebate"): "--alpha-scale 0 --levels 1 --half-spread-mult 8 --depletion-pull 0.3",
    ("ATOMUSDT", "vip0"): "--alpha-scale 1 --levels 3 --half-spread-mult 3 --depletion-pull 0.3",
    ("ATOMUSDT", "rebate"): "--alpha-scale 1 --levels 3 --half-spread-mult 5 --depletion-pull 0.3",
    ("XLMUSDT", "vip0"): "--alpha-scale 0 --levels 3 --half-spread-mult 8 --depletion-pull 0.3",
    ("XLMUSDT", "rebate"): "--alpha-scale 1 --levels 3 --half-spread-mult 2 --depletion-pull 0.3",
    ("BTCUSDT", "vip0"): "--alpha-scale 0 --levels 1 --half-spread-mult 5 --depletion-pull 0.3",
    ("BTCUSDT", "rebate"): "--alpha-scale 0 --levels 3 --half-spread-mult 8 --depletion-pull 0.3",
}
START, END = date(2026, 8, 17), date(2026, 9, 15)
PORTFOLIO = ["ADAUSDT", "ATOMUSDT", "XLMUSDT"]
T_PRIMARY, T_PER_CONFIG = 2.0, 2.7


def tstat(x: np.ndarray) -> float:
    s = x.std(ddof=1)
    return x.mean() / (s / np.sqrt(len(x))) if s > 0 else float("nan")


def main() -> int:
    days = [(START + timedelta(d)).isoformat() for d in range((END - START).days + 1)]
    frames = []
    for (sym, fee), params in FROZEN.items():
        cfg = SYMBOLS[sym]
        out = OUT / f"{sym}_{fee}"
        fixed = (f"--order-size {cfg['size']} --max-position {cfg['max']} --maker-fee-bps {FEES[fee]} "
                 f"{alpha_args(sym)} {params}")
        print(f"== {sym} {fee}: {params}", flush=True)
        r = subprocess.run([PY, str(RUNNER), "--symbol", sym, "--days", *days, "--strategy", "glft",
                            "--fixed", fixed, "--grid", "{}", "--out", str(out)], capture_output=True, text=True)
        (out / "log.txt").write_text(r.stdout + r.stderr)
        df = pd.read_csv(out / "results.csv")
        df["symbol"], df["fee"] = sym, fee
        frames.append(df)
    res = pd.concat(frames, ignore_index=True)
    res.to_csv(OUT / "daily.csv", index=False)

    md = ["# Confirmation test results\n",
          f"Pre-registered in results/PREREGISTRATION.md. Window {START} → {END} ({len(days)} days).\n",
          "## Per configuration\n",
          "| symbol | fee | days | mean $/day | t-stat | % days > 0 | total $ | gross bp | fills/day | passes t > 2.7 |",
          "|---|---|---|---|---|---|---|---|---|---|"]
    for (sym, fee), g in res.groupby(["symbol", "fee"]):
        x = g["net_pnl"].values
        gb = g["gross_pnl"].sum() / g["notional"].sum() * 1e4 if g["notional"].sum() else float("nan")
        t = tstat(x)
        md.append(f"| {sym} | {fee} | {len(x)} | {x.mean():+.2f} | {t:+.2f} | {(x > 0).mean():.0%} | "
                  f"{x.sum():+,.0f} | {gb:+.2f} | {g['maker_fills'].mean():,.0f} | {'yes' if t > T_PER_CONFIG else 'no'} |")
    md.append("\n## Pre-registered portfolio criteria (ADA + ATOM + XLM, daily sum)\n")
    md.append("| tier | days | mean $/day | t-stat | % days > 0 | verdict |")
    md.append("|---|---|---|---|---|---|")
    for fee, label in [("vip0", "Primary: VIP0 (+2.0 bp)"), ("rebate", "Secondary: rebate (−0.5 bp)")]:
        p = res[(res.fee == fee) & res.symbol.isin(PORTFOLIO)].groupby("day")["net_pnl"].sum()
        x = p.values
        t = tstat(x)
        ok = t > T_PRIMARY and (x > 0).mean() > 0.5
        md.append(f"| {label} | {len(x)} | {x.mean():+.2f} | {t:+.2f} | {(x > 0).mean():.0%} | "
                  f"{'**H1 supported**' if ok else '**H1 not supported**'} |")
    (OUT / "CONFIRMATION.md").write_text("\n".join(md) + "\n")
    print("\n".join(md))
    return 0


if __name__ == "__main__":
    sys.exit(main())
