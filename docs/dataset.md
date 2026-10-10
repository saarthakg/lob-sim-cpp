# Data

## Bybit public historical data (primary)

Bybit publishes every day of full L2 depth and every trade for its USDT
perpetuals, with no API key. Together these are the best free source of
L2 + trades that we found (see [research_notes.md](research_notes.md) §6).

| What | URL pattern | Format |
|---|---|---|
| Order book | `https://quote-saver.bycsi.com/orderbook/linear/{SYM}/{DATE}_{SYM}_ob200.data.zip` (listed by `api2.bybit.com/quote/public/support/download/list-files`) | One WebSocket JSON message per line: a snapshot, then 100 ms deltas. Linear contracts were `ob500` until about 2025-08 and `ob200` since. |
| Trades | `https://public.bybit.com/trading/{SYM}/{SYM}{DATE}.csv.gz` | `timestamp,symbol,side,size,price,tickDirection,trdMatchID,grossValue,homeNotional,foreignNotional,RPI`. Timestamps are float seconds; `side` is the taker side. |

Download with:

```bash
python python/data/download_bybit.py --symbol BTCUSDT --start 2026-09-28 --end 2026-10-05
```

- Files stay compressed. `lob_sim` streams `.zip` and `.gz` through `unzip -p` / `gzip -dc`.
- A BTCUSDT day is about 200 MB zipped and 600 MB raw, with roughly 860k book messages and 300k trades.
- It replays in about 2.5 s without a strategy and about 5 s with one.

```bash
./build/lob_sim --stats  data/raw/bybit/2026-10-03_BTCUSDT_ob200.data.zip \
                --trades data/raw/bybit/BTCUSDT2026-10-03.csv.gz
./build/lob_sim --replay data/raw/bybit/2026-10-03_BTCUSDT_ob200.data.zip \
                --trades data/raw/bybit/BTCUSDT2026-10-03.csv.gz --strategy glft --order-size 0.01
```

## Live capture (normalized v2 CSV)

`--live-capture SYMBOL --output file.csv` records Bybit `orderbook.200` and
`publicTrade` (or Binance spot depth plus aggTrade) to:

```
#instrument,BTCUSDT,0.1,0.001
ts,type,side,price,qty
1790985600124000,S,B,84475.9,5.794      S = snapshot level; one run of S rows = one snapshot
1790985600224000,D,B,84475.8,0          D = delta level (qty 0 deletes)
1790985600225000,T,B,84476.0,0.005      T = trade, side = aggressor
```

- Book rows use the matching-engine timestamp.
- Every snapshot message, including a mid-session resnapshot, is recorded.
- Prices and quantities are the venue's exact decimal strings.

**Legacy v1 captures** (`ts,event_type,first_update_id,...`, made before the
overhaul) can still be read, but they are not suitable for strategy
evaluation:
- They contain no snapshot, so a replay starts from an empty book that fills
  in only as levels change. The first recorded state of
  `btc_bybit_2.csv` had a 277-tick spread.
- They contain no trades, so there is no fill evidence except book crosses.
- They store quantity ×100, so 0.001-BTC levels rounded to zero.

## Not usable: Binance `bookDepth`

`data.binance.vision/.../bookDepth/` is **not order-book data**. It holds
cumulative depth within ±1..5% of the price, sampled every 30–60 s.

The removed `convert_binance_bookdepth.py` turned those bands into fake price
levels. Everything replayed from it, including the old `mm_btc_20240405`
runs, was meaningless.

Binance publishes no historical L2 diffs. The alternatives are:
- self-recording `@depth@100ms`
- Tardis.dev's free first-of-month `incremental_book_L2` files

## Symbol selection

`lob_sim --stats` prints the spread distribution and the arithmetic that
decides whether passive quoting can work at a given fee:

```
Break-even  quoted spread X bps vs 2 x maker fee 4.00 bps -> ±Y bps per round trip
```

Across 24 liquid Bybit perpetuals on 2026-10-01, only large-tick contracts
have a one-tick spread wider than two VIP0 maker fees:

| Symbol | 1 tick | Spread − 2×fee |
|---|---|---|
| ATOMUSDT | 5.9 bp | +2.0 bp |
| XLMUSDT | 4.6 bp | +0.5 bp |
| ADAUSDT | 4.1 bp | +0.04 bp |

Every other symbol, BTC and ETH included, is negative before any adverse
selection. For example, BTCUSDT's spread is 1 tick ($0.10) 100% of the time,
which is 0.012 bp.
