#!/usr/bin/env python3
"""
extra_oos.py — more out-of-sample days for configurations already selected.

Four test days can reject a strategy that loses every day but cannot
distinguish a small edge from zero. This re-runs each selected configuration
from study.py (stage 1 and stage-2 "_ext"), with parameters frozen exactly as
recorded in its selection log, on additional days that were never used for
selection, then pools them with the original test days.

    python experiments/extra_oos.py --days 2026-09-16 ... 2026-09-27
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import pandas as pd

sys.path.insert(0, str(Path(__file__).parent))
from study import FEES, OUT, PY, RUNNER, SYMBOLS, alpha_args, selected_params  # noqa: E402

CONFIG_RE = r"^(?P<sym>[A-Z0-9]+)_(?P<strat>naive|glft|ladder)_(?P<fee>vip0|zero|rebate)(?P<ext>_ext)?$"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--days", nargs="+", required=True)
    ap.add_argument("--fees", nargs="+", default=["vip0", "rebate"])
    a = ap.parse_args()

    rows = []
    for d in sorted(OUT.iterdir()):
        m = pd.Series([d.name]).str.extract(CONFIG_RE).iloc[0]
        if m.isna()["sym"] or m["fee"] not in a.fees or m["strat"] == "naive":
            continue
        sym, strat, fee, ext = m["sym"], m["strat"], m["fee"], (m["ext"] if isinstance(m["ext"], str) else "")
        params = selected_params(d)
        cfg = SYMBOLS[sym]
        out = OUT / f"{d.name}_xoos"
        fixed = (f"--order-size {cfg['size']} --max-position {cfg['max']} --maker-fee-bps {FEES[fee]} "
                 f"{alpha_args(sym)} {params}")
        print(f"== {d.name}: {params}", flush=True)
        r = subprocess.run([PY, str(RUNNER), "--symbol", sym, "--days", *a.days, "--strategy", strat,
                            "--fixed", fixed, "--grid", "{}", "--out", str(out)], capture_output=True, text=True)
        (out / "log.txt").write_text(r.stdout + r.stderr)
        extra = pd.read_csv(out / "results.csv")
        orig = pd.read_csv(d / "results.csv")
        orig = orig[orig["split"] == "test"]
        pooled = pd.concat([orig, extra])
        x = pooled["net_pnl"].values
        g = pooled["gross_pnl"].sum() / pooled["notional"].sum() * 1e4 if pooled["notional"].sum() else np.nan
        rows.append({"config": d.name, "strategy": strat + ext, "symbol": sym, "fee": fee, "params": params,
                     "oos_days": len(x), "net_mean": x.mean(), "net_std": x.std(ddof=1),
                     "t_stat": x.mean() / (x.std(ddof=1) / np.sqrt(len(x))) if x.std(ddof=1) > 0 else np.nan,
                     "pct_days_pos": (x > 0).mean() * 100, "net_total": x.sum(), "gross_bps": g,
                     "fills_per_day": pooled["maker_fills"].mean()})
    res = pd.DataFrame(rows).sort_values(["symbol", "fee", "strategy"])
    res.to_csv(OUT / "extra_oos.csv", index=False)
    cols = ["symbol", "strategy", "fee", "oos_days", "net_mean", "t_stat", "pct_days_pos", "net_total",
            "gross_bps", "fills_per_day"]
    md = ["# Pooled out-of-sample results (original 4 test days + extra unseen days)\n",
          "Parameters frozen from the training-day selection; no re-fitting.\n",
          "| " + " | ".join(cols) + " |", "|" + "---|" * len(cols)]
    for _, r in res[cols].iterrows():
        md.append("| " + " | ".join(f"{v:,.2f}" if isinstance(v, float) else str(v) for v in r) + " |")
    (OUT / "EXTRA_OOS.md").write_text("\n".join(md) + "\n")
    print("\n".join(md))
    return 0


if __name__ == "__main__":
    sys.exit(main())
