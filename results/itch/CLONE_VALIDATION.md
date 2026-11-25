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

14 symbols, 9 day(s), 288,295 clones (79,713 with an executed order behind them).

| model | impossible fills (before the order ahead finished) | missed fills (order behind traded first) | clones filled |
|---|---|---|---|
| exact-queue L3 | 0 (0.00%) | 2 (0.00%) | 99.9% |
| L2 power-2 | 13,096 (4.54%) | 1,400 (1.76%) | 100.0% |
| L2 risk-averse | 735 (0.25%) | 11,410 (14.31%) | 99.9% |
| L2 log | 20,947 (7.27%) | 3,264 (4.09%) | 100.0% |
