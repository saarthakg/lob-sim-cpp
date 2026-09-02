"""
load_logs.py — helpers to load simulator output CSVs into pandas DataFrames.

Usage:
    from load_logs import load_book_metrics, load_fills, load_pnl
    metrics = load_book_metrics("data/processed/run_20240405_120000")
"""

import pandas as pd
from pathlib import Path

TICK_MULTIPLIER = 100.0  # must match C++ TICK_MULTIPLIER


def _run_path(run_dir: str) -> Path:
    return Path(run_dir)


def load_book_metrics(run_dir: str) -> pd.DataFrame:
    """Load book_metrics.csv. Converts tick columns to float prices."""
    path = _run_path(run_dir) / "book_metrics.csv"
    df = pd.read_csv(path, dtype={
        "ts": "int64",
        "best_bid_ticks": "int64",
        "best_bid_qty": "int64",
        "best_ask_ticks": "int64",
        "best_ask_qty": "int64",
        "spread_ticks": "int64",
        "mid_ticks": "int64",
        "imbalance_5": "float64",
    })
    df["ts_us"] = df["ts"]
    df["ts"] = pd.to_datetime(df["ts"], unit="us", utc=True)
    df["best_bid"] = df["best_bid_ticks"] / TICK_MULTIPLIER
    df["best_ask"] = df["best_ask_ticks"] / TICK_MULTIPLIER
    df["spread"]   = df["spread_ticks"]   / TICK_MULTIPLIER
    df["mid"]      = df["mid_ticks"]      / TICK_MULTIPLIER
    return df


def load_fills(run_dir: str) -> pd.DataFrame:
    """Load fills.csv. Converts fill_price_ticks to float price."""
    path = _run_path(run_dir) / "fills.csv"
    df = pd.read_csv(path, dtype={
        "ts": "int64",
        "order_id": "int64",
        "strategy_name": "str",
        "side": "str",
        "order_type": "str",
        "fill_price_ticks": "int64",
        "fill_qty": "int64",
        "fee": "float64",
        "status": "str",
    })
    df["ts_us"] = df["ts"]
    df["ts"] = pd.to_datetime(df["ts"], unit="us", utc=True)
    df["fill_price"] = df["fill_price_ticks"] / TICK_MULTIPLIER
    return df


def load_pnl(run_dir: str) -> pd.DataFrame:
    """Load pnl.csv."""
    path = _run_path(run_dir) / "pnl.csv"
    df = pd.read_csv(path, dtype={
        "ts": "int64",
        "position": "int64",
        "cash": "float64",
        "realized_pnl": "float64",
        "unrealized_pnl": "float64",
        "mark_mid_ticks": "int64",
        "total_pnl": "float64",
    })
    df["ts_us"] = df["ts"]
    df["ts"] = pd.to_datetime(df["ts"], unit="us", utc=True)
    df["mark_mid"] = df["mark_mid_ticks"] / TICK_MULTIPLIER
    return df
