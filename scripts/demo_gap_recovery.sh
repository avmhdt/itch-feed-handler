#!/usr/bin/env bash
# AC-H-03: replay a full day over loopback multicast with 1% random drops plus a 10,000-packet
# burst, recover every gap, and end with the file-mode digest. A second run with a window smaller
# than the burst must exit 5 with a clear message.
#
#   scripts/demo_gap_recovery.sh [MMDDYYYY]
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$HOME/.local/bin:$PATH"
export UV_PROJECT_ENVIRONMENT="${UV_PROJECT_ENVIRONMENT:-$HOME/build/itch-feed-handler/venv}"

day=${1:-01302019}
date="${day:4:4}-${day:0:2}-${day:2:2}"
data_dir=${ITCH_DATA_DIR:-$HOME/data/itch-feed-handler}
file=$data_dir/interim/itch/$day.NASDAQ_ITCH50
itch=${ITCH_BIN:-$HOME/build/itch-feed-handler/release/src/itch}
group=${ITCH_MCAST_GROUP:-239.192.0.1}:${ITCH_MCAST_PORT:-30001}
retrans_port=${ITCH_RETRANS_PORT:-30002}
rate=${ITCH_DEMO_MSGS_PER_S:-1000000}
burst_at=5000000
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
: >"$work/listen_digest.txt"

echo "file-mode digest and counts"
"$itch" digest --file "$file" --order-capacity 8000000 >"$work/file_digest.txt" 2>/dev/null
"$itch" stats --file "$file" --json "$work/file_counts.json" 2>/dev/null

echo "run 1: 1% random drops plus a burst of 10,000 packets"
"$itch" listen --group "$group" --retrans "127.0.0.1:$retrans_port" --build-books \
  --order-capacity 8000000 --date "$date" --digest-out "$work/listen_digest.txt" \
  --stats-out "$work/listen_stats.json" &
listener=$!
sleep 1
"$itch" replay --file "$file" --group "$group" --retrans-port "$retrans_port" \
  --max-msgs-per-s "$rate" --drop-rate 0.01 --drop-burst "$burst_at:10000" --seed 42
run1=0
wait "$listener" || run1=$?

echo "run 2: a window smaller than the burst"
set +e
"$itch" listen --group "$group" --retrans "127.0.0.1:$retrans_port" \
  --rerequest-max-retries 10 --stats-out "$work/short_stats.json" 2>"$work/short.log" &
listener=$!
sleep 1
"$itch" replay --file "$file" --group "$group" --retrans-port "$retrans_port" --limit 7000000 \
  --max-msgs-per-s "$rate" --drop-burst "$burst_at:10000" --retrans-buffer 50000 --seed 42 \
  --linger-ms 1000
wait "$listener"
run2=$?
set -e
cat "$work/short.log"

uv run --project tools itchtools gap-report --date "$date" \
  --file-digest "$work/file_digest.txt" --listen-digest "$work/listen_digest.txt" \
  --file-counts "$work/file_counts.json" --listen-stats "$work/listen_stats.json" \
  --listen-exit "$run1" --short-exit "$run2" --short-log "$work/short.log"

python3 - "results/validation/gap_demo_$date.json" "$day" >>results/experiment_log.csv <<'PY'
import datetime
import json
import subprocess
import sys

report = json.load(open(sys.argv[1]))
stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
head = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], text=True).strip()
rate = round(report["messages"] / report["wall_s"])
print(
    f"{stamp},{head},owner-desktop-wsl2,gcc-13 release,-O3 -march=x86-64-v3,"
    f"{sys.argv[2]}.NASDAQ_ITCH50,gap_demo,1,{report['messages']},{report['wall_s']},{rate},,,"
    f"{report['gaps']} gaps; passed={report['passed']}"
)
PY
