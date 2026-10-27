#!/usr/bin/env python3
"""Render the README charts as light and dark SVGs (standard library only).

Usage: bench/plot.py <out-dir> <index-results-dir> <tpcc-results-dir>...

Reads summary.csv from the index microbenchmark and from each TPC-C results directory
(named ...-t<threads>, as run_tpcc.sh writes them).
Colors follow a validated categorical palette (slots 1-3 pass all-pairs color-vision
checks in both modes); bars carry value labels because slot 3 is under 3:1 contrast on
the light surface, and the README keeps the data tables beside each chart.
"""
import csv
import math
import re
import sys
from html import escape
from pathlib import Path

THEMES = {
    "light": {
        "surface": "#fcfcfb", "border": "rgba(11,11,11,0.10)", "primary": "#0b0b0b",
        "secondary": "#52514e", "muted": "#898781", "grid": "#e1e0d9", "baseline": "#c3c2b7",
        "series": ["#2a78d6", "#eb6834", "#1baf7a"],
    },
    "dark": {
        "surface": "#1a1a19", "border": "rgba(255,255,255,0.10)", "primary": "#f0efec",
        "secondary": "#c3c2b7", "muted": "#898781", "grid": "#2c2c2a", "baseline": "#383835",
        "series": ["#3987e5", "#d95926", "#199e70"],
    },
}
FONT = 'system-ui, -apple-system, "Segoe UI", sans-serif'
WIDTH = 680
PLOT_LEFT, PLOT_RIGHT = 64, 24


def nice_ticks(max_value, count=5):
    raw = max_value / (count - 1)
    magnitude = 10 ** math.floor(math.log10(raw))
    step = next(m * magnitude for m in (1, 2, 2.5, 5, 10) if m * magnitude >= raw)
    top = step * math.ceil(max_value / step)
    ticks, v = [], 0.0
    while v <= top + step / 2:
        ticks.append(v)
        v += step
    return ticks


def fmt(v, decimals=0):
    return f"{v:,.{decimals}f}"


def frame(theme, height, title, subtitle, series_names, body):
    t = THEMES[theme]
    legend, x = [], PLOT_LEFT
    for i, name in enumerate(series_names):
        legend.append(f'<rect x="{x}" y="62" width="10" height="10" rx="2" fill="{t["series"][i]}"/>'
                      f'<text x="{x + 16}" y="71" fill="{t["secondary"]}" font-size="12">{escape(name)}</text>')
        x += 24 + 7 * len(name)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{height}" '
            f'viewBox="0 0 {WIDTH} {height}" role="img" aria-label="{escape(title)}" '
            f'font-family=\'{FONT}\'>\n<title>{escape(title)}</title>\n'
            f'<desc>{escape(subtitle)}</desc>\n'
            f'<rect x="0.5" y="0.5" width="{WIDTH - 1}" height="{height - 1}" rx="10" '
            f'fill="{t["surface"]}" stroke="{t["border"]}"/>\n'
            f'<text x="{PLOT_LEFT}" y="30" fill="{t["primary"]}" font-size="15" font-weight="600">'
            f'{escape(title)}</text>\n'
            f'<text x="{PLOT_LEFT}" y="49" fill="{t["secondary"]}" font-size="12">{escape(subtitle)}</text>\n'
            + "".join(legend) + "\n" + body + "</svg>\n")


def y_axis(theme, ticks, y_of, decimals):
    t = THEMES[theme]
    if len(ticks) > 1 and (ticks[1] - ticks[0]) % 1:
        decimals = max(decimals, 1)  # e.g. steps of 2.5 must not print as 2, 5, 8
    out = []
    for v in ticks:
        y = y_of(v)
        color = t["baseline"] if v == 0 else t["grid"]
        out.append(f'<line x1="{PLOT_LEFT}" x2="{WIDTH - PLOT_RIGHT}" y1="{y:.1f}" y2="{y:.1f}" '
                   f'stroke="{color}" stroke-width="1"/>'
                   f'<text x="{PLOT_LEFT - 8}" y="{y + 4:.1f}" fill="{t["muted"]}" font-size="11" '
                   f'text-anchor="end" style="font-variant-numeric: tabular-nums">{fmt(v, decimals)}</text>')
    return "\n".join(out) + "\n"


