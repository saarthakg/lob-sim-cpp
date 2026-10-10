# Queue-model validation against Bitstamp L3 ground truth

| capture | horizon_s | model | probes | queue_driven_share | true_fill_rate | sim_fill_rate | rate_ratio | accuracy | precision | recall | median_abs_dt_s |
|---|---|---|---|---|---|---|---|---|---|---|---|
| bitstamp_btcusd_150min | 5 | riskaverse | 3585 | 0.327 | 0.042 | 0.041 | 0.980 | 0.999 | 1.000 | 0.980 | 0.000 |
| bitstamp_btcusd_150min | 15 | riskaverse | 3585 | 0.441 | 0.108 | 0.104 | 0.959 | 0.996 | 1.000 | 0.959 | 0.000 |
| bitstamp_btcusd_150min | 60 | riskaverse | 3585 | 0.629 | 0.305 | 0.277 | 0.908 | 0.972 | 1.000 | 0.908 | 0.000 |
| bitstamp_btcusd_150min | 5 | power1 | 3585 | 0.327 | 0.042 | 0.041 | 0.980 | 0.999 | 1.000 | 0.980 | 0.000 |
| bitstamp_btcusd_150min | 15 | power1 | 3585 | 0.441 | 0.108 | 0.105 | 0.969 | 0.994 | 0.989 | 0.959 | 0.000 |
| bitstamp_btcusd_150min | 60 | power1 | 3585 | 0.629 | 0.305 | 0.305 | 1.002 | 0.963 | 0.939 | 0.940 | 0.000 |
| bitstamp_btcusd_150min | 5 | power2 (default) | 3585 | 0.327 | 0.042 | 0.041 | 0.980 | 0.999 | 1.000 | 0.980 | 0.000 |
| bitstamp_btcusd_150min | 15 | power2 (default) | 3585 | 0.441 | 0.108 | 0.106 | 0.979 | 0.994 | 0.982 | 0.961 | 0.000 |
| bitstamp_btcusd_150min | 60 | power2 (default) | 3585 | 0.629 | 0.305 | 0.296 | 0.971 | 0.965 | 0.957 | 0.929 | 0.000 |
| bitstamp_btcusd_150min | 5 | power3 | 3585 | 0.327 | 0.042 | 0.041 | 0.980 | 0.999 | 1.000 | 0.980 | 0.000 |
| bitstamp_btcusd_150min | 15 | power3 | 3585 | 0.441 | 0.108 | 0.106 | 0.977 | 0.994 | 0.982 | 0.959 | 0.000 |
| bitstamp_btcusd_150min | 60 | power3 | 3585 | 0.629 | 0.305 | 0.297 | 0.976 | 0.965 | 0.953 | 0.930 | 0.000 |
| bitstamp_btcusd_150min | 5 | log | 3585 | 0.327 | 0.042 | 0.041 | 0.980 | 0.999 | 1.000 | 0.980 | 0.000 |
| bitstamp_btcusd_150min | 15 | log | 3585 | 0.441 | 0.108 | 0.118 | 1.088 | 0.984 | 0.891 | 0.969 | 0.000 |
| bitstamp_btcusd_150min | 60 | log | 3585 | 0.629 | 0.305 | 0.392 | 1.288 | 0.889 | 0.747 | 0.962 | 0.000 |
| bitstamp_btcusd_150min | 5 | power2, no cross-fill | 3585 | 0.327 | 0.042 | 0.034 | 0.820 | 0.992 | 1.000 | 0.820 | 0.000 |
| bitstamp_btcusd_150min | 15 | power2, no cross-fill | 3585 | 0.441 | 0.108 | 0.099 | 0.915 | 0.987 | 0.980 | 0.897 | 0.000 |
| bitstamp_btcusd_150min | 60 | power2, no cross-fill | 3585 | 0.629 | 0.305 | 0.291 | 0.954 | 0.960 | 0.956 | 0.912 | 0.000 |
| bitstamp_btcusd_150min | 5 | L3 exact (order-level) | 3585 | 0.327 | 0.042 | 0.042 | 0.993 | 0.999 | 0.993 | 0.987 | 0.000 |
| bitstamp_btcusd_150min | 15 | L3 exact (order-level) | 3585 | 0.441 | 0.108 | 0.107 | 0.985 | 0.998 | 0.997 | 0.982 | 0.000 |
| bitstamp_btcusd_150min | 60 | L3 exact (order-level) | 3585 | 0.629 | 0.305 | 0.300 | 0.985 | 0.996 | 1.000 | 0.985 | 0.000 |
| bitstamp_ethusd_150min | 5 | riskaverse | 3593 | 0.008 | 0.144 | 0.144 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_150min | 15 | riskaverse | 3593 | 0.022 | 0.300 | 0.300 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_150min | 60 | riskaverse | 3593 | 0.027 | 0.560 | 0.560 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_150min | 5 | power1 | 3593 | 0.008 | 0.144 | 0.144 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_150min | 15 | power1 | 3593 | 0.022 | 0.300 | 0.301 | 1.002 | 0.999 | 0.998 | 1.000 | 0.000 |
| bitstamp_ethusd_150min | 60 | power1 | 3593 | 0.027 | 0.560 | 0.563 | 1.005 | 0.997 | 0.995 | 1.000 | 0.000 |
| bitstamp_ethusd_150min | 5 | power2 (default) | 3593 | 0.008 | 0.144 | 0.144 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_150min | 15 | power2 (default) | 3593 | 0.022 | 0.300 | 0.300 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_150min | 60 | power2 (default) | 3593 | 0.027 | 0.560 | 0.560 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_150min | 5 | power3 | 3593 | 0.008 | 0.144 | 0.144 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_150min | 15 | power3 | 3593 | 0.022 | 0.300 | 0.300 | 1.001 | 1.000 | 0.999 | 1.000 | 0.000 |
| bitstamp_ethusd_150min | 60 | power3 | 3593 | 0.027 | 0.560 | 0.560 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_150min | 5 | log | 3593 | 0.008 | 0.144 | 0.144 | 1.002 | 1.000 | 0.998 | 1.000 | 0.000 |
| bitstamp_ethusd_150min | 15 | log | 3593 | 0.022 | 0.300 | 0.302 | 1.006 | 0.998 | 0.994 | 1.000 | 0.000 |
| bitstamp_ethusd_150min | 60 | log | 3593 | 0.027 | 0.560 | 0.567 | 1.011 | 0.994 | 0.989 | 1.000 | 0.000 |
| bitstamp_ethusd_150min | 5 | power2, no cross-fill | 3593 | 0.008 | 0.144 | 0.021 | 0.149 | 0.878 | 1.000 | 0.149 | 0.073 |
| bitstamp_ethusd_150min | 15 | power2, no cross-fill | 3593 | 0.022 | 0.300 | 0.080 | 0.266 | 0.780 | 1.000 | 0.266 | 1.182 |
| bitstamp_ethusd_150min | 60 | power2, no cross-fill | 3593 | 0.027 | 0.560 | 0.284 | 0.507 | 0.724 | 1.000 | 0.507 | 6.781 |
| bitstamp_ethusd_150min | 5 | L3 exact (order-level) | 3593 | 0.008 | 0.144 | 0.142 | 0.988 | 0.998 | 1.000 | 0.988 | 0.047 |
| bitstamp_ethusd_150min | 15 | L3 exact (order-level) | 3593 | 0.022 | 0.300 | 0.298 | 0.994 | 0.997 | 0.999 | 0.993 | 0.045 |
| bitstamp_ethusd_150min | 60 | L3 exact (order-level) | 3593 | 0.027 | 0.560 | 0.558 | 0.996 | 0.996 | 0.998 | 0.994 | 0.044 |
| bitstamp_xrpusd_150min | 5 | riskaverse | 3587 | 0.028 | 0.267 | 0.266 | 0.998 | 0.999 | 1.000 | 0.998 | 0.000 |
| bitstamp_xrpusd_150min | 15 | riskaverse | 3587 | 0.052 | 0.496 | 0.494 | 0.997 | 0.999 | 1.000 | 0.997 | 0.000 |
| bitstamp_xrpusd_150min | 60 | riskaverse | 3587 | 0.051 | 0.750 | 0.750 | 0.999 | 0.999 | 1.000 | 0.999 | 0.000 |
| bitstamp_xrpusd_150min | 5 | power1 | 3587 | 0.028 | 0.267 | 0.267 | 0.999 | 1.000 | 1.000 | 0.999 | 0.000 |
| bitstamp_xrpusd_150min | 15 | power1 | 3587 | 0.052 | 0.496 | 0.495 | 0.998 | 0.999 | 1.000 | 0.998 | 0.000 |
| bitstamp_xrpusd_150min | 60 | power1 | 3587 | 0.051 | 0.750 | 0.750 | 1.000 | 0.999 | 0.999 | 0.999 | 0.000 |
| bitstamp_xrpusd_150min | 5 | power2 (default) | 3587 | 0.028 | 0.267 | 0.267 | 1.000 | 0.999 | 0.999 | 0.999 | 0.000 |
| bitstamp_xrpusd_150min | 15 | power2 (default) | 3587 | 0.052 | 0.496 | 0.495 | 0.999 | 0.999 | 0.999 | 0.998 | 0.000 |
| bitstamp_xrpusd_150min | 60 | power2 (default) | 3587 | 0.051 | 0.750 | 0.751 | 1.000 | 0.999 | 0.999 | 1.000 | 0.000 |
| bitstamp_xrpusd_150min | 5 | power3 | 3587 | 0.028 | 0.267 | 0.267 | 1.000 | 0.999 | 0.999 | 0.999 | 0.000 |
| bitstamp_xrpusd_150min | 15 | power3 | 3587 | 0.052 | 0.496 | 0.495 | 0.999 | 0.999 | 0.999 | 0.999 | 0.000 |
| bitstamp_xrpusd_150min | 60 | power3 | 3587 | 0.051 | 0.750 | 0.750 | 1.000 | 0.999 | 1.000 | 1.000 | 0.000 |
| bitstamp_xrpusd_150min | 5 | log | 3587 | 0.028 | 0.267 | 0.267 | 0.999 | 1.000 | 1.000 | 0.999 | 0.000 |
| bitstamp_xrpusd_150min | 15 | log | 3587 | 0.052 | 0.496 | 0.496 | 1.000 | 0.999 | 0.999 | 0.999 | 0.000 |
| bitstamp_xrpusd_150min | 60 | log | 3587 | 0.051 | 0.750 | 0.751 | 1.001 | 0.998 | 0.998 | 0.999 | 0.000 |
| bitstamp_xrpusd_150min | 5 | power2, no cross-fill | 3587 | 0.028 | 0.267 | 0.049 | 0.183 | 0.781 | 0.994 | 0.182 | 0.060 |
| bitstamp_xrpusd_150min | 15 | power2, no cross-fill | 3587 | 0.052 | 0.496 | 0.160 | 0.322 | 0.664 | 0.998 | 0.322 | 1.289 |
| bitstamp_xrpusd_150min | 60 | power2, no cross-fill | 3587 | 0.051 | 0.750 | 0.441 | 0.588 | 0.690 | 0.999 | 0.587 | 6.418 |
| bitstamp_xrpusd_150min | 5 | L3 exact (order-level) | 3587 | 0.028 | 0.267 | 0.266 | 0.997 | 0.997 | 0.997 | 0.994 | 0.047 |
| bitstamp_xrpusd_150min | 15 | L3 exact (order-level) | 3587 | 0.052 | 0.496 | 0.494 | 0.997 | 0.997 | 0.998 | 0.996 | 0.046 |
| bitstamp_xrpusd_150min | 60 | L3 exact (order-level) | 3587 | 0.051 | 0.750 | 0.747 | 0.996 | 0.996 | 0.999 | 0.995 | 0.046 |
