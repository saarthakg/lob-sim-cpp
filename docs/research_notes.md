# Research brief: LOB replay and market-making backtests (BTCUSDT, Binance/Bybit)

Legend: **[V]** = I checked it online or against live files on 2026-10-09. **[M]** = from memory or the literature, not re-checked.

---

## 1. Avellaneda–Stoikov and GLFT

**Avellaneda & Stoikov (2008), "High-frequency trading in a limit order book", *Quant. Finance* 8(3).** [M]
Assumptions: the mid follows arithmetic Brownian motion with vol σ, utility is CARA with risk aversion γ, the horizon is T, and fills arrive as Poisson with intensity λ(δ) = A·e^{-kδ}, where δ is the distance from mid.
- Reservation price: r = s − q·γ·σ²·(T − t)
- Total optimal spread: δ^a + δ^b = γσ²(T − t) + (2/γ)·ln(1 + γ/k)
- Quotes are r ± spread/2.

Gotchas:
- (T − t) → 0 at the horizon, so the inventory skew vanishes there. Practitioners either roll T, for example per session, or switch to GLFT.
- The ln(1 + γ/k) term is the only part that depends on fill intensity. When γ/k is small it reduces to ≈ 2/k.

**Guéant, Lehalle & Fernandez-Tapia (2013), "Dealing with the inventory risk: a solution to the market making problem", *Math. Financ. Econ.* 7(4); arXiv:1105.3115.** [M, formula matches hftbacktest [V]]
This version uses inventory bounds and an infinite-horizon asymptotic closed form. In the general form of Guéant (2017, "Optimal market making", *Appl. Math. Finance*), with Δ the order size and ξ either γ (model A) or 0 (model B):

```
c1 = (1/(ξΔ)) · ln(1 + ξΔ/k)
c2 = sqrt( γ / (2·A·Δ·k) · (1 + ξΔ/k)^(k/(ξΔ) + 1) )
half_spread = c1 + (Δ/2)·σ·c2
skew        = σ·c2
δ_bid(q) = half_spread + skew·q        bid = fair − δ_bid
δ_ask(q) = half_spread − skew·q        ask = fair + δ_ask
```

- With ξ = 0 the limits are c1 = 1/k and c2 = sqrt(γ·e / (2AΔk)).
- Units must be consistent. hftbacktest uses δ in ticks, k in 1/tick, A in fills/s, σ in ticks/√s, and q in units of Δ.
- Mixing $ and ticks, or per-100 ms and per-second, is the most common bug.

