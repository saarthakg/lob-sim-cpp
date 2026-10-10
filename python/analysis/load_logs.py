"""
load_logs.py — load lob_sim run outputs (see cpp/src/sim/backtester.cpp).

  summary.json      run totals and PnL attribution
  fills.csv         one row per fill, with mid at fill and at +100ms..+300s
  equity.csv        sampled PnL / position (default every 1 s)
  book_metrics.csv  sampled top-of-book and imbalance features

All prices are in real quote-currency units and quantities in base units.
"""
import json
from pathlib import Path

import numpy as np
import pandas as pd

MARKOUT_COLS = ["mid_100ms", "mid_1s", "mid_5s", "mid_30s", "mid_60s", "mid_300s"]
MARKOUT_SECONDS = [0.1, 1, 5, 30, 60, 300]


def load_summary(run_dir) -> dict:
    return json.loads((Path(run_dir) / "summary.json").read_text())


def _ts(df: pd.DataFrame) -> pd.DataFrame:
    df["ts_us"] = df["ts"]
    df["time"] = pd.to_datetime(df["ts"], unit="us", utc=True)
    return df


def load_fills(run_dir) -> pd.DataFrame:
    df = _ts(pd.read_csv(Path(run_dir) / "fills.csv"))
    df["sign"] = np.where(df["side"] == "B", 1.0, -1.0)
    return df


def load_equity(run_dir) -> pd.DataFrame:
    return _ts(pd.read_csv(Path(run_dir) / "equity.csv"))


def load_book_metrics(run_dir) -> pd.DataFrame:
    return _ts(pd.read_csv(Path(run_dir) / "book_metrics.csv"))


def markout_bps(fills: pd.DataFrame) -> pd.DataFrame:
    """Qty-weighted markout in bp of price per horizon: sign·(mid_h − price)/price.
    Horizon 0 is the edge at fill (vs. the mid at the fill instant)."""
    rows = []
    w = fills["qty"]
    for h, col in [(0.0, "mid_at_fill")] + list(zip(MARKOUT_SECONDS, MARKOUT_COLS)):
        m = fills["sign"] * (fills[col] - fills["price"]) / fills["price"] * 1e4
        ok = m.notna()
        rows.append({"horizon_s": h, "bps": float((m[ok] * w[ok]).sum() / w[ok].sum()) if ok.any() else np.nan})
    return pd.DataFrame(rows)
