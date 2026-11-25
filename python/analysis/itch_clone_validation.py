#!/usr/bin/env python3
"""
itch_clone_validation.py — is the exchange simulator's queue priority right on
real Nasdaq data?

A real order's fate is in the feed, so it can referee the simulator. For a
sample of real displayed orders R1 that were eventually fully executed, a
one-share simulated clone is placed at the same price and side just after R1
entered the book. Price-time priority then brackets the clone's fill exactly:

  * not before R1 was completely filled (R1 is ahead of the clone);
  * no later than the first execution of R2, the next real order to join the
    same price after the clone (R2 is behind it).

"Early" = filled before R1 finished (an impossible fill). "Late" = R2 traded
while the clone was still unfilled (a missed fill). The bounds are computed
here in Python straight from the records, independently of the C++ book.

R1 is sampled among orders that were *not* cancelled, so the L2 models, which
expect some of the queue ahead to cancel, look worse here than on an average
order. Read their rows as "how often does the model jump a queue that really
stayed in place", not as an unconditional error rate.

    python analysis/itch_clone_validation.py --files data/processed/itch_l3/2019-12-30_*.l3bin.gz
"""
import argparse
import gzip
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "lob_sim"
REC = np.dtype([("ts", "<i8"), ("id", "<u8"), ("price", "<i8"), ("qty", "<i8"), ("aux", "<u8"),
                ("type", "u1"), ("side", "u1"), ("pad", "V6")])
HEADER = 8 + 4 + 20 + 8 + 8 + 8
ADD, DELETE, MODIFY, TRADE = 1, 2, 3, 4
MODELS = {"exact-queue L3": ["--exchange-model", "l3"],
          "L2 power-2": ["--exchange-model", "l2"],
          "L2 risk-averse": ["--exchange-model", "l2", "--queue", "riskaverse"],
          "L2 log": ["--exchange-model", "l2", "--queue", "log"]}


def load(path: Path) -> np.ndarray:
    raw = gzip.open(path).read() if path.suffix == ".gz" else path.read_bytes()
    return np.frombuffer(raw[HEADER:], dtype=REC)


def bounds(r: np.ndarray, n: int, seed: int) -> pd.DataFrame:
    """One row per sampled R1: clone price/side/time, R1's fill time, R2's first trade time."""
    ts, typ = r["ts"], r["type"]
    tod = ts % 86_400_000_000
    trades = np.flatnonzero((typ == TRADE) & (r["id"] != 0))
    # Keyed by price too: an ITCH 'C' execution at a price other than the
    # order's display price doesn't consume the displayed queue there.
    first_trade = pd.Series(ts[trades], index=pd.MultiIndex.from_arrays(
        [r["id"][trades], r["price"][trades]])).groupby(level=[0, 1]).min()
    first_trade = {k: v for k, v in first_trade.items()}
    # Fully executed: the Delete comes right after a Trade on the same order.
    dels = np.flatnonzero(typ == DELETE)
    dels = dels[dels > 0]
    ex = dels[(typ[dels - 1] == TRADE) & (r["id"][dels - 1] == r["id"][dels]) & (r["price"][dels - 1] == r["price"][dels])]
    done = pd.Series(ts[ex], index=r["id"][ex])
    adds = np.flatnonzero((typ == ADD) & (tod > 35_100_000_000) & (tod < 56_000_000_000))
    adds = adds[np.isin(r["id"][adds], done.index.values)]
    pick = np.sort(np.random.default_rng(seed).choice(adds, size=min(n, len(adds)), replace=False))
    # Adds per (side, price), to find the next one behind the clone.
    by_level = {}
    for i in np.flatnonzero(typ == ADD):
        by_level.setdefault((r["side"][i], r["price"][i]), []).append(i)
    by_level = {k: np.array(v) for k, v in by_level.items()}
    rows = []
    for i in pick:
        probe_ts = ts[i] + 1
        j = np.searchsorted(ts, probe_ts)              # event the clone reacts to
        if j >= len(ts):
            continue
        place = np.searchsorted(ts, ts[j], side="right") - 1   # it arrives after that microsecond's events
        if done[r["id"][i]] <= ts[j]:
            continue                                   # R1 already gone: nothing to queue behind
        lvl = by_level[(r["side"][i], r["price"][i])]
        k = np.searchsorted(lvl, place, side="right")
        r2 = first_trade.get((r["id"][lvl[k]], r["price"][i]), np.nan) if k < len(lvl) else np.nan
        rows.append((len(rows), r["side"][i], r["price"][i], probe_ts, done[r["id"][i]], r2))
    return pd.DataFrame(rows, columns=["probe", "side", "price", "ts", "r1_done", "r2_first"])


