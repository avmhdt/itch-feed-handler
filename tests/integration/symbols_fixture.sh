#!/usr/bin/env bash
# `itch symbols` on the fixture day equals the directory its generator recorded.
set -euo pipefail
itch_bin=$1
fixtures=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$itch_bin" symbols --file "$fixtures/mini_day.itchbin" --json "$work/symbols.json"
cmp "$work/symbols.json" "$fixtures/golden_symbols.json"
"$itch_bin" symbols --file "$fixtures/mini_day.itchbin" | grep -q '^1 FIX00 Q 100$'
