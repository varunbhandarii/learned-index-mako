#!/usr/bin/env bash
# Run the index microbenchmark (bench/index_bench.cc) for every index, key set and thread
# count, one process per configuration, and summarise the medians.
#
# Usage: bench/run_index_bench.sh [keys=1000000] [runs=3] [lookups_per_thread=5000000]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/third_party/mako/build/lim_index_bench"
KEYS="${1:-1000000}"
RUNS="${2:-3}"
LOOKUPS="${3:-5000000}"
OUT="$ROOT/bench/results/index-$(date +%Y-%m-%d)"
[ -x "$BIN" ] || { echo "build first: scripts/build_mako.sh" >&2; exit 1; }
mkdir -p "$OUT"
: > "$OUT/raw.txt"

for run in $(seq 1 "$RUNS"); do
  echo "run $run/$RUNS"
  for keyset in dense4 dense8 random8; do
    for threads in 1 2 4; do
      for index in masstree alex alex-rebuilt; do
        "$BIN" "$index" "$keyset" "$KEYS" "$threads" "$LOOKUPS" >> "$OUT/raw.txt"
      done
    done
  done
done

python3 "$ROOT/bench/summarize_index.py" "$OUT/raw.txt" "$OUT"
cat "$OUT/summary.md"
