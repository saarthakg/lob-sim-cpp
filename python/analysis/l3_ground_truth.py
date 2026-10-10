#!/usr/bin/env python3
"""
l3_ground_truth.py — exact queue-position ground truth from an L3 capture.

From a Bitstamp order-level capture (data/capture_bitstamp_l3.py) this
produces two views of the same market:

  1. <stem>.v2.csv — what an L2 simulator sees: the aggregated book,
     conflated to --conflate-ms windows (Bybit's public feed is 100 ms), plus
     every trade print. This is lob_sim's normalized v2 input.
  2. <stem>.truth.csv — hypothetical "probe" orders placed at the best bid and
     best ask every --every-s seconds (at window boundaries, so both views
     agree on the book they were placed into), and the *true* time each would
     have filled, from exact order-level priority:
       * the probe joins behind every order resting at its price;
       * it fills when a trade at its price hits a maker order that is *not*
         ahead of it (the matching engine would have reached the probe first),
         or when a trade prints through its price, or when the opposite best
         quote moves through it.
     Probes are tiny (one lot), never cancelled, and tracked up to --horizon-s.

Probe placement files <stem>.probes_bid.csv / .probes_ask.csv drive the C++
ProbeStrategy, so the *actual* simulator fill model is what gets scored
(analysis/queue_model_validation.py).
"""
import argparse
import json
import math
from collections import OrderedDict
from decimal import Decimal
from pathlib import Path

import pandas as pd


