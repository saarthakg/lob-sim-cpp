#!/usr/bin/env python3
"""
summarize_study.py — turn results/study/*/results.csv into the README tables
and figures.

Out-of-sample numbers only: every row below comes from TEST days, with
parameters chosen on TRAIN days (see study.py).

    python experiments/summarize_study.py   # writes results/study/SUMMARY.md + docs/plots/*.png
"""
import re
import sys
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python" / "analysis"))
import style  # noqa: E402

STUDY = ROOT / "results" / "study"
PLOTS = ROOT / "docs" / "plots"
FEE_BP = {"vip0": 2.0, "zero": 0.0, "rebate": -0.5}
FEE_LABEL = {"vip0": "VIP0 (+2.0 bp)", "zero": "0 bp", "rebate": "rebate (−0.5 bp)"}
NAME_RE = re.compile(r"^(?P<sym>[A-Z0-9]+)_(?P<strat>naive|glft|ladder)_(?P<fee>vip0|zero|rebate)(?P<tag>_.*)?$")


def load():
    rows = []
    for d in sorted(STUDY.iterdir()):
        m = NAME_RE.match(d.name)
        if not m or not (d / "results.csv").exists():
            continue
        df = pd.read_csv(d / "results.csv")
        df = df[df["split"] == "test"].copy()
        for k, v in m.groupdict().items():
            df[k] = v or ""
        rows.append(df)
    return pd.concat(rows, ignore_index=True)


def per_config(df: pd.DataFrame) -> pd.DataFrame:
    g = df.groupby(["sym", "strat", "fee", "tag", "params"])
    out = pd.DataFrame({
        "days": g.size(),
        "net_mean": g["net_pnl"].mean(),
        "net_std": g["net_pnl"].std(),
        "pct_days_pos": g["net_pnl"].apply(lambda s: (s > 0).mean() * 100),
        "net_total": g["net_pnl"].sum(),
        "gross_total": g["gross_pnl"].sum(),
        "fees_total": g["fees"].sum(),
        "notional": g["notional"].sum(),
        "fills_per_day": g["maker_fills"].mean() + g["taker_fills"].mean(),
        "edge": g["edge"].sum(),
        "mk5": g["markout_5s"].sum(),
        "max_dd": g["max_drawdown"].max(),
    }).reset_index()
    n = out["notional"].replace(0, np.nan)
    out["net_bps"] = out["net_total"] / n * 1e4
    out["gross_bps"] = out["gross_total"] / n * 1e4
    out["edge_bps"] = out["edge"] / n * 1e4
    out["mk5_bps"] = out["mk5"] / n * 1e4
    # For passive strategies net = gross − fee·notional, so the maker fee at
    # which a strategy breaks even is its gross bp (fill set held fixed).
    out["breakeven_maker_fee_bp"] = out["gross_bps"]
    # t-stat of mean daily net PnL (4 days: illustrative only)
    out["t_stat"] = out["net_mean"] / (out["net_std"] / np.sqrt(out["days"]))
    return out


def fmt_table(df: pd.DataFrame, cols, headers) -> str:
    lines = ["| " + " | ".join(headers) + " |", "|" + "---|" * len(headers)]
    for _, r in df.iterrows():
        cells = []
        for c in cols:
            v = r[c]
            if isinstance(v, float):
                cells.append("—" if np.isnan(v) else f"{v:,.2f}")
            else:
                cells.append(str(v))
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines)


