#!/usr/bin/env python3
"""
study.py — the full walk-forward study behind the README's results.

For every (symbol, strategy, maker-fee tier):
  1. grid-search parameters on the TRAIN days (first 4),
  2. run the selected parameters, untouched, on the TEST days (last 4).
Alpha coefficients come from analysis/signal_research.py fitted on the same
train days (results/signals/<SYMBOL>.json, 5 s horizon).

Then, for the selected configurations, sensitivity sweeps on the test days:
latency, queue model, and the fill-model bracket (book-cross fills on/off).

    python experiments/study.py                 # everything
    python experiments/study.py --only ATOMUSDT  # one symbol
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PY = sys.executable
RUNNER = ROOT / "python" / "experiments" / "run_backtests.py"
OUT = ROOT / "results" / "study"

DAYS = ["2026-09-28", "2026-09-29", "2026-09-30", "2026-10-01",
        "2026-10-02", "2026-10-03", "2026-10-04", "2026-10-05"]
TRAIN = 4

# Order sizes are a few hundred USD — small next to the touch, so the
# no-market-impact assumption holds.
SYMBOLS = {
    "BTCUSDT":  {"size": 0.01, "max": 0.1,   "large_tick": False},
    "ATOMUSDT": {"size": 100,  "max": 1000,  "large_tick": True},
    "XLMUSDT":  {"size": 1000, "max": 10000, "large_tick": True},
    "ADAUSDT":  {"size": 1000, "max": 10000, "large_tick": True},
}
FEES = {"vip0": 2.0, "zero": 0.0, "rebate": -0.5}   # maker fee, bp; taker stays 5.5 bp

GRIDS = {
    "naive": {},
    "glft": {"--alpha-scale": [0, 1], "--levels": [1, 3], "--half-spread-mult": [0.5, 1, 2],
             "--depletion-pull": [0, 0.3]},
    "ladder": {"--alpha-scale": [0, 1, 2], "--pull-threshold": [0.05, 0.15], "--levels": [2, 4],
               "--depletion-pull": [0, 0.3], "--inventory-skew": [0, 0.05]},
}


def alpha_args(sym: str) -> str:
    f = ROOT / "results" / "signals" / f"{sym}.json"
    if not f.exists():
        return ""
    c = json.loads(f.read_text())["5s"]
    return f"--alpha-micro {c['micro']:.6g} --alpha-obi {c['obi']:.6g}"


def run(sym: str, strat: str, fee_name: str, extra_fixed: str = "", grid=None, days=None, train=TRAIN,
        tag: str = "") -> Path:
    cfg = SYMBOLS[sym]
    out = OUT / f"{sym}_{strat}_{fee_name}{tag}"
    fixed = (f"--order-size {cfg['size']} --max-position {cfg['max']} "
             f"--maker-fee-bps {FEES[fee_name]} {alpha_args(sym)} {extra_fixed}")
    cmd = [PY, str(RUNNER), "--symbol", sym, "--days", *(days or DAYS), "--strategy", strat,
           "--fixed", fixed, "--grid", json.dumps(GRIDS[strat] if grid is None else grid), "--out", str(out)]
    if train and (GRIDS[strat] if grid is None else grid):
        cmd += ["--train-days", str(train)]
    print(f"== {sym} {strat} {fee_name}{tag}", flush=True)
    r = subprocess.run(cmd, capture_output=True, text=True)
    (out / "log.txt").parent.mkdir(parents=True, exist_ok=True)
    (out / "log.txt").write_text(r.stdout + r.stderr)
    tail = [l for l in r.stdout.splitlines() if "[select]" in l]
    if tail:
        print("   ", tail[-1])
    return out


def selected_params(out: Path) -> str:
    for line in (out / "log.txt").read_text().splitlines():
        if line.startswith("[select]"):
            sel = line[len("[select] "):].strip()
            if sel == "default":
                return ""
            return " ".join(f"--{kv.split('=')[0]} {kv.split('=')[1]}" for kv in sel.split())
    return ""


# Stage 2: the stage-1 selections sat on the edge of their grids (GLFT always
# picked the widest half-spread, the ladder the strongest alpha), so the grid
# is extended in those directions — same train/test protocol, new tag.
GRIDS_EXT = {
    "glft": {"--alpha-scale": [0, 1, 2], "--levels": [1, 3], "--half-spread-mult": [2, 3, 5, 8],
             "--depletion-pull": [0.3]},
    "ladder": {"--alpha-scale": [2, 3, 5], "--pull-threshold": [0.05, 0.15, 0.3], "--levels": [2, 4],
               "--depletion-pull": [0.3], "--inventory-skew": [0, 0.05]},
}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--skip-sensitivity", action="store_true")
    ap.add_argument("--extended", action="store_true", help="run only the stage-2 extended grids")
    args = ap.parse_args()
    syms = args.only or list(SYMBOLS)

    if args.extended:
        for sym in syms:
            for strat in ["glft"] + (["ladder"] if SYMBOLS[sym]["large_tick"] else []):
                for fee in ["vip0", "rebate"]:
                    run(sym, strat, fee, grid=GRIDS_EXT[strat], tag="_ext")
        return 0

    for sym in syms:
        strats = ["naive", "glft"] + (["ladder"] if SYMBOLS[sym]["large_tick"] else [])
        for strat in strats:
            for fee in FEES:
                run(sym, strat, fee)

    if args.skip_sensitivity:
        return 0
    # Sensitivity of the selected VIP0 and rebate configurations on test days.
    test_days = DAYS[TRAIN:]
    for sym in syms:
        strat = "ladder" if SYMBOLS[sym]["large_tick"] else "glft"
        for fee in ["vip0", "rebate"]:
            base = OUT / f"{sym}_{strat}_{fee}"
            params = selected_params(base)
            for lat in [0, 1000, 10000, 50000, 200000]:
                run(sym, strat, fee, f"{params} --latency-us {lat}", grid={}, days=test_days, train=0,
                    tag=f"_lat{lat}")
            for q in ["riskaverse", "log"]:
                run(sym, strat, fee, f"{params} --queue {q}", grid={}, days=test_days, train=0, tag=f"_q{q}")
            run(sym, strat, fee, f"{params} --no-cross-fill", grid={}, days=test_days, train=0, tag="_nocross")
    return 0


if __name__ == "__main__":
    sys.exit(main())
