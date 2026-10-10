# Walk-forward study — out-of-sample (test-day) results

Parameters selected on 4 training days (2026-09-28 → 10-01), evaluated once on 4 test days (2026-10-02 → 10-05). Bybit USDT perpetuals, public ob200 + trade data. Taker fee 5.5 bp throughout.

| symbol | strategy | maker fee | selected params | net $/day | % days > 0 | net bp | gross bp (= break-even maker fee) | edge at fill bp | markout 5s bp | fills/day | max DD $ |
|---|---|---|---|---|---|---|---|---|---|---|---|
| ADAUSDT | glft | rebate (−0.5 bp) | alpha-scale=1 levels=3 half-spread-mult=2 depletion-pull=0.3 | -36.76 | 0.00 | -1.15 | -1.65 | 2.01 | -1.96 | 2,869.25 | 102.81 |
| ADAUSDT | glft (ext grid) | rebate (−0.5 bp) | alpha-scale=0 levels=1 half-spread-mult=8 depletion-pull=0.3 | 8.60 | 25.00 | 15.78 | 15.28 | 9.29 | -4.24 | 138.00 | 61.12 |
| ADAUSDT | ladder | rebate (−0.5 bp) | alpha-scale=2 pull-threshold=0.05 levels=2 depletion-pull=0.3 inventory-skew=0 | -124.66 | 0.00 | -2.51 | -3.01 | 0.55 | -1.92 | 3,623.50 | 261.54 |
| ADAUSDT | ladder (ext grid) | rebate (−0.5 bp) | alpha-scale=5 pull-threshold=0.05 levels=4 depletion-pull=0.3 inventory-skew=0 | -90.19 | 25.00 | -1.62 | -2.12 | 1.34 | -1.93 | 4,082.25 | 219.18 |
| ADAUSDT | naive | rebate (−0.5 bp) | default | -551.72 | 0.00 | -1.27 | -1.77 | -0.54 | -1.80 | 31,435.62 | 966.24 |
| ADAUSDT | glft | VIP0 (+2.0 bp) | alpha-scale=1 levels=3 half-spread-mult=2 depletion-pull=0 | -47.06 | 0.00 | -3.85 | -1.85 | 3.97 | -2.67 | 1,503.50 | 114.07 |
| ADAUSDT | glft (ext grid) | VIP0 (+2.0 bp) | alpha-scale=1 levels=3 half-spread-mult=5 depletion-pull=0.3 | 3.92 | 25.00 | 3.15 | 5.15 | 9.32 | -3.27 | 286.25 | 53.56 |
| ADAUSDT | ladder | VIP0 (+2.0 bp) | alpha-scale=2 pull-threshold=0.05 levels=2 depletion-pull=0.3 inventory-skew=0 | -248.65 | 0.00 | -5.01 | -3.01 | 0.55 | -1.92 | 3,623.50 | 378.15 |
| ADAUSDT | ladder (ext grid) | VIP0 (+2.0 bp) | alpha-scale=5 pull-threshold=0.05 levels=2 depletion-pull=0.3 inventory-skew=0 | -234.69 | 0.00 | -4.98 | -2.98 | 0.57 | -1.96 | 3,461.25 | 359.32 |
| ADAUSDT | naive | VIP0 (+2.0 bp) | default | -1,638.41 | 0.00 | -3.77 | -1.77 | -0.54 | -1.80 | 31,435.62 | 2,598.15 |
| ADAUSDT | glft | 0 bp | alpha-scale=1 levels=1 half-spread-mult=2 depletion-pull=0.3 | -42.33 | 0.00 | -2.36 | -2.36 | 1.44 | -2.35 | 1,770.25 | 105.58 |
| ADAUSDT | ladder | 0 bp | alpha-scale=2 pull-threshold=0.05 levels=2 depletion-pull=0.3 inventory-skew=0 | -149.46 | 0.00 | -3.01 | -3.01 | 0.55 | -1.92 | 3,623.50 | 284.86 |
| ADAUSDT | naive | 0 bp | default | -769.06 | 0.00 | -1.77 | -1.77 | -0.54 | -1.80 | 31,435.62 | 1,291.58 |
| ATOMUSDT | glft | rebate (−0.5 bp) | alpha-scale=0 levels=3 half-spread-mult=2 depletion-pull=0.3 | -10.91 | 25.00 | -1.56 | -2.06 | 4.14 | -2.16 | 973.25 | 46.20 |
| ATOMUSDT | glft (ext grid) | rebate (−0.5 bp) | alpha-scale=1 levels=3 half-spread-mult=5 depletion-pull=0.3 | -5.41 | 50.00 | -8.40 | -8.90 | 12.97 | -4.89 | 131.00 | 78.49 |
| ATOMUSDT | ladder | rebate (−0.5 bp) | alpha-scale=2 pull-threshold=0.05 levels=4 depletion-pull=0.3 inventory-skew=0.05 | -22.24 | 0.00 | -1.29 | -1.79 | 2.81 | -1.99 | 1,795.75 | 73.74 |
| ATOMUSDT | ladder (ext grid) | rebate (−0.5 bp) | alpha-scale=2 pull-threshold=0.05 levels=4 depletion-pull=0.3 inventory-skew=0.05 | -22.24 | 0.00 | -1.29 | -1.79 | 2.81 | -1.99 | 1,795.75 | 73.74 |
| ATOMUSDT | naive | rebate (−0.5 bp) | default | -166.41 | 0.00 | -1.33 | -1.83 | -0.40 | -1.98 | 14,929.38 | 380.43 |
| ATOMUSDT | glft | VIP0 (+2.0 bp) | alpha-scale=1 levels=3 half-spread-mult=2 depletion-pull=0.3 | -16.81 | 25.00 | -5.26 | -3.26 | 6.85 | -2.99 | 472.50 | 66.43 |
| ATOMUSDT | glft (ext grid) | VIP0 (+2.0 bp) | alpha-scale=1 levels=3 half-spread-mult=3 depletion-pull=0.3 | -8.26 | 50.00 | -5.96 | -3.96 | 9.45 | -3.98 | 252.50 | 76.67 |
| ATOMUSDT | ladder | VIP0 (+2.0 bp) | alpha-scale=2 pull-threshold=0.05 levels=4 depletion-pull=0.3 inventory-skew=0.05 | -65.30 | 0.00 | -3.79 | -1.79 | 2.81 | -1.99 | 1,795.75 | 123.11 |
| ATOMUSDT | ladder (ext grid) | VIP0 (+2.0 bp) | alpha-scale=3 pull-threshold=0.05 levels=4 depletion-pull=0.3 inventory-skew=0.05 | -59.46 | 0.00 | -3.69 | -1.69 | 2.83 | -1.98 | 1,663.75 | 127.24 |
| ATOMUSDT | naive | VIP0 (+2.0 bp) | default | -479.51 | 0.00 | -3.83 | -1.83 | -0.40 | -1.98 | 14,929.38 | 936.33 |
| ATOMUSDT | glft | 0 bp | alpha-scale=1 levels=3 half-spread-mult=2 depletion-pull=0.3 | -11.48 | 50.00 | -2.14 | -2.14 | 5.04 | -2.53 | 692.00 | 48.19 |
| ATOMUSDT | ladder | 0 bp | alpha-scale=2 pull-threshold=0.05 levels=4 depletion-pull=0.3 inventory-skew=0.05 | -30.85 | 0.00 | -1.79 | -1.79 | 2.81 | -1.99 | 1,795.75 | 82.06 |
| ATOMUSDT | naive | 0 bp | default | -229.03 | 0.00 | -1.83 | -1.83 | -0.40 | -1.98 | 14,929.38 | 491.53 |
| BTCUSDT | glft | rebate (−0.5 bp) | alpha-scale=1 levels=1 half-spread-mult=2 depletion-pull=0.3 | -68.22 | 0.00 | -0.24 | -0.74 | 0.35 | -0.70 | 11,256.25 | 85.27 |
| BTCUSDT | glft (ext grid) | rebate (−0.5 bp) | alpha-scale=0 levels=3 half-spread-mult=8 depletion-pull=0.3 | 4.43 | 50.00 | 0.36 | -0.14 | 1.30 | -0.77 | 412.00 | 29.88 |
| BTCUSDT | naive | rebate (−0.5 bp) | default | -1,482.30 | 0.00 | -0.35 | -0.85 | -0.20 | -0.83 | 148,914.25 | 2,533.78 |
| BTCUSDT | glft | VIP0 (+2.0 bp) | alpha-scale=0 levels=1 half-spread-mult=2 depletion-pull=0 | -26.12 | 0.00 | -2.41 | -0.41 | 2.10 | -0.89 | 339.25 | 59.92 |
| BTCUSDT | glft (ext grid) | VIP0 (+2.0 bp) | alpha-scale=0 levels=1 half-spread-mult=5 depletion-pull=0.3 | -7.53 | 50.00 | -3.22 | -1.22 | 3.71 | -1.05 | 63.50 | 37.60 |
| BTCUSDT | naive | VIP0 (+2.0 bp) | default | -11,948.57 | 0.00 | -2.85 | -0.85 | -0.20 | -0.83 | 148,914.25 | 19,368.33 |
| BTCUSDT | glft | 0 bp | alpha-scale=1 levels=1 half-spread-mult=2 depletion-pull=0.3 | -96.60 | 0.00 | -0.73 | -0.73 | 0.59 | -0.73 | 5,355.00 | 124.50 |
| BTCUSDT | naive | 0 bp | default | -3,575.56 | 0.00 | -0.85 | -0.85 | -0.20 | -0.83 | 148,914.25 | 5,900.39 |
| XLMUSDT | glft | rebate (−0.5 bp) | alpha-scale=1 levels=3 half-spread-mult=2 depletion-pull=0.3 | 1.53 | 50.00 | 0.18 | -0.32 | 2.95 | -0.50 | 616.75 | 36.23 |
| XLMUSDT | glft (ext grid) | rebate (−0.5 bp) | alpha-scale=1 levels=3 half-spread-mult=2 depletion-pull=0.3 | 1.53 | 50.00 | 0.18 | -0.32 | 2.95 | -0.50 | 616.75 | 36.23 |
| XLMUSDT | ladder | rebate (−0.5 bp) | alpha-scale=1 pull-threshold=0.05 levels=4 depletion-pull=0.3 inventory-skew=0 | -1.10 | 50.00 | -0.07 | -0.57 | 2.01 | -0.36 | 1,016.00 | 64.11 |
| XLMUSDT | ladder (ext grid) | rebate (−0.5 bp) | alpha-scale=5 pull-threshold=0.05 levels=4 depletion-pull=0.3 inventory-skew=0 | -3.57 | 50.00 | -0.27 | -0.77 | 2.35 | -0.34 | 872.50 | 84.45 |
| XLMUSDT | naive | rebate (−0.5 bp) | default | -355.53 | 0.00 | -1.00 | -1.50 | -0.72 | -1.65 | 29,829.62 | 973.88 |
| XLMUSDT | glft | VIP0 (+2.0 bp) | alpha-scale=0 levels=3 half-spread-mult=2 depletion-pull=0.3 | -15.57 | 25.00 | -4.92 | -2.92 | 5.12 | -0.74 | 263.00 | 68.49 |
| XLMUSDT | glft (ext grid) | VIP0 (+2.0 bp) | alpha-scale=0 levels=3 half-spread-mult=8 depletion-pull=0.3 | 8.65 | 50.00 | 99.65 | 101.65 | 17.28 | -16.70 | 9.00 | 9.60 |
| XLMUSDT | ladder | VIP0 (+2.0 bp) | alpha-scale=2 pull-threshold=0.05 levels=4 depletion-pull=0.3 inventory-skew=0 | -38.30 | 25.00 | -2.73 | -0.73 | 2.19 | -0.31 | 919.25 | 104.01 |
| XLMUSDT | ladder (ext grid) | VIP0 (+2.0 bp) | alpha-scale=5 pull-threshold=0.05 levels=4 depletion-pull=0.3 inventory-skew=0 | -36.48 | 25.00 | -2.77 | -0.77 | 2.35 | -0.34 | 872.50 | 117.58 |
| XLMUSDT | naive | VIP0 (+2.0 bp) | default | -1,245.13 | 0.00 | -3.50 | -1.50 | -0.72 | -1.65 | 29,829.62 | 2,866.43 |
| XLMUSDT | glft | 0 bp | alpha-scale=0 levels=3 half-spread-mult=2 depletion-pull=0.3 | -14.58 | 25.00 | -1.74 | -1.74 | 2.63 | -0.88 | 711.50 | 63.06 |
| XLMUSDT | ladder | 0 bp | alpha-scale=1 pull-threshold=0.05 levels=4 depletion-pull=0.3 inventory-skew=0 | -8.81 | 50.00 | -0.57 | -0.57 | 2.01 | -0.36 | 1,016.00 | 67.10 |
| XLMUSDT | naive | 0 bp | default | -533.45 | 0.00 | -1.50 | -1.50 | -0.72 | -1.65 | 29,829.62 | 1,351.78 |


