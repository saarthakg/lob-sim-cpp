#!/usr/bin/env python3
"""
l2_vs_l3.py — how much does the queue model distort a strategy's results?

Runs each strategy twice on the *same* order-level market (.l3bin): once
with the exact-queue L3 exchange (our orders in the real FIFO) and once with
the L2 queue-model exchange (queue position estimated from depth changes and
prints). Maker fee 0, so the comparison isolates fill fidelity: fills, traded
volume, spread captured at the fill, markouts and gross PnL.

    python experiments/l2_vs_l3.py --l3bin data/processed/l3full/*.l3bin
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "lob_sim"

# Order sizes are small relative to Bitstamp touch sizes (no-impact regime).
SIZES = {"btcusd": 0.001, "ethusd": 0.02, "xrpusd": 50}
STRATS = {
    "naive (join touch)": ["--strategy", "naive", "--quote-on-book"],
    "ladder (3 levels)": ["--strategy", "ladder", "--levels", "3", "--quote-on-book"],
    "glft": ["--strategy", "glft", "--warmup-s", "600"],
}


def run(l3bin: Path, strat_args, model: str, out: Path) -> dict:
    pair = l3bin.name.split("_")[1]
    size = SIZES.get(pair, 1)
    cmd = [str(BIN), "--replay", str(l3bin), *strat_args, "--order-size", str(size),
           "--max-position", str(size * 10), "--maker-fee-bps", "0", "--exchange-model", model,
           "--quiet", "--no-book-metrics", "--output", str(out)]
    subprocess.run(cmd, check=True, capture_output=True)
    return json.loads((out / "summary.json").read_text())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--l3bin", nargs="+", required=True)
    ap.add_argument("--out", default=str(ROOT / "results" / "l2_vs_l3"))
    a = ap.parse_args()
    out = Path(a.out)
    rows = []
    for f in map(Path, a.l3bin):
        pair = f.name.split("_")[1]
        for sname, sargs in STRATS.items():
            r = {}
            for model in ("l3", "l2"):
                s = run(f, sargs, model, out / "runs" / f"{pair}_{sname.split()[0]}_{model}")
                n = max(s["notional"], 1e-12)
                r[model] = {"fills": s["maker_fills"], "volume": s["maker_volume"], "notional": s["notional"],
                            "edge_bp": s["edge"] / n * 1e4, "mk5_bp": s["markout"]["5s"] / n * 1e4,
                            "gross_bp": s["gross_pnl"] / n * 1e4, "gross": s["gross_pnl"]}
            rows.append((pair, sname, r))
            print(f"{pair:7s} {sname:20s} fills L3 {r['l3']['fills']:6d} / L2 {r['l2']['fills']:6d} | "
                  f"gross bp L3 {r['l3']['gross_bp']:+7.2f} / L2 {r['l2']['gross_bp']:+7.2f} | "
                  f"5s markout bp L3 {r['l3']['mk5_bp']:+7.2f} / L2 {r['l2']['mk5_bp']:+7.2f}", flush=True)

    md = ["# Same market, two exchange models: exact L3 queue vs L2 queue model\n",
          "Bitstamp order-level capture (150 min each pair), maker fee 0. L3 = our orders in the real FIFO; "
          "L2 = queue position estimated from depth changes and prints (power-2 model).\n",
          "| pair | strategy | fills L3 | fills L2 | L2/L3 fills | volume L3 | volume L2 | edge at fill bp L3 / L2 | "
          "5s markout bp L3 / L2 | gross bp L3 / L2 | gross $ L3 / L2 |",
          "|---|---|---|---|---|---|---|---|---|---|---|"]
    for pair, sname, r in rows:
        l3, l2 = r["l3"], r["l2"]
        ratio = l2["fills"] / l3["fills"] if l3["fills"] else float("nan")
        md.append(f"| {pair} | {sname} | {l3['fills']:,} | {l2['fills']:,} | {ratio:.2f} | {l3['volume']:.4g} | "
                  f"{l2['volume']:.4g} | {l3['edge_bp']:+.2f} / {l2['edge_bp']:+.2f} | "
                  f"{l3['mk5_bp']:+.2f} / {l2['mk5_bp']:+.2f} | {l3['gross_bp']:+.2f} / {l2['gross_bp']:+.2f} | "
                  f"{l3['gross']:+.2f} / {l2['gross']:+.2f} |")
    out.mkdir(parents=True, exist_ok=True)
    (out / "COMPARISON.md").write_text("\n".join(md) + "\n")
    print("\n".join(md))
    return 0


if __name__ == "__main__":
    sys.exit(main())
