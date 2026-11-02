#!/usr/bin/env bash
# `itch stats` on the fixture day equals the counts its generator recorded, and is byte-stable.
set -euo pipefail
itch_bin=$1
fixtures=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$itch_bin" stats --file "$fixtures/mini_day.itchbin" --date 2019-01-30 --json "$work/one.json" 2>/dev/null
"$itch_bin" stats --file "$fixtures/mini_day.itchbin" --date 2019-01-30 --json "$work/two.json" 2>/dev/null
cmp "$work/one.json" "$work/two.json"
cmp "$work/one.json" "$fixtures/golden_stats.json"
