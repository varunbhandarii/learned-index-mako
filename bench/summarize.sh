#!/usr/bin/env bash
# Summarise the logs written by run_tpcc.sh as a Markdown table of medians.
# Usage: bench/summarize.sh <results-dir> <threads> <alex-tables>
set -euo pipefail
DIR="$1"
THREADS="$2"
TABLES="$3"

# Median of the numbers on stdin ("NA" if there are none).
median() {
  sort -g | awk '{v[NR]=$1} END {if (NR==0) print "NA"; else if (NR%2) print v[(NR+1)/2]; else print (v[NR/2]+v[NR/2+1])/2}'
}

# Values of "key: value" lines across the runs of one engine.
values() {
  local engine="$1" key="$2"
  for f in "$DIR/$engine"-run*.log; do
    awk -v k="$key" '{sub(/^[ \t]+/, "")}
      index($0, k ": ") == 1 {split(substr($0, length(k) + 3), v, " "); print v[1]; exit}' "$f"
  done | grep -E '^-?[0-9.]+$' || true
}

metric() { values "$1" "$2" | median; }
spread() { values "$1" "$2" | sort -g | awk 'NR==1 {lo=$1} {hi=$1} END {if (NR) printf "%.0f–%.0f\n", lo, hi; else print "NA"}'; }
mib() { values "$1" "$2" | awk '{printf "%.1f\n", $1/1024}' | median; }
load_ms() {
  for f in "$DIR/$1"-run*.log; do
    awk '/timed region dataloading took/ {print $(NF-1); exit}' "$f"
  done | median
}

runs=$(ls "$DIR"/masstree-run*.log | wc -l)
echo "## TPC-C, 1 shard, $THREADS threads, $runs runs per engine (medians)"
echo
echo "ALEX tables: \`$TABLES\`. Everything else stays on Masstree in both configurations."
echo
echo "| Metric | Masstree | ALEX tables |"
echo "| :--- | ---: | ---: |"
row() { echo "| $1 | $(metric masstree "$2") | $(metric alex "$2") |"; }
row "Throughput (txn/s)" agg_throughput
echo "| Throughput range over runs | $(spread masstree agg_throughput) | $(spread alex agg_throughput) |"
row "Committed transactions" n_commits
row "Abort rate (aborts/s)" agg_abort_rate
for t in NewOrder Payment Delivery OrderStatus StockLevel; do
  row "$t latency (ms)" "${t}_local_commit_latency"
done
echo "| Load time (ms) | $(load_ms masstree) | $(load_ms alex) |"
echo "| RSS after loading (MiB) | $(mib masstree rss_after_load_kib) | $(mib alex rss_after_load_kib) |"
echo "| Peak RSS (MiB) | $(mib masstree "Maximum resident set size (kbytes)") | $(mib alex "Maximum resident set size (kbytes)") |"
echo
echo "Mako's own average-latency figure overflows, so it is omitted. Peak RSS grows with the"
echo "number of committed transactions (each NewOrder inserts rows), so it is not an index"
echo "comparison on its own."
echo
echo "ALEX index size at exit (from run 1):"
echo
echo '```'
grep -h "\[learned-index-mako\] alex table=" "$DIR"/alex-run1.log || echo "(no report found)"
echo '```'