## Sensitivity of the selected configurations (test days)

| symbol | strategy | fee | variant | net $/day | net bp | gross bp | fills/day |
|---|---|---|---|---|---|---|---|
| ADAUSDT | ladder | rebate | _lat0 | -124.66 | -2.51 | -3.01 | 3,623.50 |
| ADAUSDT | ladder | rebate | _lat1000 | -104.98 | -1.75 | -2.25 | 4,371.75 |
| ADAUSDT | ladder | rebate | _lat10000 | -125.86 | -1.99 | -2.49 | 4,773.75 |
| ADAUSDT | ladder | rebate | _lat200000 | -124.16 | -1.42 | -1.92 | 6,645.50 |
| ADAUSDT | ladder | rebate | _lat50000 | -110.57 | -1.49 | -1.99 | 5,539.75 |
| ADAUSDT | ladder | rebate | _nocross | -70.93 | -2.65 | -3.15 | 3,109.25 |
| ADAUSDT | ladder | rebate | _qlog | -107.80 | -1.77 | -2.27 | 5,502.25 |
| ADAUSDT | ladder | rebate | _qriskaverse | -116.23 | -2.92 | -3.42 | 2,909.25 |
| ADAUSDT | ladder | vip0 | _lat0 | -248.65 | -5.01 | -3.01 | 3,623.50 |
| ADAUSDT | ladder | vip0 | _lat1000 | -255.21 | -4.25 | -2.25 | 4,371.75 |
| ADAUSDT | ladder | vip0 | _lat10000 | -284.19 | -4.49 | -2.49 | 4,773.75 |
| ADAUSDT | ladder | vip0 | _lat200000 | -343.17 | -3.92 | -1.92 | 6,645.50 |
| ADAUSDT | ladder | vip0 | _lat50000 | -296.35 | -3.99 | -1.99 | 5,539.75 |
| ADAUSDT | ladder | vip0 | _nocross | -137.72 | -5.15 | -3.15 | 3,109.25 |
| ADAUSDT | ladder | vip0 | _qlog | -260.19 | -4.27 | -2.27 | 5,502.25 |
| ADAUSDT | ladder | vip0 | _qriskaverse | -215.73 | -5.42 | -3.42 | 2,909.25 |
| ATOMUSDT | ladder | rebate | _lat0 | -22.24 | -1.29 | -1.79 | 1,795.75 |
| ATOMUSDT | ladder | rebate | _lat1000 | -25.02 | -1.27 | -1.77 | 2,115.00 |
| ATOMUSDT | ladder | rebate | _lat10000 | -30.15 | -1.48 | -1.98 | 2,222.00 |
| ATOMUSDT | ladder | rebate | _lat200000 | -39.30 | -1.57 | -2.07 | 2,775.50 |
| ATOMUSDT | ladder | rebate | _lat50000 | -25.40 | -1.10 | -1.60 | 2,518.75 |
| ATOMUSDT | ladder | rebate | _nocross | -15.72 | -1.35 | -1.85 | 1,523.75 |
| ATOMUSDT | ladder | rebate | _qlog | 0.83 | 0.04 | -0.46 | 3,189.25 |
| ATOMUSDT | ladder | rebate | _qriskaverse | -19.80 | -1.51 | -2.01 | 1,424.00 |
| ATOMUSDT | ladder | vip0 | _lat0 | -65.30 | -3.79 | -1.79 | 1,795.75 |
| ATOMUSDT | ladder | vip0 | _lat1000 | -74.32 | -3.77 | -1.77 | 2,115.00 |
| ATOMUSDT | ladder | vip0 | _lat10000 | -81.14 | -3.98 | -1.98 | 2,222.00 |
| ATOMUSDT | ladder | vip0 | _lat200000 | -102.05 | -4.07 | -2.07 | 2,775.50 |
| ATOMUSDT | ladder | vip0 | _lat50000 | -82.90 | -3.60 | -1.60 | 2,518.75 |
| ATOMUSDT | ladder | vip0 | _nocross | -44.79 | -3.85 | -1.85 | 1,523.75 |
| ATOMUSDT | ladder | vip0 | _qlog | -52.59 | -2.46 | -0.46 | 3,189.25 |
| ATOMUSDT | ladder | vip0 | _qriskaverse | -52.56 | -4.01 | -2.01 | 1,424.00 |
| BTCUSDT | glft | rebate | _lat0 | -68.22 | -0.24 | -0.74 | 11,256.25 |
| BTCUSDT | glft | rebate | _lat1000 | -75.59 | -0.25 | -0.75 | 12,109.50 |
| BTCUSDT | glft | rebate | _lat10000 | -73.67 | -0.24 | -0.74 | 13,504.25 |
| BTCUSDT | glft | rebate | _lat200000 | -131.95 | -0.29 | -0.79 | 17,338.75 |
| BTCUSDT | glft | rebate | _lat50000 | -110.92 | -0.27 | -0.77 | 16,852.00 |
| BTCUSDT | glft | rebate | _nocross | -73.23 | -0.30 | -0.80 | 11,594.75 |
| BTCUSDT | glft | rebate | _qlog | -59.11 | -0.20 | -0.70 | 11,850.50 |
| BTCUSDT | glft | rebate | _qriskaverse | -67.18 | -0.26 | -0.76 | 11,002.00 |
| BTCUSDT | glft | vip0 | _lat0 | -26.12 | -2.41 | -0.41 | 339.25 |
| BTCUSDT | glft | vip0 | _lat1000 | -24.94 | -2.33 | -0.33 | 341.75 |
| BTCUSDT | glft | vip0 | _lat10000 | -25.42 | -2.26 | -0.26 | 345.50 |
| BTCUSDT | glft | vip0 | _lat200000 | -60.24 | -2.96 | -0.96 | 707.50 |
| BTCUSDT | glft | vip0 | _lat50000 | -32.88 | -2.43 | -0.43 | 441.25 |
| BTCUSDT | glft | vip0 | _nocross | -26.49 | -2.47 | -0.47 | 346.00 |
| BTCUSDT | glft | vip0 | _qlog | -26.12 | -2.41 | -0.41 | 342.25 |
| BTCUSDT | glft | vip0 | _qriskaverse | -26.12 | -2.41 | -0.41 | 337.75 |
| XLMUSDT | ladder | rebate | _lat0 | -1.10 | -0.07 | -0.57 | 1,016.00 |
| XLMUSDT | ladder | rebate | _lat1000 | 0.77 | 0.04 | -0.46 | 1,162.00 |
| XLMUSDT | ladder | rebate | _lat10000 | -2.55 | -0.14 | -0.64 | 1,238.50 |
| XLMUSDT | ladder | rebate | _lat200000 | -2.72 | -0.10 | -0.60 | 2,014.75 |
| XLMUSDT | ladder | rebate | _lat50000 | -4.07 | -0.18 | -0.68 | 1,519.25 |
| XLMUSDT | ladder | rebate | _nocross | 0.15 | 0.02 | -0.48 | 796.25 |
| XLMUSDT | ladder | rebate | _qlog | 69.12 | 2.50 | 2.00 | 3,387.00 |
| XLMUSDT | ladder | rebate | _qriskaverse | -3.44 | -0.32 | -0.82 | 724.00 |
| XLMUSDT | ladder | vip0 | _lat0 | -38.30 | -2.73 | -0.73 | 919.25 |
| XLMUSDT | ladder | vip0 | _lat1000 | -43.35 | -2.73 | -0.73 | 1,059.00 |
| XLMUSDT | ladder | vip0 | _lat10000 | -47.44 | -2.87 | -0.87 | 1,124.50 |
| XLMUSDT | ladder | vip0 | _lat200000 | -68.84 | -2.72 | -0.72 | 1,837.50 |
| XLMUSDT | ladder | vip0 | _lat50000 | -51.21 | -2.58 | -0.58 | 1,365.50 |
| XLMUSDT | ladder | vip0 | _nocross | -23.85 | -2.65 | -0.65 | 725.75 |
| XLMUSDT | ladder | vip0 | _qlog | -3.74 | -0.15 | 1.85 | 3,058.00 |
| XLMUSDT | ladder | vip0 | _qriskaverse | -43.19 | -4.46 | -2.46 | 656.00 |
