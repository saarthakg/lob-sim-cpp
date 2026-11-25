#!/usr/bin/env python3
"""
nasdaq_participation.py — post-hoc check of the Nasdaq study's no-impact
assumption: how much of the market's volume did the simulated strategy trade?

Written after the confirmation results were committed. It is not part of the
pre-registered test and changes none of its verdicts.

    python experiments/nasdaq_participation.py
"""
import sys
from pathlib import Path

import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python" / "analysis"))
from itch_clone_validation import TRADE, load  # noqa: E402
from nasdaq_study import CONFIRM_DAYS, DATA, DEV_DAYS, NAIVE, OUT, SYMBOLS  # noqa: E402

START_US, END_US = 34_500_000_000, 57_300_000_000   # 09:35 to 15:55


def session_volume() -> pd.DataFrame:
    f = OUT / "session_volume.csv"
    if not f.exists():
        rows = []
        for d in DEV_DAYS + CONFIRM_DAYS:
            for s in SYMBOLS:
                r = load(DATA / f"{d}_{s}.l3bin.gz")
                tod = r["ts"] % 86_400_000_000
                m = (r["type"] == TRADE) & (tod >= START_US) & (tod < END_US)
                rows.append((d, s, int(r["qty"][m].sum())))
        pd.DataFrame(rows, columns=["day", "symbol", "volume"]).to_csv(f, index=False)
    return pd.read_csv(f)


def main() -> int:
    vol = session_volume()
    md = ["# Participation (post hoc, not pre-registered)\n",
          "Share of Nasdaq's printed session volume that the simulated naive strategy traded. The exchange model assumes "
          "our orders change nothing, which is reasonable at a few percent and is not at tens of percent. Dollar totals "
          "scale with this volume, so read per-share numbers, and read the high-participation symbols with the most "
          "caution. No pre-registered verdict depends on this file.\n"]
    runs = {"development": "dev_latency.csv", "confirmation": "confirm.csv"}
    data = {k: pd.read_csv(OUT / f).query("args == @NAIVE").merge(vol, on=["day", "symbol"]) for k, f in runs.items()}
    share = lambda g: g.maker_shares.sum() / g.volume.sum()
    md += ["| latency µs | " + " | ".join(data) + " |", "|---|---|---|"]
    for lat in sorted(data["confirmation"].latency_us.unique()):
        md.append(f"| {lat:,} | " + " | ".join(f"{share(x[x.latency_us == lat]):.1%}" for x in data.values()) + " |")
    c = data["confirmation"]
    z = c[c.latency_us == 0].groupby("symbol", sort=False)[["net_top", "maker_shares"]].sum()
    per = z.net_top / z.maker_shares * 100
    md += ["\n## Confirmation days by symbol\n",
           "H5 pools by share volume, so it is mostly QQQ and NVDA. By symbol:\n",
           "| symbol | participation at 50 µs | share of pooled 0 µs volume | net at top rebate, 0 µs (c/share) |", "|---|---|---|---|"]
    for s in z.index:
        md.append(f"| {s} | {share(c[(c.latency_us == 50) & (c.symbol == s)]):.1%} | "
                  f"{z.maker_shares[s] / z.maker_shares.sum():.0%} | {per[s]:+.3f} |")
    md.append(f"\nAcross symbols: median {per.median():+.3f}, range {per.min():+.3f} to {per.max():+.3f}, "
              f"{int((per.abs() <= 0.10).sum())} of {len(per)} within ±0.10 c/share, {int((per > 0).sum())} positive.")
    days = c[c.latency_us == 50].groupby("day").net_top.sum()
    md += ["\n## Daily naive net at the top rebate, 50 µs (the series behind H1)\n", "| day | $ |", "|---|---|"]
    md += [f"| {d} | {v:+,.0f} |" for d, v in days.items()]
    (OUT / "PARTICIPATION.md").write_text("\n".join(md) + "\n")
    print("\n".join(md))
    return 0


if __name__ == "__main__":
    sys.exit(main())
