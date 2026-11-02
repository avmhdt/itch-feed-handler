# Decisions

Owner decisions and the choices made where the spec was silent or inconsistent.
Precedence: the owner's `DECISIONS.md`, then the spec, then `README.md`.

## Owner decisions copied from `DECISIONS.md` (recorded 2026-10-31)

Project 10 entries, plus the shared entries that apply to this repo.

- **Merging:** agents push feature branches and open PRs. PRs merge automatically once CI is
  green. Branch protection and auto-merge are turned on per repo once CI exists (M1).
- **GitHub:** the `gh` active account is `avmhdt`, with admin on the repo. Actions is enabled.
- **WRDS:** no access. Not used here.
- **Sharadar:** Full History Bundle bought directly from Sharadar, not through Nasdaq Data Link.
  Base URL `https://api.sharadar.com/v1.0`, key in `SHARADAR_API_KEY`, sent in the `x-api-key`
  header so it stays out of URLs and logs. Tables `stocks` (SEP), `funds` (SFP) and `tickers`
  are the ones used here.
- **Sharadar licence:** Personal Use License. Sharadar confirmed in writing on 2026-10-31 that
  aggregate cross-checks are allowed. Publishing stays aggregate-only, with no security-level
  data.
- **Nasdaq and downloads:** sample-file terms acknowledged on 2026-10-31 (P-09): raw and
  row-level data stay out of git, aggregates only. Downloads approved for Nasdaq ITCH samples.
  The owner's confirmation is in the main project session rather than a GitHub issue comment.
- **Money:** everything for this project is USD 0.
- **Toolchain:** installed by the owner's `setup/wsl-toolchain.sh`.
- **Storage:** stay on `D:`; free space is checked at each milestone.
- **Benchmarks:** published runs happen when no multi-week capture is running on this PC. So M6
  waits for that, and this repo stops before M6 until then.
- **Downstream:** `low-latency-matching-engine` D-05 uses `01302019.NASDAQ_ITCH50.gz`, the primary
  day here, and reuses this repo's `book_l3` for that day.
- **Accepted:** every other section 3 decision with a default stands as written.

## Spec decisions in force

| ID | Decision | Value |
|---|---|---|
| D-01 | Sample days | 01302019 (primary), 03272019 (EDT check), 01302020 |
| D-02 | Output writer | Apache Arrow C++ Parquet writer on a background thread |
| D-03 | Reference parser | `adwhit/itchy-rust` (`itchy` crate) |
| D-04 | Symbols written to Parquet | Watchlist of 50 liquid Nasdaq-listed symbols; books built for all |
| D-05 | Language | C++20 |
| D-06 | Paid data spend | Not approved; the volume check uses the ratio method |
| D-07 | Benchmark platform of record | Owner desktop under WSL2, run when no multi-week capture is active |
| D-08 | Visibility and licence | Public; MIT |
| D-09 | Bars | 1s and 1m, all hours, cross volume included and flagged |
| D-10 | Compiler of record | Clang 18 with `-O3 -march=x86-64-v3`; GCC 13 reported alongside |
| D-11 | Official closes | Sharadar SEP/SFP primary; local Stooq snapshot fallback and cross-check |

## Environment (P-01, P-02, P-04, P-06), recorded 2026-11-02

| Item | Version |
|---|---|
| Host | Windows 11 Pro 10.0.26200, WSL2 distro `Ubuntu` (version 2), `docker-desktop` also present |
| Distro | Ubuntu 22.04.5 LTS, kernel 6.18.54.1-microsoft-standard-WSL2 |
| GCC | 13.4.0 |
| Clang, clang-format, clang-tidy | 18.1.8 |
| CMake | 4.4.4 |
| Ninja | 1.13.2 |
| Rust | 1.99.1 |
| uv, Python | uv 0.12.23, CPython 3.12.15 |
| Arrow, Parquet | 26.0.0 |
| CPU, RAM | Intel Core i9-14900K, 32 logical CPUs, 62 GB visible to WSL |

Data is under `~/data/itch-feed-handler` and builds under `~/build/itch-feed-handler`, both on
WSL ext4. The Python virtual environment is at `~/build/itch-feed-handler/venv`
(`UV_PROJECT_ENVIRONMENT`) so that it does not land on `/mnt/d`.

## Choices where the spec is ambiguous or inconsistent

1. **Published md5 unavailable.** The sample site lists `*.md5sum` companions, but on 2026-11-02
   it answers HTTP 404 for them, and the FTP path of the same host timed out. DQ-14 says "where
   available", so `itchtools fetch` treats a 404 as an expected state. It then relies on three
   checks instead: the byte count against `Content-Length`, the gzip CRC and length checked by
   `pigz -dc`, and the clean end of the frame stream. The manifest records
   `gz_md5_source = computed-unverified` and the locally computed md5 and sha256.
2. **Download cap.** `ITCH_MAX_BYTES_PER_S` (default 10 MB/s) caps `itchtools fetch`, and files go
   one at a time, because live recorders share the connection. The server delivered about 1.6 MB/s
   and stalled at times, so the download retries from the bytes on disk.
3. **`data/` symlink and `.gitignore`.** The existing rule `data/` matches directories only, so a
   symlink would show up as untracked, and it would also hide the spec's `tests/data/`. Added
   `/data` and `!tests/data/`. NFR-09's "under data/" means the top-level path.
4. **Line endings.** `core.autocrlf` is on for the Windows side of this checkout, so
   `.gitattributes` forces LF.
5. **Required status check.** The `ci` job gathers every other job, so branch protection requires
   only that one context, as in the sibling repos.
6. **Fixture size.** The spec asks for about 200,000 messages and under 1 MB, which cannot both
   hold: ITCH averages about 33 bytes per framed message. The 1 MB limit is the enforced one
   (NFR-09 and the pre-commit hook), so `mini_day.itchbin` has about 25,000 messages (828 KB) and
   still contains every message type. `gen-fixture --messages` can write larger days on demand.
7. **`itchy` coverage.** `itchy` 0.3.4 has no parser for `h` and `O` and panics on them. As R-03
   allows, `ref_counts` strips those frames in front of `itchy` and counts them from the frame
   header, and its JSON lists them under `framed_only`. Everything else is counted by `itchy`.
8. **`itch stats` additions.** Added `--date`, `--lenient`, and a `system_events` key used for
   AC-W-10. Wall time goes to the log, because NFR-04 needs byte-identical JSON across runs.
9. **Eastern Time rule.** `TradingDay` applies the US daylight-saving rule in force since 2007
   directly instead of reading a tz database. ITCH 5.0 dates from 2013, so every sample day is
   covered, and `itchtools.timeutil` cross-checks the rule against `zoneinfo`.
10. **Cross settlement (R-10).** DQ-12 closes a cross window at the Q message, but real files send
    the non-printable executions of a cross after Q. A crossed book immediately after such an
    execution is counted under `crossed_in_auction`, citing ITCH 5.0 1.4.2. No other message is
    excused. See `docs/protocol-notes.md#crosses`.
11. **Locate table.** The directory lives in `state/instruments.hpp`, one module beyond the spec's
    list, so that `itch symbols` can use it without pulling in the book code.
