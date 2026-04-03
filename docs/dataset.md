# Dataset Instructions

## Binance Historical Futures bookDepth Data

### Download

Binance publishes free historical market data at [data.binance.vision](https://data.binance.vision).

Browse available files:
```
https://data.binance.vision/?prefix=data/futures/um/daily/bookDepth/BTCUSDT/
```

Download a specific day:
```bash
cd data/raw
wget https://data.binance.vision/data/futures/um/daily/bookDepth/BTCUSDT/BTCUSDT-bookDepth-2024-04-05.zip
unzip BTCUSDT-bookDepth-2024-04-05.zip
```

### Binance bookDepth Format

The downloaded CSV has columns:
```
timestamp,first_update_id,last_update_id,side,update_type,price,qty,pu
```

Where:
- `timestamp`: Unix ms
- `side`: `0` = ask, `1` = bid
- `update_type`: `0` = snapshot level, `1` = diff update
- `price`, `qty`: floating point strings

### Convert to Normalized depth_events.csv

Use the provided conversion script:
```bash
python python/analysis/convert_binance_bookdepth.py \
    --input data/raw/BTCUSDT-bookDepth-2024-04-05.csv \
    --output data/recorded/BTCUSDT-2024-04-05-depth_events.csv
```

Or pass the raw Binance CSV directly (the replay engine handles it if you write a converter).

### Replay

```bash
./build/lob_sim --replay data/recorded/BTCUSDT-2024-04-05-depth_events.csv

./build/lob_sim --strategy mm \
    --replay data/recorded/BTCUSDT-2024-04-05-depth_events.csv \
    --output data/processed/mm_run_btc_20240405
```

---

## Live Capture (Optional / Advanced)

To capture live data from Binance WebSocket:

1. Connect to `wss://stream.binance.com:9443/ws/btcusdt@depth`
2. Fetch snapshot from `https://api.binance.com/api/v3/depth?symbol=BTCUSDT&limit=1000`
3. Run the `BookSync` state machine to synchronize
4. Use `Recorder` to write to `data/recorded/btcusdt_depth_<timestamp>.csv`

The `--live` mode in `lob_sim` implements this flow (requires a WebSocket library
such as `libwebsockets` or `Boost.Beast`, which are not included by default).

---

## Normalized depth_events.csv Schema

This is the format the `ReplayEngine` reads:

```
ts,event_type,first_update_id,final_update_id,side,price_ticks,qty
1712331000123456,depth,123456789,123456790,B,6500012,120
1712331000123456,depth,123456789,123456790,A,6500100,0
```

- `ts`: microseconds since epoch
- `side`: `B` = bid, `A` = ask
- `price_ticks`: `int64_t` (double price × `TICK_MULTIPLIER`)
- `qty = 0`: delete this level
- Multiple rows with the same `(first_update_id, final_update_id)` form one event
