#!/usr/bin/env python3
"""
queue_model_validation.py — which L2 queue model matches L3 reality?

For each capture prepared by l3_ground_truth.py, runs the C++ simulator's
ProbeStrategy (never-cancelled one-lot post-only orders at the touch) on the
100 ms-conflated L2+trades view under every queue model, and compares each
probe's simulated fill time with its true L3 fill time.

Reported per model (probes whose horizon runs past the end of data excluded):
  sim/true fill rate  — ratio of predicted to actual fill probability within H
  accuracy            — share of probes whose filled/not-filled outcome matches
  precision / recall  — of simulated fills that are real / of real fills simulated
  median |dt|         — fill-time error when both filled

    python analysis/queue_model_validation.py --stems data/raw/bitstamp/bitstamp_btcusd_* --horizon-s 60
"""
import argparse
import subprocess
import sys
from pathlib import Path

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "lob_sim"

MODELS = {
    "riskaverse": ["--queue", "riskaverse"],
    "power1": ["--queue", "power", "--queue-power", "1"],
    "power2 (default)": ["--queue", "power", "--queue-power", "2"],
    "power3": ["--queue", "power", "--queue-power", "3"],
    "log": ["--queue", "log"],
    "power2, no cross-fill": ["--queue", "power", "--queue-power", "2", "--no-cross-fill"],
}


def sim_fills(stem: Path, model_args, out_root: Path) -> pd.Series:
    """probe id -> first simulated fill ts (NaN if never)."""
    res = {}
    for side, flag in (("bid", "buy"), ("ask", "sell")):
        out = out_root / side
        cmd = [str(BIN), "--replay", f"{stem}.v2.csv", "--strategy", "probe", "--probes",
               f"{stem}.probes_{side}.csv", "--side", flag, "--maker-fee-bps", "0", "--quiet",
               "--output", str(out), *model_args]
        subprocess.run(cmd, check=True, capture_output=True)
        m = pd.read_csv(out / "probe_orders.csv")
        f = pd.read_csv(out / "fills.csv")
        first = f.groupby("order_id")["ts"].min()
        m["sim_fill_ts"] = m["order_id"].map(first)
        res.update(dict(zip(m["probe"], m["sim_fill_ts"])))
    return pd.Series(res)


def score(truth: pd.DataFrame, sim: pd.Series, H: float) -> dict:
    t = truth[~truth["censored"]].copy()
    t["sim_fill_ts"] = t["probe"].map(sim)
    true_f = t["true_fill_ts"].notna() & (t["true_fill_ts"] - t["ts"] <= H)
    sim_f = t["sim_fill_ts"].notna() & (t["sim_fill_ts"] - t["ts"] <= H)
    both = true_f & sim_f
    dt = (t.loc[both, "sim_fill_ts"] - t.loc[both, "true_fill_ts"]) / 1e6
    return {
        "probes": len(t),
        "true_fill_rate": true_f.mean(),
        "sim_fill_rate": sim_f.mean(),
        "rate_ratio": sim_f.mean() / true_f.mean() if true_f.mean() > 0 else np.nan,
        "accuracy": (true_f == sim_f).mean(),
        "precision": both.sum() / sim_f.sum() if sim_f.sum() else np.nan,
        "recall": both.sum() / true_f.sum() if true_f.sum() else np.nan,
        "median_abs_dt_s": dt.abs().median(),
        "median_dt_s": dt.median(),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stems", nargs="+", required=True, help="paths without .v2.csv/.truth.csv suffix")
    ap.add_argument("--horizon-s", type=float, default=60)
    ap.add_argument("--out", default=str(ROOT / "results" / "queue_validation"))
    a = ap.parse_args()
    H = a.horizon_s * 1e6
    out_root = Path(a.out)
    out_root.mkdir(parents=True, exist_ok=True)

    rows = []
    for stem_s in a.stems:
        stem = Path(stem_s.replace(".truth.csv", "").replace(".v2.csv", ""))
        truth = pd.read_csv(f"{stem}.truth.csv")
        for name, args in MODELS.items():
            sim = sim_fills(stem, args, out_root / "runs" / stem.name / name.replace(" ", "_").replace(",", ""))
            r = score(truth, sim, H)
            r.update({"capture": stem.name, "model": name})
            rows.append(r)
            print(f"{stem.name:40s} {name:24s} true {r['true_fill_rate']:.3f} sim {r['sim_fill_rate']:.3f} "
                  f"acc {r['accuracy']:.3f} prec {r['precision']:.3f} rec {r['recall']:.3f} "
                  f"|dt| {r['median_abs_dt_s']:.2f}s", flush=True)
    df = pd.DataFrame(rows)
    df.to_csv(out_root / "validation.csv", index=False)
    cols = ["capture", "model", "probes", "true_fill_rate", "sim_fill_rate", "rate_ratio", "accuracy",
            "precision", "recall", "median_abs_dt_s"]
    md = ["| " + " | ".join(cols) + " |", "|" + "---|" * len(cols)]
    for _, r in df[cols].iterrows():
        md.append("| " + " | ".join(f"{v:.3f}" if isinstance(v, float) else str(v) for v in r) + " |")
    (out_root / "VALIDATION.md").write_text(
        f"# Queue-model validation against Bitstamp L3 ground truth (horizon {a.horizon_s:.0f}s)\n\n" + "\n".join(md) + "\n")
    print(f"wrote {out_root / 'VALIDATION.md'}")


if __name__ == "__main__":
    sys.exit(main())
