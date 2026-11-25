#!/usr/bin/env python3
"""Figures for the Nasdaq and execution studies -> docs/plots/."""
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
PLOTS = ROOT / "docs" / "plots"
BLUE, ORANGE, AQUA = "#257de0", "#f47230", "#00b894"
INK, MUTED, GRID = "#191919", "#666666", "#e6e6e6"


def style(ax):
    ax.set_facecolor("white")
    for s in ("top", "right", "left"):
        ax.spines[s].set_visible(False)
    ax.spines["bottom"].set_color("#bbbbbb")
    ax.tick_params(colors=MUTED, length=0, labelsize=9)
    ax.set_axisbelow(True)


def latency():
    n = ROOT / "results" / "nasdaq"
    files = [("development days, 2019-21", "dev_latency.csv", BLUE, "o"),
             ("confirmation days, 2025-26", "confirm.csv", ORANGE, "s")]
    fig, ax = plt.subplots(figsize=(8.2, 4.4), dpi=150)
    lats, ends = None, []
    for label, f, color, marker in files:
        if not (n / f).exists():
            continue
        d = pd.read_csv(n / f)
        d = d[d.args.str.contains("naive")]
        g = d.groupby("latency_us")[["net_none", "maker_shares"]].sum()
        y = g.net_none / g.maker_shares * 100
        lats = list(y.index)
        ax.plot(range(len(y)), y.values, color=color, lw=2, marker=marker, ms=7, mec="white", mew=1.5, label=label, zorder=3)
        ends.append((y.values[-1], label))
    for level, text in ((-0.13, "base rebate (Tape C) breaks even"), (-0.30, "top rebate breaks even")):
        ax.axhline(level, color=MUTED, lw=1, ls=(0, (4, 3)), zorder=1)
        ax.text(len(lats) - 0.9, level + 0.008, text, ha="right", va="bottom", fontsize=8.5, color=MUTED)
    ax.set_xticks(range(len(lats)))
    ax.set_xticklabels([f"{l:,} µs" if l < 1000 else f"{l // 1000} ms" for l in lats])
    ax.set_ylim(-0.85, 0.0)
    ax.grid(axis="y", color=GRID, lw=0.8)
    ax.set_xlabel("each-way latency", color=MUTED, fontsize=9)
    ax.set_ylabel("cents per share, before rebates", color=MUTED, fontsize=9)
    ax.set_title("Joining the touch on Nasdaq: net per share against latency", loc="left", color=INK, fontsize=11.5, pad=12)
    ax.legend(frameon=False, fontsize=9, labelcolor=INK, loc="lower left")
    style(ax)
    fig.tight_layout()
    fig.savefig(PLOTS / "nasdaq_latency.png")


def execution():
    d = pd.read_csv(ROOT / "results" / "execution" / "summary.csv")
    algos = [("join", BLUE), ("peg", ORANGE), ("smart", AQUA)]
    syms = list(dict.fromkeys(d.symbol))
    fig, ax = plt.subplots(figsize=(8.2, 3.8), dpi=150)
    h = 0.24
    for j, (a, color) in enumerate(algos):
        for i, s in enumerate(syms):
            v = d[(d.symbol == s) & (d.algo == a)].saving_bps.iloc[0]
            y = i + (j - 1) * (h + 0.03)
            ax.barh(y, v, height=h, color=color, label=a if i == 0 else None, zorder=2)
            ax.text(v + 0.05, y, f"{v:.1f}", va="center", fontsize=8.5, color=INK)
    ax.set_yticks(range(len(syms)))
    ax.set_yticklabels(syms, color=INK)
    ax.invert_yaxis()
    ax.set_xlabel("bp saved per parent order against crossing the spread (fees included)", color=MUTED, fontsize=9)
    ax.set_title("Passive execution beats crossing on every symbol (50 days, 12,000 parents each)", loc="left", color=INK,
                 fontsize=11.5, pad=12)
    ax.grid(axis="x", color=GRID, lw=0.8)
    ax.legend(frameon=False, fontsize=9, labelcolor=INK, loc="center right")
    ax.set_xlim(0, 5.0)
    style(ax)
    fig.tight_layout()
    fig.savefig(PLOTS / "execution_savings.png")


if __name__ == "__main__":
    latency()
    execution()
    sys.exit(0)
