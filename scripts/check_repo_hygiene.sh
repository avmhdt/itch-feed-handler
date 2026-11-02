#!/usr/bin/env bash
# NFR-09: no market data or oversized files in git.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
limit=$((1024 * 1024))
status=0
while IFS= read -r -d '' path; do
  case "$path" in
    *.gz | *.parquet | *.itch | data/*)
      echo "forbidden tracked file: $path" >&2
      status=1
      ;;
  esac
  [ -f "$path" ] || continue
  case "$path" in reports/*) continue ;; esac
  size=$(wc -c <"$path")
  if [ "$size" -gt "$limit" ]; then
    echo "tracked file over 1 MiB: $path ($size bytes)" >&2
    status=1
  fi
done < <(git ls-files -z)
exit "$status"
