#!/bin/bash
# latency_sweep.sh — run MarketMaker at different simulated latencies and collect PnL.
#
# Run from the repo root:
#   bash python/experiments/latency_sweep.sh
#
# Results land in data/processed/latency_<N>us/.
# After running, visualise with:
#   python python/analysis/latency_sensitivity.py \
#       --runs data/processed/latency_0us data/processed/latency_1000us \
#              data/processed/latency_5000us data/processed/latency_10000us \
#              data/processed/latency_50000us data/processed/latency_100000us \
#       --save

set -euo pipefail

REPLAY="data/recorded/btc_bybit_2.csv"
BUILD="./build/lob_sim"

if [[ ! -f "$BUILD" ]]; then
    echo "ERROR: $BUILD not found. Build the project first: cmake --build build"
    exit 1
fi

if [[ ! -f "$REPLAY" ]]; then
    echo "ERROR: $REPLAY not found. Capture live data first or update REPLAY path."
    exit 1
fi

for LATENCY in 0 1000 5000 10000 50000 100000; do
    DIR="data/processed/latency_${LATENCY}us"
    echo "── Latency ${LATENCY} µs → $DIR"
    "$BUILD" --strategy mm --replay "$REPLAY" --output "$DIR" --latency-us "$LATENCY"
    LAST=$(tail -1 "$DIR/pnl.csv" 2>/dev/null || echo "(no pnl.csv)")
    echo "   Last PnL row: $LAST"
done

echo ""
echo "Done. Run the sensitivity analysis with:"
echo "  python python/analysis/latency_sensitivity.py \\"
echo "      --runs data/processed/latency_0us data/processed/latency_1000us \\"
echo "             data/processed/latency_5000us data/processed/latency_10000us \\"
echo "             data/processed/latency_50000us data/processed/latency_100000us \\"
echo "      --save"
