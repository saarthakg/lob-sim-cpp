# Queue priority against real Nasdaq orders

A real order's fate is in the feed, so it can referee the simulator. For a
sample of real displayed orders R1 that were eventually fully executed, a
one-share simulated clone is placed at the same price and side just after R1
entered the book. Price-time priority then brackets the clone's fill exactly:

  * not before R1 was completely filled (R1 is ahead of the clone);
  * no later than the first execution of R2, the next real order to join the
    same price after the clone (R2 is behind it).

"Early" = filled before R1 finished (an impossible fill). "Late" = R2 traded
while the clone was still unfilled (a missed fill). The bounds are computed
here in Python straight from the records, independently of the C++ book.

R1 is sampled among orders that were *not* cancelled, so the L2 models, which
expect some of the queue ahead to cancel, look worse here than on an average
order. Read their rows as "how often does the model jump a queue that really
stayed in place", not as an unconditional error rate.

14 symbols, 1 day(s), 26,013 clones (6,622 with an executed order behind them).

| model | impossible fills (before the order ahead finished) | missed fills (order behind traded first) | clones filled |
|---|---|---|---|
| exact-queue L3 | 0 (0.00%) | 2 (0.03%) | 100.0% |
| L2 power-2 | 3,876 (14.90%) | 107 (1.62%) | 100.0% |
| L2 risk-averse | 105 (0.40%) | 1,553 (23.45%) | 99.8% |
| L2 log | 6,021 (23.15%) | 303 (4.58%) | 100.0% |
