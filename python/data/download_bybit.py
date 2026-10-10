#!/usr/bin/env python3
"""
download_bybit.py — fetch Bybit's free public historical L2 + trade data.

  Order book (ob200/ob500 WebSocket messages, one JSON per line, 100 ms deltas):
    listed by   https://api2.bybit.com/quote/public/support/download/list-files
    served from https://quote-saver.bycsi.com/orderbook/linear/{SYM}/{DATE}_{SYM}_ob200.data.zip
  Trades (every print, taker side, 0.1 ms timestamps):
    https://public.bybit.com/trading/{SYM}/{SYM}{DATE}.csv.gz

Files are kept compressed; lob_sim reads .zip / .gz directly through a pipe.

Usage:
    python python/data/download_bybit.py --symbol BTCUSDT --start 2026-09-28 --end 2026-10-05
    python python/data/download_bybit.py --symbol ADAUSDT --start 2026-10-01 --end 2026-10-01 --out data/raw/bybit
"""

import argparse
import json
import sys
import urllib.request
from datetime import date, timedelta
from pathlib import Path

LIST_URL = ("https://api2.bybit.com/quote/public/support/download/list-files"
            "?bizType=contract&productId=orderbook&symbols={sym}&interval=daily"
            "&periods=&startDay={start}&endDay={end}")
TRADES_URL = "https://public.bybit.com/trading/{sym}/{sym}{day}.csv.gz"
UA = {"User-Agent": "lob-sim-cpp research downloader"}


def fetch(url: str, dest: Path) -> None:
    if dest.exists() and dest.stat().st_size > 0:
        print(f"  exists  {dest.name}")
        return
    tmp = dest.with_suffix(dest.suffix + ".part")
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
        while chunk := r.read(1 << 20):
            f.write(chunk)
    tmp.rename(dest)
    print(f"  fetched {dest.name} ({dest.stat().st_size / 1e6:.1f} MB)")


def list_book_files(sym: str, start: str, end: str) -> list:
    req = urllib.request.Request(LIST_URL.format(sym=sym, start=start, end=end), headers=UA)
    with urllib.request.urlopen(req, timeout=60) as r:
        payload = json.load(r)
    return payload.get("result", {}).get("list", []) or []


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--symbol", required=True)
    ap.add_argument("--start", required=True, help="YYYY-MM-DD")
    ap.add_argument("--end", required=True, help="YYYY-MM-DD (inclusive)")
    ap.add_argument("--out", default="data/raw/bybit")
    ap.add_argument("--no-trades", action="store_true")
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    files = {f["date"]: f for f in list_book_files(args.symbol, args.start, args.end)}

    d, last = date.fromisoformat(args.start), date.fromisoformat(args.end)
    while d <= last:
        day = d.isoformat()
        print(day)
        if day in files:
            fetch(files[day]["url"], out / files[day]["filename"])
        else:
            print(f"  no order book file listed for {args.symbol} {day}")
        if not args.no_trades:
            try:
                fetch(TRADES_URL.format(sym=args.symbol, day=day), out / f"{args.symbol}{day}.csv.gz")
            except Exception as e:  # noqa: BLE001 — report and continue
                print(f"  trades unavailable: {e}")
        d += timedelta(days=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
