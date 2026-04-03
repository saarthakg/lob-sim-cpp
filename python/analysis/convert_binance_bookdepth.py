#!/usr/bin/env python3
"""
convert_binance_bookdepth.py — Convert Binance historical bookDepth CSV to
the normalized depth_events.csv format expected by the C++ ReplayEngine.

Binance bookDepth format (from data.binance.vision futures/um/daily/bookDepth):
  timestamp, percentage, depth, notional

  - timestamp: datetime string (UTC)
  - percentage: -5, -4, -3, -2, -1, 1, 2, 3, 4, 5  (% from mid price)
  - depth:    cumulative BTC quantity within |percentage|% of mid (bid or ask side)
  - notional: cumulative USD notional within |percentage|% of mid

  negative % = bid side, positive % = ask side.
  Depth is cumulative from mid outward:
    depth at -3% includes depth at -2% and -1%.
  Marginal depth at band [-N%, -(N-1)%] = depth[-N%] - depth[-(N-1)%].
  Price for a band ≈ notional / depth (weighted average price in that band).

Output format:
  ts, event_type, first_update_id, final_update_id, side, price_ticks, qty

Usage:
    python convert_binance_bookdepth.py \\
        --input  data/raw/BTCUSDT-bookDepth-2024-04-05.csv \\
        --output data/recorded/btc-20240405.csv \\
        [--tick-multiplier 100]
"""

import argparse
import csv
import math
from datetime import datetime, timezone
from collections import defaultdict
from pathlib import Path


TICK_MULTIPLIER = 100.0


def parse_timestamp_us(ts_str: str) -> int:
    """Parse '2024-04-05 00:00:08' to microseconds since epoch."""
    dt = datetime.strptime(ts_str, "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc)
    return int(dt.timestamp() * 1_000_000)


def to_ticks(price: float, multiplier: float) -> int:
    return int(round(price * multiplier))


def qty_to_int(qty: float, multiplier: float) -> int:
    return int(round(qty * multiplier))


def process_snapshot(rows: list, tick_multiplier: float, uid: int) -> list:
    """
    Convert one snapshot (all rows at the same timestamp) to a list of
    depth_event CSV row strings.

    Returns list of (ts_us, first_uid, final_uid, side_char, price_ticks, qty_ticks)
    """
    ts_us = rows[0]["ts_us"]

    # Separate bid and ask rows; sort by |percentage| ascending (closest to mid first)
    bids = sorted([r for r in rows if r["pct"] < 0], key=lambda r: r["pct"], reverse=True)  # -1, -2, -3...
    asks = sorted([r for r in rows if r["pct"] > 0], key=lambda r: r["pct"])                # 1, 2, 3...

    result_rows = []

    def emit_levels(levels, side_char):
        prev_depth = 0.0
        prev_notional = 0.0
        for lvl in levels:
            cum_depth    = lvl["depth"]
            cum_notional = lvl["notional"]
            pct          = abs(lvl["pct"])

            marginal_depth    = cum_depth - prev_depth
            marginal_notional = cum_notional - prev_notional

            if marginal_depth > 1e-9:
                avg_price = marginal_notional / marginal_depth
            elif cum_depth > 1e-9:
                avg_price = cum_notional / cum_depth
            else:
                prev_depth    = cum_depth
                prev_notional = cum_notional
                continue

            price_ticks = to_ticks(avg_price, tick_multiplier)
            qty_ticks   = qty_to_int(marginal_depth, tick_multiplier)

            if qty_ticks > 0 and price_ticks > 0:
                result_rows.append((ts_us, uid, uid, side_char, price_ticks, qty_ticks))

            prev_depth    = cum_depth
            prev_notional = cum_notional

    emit_levels(bids, "B")
    emit_levels(asks, "A")

    return result_rows


def convert(input_path: str, output_path: str, tick_multiplier: float):
    Path(output_path).parent.mkdir(parents=True, exist_ok=True)

    # Read all rows and group by timestamp
    snapshots = defaultdict(list)
    with open(input_path, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            ts_str  = row["timestamp"]
            pct     = int(row["percentage"])
            depth   = float(row["depth"])
            notional = float(row["notional"])
            ts_us   = parse_timestamp_us(ts_str)
            snapshots[ts_us].append({"ts_us": ts_us, "pct": pct, "depth": depth, "notional": notional})

    timestamps_sorted = sorted(snapshots.keys())
    print(f"Found {len(timestamps_sorted)} snapshots spanning "
          f"{len(timestamps_sorted) * 30:.0f}s of data")

    uid = 1
    total_rows = 0
    event_count = 0

    with open(output_path, "w", newline="") as fout:
        writer = csv.writer(fout)
        writer.writerow(["ts", "event_type", "first_update_id", "final_update_id",
                          "side", "price_ticks", "qty"])

        # First snapshot: emit all levels as "new" (full book replacement)
        # Subsequent snapshots: first emit deletes for all old levels, then new levels
        prev_bid_prices = set()
        prev_ask_prices = set()

        for ts_us in timestamps_sorted:
            rows = snapshots[ts_us]
            level_rows = process_snapshot(rows, tick_multiplier, uid)

            new_bid_prices = {r[4] for r in level_rows if r[3] == "B"}
            new_ask_prices = {r[4] for r in level_rows if r[3] == "A"}

            # Delete levels that no longer exist in this snapshot
            for px in prev_bid_prices - new_bid_prices:
                writer.writerow([ts_us, "depth", uid, uid, "B", px, 0])
                total_rows += 1
            for px in prev_ask_prices - new_ask_prices:
                writer.writerow([ts_us, "depth", uid, uid, "A", px, 0])
                total_rows += 1

            # Write new/updated levels
            for (ts, first, final, side, price, qty) in level_rows:
                writer.writerow([ts, "depth", first, final, side, price, qty])
                total_rows += 1

            prev_bid_prices = new_bid_prices
            prev_ask_prices = new_ask_prices
            uid += 1
            event_count += 1

            if event_count % 500 == 0:
                print(f"  Processed {event_count} snapshots...")

    print(f"Done. {event_count} snapshots → {total_rows} rows written to {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Convert Binance bookDepth CSV to normalized format")
    parser.add_argument("--input",  required=True, help="Input Binance bookDepth CSV")
    parser.add_argument("--output", required=True, help="Output normalized depth_events.csv")
    parser.add_argument("--tick-multiplier", type=float, default=100.0,
                        help="Price tick multiplier (default: 100)")
    args = parser.parse_args()
    convert(args.input, args.output, args.tick_multiplier)


if __name__ == "__main__":
    main()
