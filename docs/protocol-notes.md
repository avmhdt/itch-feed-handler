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
