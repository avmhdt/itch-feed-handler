#!/usr/bin/env bash
# `itch book-at`, `check` and `digest` on the fixture day against the generator's golden files.
set -euo pipefail
itch_bin=$1
fixtures=$2
day=$fixtures/mini_day.itchbin
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$itch_bin" book-at --file "$day" --date 2019-01-30 --symbol FIX01 --time 10:30:00.000 \
  --depth 10 >"$work/book_at.txt" 2>/dev/null
cmp "$work/book_at.txt" "$fixtures/golden_book_at.txt"

"$itch_bin" check --file "$day" --date 2019-01-30 --per-symbol --report "$work/checks.json" 2>/dev/null
python3 "$(dirname "$0")/check_outcome.py" "$work/checks.json" "$fixtures/golden_outcome.json"

first=$("$itch_bin" digest --file "$day" 2>/dev/null)
dense=$("$itch_bin" digest --file "$day" --order-map unordered_dense 2>/dev/null)
standard=$("$itch_bin" digest --file "$day" --order-map std 2>/dev/null)
[ "$first" = "$dense" ] && [ "$first" = "$standard" ] || {
  echo "digests differ across order maps: $first $dense $standard" >&2
  exit 1
}

early=$("$itch_bin" digest --file "$day" --at 10:00:00 2>/dev/null)
[ "$early" != "$first" ] || {
  echo "digest --at had no effect" >&2
  exit 1
}

set +e
"$itch_bin" book-at --file "$day" --symbol NOPE --time 10:00:00 >/dev/null 2>&1
status=$?
set -e
[ "$status" -eq 2 ] || {
  echo "expected exit 2 for an unknown symbol, got $status" >&2
  exit 1
}