def bar_path(x, y_top, w, y_base, r=4):
    r = min(r, (y_base - y_top), w / 2)
    return (f"M{x:.1f},{y_base:.1f} V{y_top + r:.1f} Q{x:.1f},{y_top:.1f} {x + r:.1f},{y_top:.1f} "
            f"H{x + w - r:.1f} Q{x + w:.1f},{y_top:.1f} {x + w:.1f},{y_top + r:.1f} V{y_base:.1f} Z")


def grouped_bars(theme, title, subtitle, groups, series_names, values, decimals=0, label_decimals=0):
    """values[g][s] is the value of series s in group g."""
    t = THEMES[theme]
    height, top, bottom = 330, 92, 290
    ticks = nice_ticks(max(max(row) for row in values))
    y_of = lambda v: bottom - (bottom - top) * v / ticks[-1]
    body = y_axis(theme, ticks, y_of, decimals)
    plot_w = WIDTH - PLOT_LEFT - PLOT_RIGHT
    group_w = plot_w / len(groups)
    bar_w, gap = 34, 2
    cluster_w = len(series_names) * bar_w + (len(series_names) - 1) * gap
    for g, group in enumerate(groups):
        x0 = PLOT_LEFT + g * group_w + (group_w - cluster_w) / 2
        for s, value in enumerate(values[g]):
            x = x0 + s * (bar_w + gap)
            body += (f'<path d="{bar_path(x, y_of(value), bar_w, bottom)}" fill="{t["series"][s]}">'
                     f'<title>{escape(series_names[s])}, {escape(group)}: {fmt(value, label_decimals)}</title></path>'
                     f'<text x="{x + bar_w / 2:.1f}" y="{y_of(value) - 6:.1f}" fill="{t["secondary"]}" '
                     f'font-size="11" text-anchor="middle" style="font-variant-numeric: tabular-nums">'
                     f'{fmt(value, label_decimals)}</text>\n')
        body += (f'<text x="{PLOT_LEFT + g * group_w + group_w / 2:.1f}" y="{bottom + 22}" '
                 f'fill="{t["secondary"]}" font-size="12" text-anchor="middle">{escape(group)}</text>\n')
    return frame(theme, height, title, subtitle, series_names, body)


def lines(theme, title, subtitle, xs, series_names, values, decimals=0):
    """values[s][i] is the value of series s at xs[i]."""
    t = THEMES[theme]
    height, top, bottom = 330, 92, 290
    right_pad = 120  # room for end labels
    ticks = nice_ticks(max(max(row) for row in values))
    y_of = lambda v: bottom - (bottom - top) * v / ticks[-1]
    x_of = lambda i: PLOT_LEFT + 30 + i * (WIDTH - PLOT_LEFT - PLOT_RIGHT - right_pad - 30) / (len(xs) - 1)
    body = y_axis(theme, ticks, y_of, decimals)
    for i, x in enumerate(xs):
        body += (f'<text x="{x_of(i):.1f}" y="{bottom + 22}" fill="{t["secondary"]}" font-size="12" '
                 f'text-anchor="middle">{escape(str(x))}</text>\n')
    for s, row in enumerate(values):
        pts = " ".join(f"{x_of(i):.1f},{y_of(v):.1f}" for i, v in enumerate(row))
        body += f'<polyline points="{pts}" fill="none" stroke="{t["series"][s]}" stroke-width="2" stroke-linejoin="round"/>\n'
        for i, v in enumerate(row):
            body += (f'<circle cx="{x_of(i):.1f}" cy="{y_of(v):.1f}" r="4" fill="{t["series"][s]}" '
                     f'stroke="{t["surface"]}" stroke-width="2"><title>{escape(series_names[s])}, '
                     f'{escape(str(xs[i]))}: {fmt(v, 1)}</title></circle>\n')
    # End labels, nudged apart so they never overlap.
    ends = sorted(((y_of(row[-1]), s, row[-1]) for s, row in enumerate(values)))
    placed = []
    for y, s, v in ends:
        if placed and y - placed[-1] < 15:
            y = placed[-1] + 15
        placed.append(y)
        body += (f'<text x="{x_of(len(xs) - 1) + 10:.1f}" y="{y + 4:.1f}" fill="{t["secondary"]}" '
                 f'font-size="12">{escape(series_names[s])} {fmt(v, 1)}</text>\n')
    return frame(theme, height, title, subtitle, series_names, body)


