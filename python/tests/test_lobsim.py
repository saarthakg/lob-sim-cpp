import json
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
import pytest

import lobsim
from lobsim import ADD, DELETE, MODIFY, TRADE

ROOT = Path(__file__).resolve().parents[2]
DTYPE = [("ts", "<i8"), ("id", "<u8"), ("price", "<i8"), ("qty", "<i8"), ("aux", "<u8"), ("type", "u1"), ("side", "u1")]
DAY = 100 * 86_400_000_000


def synthetic_day(n=4000, seed=1):
    """A two-sided order-level market that drifts, trades and cancels."""
    rng = np.random.default_rng(seed)
    rec, live, ts, oid = [], {0: [], 1: []}, DAY + 34_200_000_000, 1
    mid = 10_000

    def add(side, price, qty):
        nonlocal oid
        rec.append((ts, oid, price, qty, 0, ADD, side))
        live[side].append([oid, price, qty])
        oid += 1

    for k in range(5):
        for _ in range(5):
            add(0, mid - 1 - k, 100)
            add(1, mid + 1 + k, 100)
    for _ in range(n):
        ts += int(rng.integers(50, 4000))
        side = int(rng.integers(2))
        book = live[side]
        best = max(p for _, p, _ in book) if side == 0 else min(p for _, p, _ in book)
        u = rng.random()
        if u < 0.5 or len(book) < 15:
            add(side, best - int(rng.integers(3)) if side == 0 else best + int(rng.integers(3)), int(rng.integers(1, 4)) * 50)
        elif u < 0.75:   # cancel somewhere
            o = book.pop(int(rng.integers(len(book))))
            rec.append((ts, o[0], o[1], 0, 0, DELETE, side))
        else:            # trade against the front of the best level
            i = next(i for i, o in enumerate(book) if o[1] == best)
            o, q = book[i], min(50, book[i][2])
            rec.append((ts, o[0], best, q, 0, TRADE, 1 - side))
            if q == o[2]:
                book.pop(i)
                rec.append((ts, o[0], best, 0, 0, DELETE, side))
            else:
                o[2] -= q
                rec.append((ts, o[0], best, o[2], 0, MODIFY, side))
    return np.array(rec, dtype=DTYPE)


@pytest.fixture(scope="module")
def day(tmp_path_factory):
    f = tmp_path_factory.mktemp("data") / "day.l3bin"
    lobsim.write_l3(str(f), "SYN", 0.01, 1.0, synthetic_day())
    return f


OPTS = dict(quote_on_book=True, order_size=50, max_position=500, latency_us=100, maker_fee_per_unit=-0.003)


def test_l3_round_trip(day):
    a, b = synthetic_day(), lobsim.read_l3(str(day))
    assert len(a) == len(b)
    for c in ("ts", "id", "price", "qty", "type", "side"):
        assert (a[c] == b[c]).all()
    assert lobsim.instrument(str(day)) == ("SYN", 0.01, 1.0)


def test_builtin_strategy_result_is_consistent(day):
    r = lobsim.backtest(day, "naive", **OPTS)
    assert r["maker_fills"] > 10
    f = r.fills
    assert len(f) == r["maker_fills"] + r["taker_fills"]
    # Rebuild the accounting from the fills alone.
    cash = -(f["side"] * f["price"] * f["qty"]).sum() - f["fee"].sum()
    pos = (f["side"] * f["qty"]).sum()
    assert pos == pytest.approx(r["final_position"])
    assert cash + pos * r["mid_end"] == pytest.approx(r["net_pnl"], abs=1e-6)
    assert f["fee"].sum() == pytest.approx(r["fees"])
    assert r.equity["net_pnl"].size > 0
    assert r.summary["config"]["--order-size"] == "50"


def test_unknown_options_are_errors(day):
    with pytest.raises(RuntimeError, match="did you mean --order-size"):
        lobsim.backtest(day, "naive", order_siez=1)
    with pytest.raises(RuntimeError, match="must be one of"):
        lobsim.backtest(day, "naive", queue="pwoer")
    with pytest.raises(TypeError, match="switch"):
        lobsim.backtest(day, "naive", quote_on_book=1)
    with pytest.raises(RuntimeError, match="unknown strategy"):
        lobsim.backtest(day, "nope")
    with pytest.raises(RuntimeError, match="cannot open"):
        lobsim.backtest("missing.l3bin", "naive")


