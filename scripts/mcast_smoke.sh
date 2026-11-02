#!/usr/bin/env bash
# P-13: does this machine deliver UDP multicast on loopback? Replays the fixture with no drops and
# compares the digest with file mode. Prints PASS or FAIL.
set -euo pipefail
cd "$(dirname "$0")/.."
itch=${ITCH_BIN:-$HOME/build/itch-feed-handler/release/src/itch}
group=${ITCH_MCAST_GROUP:-239.192.0.1}:${ITCH_MCAST_PORT:-30001}
retrans_port=${ITCH_RETRANS_PORT:-30002}
fixture=tests/fixtures/mini_day.itchbin
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$itch" listen --group "$group" --retrans "127.0.0.1:$retrans_port" --build-books \
  --digest-out "$work/digest.txt" --idle-timeout-ms 10000 2>/dev/null &
listener=$!
sleep 0.5
"$itch" replay --file "$fixture" --group "$group" --retrans-port "$retrans_port" \
  --max-msgs-per-s 200000 --linger-ms 500 2>/dev/null

if wait "$listener" && [ "$(cat "$work/digest.txt")" = "$("$itch" digest --file "$fixture" 2>/dev/null)" ]; then
  echo "PASS: multicast on loopback delivers and the digest equals file mode"
else
  echo "FAIL: multicast on loopback did not deliver; see R-01 for the unicast fallback" >&2
  exit 1
fi
