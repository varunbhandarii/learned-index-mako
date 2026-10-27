# learned-index-mako

Putting the [ALEX](https://github.com/microsoft/ALEX) learned index inside
[Mako](https://github.com/makodb/mako), a distributed transactional database (OSDI '25),
and measuring honestly where it helps and where it doesn't.

> **Status: work in progress.** This repo is a correctness-first rebuild of an earlier
> course project ([alex-integration-mako](https://github.com/varunbhandarii/alex-integration-mako)).
> Results will be added once they are measured with the benchmark in this repo.

## Why

A learned index replaces tree traversal with a model that predicts where a key is stored.
In standalone benchmarks, learned indexes can use less memory and answer lookups faster
than B-trees. This project asks whether that still holds inside a real OLTP engine, with
optimistic concurrency control, multi-version rows, replication and range scans.

## Approach

- **Order-preserving keys.** Keys are mapped to integers without hashing, so range scans
  walk the index in order instead of sorting the whole table.
- **ALEX only where it fits.** Tables whose keys fit in 64 bits use ALEX; everything else
  stays on Masstree, Mako's default index.
- **Serializability preserved.** Missed lookups and range scans are protected against
  phantoms, and inserts are atomic.
- **Tested.** Standalone unit tests run with sanitizers in CI.
- **Fair benchmarks.** Both indexes run with the same configuration and duration, reporting
  throughput, aborts, latency and memory.

## Layout

```
learned-index-mako/
├── src/            ALEX adapter
├── patches/        changes applied to the pinned Mako and ALEX sources
├── tests/          standalone unit tests
├── bench/          Masstree vs ALEX benchmark scripts and results
└── third_party/    Mako and ALEX as pinned git submodules
```

## Credits

- [Mako](https://github.com/makodb/mako): MIT License, © Shuai Mu and contributors.
- [ALEX](https://github.com/microsoft/ALEX): MIT License, © Microsoft Corporation.
- The original course project (CSE 532: Database Systems) was built with Manush Patel and
  Dharma Bagadia.

## License

MIT. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
