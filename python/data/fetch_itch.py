#!/usr/bin/env python3
"""
fetch_itch.py — stream Nasdaq's free TotalView-ITCH 5.0 sample days into
per-symbol .l3bin.gz files.

A day is 3-18 GB compressed, so nothing raw is stored: the download is piped
through gzip into `lob_sim --extract-itch -`, and only the chosen symbols'
order-level events are kept (a few MB each, gzipped).

    python data/fetch_itch.py --days 2019-12-30 2025-12-08 --symbols INTC CSCO
    python data/fetch_itch.py --list
"""
import argparse
import http.client
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "lob_sim"
BASE = "https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/"

# Every full ITCH 5.0 day listed on the server on 2026-11-25. 2025-11-28 is a
# half day (13:00 close) and is left out.
FILES = {
    "2019-01-30": "01302019.NASDAQ_ITCH50.gz",
    "2019-03-27": "03272019.NASDAQ_ITCH50.gz",
    "2019-07-30": "07302019.NASDAQ_ITCH50.gz",
    "2019-08-30": "08302019.NASDAQ_ITCH50.gz",
    "2019-10-18": "S101819-v50.txt.gz",
    "2019-10-30": "10302019.NASDAQ_ITCH50.gz",
    "2019-12-30": "12302019.NASDAQ_ITCH50.gz",
    "2020-01-30": "01302020.NASDAQ_ITCH50.gz",
    "2021-07-13": "S071321-v50.txt.gz",
    "2021-08-13": "S081321-v50.txt.gz",
    "2025-12-08": "S120825-v50.txt.gz",
    "2025-12-09": "S120925-v50.txt.gz",
    "2025-12-10": "S121025-v50.txt.gz",
    "2025-12-11": "S121125-v50.txt.gz",
    "2025-12-12": "S121225-v50.txt.gz",
    "2026-05-15": "itch50_05_15.gz",
    "2026-05-18": "itch50_05_18.gz",
    "2026-06-12": "S061226-v50.txt.gz",
}
SYMBOLS = ["SIRI", "INTC", "CSCO", "CMCSA", "AMD", "MU", "F", "BAC", "PFE", "T", "AAPL", "MSFT", "QQQ", "NVDA"]


def pump(url: str, sink, attempts: int = 60) -> None:
    """Stream url into sink, resuming by byte range after a dropped connection.

    curl --retry restarts a transfer from byte 0, which into a pipe would
    append a second copy; here a retry continues at the exact next byte.
    Stops quietly when the reader closes the pipe (it has seen 16:00)."""
    pos, total, failures = 0, None, 0
    while total is None or pos < total:
        try:
            req = urllib.request.Request(url, headers={"Range": f"bytes={pos}-"})
            with urllib.request.urlopen(req, timeout=60) as r:
                if r.status != 206 or not r.headers["Content-Range"].startswith(f"bytes {pos}-"):
                    raise RuntimeError(f"server ignored the byte range (status {r.status})")
                total = int(r.headers["Content-Range"].rsplit("/", 1)[1])
                while chunk := r.read(1 << 20):
                    try:
                        sink.write(chunk)
                    except BrokenPipeError:
                        return
                    pos += len(chunk)
                    failures = 0
        except (OSError, http.client.HTTPException) as e:
            failures += 1
            if failures >= attempts:
                print(f"giving up at byte {pos}: {e}", flush=True)
                return
            time.sleep(min(60, 2 ** failures))


def fetch(day: str, symbols: list, out: Path) -> bool:
    missing = [s for s in symbols if not (out / f"{day}_{s}.l3bin.gz").exists()]
    if not missing:
        print(f"{day}: already extracted", flush=True)
        return True
    local = ROOT / "data" / "raw" / "itch" / FILES[day]
    src = str(local) if local.exists() else "-"
    # Work in a private directory and rename finished files into place, so a
    # concurrent or interrupted run can never leave a partial file behind.
    tmp = Path(tempfile.mkdtemp(prefix=f".{day}_", dir=out))
    args = [str(BIN), "--extract-itch", src, "--date", day, "--symbols", ",".join(missing), "--output", str(tmp)]
    if src == "-":
        gz = subprocess.Popen(["gzip", "-dc"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)

        def feed():
            pump(BASE + FILES[day], gz.stdin)
            try:
                gz.stdin.close()
            except BrokenPipeError:
                pass   # extractor finished at 16:00 and closed the pipe

        t = threading.Thread(target=feed, daemon=True)
        t.start()
        r = subprocess.run(args, stdin=gz.stdout, capture_output=True, text=True)
        gz.stdout.close()
        t.join()
        gz.wait()
    else:
        r = subprocess.run(args, capture_output=True, text=True)
    print(r.stdout + r.stderr, flush=True)
    if r.returncode != 0:
        print(f"{day}: FAILED (rc {r.returncode}); nothing kept", flush=True)
        shutil.rmtree(tmp)
        return False
    for f in sorted(tmp.glob("*.l3bin")):
        subprocess.run(["gzip", "-1", str(f)], check=True)
        os.replace(f"{f}.gz", out / f"{f.name}.gz")
    shutil.rmtree(tmp)
    with open(out / "extract_log.txt", "a") as log:
        log.write(r.stdout)
    return True


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--days", nargs="+", default=list(FILES))
    ap.add_argument("--symbols", nargs="+", default=SYMBOLS)
    ap.add_argument("--out", default=str(ROOT / "data" / "processed" / "itch_l3"))
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    if a.list:
        for d, f in FILES.items():
            print(d, BASE + f)
        return 0
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    ok = [fetch(d, a.symbols, out) for d in a.days]
    return 0 if all(ok) else 1


if __name__ == "__main__":
    sys.exit(main())
