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

<!-- RESULTS -->

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
   to `get_next_filled_position()`, which then reads one bitmap word past the array
   (ThreadSanitizer reported it as a heap use-after-free).
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
bench/                  TPC-C benchmark script and results
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

Mako with ALEX, and the benchmark (Linux; the Docker image has every dependency):

```bash
docker build -t learned-index-mako docker/
docker run --rm -it -v "$PWD:/lim" learned-index-mako
scripts/build_mako.sh          # fetches pinned Mako, applies the patch, builds dbtest
bench/run_tpcc.sh 2 3          # 2 threads, 3 runs per configuration
```

## Limitations

- Keys longer than 8 bytes (TPC-C `customer`, `order_line`, ...) stay on Masstree.
  Order-preserving compression of wider keys would extend coverage.
- Phantom protection is per index, not per node: any insert into an ALEX table aborts
  concurrent transactions that missed or scanned in it. Fine for read-mostly tables,
  costly for insert-heavy ones.
- One reader-writer lock per table. A concurrent learned index, such as ALEX+ from the
  [GRE benchmark](https://github.com/gre4index/GRE), would scale further.
- Mako is pinned at its November 2025 commit (`0b62cf1`).

## Background

This is a rebuild of a CSE 532 (Database Systems) course project,
[alex-integration-mako](https://github.com/varunbhandarii/alex-integration-mako), built
with Manush Patel and Dharma Bagadia. That version mapped keys to integers with
`std::stoull` and hashing, which made distinct keys collide and forced every range scan to
sort the whole table. It also skipped phantom protection for ALEX tables. Its reported
memory saving came from runs that committed hundreds of times fewer transactions than the
baseline.

## Credits

- [Mako](https://github.com/makodb/mako): MIT License, © Shuai Mu and contributors.
- [ALEX](https://github.com/microsoft/ALEX): MIT License, © Microsoft Corporation.

## License

MIT. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
