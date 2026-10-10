#!/usr/bin/env python3
"""
capture_bitstamp_l3.py — record Bitstamp's public *order-level* (L3) feed.

Bitstamp is one of the few venues that still publishes every order's
create / change / delete event without authentication (Coinbase's `full`
channel and LOBSTER's samples now need an account). L3 data is the ground
truth for queue position, which L2 simulators can only estimate — see
analysis/queue_model_validation.py.

Writes JSON lines: first a REST snapshot of every resting order
(order_book?group=2), then raw WebSocket messages from live_orders_<pair>
and live_trades_<pair>, each wrapped with the local receive time.

    python python/data/capture_bitstamp_l3.py --pairs btcusd xrpusd --minutes 120 --out data/raw/bitstamp
"""
import argparse
import asyncio
import json
import time
import urllib.request
from pathlib import Path

import websockets

WS = "wss://ws.bitstamp.net"
REST = "https://www.bitstamp.net/api/v2/order_book/{pair}/?group=2"


def snapshot(pair: str) -> dict:
    req = urllib.request.Request(REST.format(pair=pair), headers={"User-Agent": "lob-sim-cpp"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


async def capture(pairs, minutes, out_dir: Path):
    files = {p: open(out_dir / f"bitstamp_{p}_{time.strftime('%Y%m%d_%H%M%S')}.l3.jsonl", "w") for p in pairs}
    deadline = time.time() + minutes * 60
    async with websockets.connect(WS, max_size=None, ping_interval=20) as ws:
        for p in pairs:
            for ch in (f"live_orders_{p}", f"live_trades_{p}"):
                await ws.send(json.dumps({"event": "bts:subscribe", "data": {"channel": ch}}))
        # Snapshots are taken *after* subscribing; the reconstruction drops
        # stream events older than the snapshot's microtimestamp.
        await asyncio.sleep(2)
        for p in pairs:
            snap = await asyncio.to_thread(snapshot, p)
            files[p].write(json.dumps({"recv_us": int(time.time() * 1e6), "snapshot": snap}) + "\n")
        n = 0
        while time.time() < deadline:
            msg = json.loads(await ws.recv())
            ch = msg.get("channel", "")
            pair = ch.rsplit("_", 1)[-1]
            if pair in files and msg.get("event", "").startswith(("order_", "trade")):
                files[pair].write(json.dumps({"recv_us": int(time.time() * 1e6), "msg": msg}) + "\n")
                n += 1
                if n % 50000 == 0:
                    print(f"{n} messages, {int(deadline - time.time())}s left", flush=True)
    for f in files.values():
        f.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pairs", nargs="+", default=["btcusd"])
    ap.add_argument("--minutes", type=float, default=60)
    ap.add_argument("--out", default="data/raw/bitstamp")
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    asyncio.run(capture(a.pairs, a.minutes, out))


if __name__ == "__main__":
    main()
