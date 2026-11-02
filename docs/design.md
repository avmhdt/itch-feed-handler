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
