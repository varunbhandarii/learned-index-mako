#!/usr/bin/env python3
"""Summarise run_index_bench.sh output as medians: summary.md and summary.csv."""
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

INDEXES = ["masstree", "alex", "alex-rebuilt"]
KEYSETS = {
    "dense4": "dense 4-byte ids (like TPC-C item)",
    "dense8": "dense 8-byte (warehouse, item) (like TPC-C stock)",
    "random8": "random 8-byte keys",
}


def parse(path):
    rows = []
    for line in Path(path).read_text().splitlines():
        if line.strip():
            rows.append(dict(field.split("=", 1) for field in line.split()))
    return rows


def main():
    raw, out = sys.argv[1], Path(sys.argv[2])
    groups = defaultdict(list)
    for r in parse(raw):
        groups[(r["index"], r["keys"], int(r["threads"]))].append(r)

    def med(rows, field):
        return statistics.median(float(r[field]) for r in rows)

    summary = []
    for (index, keys, threads), rows in sorted(groups.items()):
        n = int(rows[0]["n"])
        summary.append({
            "index": index, "keys": keys, "threads": threads, "n": n, "runs": len(rows),
            "lookup_ns": round(med(rows, "lookup_ns"), 1),
            "lookup_mops": round(med(rows, "lookup_mops"), 2),
            "load_ms": round(med(rows, "load_ms"), 1),
            "rss_bytes_per_key": round(med(rows, "bytes_per_key"), 1),
            "index_bytes_per_key": round(med(rows, "alex_index_bytes") / n, 1) if index != "masstree" else "",
        })

    with open(out / "summary.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(summary[0].keys()))
        w.writeheader()
        w.writerows(summary)

    by = {(s["index"], s["keys"], s["threads"]): s for s in summary}
    n = summary[0]["n"]
    runs = summary[0]["runs"]
    lines = [f"## Index microbenchmark: {n:,} keys, {runs} runs per configuration (medians)", ""]
    lines += ["Lookups are uniform random point lookups. Memory is the growth in resident memory",
              "while building the index, with freed pages returned to the OS first; values are",
              "pointers, so it is the index alone.", ""]
    for keys, label in KEYSETS.items():
        lines += [f"### {label}", "",
                  "| Index | Lookup, 1 thread (ns) | Lookups/s, 4 threads (M) | Build (ms) | Memory (bytes/key) |",
                  "| :--- | ---: | ---: | ---: | ---: |"]
        for index in INDEXES:
            one, four = by.get((index, keys, 1)), by.get((index, keys, 4))
            if not one:
                continue
            mem = f"{one['rss_bytes_per_key']}"
            if one["index_bytes_per_key"] != "":
                mem += f" (ALEX counts {one['index_bytes_per_key']})"
            lines.append(f"| {index} | {one['lookup_ns']} | {four['lookup_mops'] if four else 'NA'} "
                         f"| {one['load_ms']} | {mem} |")
        lines.append("")
    (out / "summary.md").write_text("\n".join(lines))


if __name__ == "__main__":
    main()
