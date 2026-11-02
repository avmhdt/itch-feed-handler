# Design

## Dispatch

Every message carries a stock locate, a small integer meant to be an array index (ITCH 5.0
"Architecture"). `BookManager` holds flat arrays of 65,536 books, counters and states, so routing a
message is one indexed load, and no symbol string is ever hashed.

## Order lookup

Order references are unique across all symbols for the day, so one `OrderStore` serves every book.
It has a pool of 32-byte `OrderNode`s linked by index, and a map from reference to pool index. Three
maps sit behind one interface for the comparison in AC-H-05: a linear-probing table with
backward-shift deletion (the default), `ankerl::unordered_dense` and `std::unordered_map`. Order maps
are chosen at run time through one predictable branch, so no virtual call is involved.

## Book

Each side is a vector of price levels sorted with the best price at the back. Almost all activity is
near the touch, so a short scan from the back finds the level, and inserting or removing one moves
very little. Levels further out fall back to binary search. Each level is a FIFO queue threaded
through the pool.

Prices and shares stay integers throughout (NFR-05).

## Outputs

The hot thread pushes fixed-size records onto one bounded SPSC queue per dataset, and a writer
thread turns them into Arrow tables and partitioned Parquet, so Arrow never appears on the hot path.
`Sinks` is the only type that knows about it.

Trades stay in memory until the end of the day, because a Broken Trade (1.5.3) can arrive at any
time and a Parquet file cannot be edited. The correction pass flags broken rows, writes `trades`, and
only then builds `bars` from the corrected rows. Bars therefore run on the writer side at the end of
the day, not on the hot thread as the spec sketches. The internal volume counter stays on the hot
thread, so AC-W-03 compares two independent paths.

Consumers read datasets by partition. This DuckDB query is exercised by
`tools/tests/test_downstream_contract.py`:

```sql
SELECT symbol,
       SUM(qty) FILTER (WHERE printable AND NOT broken) AS volume
FROM read_parquet('data/trades/venue=XNAS/symbol=AAPL/date=2019-01-30/*.parquet',
                  hive_partitioning = true)
GROUP BY symbol;
```
