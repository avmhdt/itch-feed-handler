#!/usr/bin/env bash
# One command from raw files to every validation report (NFR-14).
#
#   scripts/reproduce.sh            fetch, build, validate each sample day
#   scripts/reproduce.sh --verify   do that twice and compare counts, digests and content hashes
#
# Published benchmarks and figures join this script at M6 and M7.
set -euo pipefail

cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$HOME/.cargo/bin:$PATH"
export DATA_ROOT="${DATA_ROOT:-$HOME/data/itch-feed-handler}"
export ITCH_DATA_DIR="${ITCH_DATA_DIR:-$DATA_ROOT}"
export ITCH_MCAST_GROUP="${ITCH_MCAST_GROUP:-239.192.0.1}"
export ITCH_MCAST_IFACE="${ITCH_MCAST_IFACE:-127.0.0.1}"
export ITCH_MOLD_SESSION="${ITCH_MOLD_SESSION:-ITCHDEMO01}"
export UV_PROJECT_ENVIRONMENT="${UV_PROJECT_ENVIRONMENT:-$HOME/build/itch-feed-handler/venv}"
export CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-$HOME/build/itch-feed-handler/cargo}"

days=(01302019 03272019 01302020)
itch="$HOME/build/itch-feed-handler/release/src/itch"
tools=(uv run --project tools itchtools)
log=results/experiment_log.csv

iso_date() { echo "${1:4:4}-${1:0:2}-${1:2:2}"; }

log_run() { # mode file messages wall_s notes
  local rate
  rate=$(awk -v m="$3" -v s="$4" 'BEGIN { printf "%.0f", (s > 0 ? m / s : 0) }')
  printf '%s,%s,%s,%s,%s,%s,%s,1,%s,%s,%s,,,%s\n' \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$(git rev-parse --short HEAD)" \
    "${ITCH_BENCH_MACHINE:-owner-desktop-wsl2}" "gcc-13 release" "-O3 -march=x86-64-v3" \
    "$(basename "$2")" "$1" "$3" "$4" "$rate" "$5" >>"$log"
}

prepare() {
  "${tools[@]}" fetch --days "$(IFS=,; echo "${days[*]}")" --decompress --keep-gz
  cmake --preset release -DCMAKE_C_COMPILER=gcc-13 -DCMAKE_CXX_COMPILER=g++-13
  cmake --build --preset release
  cargo build --release --locked --manifest-path tools/ref_counts/Cargo.toml
}

run_day() { # MMDDYYYY
  local day=$1 date file start wall messages
  date=$(iso_date "$day")
  file="$ITCH_DATA_DIR/interim/itch/$day.NASDAQ_ITCH50"

  "$itch" stats --file "$file" --json "results/validation/counts_$date.json"
  "${tools[@]}" ref-counts --file "$file"
  "${tools[@]}" validate-counts --date "$date"

  start=$(date +%s.%N)
  "$itch" check --file "$file" --symbols all --order-capacity 8000000 \
    --report "results/validation/checks_$date.json"
  wall=$(awk -v a="$start" -v b="$(date +%s.%N)" 'BEGIN { print b - a }')
  messages=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['messages'])" \
    "results/validation/checks_$date.json")
  log_run check "$file" "$messages" "$wall" "reproduce.sh validation"

  start=$(date +%s.%N)
  "$itch" build --config configs/default.toml --file "$file" \
    --report "$ITCH_DATA_DIR/reports/build_$date.json"
  wall=$(awk -v a="$start" -v b="$(date +%s.%N)" 'BEGIN { print b - a }')
  log_run build "$file" "$messages" "$wall" "reproduce.sh watchlist Parquet"

  "${tools[@]}" ref-daily --date "$date"
  "${tools[@]}" validate-close --date "$date"
  "${tools[@]}" validate-volume --date "$date"
}

fingerprint() { # prints one line per day: counts hash, digest, Parquet content hash
  local day date digest
  for day in "${days[@]}"; do
    date=$(iso_date "$day")
    digest=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['digest'])" \
      "$ITCH_DATA_DIR/reports/build_$date.json")
    printf '%s %s %s %s\n' "$date" "$(sha256sum "results/validation/counts_$date.json" | cut -d' ' -f1)" \
      "$digest" "$("${tools[@]}" content-hash --date "$date")"
  done
}

prepare
for day in "${days[@]}"; do run_day "$day"; done

if [ "${1:-}" = "--verify" ]; then
  first=$(fingerprint)
  for day in "${days[@]}"; do run_day "$day"; done
  second=$(fingerprint)
  printf '%s\n' "$first" >"$ITCH_DATA_DIR/reports/verify_fingerprint.txt"
  if [ "$first" != "$second" ]; then
    echo "reproduce.sh --verify: runs differ" >&2
    diff <(printf '%s\n' "$first") <(printf '%s\n' "$second") >&2 || true
    exit 1
  fi
  echo "reproduce.sh --verify: identical"
fi
