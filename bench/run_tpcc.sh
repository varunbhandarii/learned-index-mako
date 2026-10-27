#!/usr/bin/env bash
# Run Mako's TPC-C driver under several index configurations with the same settings, and
# summarise the results.
#
# Usage: bench/run_tpcc.sh [threads=2] [runs=3] [alex_tables=item,stock,warehouse,district]
# ENGINES (default "masstree alex-inserts alex") picks the configurations:
#   masstree      every table on Masstree
#   alex-inserts  ALEX tables as built by Mako's loaders (MAKO_ALEX_REBUILD=0)
#   alex          ALEX tables, rebuilt with ALEX's bulk loader once loading finishes
#
# Each run is one shard without replication for Mako's fixed 30 s measurement window,
# preceded by loading (one warehouse per thread). Configurations are interleaved within
# each run. Per-run logs and summary.md/summary.csv go to $RESULTS_DIR (default
# bench/results/tpcc-<date>-t<threads>/).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
THREADS="${1:-2}"
RUNS="${2:-3}"
TABLES="${3:-item,stock,warehouse,district}"
ENGINES="${ENGINES:-masstree alex-inserts alex}"
MAKO="$ROOT/third_party/mako"
OUT="${RESULTS_DIR:-$ROOT/bench/results/tpcc-$(date +%Y-%m-%d)-t$THREADS}"
mkdir -p "$OUT"
[ -x "$MAKO/build/dbtest" ] || { echo "build first: scripts/build_mako.sh" >&2; exit 1; }

# VmRSS in KiB of the dbtest process once loading has finished.
rss_after_load() {
  local log="$1" pid=""
  for _ in $(seq 1 600); do
    pid="$(pgrep -n -x dbtest || true)"
    if [ -n "$pid" ] && grep -q "Finish loading data" "$log" 2>/dev/null; then
      awk '/^VmRSS:/ {print $2}' "/proc/$pid/status" 2>/dev/null && return
    fi
    sleep 0.5
  done
  echo "NA"
}

run_once() {
  local engine="$1" i="$2" log="$OUT/$engine-run$2.log"
  pkill -9 -x dbtest 2>/dev/null || true
  (
    cd "$MAKO"
    unset MAKO_INDEX MAKO_ALEX_REBUILD
    case "$engine" in
      alex) export MAKO_INDEX=alex MAKO_ALEX_TABLES="$TABLES" ;;
      alex-inserts) export MAKO_INDEX=alex MAKO_ALEX_TABLES="$TABLES" MAKO_ALEX_REBUILD=0 ;;
    esac
    /usr/bin/time -v timeout 600 ./build/dbtest --num-threads "$THREADS" --shard-index 0 \
      --shard-config "src/mako/config/local-shards1-warehouses$THREADS.yml" \
      -F "config/1leader_2followers/paxos${THREADS}_shardidx0.yml" -F config/occ_paxos.yml \
      -P localhost
  ) > "$log" 2>&1 &
  local runner=$!
  local rss
  rss="$(rss_after_load "$log")"
  wait "$runner" || echo "run exited with status $? (see $log)" >&2
  echo "rss_after_load_kib: $rss" >> "$log"
}

for i in $(seq 1 "$RUNS"); do
  for engine in $ENGINES; do
    echo "run $i/$RUNS: $engine"
    run_once "$engine" "$i"
  done
done

"$ROOT/bench/summarize.sh" "$OUT" "$THREADS" "$TABLES" > "$OUT/summary.md"
cat "$OUT/summary.md"
