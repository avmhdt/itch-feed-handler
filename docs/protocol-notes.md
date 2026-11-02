# Protocol notes

Section numbers refer to the Nasdaq TotalView-ITCH 5.0 specification, revision of April 28, 2023.

## Decoding

- All integers are big-endian and most fields are unaligned, so `load_be` goes through `memcpy`
  ("Data Types"). Timestamps are 48-bit nanoseconds since midnight Eastern.
- Prices are `Price(4)`, except the MWCB levels, which are `Price(8)` (1.2.5.1).
- Alpha fields are left-justified and space-padded ("Data Types").
- The 23 message lengths in `include/itch/proto/spec_table.hpp` come from the field tables in
  section 1, and `messages.hpp` asserts each view against that table at compile time.
- Trade (1.5.1) always carries order reference zero and, since 2014-07-14, side "B", so neither
  field says anything about the match. `trades.side` is 0 for these rows.

## Replace

An Order Replace (1.4.5) ends the original order and starts a new one under a new reference.

- The old reference is dead at once: all later messages use the new one.
- Side, stock and attribution do not appear in the message, so they carry over from the original.
- The new order joins the back of the queue at its price. Priority is lost even when the price is
  unchanged and the size only shrinks. `OrderBook::relink` in `src/book/order_book.cpp` does this,
  and `tests/unit/order_book_replace_test.cpp` covers it.
- The accounting identity (DQ-13) treats it as both a removal and an addition: the remaining shares
  of the original count as `replaced_out`, and the new shares count as `added`.

## Crosses

- NOII (1.6) starts at 09:25 for the opening cross and 15:50 for the closing cross. For halts, IPOs
  and pauses it runs at one-second intervals after the quoting period starts.
- Orders may overlap during these periods, so a crossed book is not an error there. DQ-12 opens a
  window at the first NOII of the cross, or at the scheduled time for Nasdaq-listed symbols, and
  closes it at the Q message.
- Nasdaq runs its opening and closing crosses for Nasdaq-listed issues only, so the scheduled
  windows apply to market categories Q, G and S (1.2.1).
- A halted symbol is in a window from its H message until it resumes. If a NOII has announced a
  halt cross, the window stays open until that cross prints, because the Trading Action resuming
  the symbol can come before the Q message (1.5.2).
- Cross Trade (1.5.2) is the bulk print. Shares matched in a cross also appear as non-printable
  executions (1.4.2), which are not counted again in volume (DQ-09).