def simulate(path: Path, b: pd.DataFrame, args: list, tmp: Path) -> pd.Series:
    res = {}
    for side, flag in ((0, "buy"), (1, "sell")):
        x = b[b.side == side].sort_values("ts")
        if x.empty:
            continue
        csv, out = tmp / "probes.csv", tmp / "run"
        pd.DataFrame({"probe": x.probe, "ts": x.ts, "price": x.price * 0.01}).to_csv(csv, index=False)
        subprocess.run([str(BIN), "--replay", str(path), "--strategy", "probe", "--probes", str(csv), "--side", flag,
                        "--maker-fee-bps", "0", "--quiet", "--no-book-metrics", "--sample-ms", "600000",
                        "--output", str(out), *args], check=True, capture_output=True)
        m = pd.read_csv(out / "probe_orders.csv")
        f = pd.read_csv(out / "fills.csv").groupby("order_id")["ts"].min()
        m["fill"] = m["order_id"].map(f)
        res.update(dict(zip(m.probe, m.fill)))
    return b.probe.map(res)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--files", nargs="+", required=True)
    ap.add_argument("--n", type=int, default=2000, help="clones per file")
    ap.add_argument("--out", default=str(ROOT / "results" / "itch"))
    a = ap.parse_args()
    rows = []
    for f in map(Path, a.files):
        sym, day = f.name.split("_")[1].split(".")[0], f.name[:10]
        b = bounds(load(f), a.n, seed=7)
        with tempfile.TemporaryDirectory() as t:
            for model, args in MODELS.items():
                fill = simulate(f, b, args, Path(t))
                early = (fill < b.r1_done).sum()
                have2 = b.r2_first.notna()
                late = (have2 & ~(fill <= b.r2_first)).sum()
                rows.append({"day": day, "symbol": sym, "model": model, "clones": len(b), "with_r2": int(have2.sum()),
                             "early": int(early), "late": int(late), "filled": int(fill.notna().sum())})
                print(rows[-1], flush=True)
    df = pd.DataFrame(rows)
    g = df.groupby("model", sort=False)[["clones", "with_r2", "early", "late", "filled"]].sum()
    md = ["# Queue priority against real Nasdaq orders\n", "\n\n".join((__doc__ or "").split("\n\n")[1:5]).strip() + "\n",
          f"{df.symbol.nunique()} symbols, {df.day.nunique()} day(s), {g.clones.iloc[0]:,} clones "
          f"({g.with_r2.iloc[0]:,} with an executed order behind them).\n",
          "| model | impossible fills (before the order ahead finished) | missed fills (order behind traded first) | clones filled |",
          "|---|---|---|---|"]
    for m, x in g.iterrows():
        md.append(f"| {m} | {x.early:,} ({x.early / x.clones:.2%}) | {x.late:,} ({x.late / x.with_r2:.2%}) | {x.filled / x.clones:.1%} |")
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    df.to_csv(out / "clone_validation.csv", index=False)
    (out / "CLONE_VALIDATION.md").write_text("\n".join(md) + "\n")
    print("\n".join(md))
    return 0


if __name__ == "__main__":
    sys.exit(main())
