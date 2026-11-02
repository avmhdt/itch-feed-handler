#!/usr/bin/env bash
# `itch stats --raw-types` on a tiny hand-built file, then on a truncated one.
set -euo pipefail
itch_bin=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

printf '\x00\x02S\x01\x00\x01A\x00\x01A' >"$work/ok.bin"
report=$("$itch_bin" stats --file "$work/ok.bin" --raw-types 2>/dev/null)
expected='{"bytes":10,"by_type":{"0x41":2,"0x53":1},"empty_frames":0,"total":3,"truncated":false}'
[ "$report" = "$expected" ] || {
  echo "unexpected report: $report" >&2
  exit 1
}

printf '\x00\x05S' >"$work/bad.bin"
set +e
"$itch_bin" stats --file "$work/bad.bin" --raw-types >/dev/null 2>&1
status=$?
set -e
[ "$status" -eq 3 ] || {
  echo "expected exit 3 for truncation, got $status" >&2
  exit 1
}