def main():
    style.apply()
    PLOTS.mkdir(parents=True, exist_ok=True)
    df = load()
    cfg = per_config(df)
    main_ = cfg[cfg["tag"].isin(["", "_ext"])].copy()
    main_["strat"] = main_["strat"] + main_["tag"].str.replace("_ext", " (ext grid)")
    main_["fee_label"] = main_["fee"].map(FEE_LABEL)
    main_ = main_.sort_values(["sym", "fee", "strat"])

    md = ["# Walk-forward study — out-of-sample (test-day) results\n",
          "Parameters selected on 4 training days (2026-09-28 → 10-01), evaluated once on 4 test days "
          "(2026-10-02 → 10-05). Bybit USDT perpetuals, public ob200 + trade data. Taker fee 5.5 bp throughout.\n"]
    md.append(fmt_table(main_, ["sym", "strat", "fee_label", "params", "net_mean", "pct_days_pos", "net_bps",
                                "gross_bps", "edge_bps", "mk5_bps", "fills_per_day", "max_dd"],
                        ["symbol", "strategy", "maker fee", "selected params", "net $/day", "% days > 0",
                         "net bp", "gross bp (= break-even maker fee)", "edge at fill bp",
                         "markout 5s bp", "fills/day", "max DD $"]))
    sens = cfg[~cfg["tag"].isin(["", "_ext"])].copy()
    if len(sens):
        md.append("\n\n## Sensitivity of the selected configurations (test days)\n")
        md.append(fmt_table(sens.sort_values(["sym", "fee", "tag"]),
                            ["sym", "strat", "fee", "tag", "net_mean", "net_bps", "gross_bps", "fills_per_day"],
                            ["symbol", "strategy", "fee", "variant", "net $/day", "net bp", "gross bp",
                             "fills/day"]))
    (STUDY / "SUMMARY.md").write_text("\n".join(md) + "\n")
    cfg.to_csv(STUDY / "summary_table.csv", index=False)
    print("\n".join(md))

    # Figure 1: gross bp (= break-even maker fee) per symbol/strategy vs fee tiers.
    piv = main_[(main_["fee"] == "vip0") & (main_["tag"] == "")].pivot_table(index="sym", columns="strat", values="gross_bps")
    piv = piv.reindex(columns=[c for c in ["naive", "glft", "ladder"] if c in piv.columns])
    fig, ax = plt.subplots(figsize=(9, 4.2))
    xs = np.arange(len(piv.index))
    w = 0.8 / max(1, len(piv.columns))
    for i, c in enumerate(piv.columns):
        ax.bar(xs + i * w - 0.4 + w / 2, piv[c].values, width=w - 0.02, label=c, color=style.SERIES[i])
    for fee, ls in [("vip0", "--"), ("rebate", ":")]:
        ax.axhline(-FEE_BP[fee], color=style.TEXT_2, linestyle=ls, linewidth=1,
                   label=f"needed to break even at {FEE_LABEL[fee]}")
    ax.axhline(0, color=style.TEXT, linewidth=0.6)
    ax.set_xticks(xs)
    ax.set_xticklabels(piv.index)
    ax.set_ylabel("gross PnL, bp of traded notional")
    ax.set_title("Out-of-sample gross edge before fees (VIP0-selected configs, test days)")
    ax.legend(loc="lower left", ncol=2)
    fig.tight_layout()
    fig.savefig(PLOTS / "study_gross_edge.png", dpi=140)

    # Figure 2: latency sensitivity (net bp) for selected configs.
    lat = sens[sens["tag"].str.startswith("_lat")].copy()
    if len(lat):
        lat["latency_ms"] = lat["tag"].str.extract(r"_lat(\d+)").astype(float) / 1000
        fig, ax = plt.subplots(figsize=(9, 4.2))
        for i, ((sym, fee), g) in enumerate(lat.groupby(["sym", "fee"])):
            g = g.sort_values("latency_ms")
            ax.plot(g["latency_ms"].clip(lower=0.1), g["gross_bps"], marker="o", markersize=4,
                    label=f"{sym} {g['strat'].iloc[0]} ({fee})", color=style.SERIES[i % 8])
        ax.set_xscale("log")
        ax.axhline(0, color=style.TEXT, linewidth=0.6)
        ax.set_xlabel("one-way latency on feed, entry and response (ms; 0 plotted at 0.1)")
        ax.set_ylabel("gross PnL, bp of notional")
        ax.set_title("Latency sensitivity of selected configurations (test days)")
        ax.legend(loc="best")
        fig.tight_layout()
        fig.savefig(PLOTS / "study_latency.png", dpi=140)
    print(f"\nwrote {STUDY / 'SUMMARY.md'} and plots in {PLOTS}")


if __name__ == "__main__":
    main()
