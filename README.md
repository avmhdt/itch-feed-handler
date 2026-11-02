# Market data feed handler (Nasdaq ITCH 5.0)

> Parse a real exchange binary protocol and rebuild every order book for a full trading day.

**Status:** in build (M4)

| | |
|---|---|
| Track | Quant Developer |
| Estimated effort | 3–5 weeks |
| Stack | C++20 or Rust |
| Data cost | Free |
| Strongest for | HFT quant dev, market-data teams |

## Requirements

- [x] Zero-copy parsing of every TotalView-ITCH 5.0 message type (big-endian, fixed width). (FR-02)
- [x] An order-level book per symbol: add, execute, execute with price, cancel, delete and replace. (FR-05 to FR-09)
- [x] Handling of trading-state, cross and NOII messages; output of trades, OHLCV and VWAP bars. (FR-10 to FR-16)
- [ ] A transport layer that replays the file as MoldUDP64 over local UDP multicast, with gap detection and recovery.
- [x] Memory-mapped streaming over multi-GB files, and a CLI that dumps book state at any timestamp. (FR-01, FR-18, AC-W-06)

## Data

Raw and licensed data live in `data/`, which is git-ignored. Only download scripts and small synthetic fixtures are committed.

| Dataset | Cost | What it gives you |
|---|---|---|
| [Nasdaq ITCH sample files](https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/) | Free | Full trading days of TotalView-ITCH 5.0, several GB each. |
| [ITCH 5.0 specification](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHspecification.pdf) | Free | The protocol you implement. MoldUDP64 specs are on the same site. |
| [Official daily closes (any source)](https://stooq.com/db/h/) | Free | Cross-check your closing-cross prices and volumes. |

## Success criteria

### Works (correct and reproducible)

- [x] Message counts by type match a reference parser across the whole file. (AC-W-01)
- [x] Closing-cross prices and daily volumes match official figures for liquid symbols. (AC-W-02, AC-W-03)
- [x] Books never cross outside auctions, and every order is accounted for at the end of the day. (AC-W-04, AC-W-05)

### Impresses hirers

- [ ] A full day parsed in seconds, with messages per second published for parse-only and full book build, plus flamegraphs.
- [ ] Benchmarks against a Python baseline and an existing open-source parser.
- [ ] A gap-recovery demo: drop packets on purpose and show a correct resync.
- [ ] A fuzzed parser that never crashes on malformed input.

## Questions this project must answer

- How do you handle a replace that changes price?
- Where does the time go: parsing, hashing or book updates?
- What changes when this reads from a NIC instead of a file?
