#!/usr/bin/env bash
# Summarise the logs written by run_tpcc.sh: summary.csv (metric,engine,median) in the
# results directory, and a Markdown table of medians on stdout.
# Usage: bench/summarize.sh <results-dir> <threads> <alex-tables>
set -euo pipefail
DIR="$1"
THREADS="$2"
TABLES="$3"

ENGINES=()
for e in masstree alex-inserts alex; do
  ls "$DIR/$e"-run*.log > /dev/null 2>&1 && ENGINES+=("$e")
done
declare -A LABEL=([masstree]="Masstree" [alex-inserts]="ALEX, built by inserts" [alex]="ALEX, rebuilt")

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

load_ms() {
  for f in "$DIR/$1"-run*.log; do
    awk '/timed region dataloading took/ {print $(NF-1); exit}' "$f"
  done | median
}

spread() { values "$1" "$2" | sort -g | awk 'NR==1 {lo=$1} {hi=$1} END {if (NR) printf "%.0f–%.0f\n", lo, hi; else print "NA"}'; }

# metric id, label, how to compute (key or special)
ROWS=(
  "throughput|Throughput (txn/s)|agg_throughput"
  "throughput_range|Throughput range over runs|range:agg_throughput"
  "commits|Committed transactions|n_commits"
  "abort_rate|Abort rate (aborts/s)|agg_abort_rate"
  "neworder_ms|NewOrder latency (ms)|NewOrder_local_commit_latency"
  "payment_ms|Payment latency (ms)|Payment_local_commit_latency"
  "delivery_ms|Delivery latency (ms)|Delivery_local_commit_latency"
  "orderstatus_ms|OrderStatus latency (ms)|OrderStatus_local_commit_latency"
  "stocklevel_ms|StockLevel latency (ms)|StockLevel_local_commit_latency"
  "load_ms|Load time (ms)|load"
  "rss_after_load_mib|RSS after loading (MiB)|mib:rss_after_load_kib"
  "peak_rss_mib|Peak RSS (MiB)|mib:Maximum resident set size (kbytes)"
)

value_for() {
  local engine="$1" how="$2"
  case "$how" in
    load) load_ms "$engine" ;;
    range:*) spread "$engine" "${how#range:}" ;;
    mib:*) values "$engine" "${how#mib:}" | awk '{printf "%.1f\n", $1/1024}' | median ;;
    *) values "$engine" "$how" | median ;;
  esac
}

runs=$(ls "$DIR/${ENGINES[0]}"-run*.log | wc -l)
echo "metric,engine,value" > "$DIR/summary.csv"
echo "## TPC-C, 1 shard, $THREADS threads, $runs runs per configuration (medians)"
echo
echo "ALEX tables: \`$TABLES\`. Everything else stays on Masstree in every configuration."
echo
header="| Metric |"; rule="| :--- |"
for e in "${ENGINES[@]}"; do header+=" ${LABEL[$e]} |"; rule+=" ---: |"; done
echo "$header"; echo "$rule"
for row in "${ROWS[@]}"; do
  IFS='|' read -r id label how <<< "$row"
  line="| $label |"
  for e in "${ENGINES[@]}"; do
    v="$(value_for "$e" "$how")"
    line+=" $v |"
    echo "$id,$e,$v" >> "$DIR/summary.csv"
  done
  echo "$line"
done
echo
echo "Mako's own average-latency figure overflows, so it is omitted. Peak RSS grows with the"
echo "number of committed transactions (each NewOrder inserts rows), so it is not an index"
echo "comparison on its own."
echo
if ls "$DIR"/alex-run1.log > /dev/null 2>&1; then
  echo "ALEX rebuild and index size (from run 1 of \"${LABEL[alex]}\"):"
  echo
  echo '```'
  grep -h "\[learned-index-mako\] \(rebuilt\|alex table=\)" "$DIR"/alex-run1.log || echo "(no report found)"
  echo '```'
fi
