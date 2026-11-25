"""lobsim — limit-order-book backtesting on a C++ engine.

    import lobsim

    # A built-in strategy; options are the command line's, with underscores.
    r = lobsim.backtest("2019-12-30_INTC.l3bin.gz", "naive", quote_on_book=True, order_size=100,
                        max_position=500, latency_us=50, maker_fee_per_unit=-0.003,
                        session_start="09:35:00", session_end="15:55:00")
    r.summary["net_pnl"], r.fills["price"], r.equity["net_pnl"]

    # Your own strategy. Prices are ticks, sizes are lots.
    class Join(lobsim.Strategy):
        def on_book(self, ctx):
            want = ctx.best_bid[0]
            mine = [(i, o) for i, o in ctx.orders.items() if not o["cancel_sent"]]
            for i, o in mine:
                if o["price"] != want:
                    ctx.cancel(i)
            if not mine:
                ctx.limit("buy", want, 100)

    lobsim.backtest("day.l3bin.gz", Join(), latency_us=50)

An unknown option is an error, never ignored. Built-in strategies release the
GIL, so a thread pool runs them in parallel. Python strategies cost roughly a
microsecond per callback.
"""
import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Union

import numpy as np

from ._lobsim import Book, Context, Report, Strategy, Trade, instrument, is_flag, read_l3, write_l3
from ._lobsim import run as _run

__all__ = ["backtest", "Result", "Strategy", "Context", "Report", "Trade", "Book", "read_l3", "write_l3", "instrument",
           "ADD", "DELETE", "MODIFY", "TRADE", "FILL_CAUSES"]

ADD, DELETE, MODIFY, TRADE = 1, 2, 3, 4                       # L3 record types
FILL_CAUSES = ["none", "taker", "trade_at_price", "trade_through", "book_cross"]   # Result.fills["cause"]


@dataclass
class Result:
    """What a backtest produced. Money is quote currency, sizes are base units."""
    summary: dict                                  #: everything in summary.json, options included under "config"
    fills: np.ndarray = field(repr=False)          #: structured array, one row per fill
    equity: np.ndarray = field(repr=False)         #: structured array, one row per sample interval

    def __getitem__(self, key: str) -> Any:
        return self.summary[key]


def _options(kwargs: dict) -> dict:
    out = {}
    for k, v in kwargs.items():
        name = "--" + k.replace("_", "-")
        if is_flag(name):
            if not isinstance(v, (bool, np.bool_)):
                raise TypeError(f"{k} is a switch: pass True or False, not {v!r}")
            if v:
                out[name] = ""
        elif v is not None:
            # repr keeps every digit of a float (str(np.float64) would too, but not its repr).
            out[name] = repr(float(v)) if isinstance(v, (float, np.floating)) else str(v)
    return out


def backtest(book: Union[str, Path], strategy: Union[str, Strategy] = "naive", *, trades: Union[str, Path, None] = None,
             output: Union[str, Path, None] = None, equity: bool = True, **options) -> Result:
    """Run one backtest.

    book      .l3bin(.gz) order-level file (exact queue priority) or Bybit ob200 book file
    strategy  a built-in name (naive, glft, ladder, imbalance, twap, exec) or a Strategy instance
    trades    trades file for a Bybit book
    output    directory for the CLI's CSV and JSON files (default: write nothing)
    equity    keep the equity curve in the result
    options   any lob_sim option, e.g. order_size=100, latency_us=50, queue="log", no_cross_fill=True
    """
    if output is None:
        options.setdefault("no_book_metrics", True)
    js, fills, eq = _run(str(book), str(trades or ""), strategy, _options(options), str(output or ""), equity)
    return Result(json.loads(js), fills, eq)
