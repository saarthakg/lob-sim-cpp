"""
nasdaq_confirm.py — the pre-registered test of results/nasdaq/PREREGISTRATION.md.
Run through `nasdaq_study.py --stage confirm`.
"""
import re

import numpy as np
import pandas as pd

from nasdaq_study import (CONFIRM_DAYS, DATA, LATENCIES, NAIVE, OUT, REBATES, SELECT_LATENCY_US, SYMBOLS, ladder_args,
                          portfolio, sweep, tstat)

LADDER = ladder_args({"--levels": 3, "--pull-threshold": 0, "--inventory-skew": 0})   # frozen
T = 3.0


def halted_symbol_days() -> set:
    """(day, symbol) pairs with a trading halt in session, from the extraction log."""
    out, day = set(), None
    for line in (DATA / "extract_log.txt").read_text().splitlines():
        if m := re.match(r"ITCH extract (\S+)", line):
            day = m.group(1)
        elif m := re.match(r"\s+(\S+)\s+records.*halts (\d+)", line):
            if int(m.group(2)) > 0:
                out.add((day, m.group(1)))
    return out


def main(jobs: int) -> int:
    halted = halted_symbol_days()
    have = [(s, d) for d in CONFIRM_DAYS for s in SYMBOLS
            if (DATA / f"{d}_{s}.l3bin.gz").exists() and (d, s) not in halted]
    df = sweep([(s, d, g, l) for s, d in have for g in (NAIVE, LADDER) for l in LATENCIES], jobs)
    df.to_csv(OUT / "confirm.csv", index=False)
    days = df.day.nunique()
    cell = lambda args, lat: df[(df.args == args) & (df.latency_us == lat)]
    per_share = lambda x, col: x.groupby("day")[col].sum() / x.groupby("day").maker_shares.sum() * 100

    n50, l50 = cell(NAIVE, SELECT_LATENCY_US), cell(LADDER, SELECT_LATENCY_US)
    h1 = portfolio(n50, "net_top")
    h2 = portfolio(l50, "net_top")
    h3 = per_share(l50, "mk_5s") - per_share(n50, "mk_5s")
    h4 = per_share(cell(NAIVE, 10), "net_none") - per_share(cell(NAIVE, 10000), "net_none")
    z = cell(NAIVE, 0)
    h5 = z.net_top.sum() / z.maker_shares.sum() * 100
    v = lambda ok: "**supported**" if ok else "**not supported**"
    md = ["# Nasdaq confirmation results\n",
          f"Pre-registered in PREREGISTRATION.md. {days} days, {df.symbol.nunique()} symbols, {len(have)} symbol-days "
          f"(excluded for halts: {sorted(halted & {(d, s) for d in CONFIRM_DAYS for s in SYMBOLS}) or 'none'}).\n",
          "| | Statistic | Mean | t | Days with the predicted sign | Verdict |", "|---|---|---|---|---|---|",
          f"| H1 | naive net at top rebate, $/day | {h1.mean():+,.0f} | {tstat(h1):+.2f} | {(h1 < 0).mean():.0%} | {v(tstat(h1) < -T)} |",
          f"| H2 | ladder net at top rebate, $/day | {h2.mean():+,.0f} | {tstat(h2):+.2f} | {(h2 > 0).mean():.0%} | "
          f"{v(tstat(h2) > T and (h2 > 0).mean() > 0.5)} |",
          f"| H3 | ladder - naive 5 s markout, c/share | {h3.mean():+.3f} | {tstat(h3):+.2f} | {(h3 < 0).mean():.0%} | {v(tstat(h3) < -T)} |",
          f"| H4 | naive net before rebates, 10 µs - 10 ms, c/share | {h4.mean():+.3f} | {tstat(h4):+.2f} | {(h4 > 0).mean():.0%} | {v(tstat(h4) > T)} |",
          f"| H5 | naive net at top rebate, 0 µs, c/share pooled | {h5:+.3f} | — | — | {v(abs(h5) <= 0.10)} |",
          "\n## Latency (pooled, cents per maker share)\n",
          "| latency µs | " + " | ".join(f"{n}: shares/day | edge | 5 s markout | net no rebate | net base | net top"
                                          for n in ("naive", "ladder")) + " |", "|---|" + "---|" * 12]
    for lat in LATENCIES:
        cells = []
        for args in (NAIVE, LADDER):
            x = cell(args, lat)
            sh = max(x.maker_shares.sum(), 1)
            cells.append(f"{sh / days:,.0f} | " + " | ".join(f"{x[c].sum() / sh * 100:+.3f}" for c in
                                                            ("edge", "mk_5s", "net_none", "net_base", "net_top")))
        md.append(f"| {lat:,} | " + " | ".join(cells) + " |")
    md += ["\n## Per symbol, 50 µs (pooled over days, cents per maker share)\n",
           "| symbol | days | naive shares/day | edge | 5 s markout | net no rebate | net top | ladder net top |",
           "|---|---|---|---|---|---|---|---|"]
    for s, x in n50.groupby("symbol", sort=False):
        y = l50[l50.symbol == s]
        c = lambda f, col: f[col].sum() / max(f.maker_shares.sum(), 1) * 100
        md.append(f"| {s} | {x.day.nunique()} | {x.maker_shares.mean():,.0f} | {c(x, 'edge'):+.3f} | {c(x, 'mk_5s'):+.3f} | "
                  f"{c(x, 'net_none'):+.3f} | {c(x, 'net_top'):+.3f} | {c(y, 'net_top'):+.3f} |")
    (OUT / "CONFIRMATION.md").write_text("\n".join(md) + "\n")
    print("\n".join(md))
    return 0
