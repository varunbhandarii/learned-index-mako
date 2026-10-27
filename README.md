# learned-index-mako

[![CI](https://github.com/varunbhandarii/learned-index-mako/actions/workflows/ci.yml/badge.svg)](https://github.com/varunbhandarii/learned-index-mako/actions/workflows/ci.yml)

The [ALEX](https://github.com/microsoft/ALEX) learned index inside
[Mako](https://github.com/makodb/mako) (OSDI '25), a distributed transactional key-value
store, with serializability preserved and a fair comparison against Mako's default index,
Masstree.

A learned index replaces tree traversal with a model that predicts where a key is stored.
In standalone benchmarks that can save memory and time. This project asks what happens
inside a real OLTP engine, with optimistic concurrency control, multi-version records and
range scans.

## Results

All numbers were measured on a 4-vCPU, 8 GB Docker Desktop VM on a Windows 11 laptop, with
GCC 12. Raw logs and summaries are in [`bench/results/`](bench/results/).

### TPC-C on Mako

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/img/tpcc-throughput-dark.svg">
  <img alt="TPC-C throughput: 100k vs 132k transactions/s at 2 threads, 184k vs 216k at 4 threads, Masstree only vs ALEX tables" src="docs/img/tpcc-throughput-light.svg" width="680">
</picture>

One shard without replication, one warehouse per worker thread, Mako's 30-second
measurement window. **Masstree** keeps every table on Masstree. **ALEX tables** moves
`item`, `stock`, `warehouse` and `district` to ALEX; all other tables stay on Masstree.
Medians of interleaved runs, with ranges in parentheses.

| | Masstree | ALEX tables | Change |
| :--- | ---: | ---: | ---: |
| Throughput, 4 threads, 3 runs (txn/s) | 184,264 (168,715–187,181) | 215,850 (211,733–222,658) | **+17%** |
| Throughput, 2 threads, 5 runs (txn/s) | 100,223 (96,842–116,593) | 131,963 (82,635–143,040) | +32%, ranges overlap |
| StockLevel latency, 4 threads | 0.101 ms | 0.067 ms | **−34%** |
| NewOrder latency, 4 threads | 0.024 ms | 0.020 ms | −14% |
| Abort rate, 4 threads | 28 /s | 26 /s | no change |
| Memory (RSS) after loading, 4 threads | 519 MiB | 518 MiB | no change |
| Index size of the ALEX tables | | 23.4 bytes/key, 11 MiB in total | 2% of RSS |

What this shows:

- **Faster point lookups.** StockLevel looks up about 200 `stock` rows per transaction and
  NewOrder about 20 `item` and `stock` rows. TPC-C's keys are dense integers, so ALEX's
  linear models predict positions almost exactly, and a lookup touches fewer cache lines
  than a descent through Masstree.
- **No memory saving.** The rows, not the index, take the memory. ALEX's index for these
  tables is 23.4 bytes per key, about 2% of the process, so swapping it leaves RSS within
  run-to-run noise.
- **No extra aborts.** Phantom protection for ALEX tables costs nothing here, because those
  tables get no inserts after loading.

This is a small machine with short runs, so treat the size of the throughput gain as
indicative: the 4-thread ranges are separate, but the 2-thread ranges overlap. Reproduce
with `bench/run_tpcc.sh` (see [Build and run](#build-and-run)).

### The index on its own

[`bench/index_bench.cc`](bench/index_bench.cc) compares the indexes without the rest of
the database: one million keys, uniform random point lookups, values that are pointers so
that memory is the index alone, and Masstree configured exactly as Mako's tables are.
**ALEX** is built by inserts, as Mako builds it while loading; **ALEX, rebuilt** is then
repacked with ALEX's bulk loader (`AlexIndex::rebuild()`). Medians of 3 runs, one process
per configuration.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/img/lookup-latency-dark.svg">
  <img alt="Single-thread lookup latency in nanoseconds for Masstree, ALEX and rebuilt ALEX on three key sets" src="docs/img/lookup-latency-light.svg" width="680">
</picture>

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/img/lookup-scaling-dark.svg">
  <img alt="Lookup throughput at 1, 2 and 4 threads: Masstree scales to 9.8 million per second, ALEX flattens near 7" src="docs/img/lookup-scaling-light.svg" width="680">
</picture>

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/img/index-memory-dark.svg">
  <img alt="Index memory in bytes per key for Masstree, ALEX and rebuilt ALEX on three key sets" src="docs/img/index-memory-light.svg" width="680">
</picture>

| Key set | Lookup, 1 thread: Masstree / ALEX / rebuilt | Lookups/s, 4 threads: Masstree / rebuilt | Bytes per key: Masstree / ALEX / rebuilt |
| :--- | :--- | :--- | :--- |
| Dense 4-byte ids (item) | 467 / 307 / 166 ns | 9.8 M / 6.9 M | 23.3 / 25.0 / 24.1 |
| Dense 8-byte pairs (stock) | 473 / 471 / 169 ns | 9.8 M / 7.4 M | 23.3 / 25.6 / 25.1 |
| Random 8-byte keys | 550 / 268 / 202 ns | 7.7 M / 6.8 M | 33.7 / 25.2 / 24.1 |

What this shows:

- **A packed learned index is much faster per lookup.** Rebuilt, ALEX answers a lookup in
  166–202 ns against Masstree's 467–550 ns, 2.7–2.8× faster on one thread. Built by
  inserts, its gain depends on the keys: 1.5× on item-like ids, none on stock-like pairs.
  Mako builds its tables by inserts, so rebuilding ALEX tables after loading is the
  obvious next step.
- **The reader lock limits scaling.** Masstree's lock-free reads reach 9.8 M lookups/s on
  4 threads. ALEX stops near 7 M, because every lookup takes the table's reader lock and
  the threads contend for that lock's cache line. TPC-C still gains, because lookups are
  only part of each transaction.
- **Memory is a tie on TPC-C keys.** On dense keys both indexes use 23–26 bytes per key.
  On random keys, where Masstree's nodes fill less evenly, ALEX uses 27% less.

### With replication

Mako's own 1-shard replication test (a leader, two followers and a learner, 2 threads
each) passes with ALEX tables on the leader. The followers replayed 3,207 log batches
(2,413 with Masstree only), and the leader persisted 42,826 transactions/s against
31,291 (one run each, so indicative; logs in
[`bench/results/replication-2026-10-27/`](bench/results/replication-2026-10-27/)). The
followers keep every table on Masstree, though: they address tables by numeric id while
replaying and never learn the names that select ALEX. Each process choosing its own index
is safe, but replay into ALEX is not exercised yet.

## How it works

### Keys keep their order

ALEX only indexes numbers. Mako keys are byte strings whose integer fields are encoded
big-endian, so for keys of up to 8 bytes the big-endian integer value of the bytes is an
exact, order-preserving encoding:

```
TPC-C stock key (warehouse 3, item 1042)  ->  00 00 00 03 00 00 04 12  ->  0x0000000300000412
```

ALEX's numeric order is then the table's key order: range scans walk the index directly,
and two different keys can never map to the same number.
[`AlexIndex`](src/alex_index.h) accepts bounds of any length and converts them to exact
numeric ranges, so it matches Masstree's scan semantics for every bound.

### Only where it fits

A table uses ALEX when `MAKO_INDEX=alex` and its name is in `MAKO_ALEX_TABLES` (default
`item,stock,warehouse,district`, the TPC-C tables with keys of 8 bytes or less). Every
other table stays on Masstree. A table's index is chosen once, when Mako names it, before
any data is loaded.

### Transactions stay serializable

Mako runs optimistic concurrency control (STO). Values in ALEX tables are the same
versioned records Masstree tables use, so reads, writes, locking, commit and abort are
unchanged. Two things needed real work ([`patches/mako.patch`](patches/mako.patch)):

- **Phantoms.** On Masstree, a lookup that misses records the version of the tree node it
  landed in, and commit fails if that node changed. An ALEX index has a structural version
  instead, bumped on every insert and erase. Misses and scans record it, and commit fails
  if it moved. That is coarser than per-node validation, but the tables that suit ALEX
  have no inserts once loaded, so it costs no aborts there.
- **Inserts.** `insert_if_absent` checks and inserts under one lock, so two transactions
  can't both insert the same key, and a transaction's own insert doesn't invalidate a
  version it observed just before. Removed records are freed through Mako's RCU, as on
  Masstree.

### Concurrency

Readers share a reader-writer lock, and writers take it exclusively. ALEX is
single-threaded, and its lookups also update per-node statistics, so even shared-lock
reads would race. [`patches/alex.patch`](patches/alex.patch) makes those updates
optional, and the adapter turns them off.

### Bugs found in ALEX

The randomized differential test (below), run under sanitizers, found four problems in
ALEX itself, fixed in [`patches/alex.patch`](patches/alex.patch):

1. **Out-of-bounds read.** `find_lower()`/`find_upper()` can pass `pos == data_capacity_`
   to `get_next_filled_position()`, which then reads one bitmap word past the array when
   the capacity is a multiple of 64 (first seen under ThreadSanitizer; a direct node-level
   test reproduces it as an AddressSanitizer heap-buffer-overflow).
2. **Data race.** Lookups write statistics, as described above.
3. **Undefined behaviour.** `delete_node()` downcast nodes after destroying them (UBSan).
4. **Signed overflow.** `extract_rightmost_one()` negated a bitmap word as `int64_t`, which
   overflows when only the top bit is set (UBSan).

ALEX's node-splitting code also casts data nodes to `AlexDataNode<T, P>` with default
template arguments, so the adapter uses `Alex<uint64_t, void*>` and enforces unique keys
itself.

## Testing

[`tests/alex_index_test.cc`](tests/alex_index_test.cc):

- unit tests for lookups, inserts, versions, rejected keys, and scans with bounds shorter
  than, equal to and longer than the key length;
- a randomized differential test of 20,000 operations per key length (1 to 8 bytes)
  against `std::map`, using key bytes such as `0x00`, `0x7f`, `0x80` and `0xff` to hit
  padding and boundary cases;
- a concurrency test with readers checking every value they see while a writer inserts
  and erases.

CI runs them as a plain build, with AddressSanitizer + UndefinedBehaviorSanitizer, and
with ThreadSanitizer, and checks that `patches/mako.patch` applies to the pinned Mako.

## Repository layout

```
src/alex_index.{h,cc}   order-preserving ALEX adapter (compiled as C++17, see below)
patches/alex.patch      ALEX fixes, applied to a copy at build time
patches/mako.patch      Mako integration: MassTrans.hh and the CMake hook
tests/                  unit, differential and concurrency tests
bench/                  TPC-C and index benchmarks, results, chart script
docs/img/               charts used in this README (light and dark)
docker/Dockerfile       build environment (Mako's own dependency list)
third_party/            ALEX and Mako as pinned submodules
```

ALEX uses `std::allocator::rebind`, which C++20 removed, so `alex_index.cc` is compiled as
C++17 while Mako is C++23. Only plain types cross [`alex_index.h`](src/alex_index.h).

## Build and run

Unit tests (needs CMake, a C++17 compiler and `patch`):

```bash
git submodule update --init third_party/ALEX
cmake -S . -B build -DLIM_SANITIZE=address   # or thread, or leave it out
cmake --build build -j && ctest --test-dir build --output-on-failure
```

Mako with ALEX, and the benchmarks (Linux; the Docker image has every dependency):

```bash
docker build -t learned-index-mako docker/
docker run --rm -it -v "$PWD:/lim" learned-index-mako
scripts/build_mako.sh          # fetches pinned Mako, applies the patch, builds the binaries
bench/run_tpcc.sh 2 3          # TPC-C: 2 threads, 3 runs per configuration
bench/run_index_bench.sh       # index microbenchmark: 1M keys, 3 runs
python3 bench/plot.py bench/results/index-<date> bench/results/<tpcc-t2> bench/results/<tpcc-t4> docs/img
```

## Limitations

- Keys longer than 8 bytes (TPC-C `customer`, `order_line`, ...) stay on Masstree.
  Order-preserving compression of wider keys would extend coverage.
- Phantom protection is per index, not per node: any insert into an ALEX table aborts
  concurrent transactions that missed or scanned in it. Fine for read-mostly tables,
  costly for insert-heavy ones.
- One reader-writer lock per table, which caps lookup scaling (see the microbenchmark).
  ALEX tables get no structural changes after loading, so lookups could skip the lock; a
  concurrent learned index such as ALEX+ from the
  [GRE benchmark](https://github.com/gre4index/GRE) would also scale further.
- ALEX tables are built by inserts and not rebuilt after loading, which leaves most of
  ALEX's lookup advantage unused.
- Replicas keep their tables on Masstree, because Mako's followers never learn table
  names. Selecting ALEX by table id, or sending names to followers, would fix that.
- Mako is pinned at its November 2025 commit (`0b62cf1`).

## Background

This is a rebuild of a CSE 532 (Database Systems) course project built with Manush Patel
and Dharma Bagadia. That version mapped keys to integers with
`std::stoull` and hashing, which made distinct keys collide and forced every range scan to
sort the whole table, and it skipped phantom protection for ALEX tables. In its own logs,
ALEX runs committed 17 transactions per second against Masstree's 21,141, so its reported
3.4× memory saving came from runs that did several hundred times less work. This rebuild
puts only suitable tables on ALEX, keeps keys in order, and is faster than Masstree on
the same benchmark.

## Credits

- [Mako](https://github.com/makodb/mako): MIT License, © Shuai Mu and contributors.
- [ALEX](https://github.com/microsoft/ALEX): MIT License, © Microsoft Corporation.

## License

MIT. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