**Calibrating λ(δ) = A·e^{-kδ}: nkaz001/hftbacktest GLFT tutorial** ([GLFT tutorial](https://hftbacktest.readthedocs.io/en/latest/tutorials/GLFT%20Market%20Making%20Model%20and%20Grid%20Trading.html)) [V]
- **Data:** Binance USD-M futures, ETHUSDT on 2022-10-03 and over 2022-10-03 to 07, and LTCUSDT over 2023-07-01 to 05. The book is initialised from the previous day's end-of-day snapshot, and feed and order latency are replayed from history.
- **Arrival depth:** in every 100 ms step, take the furthest distance in ticks from the mid of any trade in that step. Trades through the mid are counted, and queue position is ignored.
- **Intensity estimate:** build a histogram of these depths, giving λ(δ) for each tick bucket over a rolling 10-minute window (6,000 steps, divided by 600 to get per-second rates).
- **Fit:** an OLS fit of ln λ on δ at bucket midpoints 0.5, 1.5, … gives ln λ = ln A − kδ.
- **The exponential fits badly.** Over the full range it overestimates near the mid and underestimates far out. The author refits on the first ~70 ticks only:
  - full range: A ≈ 0.84, k ≈ 0.017
  - truncated: A ≈ 2.99, k ≈ 0.042
- **Known bug left in:** `measure_trading_intensity` returns half-tick units.
- **σ:** the standard deviation of 100 ms mid changes × √10, giving ticks/√s (≈10.7 for ETH).
- **Refresh:** A, k and σ are re-estimated every 5 s on the 10-minute window. Quotes refresh every 100 ms.
- **Strategy parameters:**
  - γ = 0.05, Δ = 1, order quantity 1 lot, max position 20
  - `adj1 = 1` scales the half-spread, `adj2 = 0.05` scales the skew. The raw skew was too strong: two units of inventory wiped out the whole half-spread.
- **Grid quoting:**
  - 20 levels per side, spaced at interval = max(round(half_spread_ticks)·tick, tick).
  - Bids are floored and asks ceiled onto the grid.
  - Only orders that leave the grid are cancelled, which keeps queue priority on the levels that stay. This is a key practical trick.
- **Fees and fill model:**
  - maker −0.005% (a rebate; the author calls it the best Binance Futures MM tier), taker 0.07%
  - GTX (post-only) orders
  - `power_prob_queue_model(2.0)`, no partial fills
- **Reported results:**

| Run | Sharpe | Return | Notes |
|---|---|---|---|
| ETH, 1 day, no skew adjustment | −246 | −2.06% | |
| ETH, 1 day, with adj2 | 1.2 | +0.036% | "at best breakeven with rebates" |
| ETH, 5 days, GLFT | 16.3 | 3.1% | |
| ETH, 5 days, GLFT + grid | 19.8 | 5.6% | max drawdown 0.74% |
| LTC, 5 days | 17.2 | 12.25% | |

- **Caveats in the tutorial:**
  - It says profit comes mainly from rebates on high volume.
  - It uses fair price = mid, with no alpha.
  - It models inventory risk only.
  - It is "educational only".
- **Follow-up tutorial on order-book-imbalance alpha** ([link](https://hftbacktest.readthedocs.io/en/latest/tutorials/Market%20Making%20with%20Alpha%20-%20Order%20Book%20Imbalance.html)) [V]:
  - Alpha is the z-score (1 h rolling window) of Σbid qty − Σask qty within ±2.5% of mid.
  - fair = mid + c1·alpha; reservation = fair − skew·(pos/order size).
  - bid = min(res − hs, best bid), ask = max(res + hs, best ask).
  - BTC May 2023: Sharpe 10.8, 0.0139% per trade.
  - BTC May–Jul 2025: Sharpe 3.0, only 0.0044% per trade. The edge decays, partly because the author moved to a harsher queue model (power 3).
  - All runs still assume the −0.5 bp rebate.

**Better calibration practice** [M]
- Measure fills of a hypothetical resting order at depth δ in place of max trade depth, i.e. ask whether a trade printed at or through mid ± δ within Δt. That gives P(fill within Δt | δ) = 1 − exp(−λ(δ)Δt). Fit it by Poisson MLE or nonlinear least squares, not log-OLS: log-OLS weights the noisy deep buckets badly and cannot handle zero counts.
- Fit only over the range of δ you will actually quote.
- On BTCUSDT the spread is almost always 1 tick, so λ(δ) is defined on only a few discrete points: δ = 0.5 tick (touch) and 1.5, 2.5, …
- Queue position dominates at the touch, so A·e^{-kδ} without a queue model overstates fills there.

## 2. Hummingbot `avellaneda_market_making`

Trading-intensity estimator ([`trading_intensity.pyx`](https://github.com/hummingbot/hummingbot/blob/master/hummingbot/strategy/__utils__/trailing_indicators/trading_intensity.pyx)) [V]:
- Each public trade is tagged with price_level = |trade price − last mid quoted before the trade|.
- The buffer keeps the last `sampling_length` (default 30) quote-timestamp groups.
- λ(level) is the **sum of traded amount** at each distinct level. It is volume-weighted and **not normalised by time**.
- The fit is `scipy.optimize.curve_fit(a·exp(−b·x))` with method `dogbox`, bounds [0, ∞), warm-started from the previous (α, κ). On `RuntimeError` or `ValueError` the old values are silently kept.
- Volatility comes from `InstantVolatilityIndicator` over a similar buffer.

Known issues [M, consistent with code]:
- α carries volume units and is not a rate, so it gets confused with A as a fill intensity.
- κ is in price units ($), so it is not comparable across assets.
- The buffer is short, so estimates are noisy.
- With a 1-tick spread, most trades sit at one level and the fit is degenerate (κ → huge or → 0). GitHub issues report κ/α blowing up and spreads collapsing to `min_spread`.
- The defaults use a finite or "infinite" horizon with `risk_factor` γ and an inventory target %. Users often find the AS spread is either below fees or absurdly wide.

## 3. Queue position modelling from L2 data and trades

**hftbacktest** ([queue.rs](https://github.com/nkaz001/hftbacktest/blob/master/hftbacktest/src/backtest/models/queue.rs), [order fill docs](https://hftbacktest.readthedocs.io/en/latest/order_fill.html)) [V]

On entry, front = displayed qty at your price; you join the back.

*Trade at your price, qty x:* front −= x. ProbQueueModel also accumulates `cum_trade_qty`.

*`RiskAdverseQueueModel` on a depth change:* front = min(front, new_qty). Cancels are assumed to happen behind you; only trades and level shrinkage advance you.

*`ProbQueueModel` on a depth change:*
```
chg  = prev_qty − new_qty − cum_trade_qty;  cum_trade_qty = 0
if chg < 0: front = min(front, new_qty); return     # level grew: arrivals join behind
back = prev_qty − front
p    = prob(front, back)                            # p = P(decrease is BEHIND you)
est_front = front − (1 − p)·chg + min(back − p·chg, 0)
front = min(est_front, new_qty)
```

*Fill rule:* exec = round(−front / lot). If exec > 0, fill exec lots and set front = 0. So a trade that overshoots the queue fills you.

Probability functions:
- `PowerProbQueueFunc(n)`: p = b^n / (b^n + f^n), where b = back and f = front
- `PowerProbQueueFunc2`: p = b^n / (b + f)^n
- `PowerProbQueueFunc3`: p = 1 − (f/(f + b))^n
- `LogProbQueueFunc`: p = ln(1+b) / (ln(1+b) + ln(1+f))
- `LogProbQueueFunc2`: p = ln(1+b) / ln(1+b+f)

f(0) = 0 and f(1) = 1 is the boundary condition: at the head every cancel is behind you, at the tail every cancel is ahead.

`NoPartialFillExchange` fills your whole order when:
- your price crosses the opposite best, **or**
- a trade prints strictly through your price, **or**
- you are at the front and a trade prints at your price.

`PartialFillExchange` fills only the remaining trade qty at the front. The docs warn that replay cannot model market impact.

**Rigtorp, "Estimating order queue position" (2013)** ([link](https://rigtorp.se/2013/06/08/estimating-order-queue-position.html)) [V]
- Initial estimate V̂ = Q − S, after detecting your own S appearing in the feed.
- A fill-caused decrease applies fully: V̂ ← max(V̂ + ΔQ, 0).
- A cancel or unknown decrease applies with probability p: V̂ ← max(V̂ + p·ΔQ, 0), where p = f(V̂) / (f(V̂) + f(Q − S − V̂)) and f is ln(1+x) or the identity. f should be calibrated on live fills.

**Literature** [M]
- Moallemi & Yuan, "A Model for Queue Position Valuation in a Limit Order Book" (2016/17 working paper, Columbia). In large-tick assets, the value of the front versus the back of the queue is of the order of the half-spread, so ignoring queue position overstates PnL by roughly the whole edge.
- Huang, Lehalle & Rosenbaum (2015), "Simulating and analyzing order book data: the queue-reactive model", *JASA*.
- Cont, Stoikov & Talreja (2010), stochastic order book model.
- Lehalle & Mounjid (2017), "Limit order strategic placement with adverse selection risk and the role of latency".

**L2-diff-only practicalities (critical for Bybit 100 ms and Binance `@depth@100ms`)** [M + V on cadence]
- Updates are **conflated**. One 100 ms delta can hide add→cancel→add sequences and many trades. [V] Bybit ob200 `ts` steps are exactly 100 ms.
- Always process trades with depth. A level decrease that matches traded volume in the same interval is execution and advances you fully. Only the residual is a cancel to apportion. That is the `cum_trade_qty` subtraction above.
- Without trade prints you cannot tell fills from cancels. A depth-only backtest must either assume every decrease is ahead of you (optimistic) or assume nothing is (no touch fills ever). Both badly bias PnL.
- Requiring "fill when price is touched" is optimistic. Requiring "fill only on a trade-through" is pessimistic. Bracket results with both, plus a probabilistic queue model.
- Align clocks: use the exchange matching timestamp (Bybit `cts`, Binance `T`), not receive time, to order trades against depth. Then add latency separately on top.
- When a level vanishes and reappears, reset front to the new qty, i.e. you are at the back.
- Your own order is not in the replayed book. Hftbacktest-style models keep the queue as a separate estimate and do not inject your size into the book.

## 4. Microprice, imbalance and OFI

**Stoikov (2018), "The micro-price: a high-frequency estimator of future prices", *Quant. Finance* 18(12); SSRN 2970694.** [M]
- Imbalance I = Q_b / (Q_b + Q_a). The weighted mid is mid + S·(I − ½), but it is noisy and over-reacts.
- The micro-price is P^micro = mid + g(I, S), defined as the limit of E[mid_{τ_i}] over successive mid-change times.
- Estimation:
  1. Discretise I into about 10 buckets and S into 1–2 ticks.
  2. Symmetrise the data by adding the mirrored (1 − I, −Δmid) observation.
  3. Estimate the transient transition matrix Q, the absorbing matrices R1 (mid-change size) and R2 (state after the move), and K (the vector of mid-change values).
  4. Compute G1 = (I − Q)⁻¹R1K and B = (I − Q)⁻¹R2.
  5. Then g = Σ_{i≥0} Bⁱ G1, which converges in about 6 iterations.
- For 1-tick-spread assets such as BTCUSDT perps, g(I) is a smooth S-curve, roughly ±0.3–0.45 tick at the extremes.
- Use it as `fair` in the GLFT quote in place of mid.

**Cont, Kukanov & Stoikov (2014), "The price impact of order book events", *J. Fin. Econometrics* 12(1).** [M]
- Per best-quote update n:
  e_n = 1{Pᵇₙ ≥ Pᵇₙ₋₁}·qᵇₙ − 1{Pᵇₙ ≤ Pᵇₙ₋₁}·qᵇₙ₋₁ − 1{Pᵃₙ ≤ Pᵃₙ₋₁}·qᵃₙ + 1{Pᵃₙ ≥ Pᵃₙ₋₁}·qᵃₙ₋₁
- OFI_k is the sum of e_n over the interval. They fit ΔP_k = β·OFI_k + ε with β ∝ 1/average depth, and find R² ≈ 65% on 10 s windows for US equities.
- This regression is **contemporaneous**, not predictive. Lagged OFI has far lower R².
- Multi-level extension: Xu, Gould & Howison (2019), "Multi-level order-flow imbalance". Cross-asset version: Cont, Cucuringu & Zhang (2023), *Quant. Finance*.

**Crypto horizons and IC** [M, treat as priors]
- On BTC perps, top-of-book or few-level imbalance predicts the sign of the next mid change well. Directional accuracy is often 60–70% conditional on extreme I.
- Correlation with forward 1 s–10 s mid returns is about 0.05–0.2, decaying over roughly 10–60 s.
- The predicted move is usually a fraction of a tick, below the fee. It is useful for **skewing and suppressing quotes** (don't bid when I ≪ ½) but rarely for taking.
- Measure it yourself: rank-IC of the signal against forward microprice/mid returns at 100 ms, 1 s, 5 s, 30 s, 60 s, per day, then report the mean and t-stat across days.

## 5. Adverse selection, fees and tick-size economics

**Markouts** [M]
- For a fill at price p, side s (+1 buy), time t: markout(τ) = s·(m_{t+τ} − p)/p, in bp, for τ ∈ {0, 100 ms, 1 s, 5 s, 30 s, 60 s, 300 s}. m is mid or microprice.
- τ = 0 gives the realised half-spread. The drop from τ = 0 to τ = ∞ is adverse selection.
- Break down by: fill queue position, signal bucket, time of day, volatility regime, and whether the level then depleted.
- PnL attribution: total = Σ spread capture (vs mid at fill) + Σ adverse-selection markout + inventory/hedge PnL − fees + rebates. A healthy MM shows positive capture, negative markout, and net > 0 only after rebates.
- Naive MM loses because touch fills happen disproportionately just before the mid moves through you (informed or sweeping flow). Latency makes it worse: stale quotes get picked off before cancels land.
- Backtests that ignore latency and queue position mostly book the "good" fills and miss the toxic ones.

**Fees** [V via third-party sources plus Binance's support article; confirm on the live fee pages]
- Binance USD-M Regular/VIP0: maker 0.020%, taker 0.050%. A BNB-payment discount of about 10% applies.
- Bybit derivatives VIP0: maker 0.020%, taker 0.055%.
- Rebates exist only for MM-program or top tiers. hftbacktest models −0.005% as Binance's best.

**Tick economics** [V tick = 0.10 on both venues from data. Prices: ~$108k on 2025-09-01]
- One tick = 0.10 / 108,000 ≈ 0.093 bp (0.0009%). The user's 0.0015% figure corresponds to BTC ≈ $67k.
- The spread is almost always 1 tick. A maker round trip at VIP0 costs 2 × 2 bp = 4 bp, about 43 ticks.
- Capturing the full 1-tick spread on every round trip still loses about 3.9 bp gross of adverse selection. **Retail-fee MM on BTCUSDT is structurally negative-EV.**
- Viable regimes:
  1. Maker fee ≤ 0 (rebate tiers), where profit ≈ rebate + small spread − adverse selection.
  2. Very selective quoting: quote only when imbalance/microprice says the fill is likely benign, and hold inventory to capture moves of several bp. That is really short-horizon directional trading using limit orders.
  3. Wider-tick or less-liquid pairs, where the spread is many bp.
- The backtester must report results at VIP0 **and** at a rebate tier. Otherwise "profitable" results are meaningless.

## 6. Public data sources (URL patterns verified by HTTP requests)

**Binance — data.binance.vision** [V]
- Pattern: `https://data.binance.vision/data/futures/um/daily/{type}/{SYM}/{SYM}-{type}-{YYYY-MM-DD}.zip` (+ `.CHECKSUM`). `monthly/` exists too. To list, use `https://s3-ap-northeast-1.amazonaws.com/data.binance.vision?delimiter=/&prefix=data/futures/um/daily/...`.
- Futures UM daily types: `aggTrades, bookDepth, bookTicker, indexPriceKlines, klines, markPriceKlines, metrics, premiumIndexKlines, trades`.
- `trades`: `id,price,qty,quote_qty,time,is_buyer_maker`. Time is ms. `is_buyer_maker=true` means the aggressor sold. BTCUSDT 2025-09-01 has about 3.27 M rows.
- `aggTrades`: `agg_trade_id,price,quantity,first_trade_id,last_trade_id,transact_time,is_buyer_maker`.
- `bookTicker`: `update_id,best_bid_price,best_bid_qty,best_ask_price,best_ask_qty,transaction_time,event_time`. It is tick-by-tick BBO, **but for BTCUSDT it only covers 2023-05-16 … 2024-03-30**, then stops.
- `bookDepth`: `timestamp,percentage,depth,notional`. It holds cumulative depth at ±1..5% of price, sampled roughly every 30–60 s. **It is not an order book and is useless for LOB replay.**
- **No L2 diff data is published.** Full depth requires self-recording `<sym>@depth@100ms` (with U/u/pu sequence checks and a REST snapshot) or a paid vendor.
- Spot daily types are only `aggTrades, klines, trades`.

**Bybit** [V]
- Trades: `https://public.bybit.com/trading/{SYM}/{SYM}{YYYY-MM-DD}.csv.gz`. Updated through yesterday.
  - Columns: `timestamp,symbol,side,size,price,tickDirection,trdMatchID,grossValue,homeNotional,foreignNotional,RPI`.
  - `timestamp` is float seconds with 4 decimals (0.1 ms). `side` is the taker side. `RPI` flags Retail-Price-Improvement fills.
  - `public.bybit.com/spot/` exists for spot.
- Order book: `https://quote-saver.bycsi.com/orderbook/{linear|spot}/{SYM}/{YYYY-MM-DD}_{SYM}_{obN}.data.zip`. This is the host behind bybit.com/derivatives/en/history-data; `quote-saving.bybit.com` did not resolve in my checks.
- **Linear BTCUSDT switched from `ob500` (available through at least 2025-08-15) to `ob200` (2025-09-01 onward).** Files are about 180–350 MB per day. Spot `ob200` exists for 2025-09-01.
- Format: one WebSocket JSON message per line, e.g. `{"topic":"orderbook.200.BTCUSDT","type":"snapshot"|"delta","ts":ms,"cts":ms,"data":{"s","b":[[px,qty],...],"a":[...],"u":updateId,"seq":crossSeq}}`.
  - The first line is a 200-level snapshot. Deltas follow every 100 ms, and qty = 0 means delete the level.
  - Further snapshots can appear mid-file (e.g. on service restart); on `type=="snapshot"` you must rebuild the book from scratch.
  - NautilusTrader notes the file can start about 1 min before midnight UTC ([tutorial](https://nautilustrader.io/docs/nightly/tutorials/backtest_orderbook_bybit/)).
- Bybit trades plus ob200/ob500 deltas form the **best free L2 + trades pair** for this project.

**Tardis.dev** [V]
- The first day of each month is free without an API key. Other days return 401.
- Pattern: `https://datasets.tardis.dev/v1/{exchange}/{data_type}/{YYYY}/{MM}/01/{SYM}.csv.gz`, with exchange `binance-futures` or `bybit`.
- `incremental_book_L2`: `exchange,symbol,timestamp,local_timestamp,is_snapshot,side,price,amount`. Times are µs. `amount` is the **new absolute level size**, not a delta; 0 means remove. A run of `is_snapshot=true` rows resets the book.
- `trades`: `exchange,symbol,timestamp,local_timestamp,id,side,price,amount`, where `side` is the aggressor.
- Also `book_snapshot_25` (wide columns `asks[i].price` …), `quotes`, `derivative_ticker`, `liquidations`.
- This gives free **Binance futures full-depth diffs**, about 12 days a year.

**LOBSTER** [M]
- Free samples at https://lobsterdata.com/info/DataSamples.php: AAPL, AMZN, GOOG, INTC, MSFT on 2012-06-21, at levels 1/5/10/30/50.
- `message` file: `Time(sec after midnight), Type(1 submit, 2 partial cancel, 3 delete, 4 exec visible, 5 exec hidden, 6 cross, 7 halt), OrderID, Size, Price(×10⁴), Direction(±1)`.
- `orderbook` file: `AskP1, AskS1, BidP1, BidS1, …`.
- This is true L3 data, useful to **validate your L2 queue model against ground truth**.

**Crypto Lake** [M, unverified]
- Sample data via `pip install lakeapi`, then `lakeapi.use_sample_data(anonymous_access=True)` and `lakeapi.load_data(table="book"|"trades"|"book_delta_v2", ...)`.
- Samples are a few symbols and days on Binance futures; full access is paid.

## 7. Backtesting rigour

- **Walk-forward:**
  - Select parameters (γ, adj, grid spacing, signal weight c1) on days [d−N, d−1] and test on day d, then roll.
  - Never tune on the test day. Estimate rolling A, k, σ strictly causally (hftbacktest uses a 10-minute trailing window).
- **Many days and regimes:** use at least 20–30 days spanning trend, chop and high-vol events (Tardis free days give about 12 monthly samples). Report the distribution of daily PnL (mean, median, % positive days, worst day), not only the aggregate.
- **Sharpe:**
  - Compute it from daily PnL and annualise with √365 for 24/7 crypto. State the frequency: 5-minute resampling inflates it.
  - MM Sharpes of 10–20 on 5 days (hftbacktest) are not evidence of anything at retail fees.
  - Correct for search with the Deflated Sharpe Ratio (Bailey & López de Prado 2014, *J. Portfolio Mgmt.*). Estimate the probability of backtest overfitting with CSCV (Bailey, Borwein, López de Prado & Zhu 2017, *J. Comp. Finance*).
  - Bootstrap daily PnL for confidence intervals.
- **Sensitivity sweeps (mandatory for a fill simulator):**
  - queue model: RiskAverse vs Prob(power 2/3, log) vs optimistic touch-fill
  - latency: 0 / 10 / 50 / 200 ms, applied to both feed and order entry
  - fee tier: VIP0 vs rebate
  - partial vs no partial fills

  If the sign of PnL flips across these, the result is a modelling artefact.
- **Report:** gross spread capture, markouts, fees and rebates, inventory PnL, turnover, fill ratio, average queue position at fill, max inventory, and time at the inventory limit.
- **Sanity baselines:**
  - A symmetric fixed-spread quoter with no skew should lose roughly fees plus adverse selection.
  - A strategy using future-peeking fills, where the next mid is known, should look absurdly good. If yours looks like that, look for look-ahead in the event ordering, especially trades vs depth at equal timestamps.
- **Validation:** if live paper trading is possible, compare simulated fills/hour and markout curves to live, then pick the queue model that matches. The hftbacktest queue-model tutorial frames model choice exactly this way ([link](https://hftbacktest.readthedocs.io/en/latest/tutorials/Probability%20Queue%20Models.html)).

---

### Key sources
- hftbacktest GLFT tutorial: https://hftbacktest.readthedocs.io/en/latest/tutorials/GLFT%20Market%20Making%20Model%20and%20Grid%20Trading.html
- hftbacktest OBI alpha tutorial: https://hftbacktest.readthedocs.io/en/latest/tutorials/Market%20Making%20with%20Alpha%20-%20Order%20Book%20Imbalance.html
- hftbacktest fill/queue docs: https://hftbacktest.readthedocs.io/en/latest/order_fill.html ; source: https://github.com/nkaz001/hftbacktest/blob/master/hftbacktest/src/backtest/models/queue.rs
- Hummingbot intensity estimator: https://github.com/hummingbot/hummingbot/blob/master/hummingbot/strategy/__utils__/trailing_indicators/trading_intensity.pyx
- Rigtorp queue position: https://rigtorp.se/2013/06/08/estimating-order-queue-position.html
- NautilusTrader Bybit tutorial: https://nautilustrader.io/docs/nightly/tutorials/backtest_orderbook_bybit/
- Binance fees: https://www.binance.com/en/support/articles/360033544231 ; Bybit VIP0 (secondary): https://www.quantconnect.com/docs/v2/writing-algorithms/reality-modeling/transaction-fees/supported-models
- Data: https://data.binance.vision , https://public.bybit.com/trading/ , https://quote-saver.bycsi.com/orderbook/ , https://datasets.tardis.dev/v1/ , https://lobsterdata.com/info/DataSamples.php
- Papers: Avellaneda & Stoikov 2008; Guéant, Lehalle & Fernandez-Tapia 2013 (arXiv:1105.3115); Guéant 2017; Stoikov 2018 (SSRN 2970694); Cont, Kukanov & Stoikov 2014 (arXiv:1011.6402); Moallemi & Yuan 2016; Huang, Lehalle & Rosenbaum 2015; Bailey & López de Prado 2014.