def tpcc_throughput(results_dir, engines):
    """Median throughput per engine from a run_tpcc.sh results directory."""
    rows = list(csv.DictReader(open(Path(results_dir) / "summary.csv")))
    by_engine = {r["engine"]: float(r["value"]) for r in rows if r["metric"] == "throughput"}
    return [by_engine[e] for e in engines]


def main():
    out, index_dir = Path(sys.argv[1]), Path(sys.argv[2])
    tpcc_dirs = sorted((Path(d) for d in sys.argv[3:]),
                       key=lambda d: int(re.search(r"-t(\d+)$", d.name).group(1)))
    out.mkdir(parents=True, exist_ok=True)
    rows = list(csv.DictReader(open(index_dir / "summary.csv")))
    get = lambda idx, keys, threads, field: float(next(
        r[field] for r in rows if r["index"] == idx and r["keys"] == keys and int(r["threads"]) == threads))

    names = ["Masstree", "ALEX", "ALEX, rebuilt"]
    idxs = ["masstree", "alex", "alex-rebuilt"]
    keysets = [("dense4", "Dense 4-byte (item)"), ("dense8", "Dense 8-byte (stock)"),
               ("random8", "Random 8-byte")]
    n = int(rows[0]["n"])
    tpcc_engines = ["masstree", "alex-inserts", "alex"]
    tpcc_groups, tpcc_values = [], []
    for d in tpcc_dirs:
        threads = int(re.search(r"-t(\d+)$", d.name).group(1))
        tpcc_groups.append(f"{threads} thread{'s' if threads > 1 else ''}")
        tpcc_values.append([v / 1000 for v in tpcc_throughput(d, tpcc_engines)])

    for theme in THEMES:
        charts = {
            "tpcc-throughput": grouped_bars(
                theme, "TPC-C throughput on Mako (higher is better)",
                "Thousands of transactions per second (median); ALEX tables: item, stock, "
                "warehouse, district",
                tpcc_groups, ["Masstree only", "ALEX, built by inserts", "ALEX, rebuilt"],
                tpcc_values),
            "lookup-latency": grouped_bars(
                theme, "Point lookup, one thread (lower is better)",
                f"Nanoseconds per uniform random lookup, {n:,} keys",
                [label for _, label in keysets], names,
                [[get(i, k, 1, "lookup_ns") for i in idxs] for k, _ in keysets]),
            "lookup-scaling": lines(
                theme, "Lookup throughput by thread count (higher is better)",
                f"Millions of lookups per second, dense 8-byte keys (stock), {n:,} keys",
                ["1 thread", "2 threads", "4 threads"], names,
                [[get(i, "dense8", th, "lookup_mops") for th in (1, 2, 4)] for i in idxs], decimals=0),
            "index-memory": grouped_bars(
                theme, "Index memory (lower is better)",
                f"Bytes of resident memory per key, index only, {n:,} keys",
                [label for _, label in keysets], names,
                [[get(i, k, 1, "rss_bytes_per_key") for i in idxs] for k, _ in keysets],
                label_decimals=1),
        }
        for name, svg in charts.items():
            (out / f"{name}-{theme}.svg").write_text(svg)


if __name__ == "__main__":
    main()
