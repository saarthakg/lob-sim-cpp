# Queue-model validation against Bitstamp L3 ground truth

| capture | horizon_s | model | probes | queue_driven_share | true_fill_rate | sim_fill_rate | rate_ratio | accuracy | precision | recall | median_abs_dt_s |
|---|---|---|---|---|---|---|---|---|---|---|---|
| bitstamp_btcusd_70min | 5 | riskaverse | 1642 | 0.292 | 0.044 | 0.043 | 0.972 | 0.999 | 1.000 | 0.972 | 0.000 |
| bitstamp_btcusd_70min | 15 | riskaverse | 1642 | 0.407 | 0.115 | 0.110 | 0.958 | 0.995 | 1.000 | 0.958 | 0.000 |
| bitstamp_btcusd_70min | 60 | riskaverse | 1642 | 0.582 | 0.296 | 0.269 | 0.907 | 0.973 | 1.000 | 0.907 | 0.000 |
| bitstamp_btcusd_70min | 5 | power1 | 1642 | 0.292 | 0.044 | 0.043 | 0.972 | 0.999 | 1.000 | 0.972 | 0.000 |
| bitstamp_btcusd_70min | 15 | power1 | 1642 | 0.407 | 0.115 | 0.111 | 0.968 | 0.994 | 0.989 | 0.958 | 0.000 |
| bitstamp_btcusd_70min | 60 | power1 | 1642 | 0.582 | 0.296 | 0.286 | 0.965 | 0.967 | 0.959 | 0.926 | 0.000 |
| bitstamp_btcusd_70min | 5 | power2 (default) | 1642 | 0.292 | 0.044 | 0.043 | 0.972 | 0.999 | 1.000 | 0.972 | 0.000 |
| bitstamp_btcusd_70min | 15 | power2 (default) | 1642 | 0.407 | 0.115 | 0.113 | 0.979 | 0.993 | 0.978 | 0.958 | 0.000 |
| bitstamp_btcusd_70min | 60 | power2 (default) | 1642 | 0.582 | 0.296 | 0.284 | 0.961 | 0.959 | 0.949 | 0.912 | 0.000 |
| bitstamp_btcusd_70min | 5 | power3 | 1642 | 0.292 | 0.044 | 0.043 | 0.972 | 0.999 | 1.000 | 0.972 | 0.000 |
| bitstamp_btcusd_70min | 15 | power3 | 1642 | 0.407 | 0.115 | 0.113 | 0.984 | 0.992 | 0.973 | 0.958 | 0.000 |
| bitstamp_btcusd_70min | 60 | power3 | 1642 | 0.582 | 0.296 | 0.291 | 0.984 | 0.960 | 0.939 | 0.924 | 0.000 |
| bitstamp_btcusd_70min | 5 | log | 1642 | 0.292 | 0.044 | 0.043 | 0.972 | 0.999 | 1.000 | 0.972 | 0.000 |
| bitstamp_btcusd_70min | 15 | log | 1642 | 0.407 | 0.115 | 0.123 | 1.069 | 0.982 | 0.896 | 0.958 | 0.000 |
| bitstamp_btcusd_70min | 60 | log | 1642 | 0.582 | 0.296 | 0.387 | 1.309 | 0.879 | 0.726 | 0.951 | 0.000 |
| bitstamp_btcusd_70min | 5 | power2, no cross-fill | 1642 | 0.292 | 0.044 | 0.034 | 0.778 | 0.990 | 1.000 | 0.778 | 0.000 |
| bitstamp_btcusd_70min | 15 | power2, no cross-fill | 1642 | 0.407 | 0.115 | 0.105 | 0.910 | 0.985 | 0.977 | 0.889 | 0.000 |
| bitstamp_btcusd_70min | 60 | power2, no cross-fill | 1642 | 0.582 | 0.296 | 0.281 | 0.949 | 0.956 | 0.948 | 0.899 | 0.000 |
| bitstamp_ethusd_70min | 5 | riskaverse | 1660 | 0.000 | 0.145 | 0.145 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_70min | 15 | riskaverse | 1660 | 0.012 | 0.307 | 0.307 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_70min | 60 | riskaverse | 1660 | 0.011 | 0.549 | 0.549 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_70min | 5 | power1 | 1660 | 0.000 | 0.145 | 0.145 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_70min | 15 | power1 | 1660 | 0.012 | 0.307 | 0.307 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_70min | 60 | power1 | 1660 | 0.011 | 0.549 | 0.549 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_70min | 5 | power2 (default) | 1660 | 0.000 | 0.145 | 0.145 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_70min | 15 | power2 (default) | 1660 | 0.012 | 0.307 | 0.307 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_70min | 60 | power2 (default) | 1660 | 0.011 | 0.549 | 0.549 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_70min | 5 | power3 | 1660 | 0.000 | 0.145 | 0.145 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_70min | 15 | power3 | 1660 | 0.012 | 0.307 | 0.307 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_70min | 60 | power3 | 1660 | 0.011 | 0.549 | 0.549 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_70min | 5 | log | 1660 | 0.000 | 0.145 | 0.145 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_ethusd_70min | 15 | log | 1660 | 0.012 | 0.307 | 0.308 | 1.004 | 0.999 | 0.996 | 1.000 | 0.000 |
| bitstamp_ethusd_70min | 60 | log | 1660 | 0.011 | 0.549 | 0.552 | 1.005 | 0.997 | 0.995 | 1.000 | 0.000 |
| bitstamp_ethusd_70min | 5 | power2, no cross-fill | 1660 | 0.000 | 0.145 | 0.023 | 0.158 | 0.878 | 1.000 | 0.158 | 0.254 |
| bitstamp_ethusd_70min | 15 | power2, no cross-fill | 1660 | 0.012 | 0.307 | 0.080 | 0.259 | 0.773 | 1.000 | 0.259 | 2.294 |
| bitstamp_ethusd_70min | 60 | power2, no cross-fill | 1660 | 0.011 | 0.549 | 0.269 | 0.490 | 0.720 | 1.000 | 0.490 | 7.336 |
| bitstamp_xrpusd_70min | 5 | riskaverse | 1653 | 0.035 | 0.277 | 0.276 | 0.998 | 0.999 | 1.000 | 0.998 | 0.000 |
| bitstamp_xrpusd_70min | 15 | riskaverse | 1653 | 0.059 | 0.499 | 0.497 | 0.996 | 0.998 | 1.000 | 0.996 | 0.000 |
| bitstamp_xrpusd_70min | 60 | riskaverse | 1653 | 0.050 | 0.748 | 0.748 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_xrpusd_70min | 5 | power1 | 1653 | 0.035 | 0.277 | 0.277 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_xrpusd_70min | 15 | power1 | 1653 | 0.059 | 0.499 | 0.498 | 0.999 | 0.999 | 1.000 | 0.999 | 0.000 |
| bitstamp_xrpusd_70min | 60 | power1 | 1653 | 0.050 | 0.748 | 0.750 | 1.002 | 0.999 | 0.998 | 1.000 | 0.000 |
| bitstamp_xrpusd_70min | 5 | power2 (default) | 1653 | 0.035 | 0.277 | 0.278 | 1.002 | 0.999 | 0.998 | 1.000 | 0.000 |
| bitstamp_xrpusd_70min | 15 | power2 (default) | 1653 | 0.059 | 0.499 | 0.499 | 1.000 | 0.999 | 0.999 | 0.999 | 0.000 |
| bitstamp_xrpusd_70min | 60 | power2 (default) | 1653 | 0.050 | 0.748 | 0.750 | 1.002 | 0.999 | 0.998 | 1.000 | 0.000 |
| bitstamp_xrpusd_70min | 5 | power3 | 1653 | 0.035 | 0.277 | 0.278 | 1.002 | 0.999 | 0.998 | 1.000 | 0.000 |
| bitstamp_xrpusd_70min | 15 | power3 | 1653 | 0.059 | 0.499 | 0.499 | 1.000 | 0.999 | 0.999 | 0.999 | 0.000 |
| bitstamp_xrpusd_70min | 60 | power3 | 1653 | 0.050 | 0.748 | 0.749 | 1.001 | 0.999 | 0.999 | 1.000 | 0.000 |
| bitstamp_xrpusd_70min | 5 | log | 1653 | 0.035 | 0.277 | 0.277 | 1.000 | 1.000 | 1.000 | 1.000 | 0.000 |
| bitstamp_xrpusd_70min | 15 | log | 1653 | 0.059 | 0.499 | 0.498 | 0.999 | 0.999 | 1.000 | 0.999 | 0.000 |
| bitstamp_xrpusd_70min | 60 | log | 1653 | 0.050 | 0.748 | 0.750 | 1.002 | 0.999 | 0.998 | 1.000 | 0.000 |
| bitstamp_xrpusd_70min | 5 | power2, no cross-fill | 1653 | 0.035 | 0.277 | 0.050 | 0.181 | 0.772 | 0.988 | 0.179 | 0.065 |
| bitstamp_xrpusd_70min | 15 | power2, no cross-fill | 1653 | 0.059 | 0.499 | 0.159 | 0.319 | 0.659 | 0.996 | 0.318 | 1.844 |
| bitstamp_xrpusd_70min | 60 | power2, no cross-fill | 1653 | 0.050 | 0.748 | 0.418 | 0.559 | 0.667 | 0.997 | 0.557 | 7.445 |