class L3Book:
    def __init__(self):
        self.side = {0: {}, 1: {}}       # 0 bids, 1 asks: price -> OrderedDict(id -> amount)
        self.where = {}                  # id -> (side, price)

    def add(self, s, oid, px, amt):
        lvl = self.side[s].setdefault(px, OrderedDict())
        lvl[oid] = amt
        self.where[oid] = (s, px)

    def change(self, oid, amt):
        if oid not in self.where:
            return
        s, px = self.where[oid]
        if amt <= 0:
            self.delete(oid)
        else:
            self.side[s][px][oid] = amt

    def delete(self, oid):
        loc = self.where.pop(oid, None)
        if not loc:
            return
        s, px = loc
        lvl = self.side[s].get(px)
        if lvl is not None:
            lvl.pop(oid, None)
            if not lvl:
                del self.side[s][px]

    def best(self, s):
        d = self.side[s]
        if not d:
            return None
        return max(d) if s == 0 else min(d)

    def level_qty(self, s, px):
        lvl = self.side[s].get(px)
        return sum(lvl.values()) if lvl else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--capture", required=True)
    ap.add_argument("--out-dir", default=None)
    ap.add_argument("--conflate-ms", type=float, default=100)
    ap.add_argument("--every-s", type=float, default=5)
    ap.add_argument("--horizon-s", type=float, default=300)
    ap.add_argument("--tick", type=float, default=0.01)
    ap.add_argument("--lot", type=float, default=1e-8)
    a = ap.parse_args()

    cap = Path(a.capture)
    out_dir = Path(a.out_dir) if a.out_dir else cap.parent
    stem = out_dir / cap.name.replace(".l3.jsonl", "")
    W = int(a.conflate_ms * 1000)
    H = int(a.horizon_s * 1e6)
    EVERY = int(a.every_s * 1e6)
    q = Decimal(str(a.tick))
    lotq = Decimal(str(a.lot))

    def px_str(p: Decimal) -> str:
        return str(p.quantize(q))

    def qty_str(x: Decimal) -> str:
        return str(x.quantize(lotq))

    book = L3Book()
    out = open(f"{stem}.v2.csv", "w")
    out.write(f"#instrument,{cap.name.split('_')[1].upper()},{a.tick},{a.lot}\n")
    out.write("ts,type,side,price,qty\n")

    snap_us = None
    last_emitted = {0: {}, 1: {}}   # price -> qty as last written to the L2 view
    window_end = None
    probes = []   # dicts with tracking state
    next_probe = None

    def emit_window(ts):
        """Write D rows for every level that changed since the last window."""
        for s, sc in ((0, "B"), (1, "A")):
            cur = {p: sum(l.values()) for p, l in book.side[s].items()}
            prev = last_emitted[s]
            for p in set(cur) | set(prev):
                if cur.get(p, 0) != prev.get(p, 0):
                    out.write(f"{ts},D,{sc},{px_str(p)},{qty_str(cur.get(p, Decimal(0)))}\n")
            last_emitted[s] = cur

    def place_probes(ts):
        for s in (0, 1):
            px = book.best(s)
            if px is None:
                continue
            lvl = book.side[s][px]
            probes.append({"ts": ts, "side": s, "price": px, "ahead": set(lvl.keys()),
                           "ahead_qty": sum(lvl.values()), "fill_ts": None})

    def check_fill(p, ts, kind, tr_px=None, maker=None):
        if p["fill_ts"] is not None or ts > p["ts"] + H:
            return
        s, px = p["side"], p["price"]
        if kind == "trade":
            through = (tr_px < px) if s == 0 else (tr_px > px)
            if through or (tr_px == px and maker is not None and maker not in p["ahead"]):
                p["fill_ts"] = ts
        elif kind == "cross":
            # Only on a settled, uncrossed book: Bitstamp publishes an incoming
            # taker limit order as order_created *before* it matches, so the raw
            # book is transiently crossed by orders that never rest.
            bb, ba = book.best(0), book.best(1)
            if bb is None or ba is None or bb >= ba:
                return
            if (s == 0 and ba <= px) or (s == 1 and bb >= px):
                p["fill_ts"] = ts

    active = []
    with open(cap) as f:
        for line in f:
            rec = json.loads(line)
            if "snapshot" in rec:
                snap = rec["snapshot"]
                snap_us = int(snap["microtimestamp"])
                for s, key in ((0, "bids"), (1, "asks")):
                    rows = sorted(snap[key], key=lambda r: int(r[2]))   # id order = time priority
                    for p_, amt, oid in rows:
                        book.add(s, int(oid), Decimal(p_), Decimal(amt))
                for s, sc in ((0, "B"), (1, "A")):
                    cur = {p: sum(l.values()) for p, l in book.side[s].items()}
                    for p_, qq in cur.items():
                        out.write(f"{snap_us},S,{sc},{px_str(p_)},{qty_str(qq)}\n")
                    last_emitted[s] = cur
                window_end = (snap_us // W + 1) * W
                next_probe = (snap_us // EVERY + 1) * EVERY
                continue
            if snap_us is None:
                continue
            msg = rec["msg"]
            ev = msg.get("event", "")
            d = msg["data"]
            ts = int(d.get("microtimestamp", 0))
            if ts <= snap_us:
                continue
            # close conflation windows (and place probes on boundaries) up to ts
            while ts >= window_end:
                for p in active:
                    check_fill(p, window_end, "cross")
                emit_window(window_end)
                if window_end >= next_probe:
                    n0 = len(probes)
                    place_probes(window_end)
                    active.extend(probes[n0:])
                    next_probe += EVERY
                window_end += W

            if ev == "order_created":
                book.add(int(d["order_type"]), int(d["id"]), Decimal(d["price_str"]), Decimal(d["amount_str"]))
            elif ev == "order_changed":
                book.change(int(d["id"]), Decimal(d["amount_str"]))
            elif ev == "order_deleted":
                book.delete(int(d["id"]))
            elif ev == "trade":
                tr_px = Decimal(d["price_str"])
                aggr_buy = int(d["type"]) == 0
                maker = int(d["sell_order_id"]) if aggr_buy else int(d["buy_order_id"])
                out.write(f"{ts},T,{'B' if aggr_buy else 'A'},{px_str(tr_px)},{qty_str(Decimal(d['amount_str']))}\n")
                passive_side = 1 if aggr_buy else 0
                for p in active:
                    if p["side"] == passive_side:
                        check_fill(p, ts, "trade", tr_px, maker)
                continue
            if len(active) > 2000:
                active = [p for p in active if p["fill_ts"] is None and ts <= p["ts"] + H]
    out.close()

    end_ts = window_end
    rows = []
    for i, p in enumerate(probes):
        censored = p["fill_ts"] is None and p["ts"] + H > end_ts
        rows.append({"probe": i, "ts": p["ts"], "side": "B" if p["side"] == 0 else "A",
                     "price": float(p["price"]), "ahead_qty": float(p["ahead_qty"]),
                     "true_fill_ts": p["fill_ts"], "censored": censored})
    truth = pd.DataFrame(rows)
    truth.to_csv(f"{stem}.truth.csv", index=False)
    for sc in ("B", "A"):
        t = truth[truth.side == sc][["probe", "ts", "price"]]
        t.to_csv(f"{stem}.probes_{'bid' if sc == 'B' else 'ask'}.csv", index=False)
    filled = truth.true_fill_ts.notna().mean()
    print(f"{len(truth)} probes, {filled:.1%} truly filled within {a.horizon_s:.0f}s; wrote {stem}.*")


if __name__ == "__main__":
    main()
