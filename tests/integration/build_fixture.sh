#!/usr/bin/env bash
# `itch build` and `itch bars` on the fixture day: all datasets written, identical on a second run.
set -euo pipefail
itch_bin=$1
repo=$2
day=$repo/tests/fixtures/mini_day.itchbin
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

printf 'FIX00\nFIX01\n' >"$work/watchlist.txt"
sed "s#configs/watchlist.txt#$work/watchlist.txt#" "$repo/configs/default.toml" >"$work/config.toml"
export ITCH_DATA_DIR=$work ITCH_MCAST_GROUP=239.192.0.1 ITCH_MCAST_IFACE=127.0.0.1
export ITCH_MOLD_SESSION=ITCHDEMO01

build() {
  "$itch_bin" build --config "$work/config.toml" --file "$day" --date 2019-01-30 \
    --root "$work/$1" --report "$work/$1.json" 2>/dev/null
}

build first
build second
cmp "$work/first.json" "$work/second.json"
for dataset in book_l3 trades bbo bars book_snapshots trading_state noii instruments; do
  ls "$work/first/$dataset/venue=XNAS/symbol=FIX01/date=2019-01-30/part-0000.parquet" >/dev/null ||
    ls "$work/first/$dataset/venue=XNAS/symbol=_MARKET/date=2019-01-30/part-0000.parquet" >/dev/null
done
(cd "$work/first" && find . -name '*.parquet' | sort | xargs sha256sum) >"$work/first.sum"
(cd "$work/second" && find . -name '*.parquet' | sort | xargs sha256sum) >"$work/second.sum"
cmp "$work/first.sum" "$work/second.sum"

"$itch_bin" build --config "$work/config.toml" --file "$day" --date 2019-01-30 \
  --root "$work/some" --datasets trades,bars --outputs-for all 2>/dev/null
[ ! -e "$work/some/book_l3" ] || {
  echo "--datasets did not limit the output" >&2
  exit 1
}
ls "$work/some/trades/venue=XNAS/symbol=FIX19/date=2019-01-30/part-0000.parquet" >/dev/null

"$itch_bin" bars --file "$day" --date 2019-01-30 --symbol FIX01 --interval 1m 2>/dev/null |
  grep -q '^09:30:00.000000000 '
"$itch_bin" bars --file "$day" --date 2019-01-30 --symbol FIX01 --interval 5s --session rth \
  --out "$work/bars/out.parquet" 2>/dev/null
ls "$work/bars/out.parquet" >/dev/null

set +e
"$itch_bin" bars --file "$day" --date 2019-01-30 --symbol FIX01 --interval 7h >/dev/null 2>&1
status=$?
set -e
[ "$status" -eq 2 ] || {
  echo "expected exit 2 for a bad interval, got $status" >&2
  exit 1
}