class PyNaive(lobsim.Strategy):
    """NaiveMarketMaker in Python: the C++ one is the oracle for the callback layer."""

    def __init__(self, qty, max_pos):
        super().__init__()
        self.qty, self.max_pos, self.books, self.trades, self.reports = qty, max_pos, 0, 0, 0

    def on_book(self, ctx):
        self.books += 1
        for side, level, ok in (("buy", ctx.best_bid, ctx.position + self.qty <= self.max_pos),
                                ("sell", ctx.best_ask, ctx.position - self.qty >= -self.max_pos)):
            want = {level[0]} if ok else set()
            have = set()
            for i, o in ctx.orders.items():
                if o["side"] != side or o["cancel_sent"]:
                    continue
                if o["price"] in want:
                    have.add(o["price"])
                else:
                    ctx.cancel(i)
            for p in want - have:
                ctx.limit(side, p, self.qty)

    def on_trade(self, ctx, trade):
        self.trades += 1
        assert trade.side in ("buy", "sell") and trade.qty > 0

    def on_order_update(self, ctx, rep):
        self.reports += 1


def test_python_strategy_matches_the_cpp_one(day):
    s = PyNaive(50, 500)
    # The C++ run's gateway leaves room for one resting order beyond the limit.
    py = lobsim.backtest(day, s, latency_us=100, maker_fee_per_unit=-0.003, max_position=550)
    cpp = lobsim.backtest(day, "naive", **OPTS)
    assert s.books > 1000 and s.trades > 100 and s.reports > 50
    assert py["net_pnl"] == pytest.approx(cpp["net_pnl"], abs=1e-9)
    assert len(py.fills) == len(cpp.fills)
    for c in ("ts", "side", "price", "qty", "queue_ahead_at_entry"):
        assert (py.fills[c] == cpp.fills[c]).all()


def test_exception_in_a_python_strategy_propagates(day):
    class Boom(lobsim.Strategy):
        def on_book(self, ctx):
            raise ValueError("boom")

    with pytest.raises(ValueError, match="boom"):
        lobsim.backtest(day, Boom())


def test_timer_and_lifecycle(day):
    class T(lobsim.Strategy):
        timer_us = 1_000_000

        def __init__(self):
            super().__init__()
            self.log, self.ticks = [], []

        def on_start(self, ctx):
            self.log.append("start")

        def on_timer(self, ctx):
            self.ticks.append(ctx.now)

        def on_finish(self, ctx):
            self.log.append("finish")

    t = T()
    lobsim.backtest(day, t)
    assert t.log == ["start", "finish"]
    assert len(t.ticks) > 3
    assert set(np.diff(t.ticks)) == {1_000_000}


def test_builtin_runs_in_parallel_are_identical(day):
    with ThreadPoolExecutor(4) as ex:
        res = list(ex.map(lambda _: lobsim.backtest(day, "ladder", alpha_micro=1, **OPTS), range(8)))
    assert len({r["net_pnl"] for r in res}) == 1
    assert len({len(r.fills) for r in res}) == 1


def test_book():
    b = lobsim.Book()
    assert b.add(1, "buy", 100, 50) and b.add(2, "buy", 100, 30) and b.add(3, "sell", 102, 10)
    assert not b.add(1, "buy", 100, 5)
    assert b.queue_ahead(2) == 50
    b.modify(1, 20)
    assert b.queue_ahead(2) == 20
    assert b.match("sell", 100, 25) == [(1, 100, 20), (2, 100, 5)]
    assert b.levels("buy") == [(100, 25)]
    assert b.queue_ahead(99) is None
    assert len(b) == 2
    with pytest.raises(ValueError):
        b.add(9, "sideways", 1, 1)


def test_book_apply_matches_a_backtest_source(day):
    b = lobsim.Book()
    b.apply(lobsim.read_l3(str(day)))
    assert b.levels("buy")[0][0] < b.levels("sell")[0][0]


@pytest.mark.parametrize("build", ["build", "build-dev"])
def test_same_numbers_as_the_command_line(day, tmp_path, build):
    exe = ROOT / build / "lob_sim"
    if not exe.exists():
        pytest.skip("CLI not built")
    r = lobsim.backtest(day, "naive", **OPTS)
    args = [str(exe), "--replay", str(day), "--strategy", "naive", "--quote-on-book", "--order-size", "50",
            "--max-position", "500", "--latency-us", "100", "--maker-fee-per-unit", "-0.003", "--quiet",
            "--output", str(tmp_path)]
    subprocess.run(args, check=True)
    s = json.loads((tmp_path / "summary.json").read_text())
    for k in ("net_pnl", "gross_pnl", "maker_fills", "orders_sent", "edge", "max_drawdown"):
        assert s[k] == r[k]
