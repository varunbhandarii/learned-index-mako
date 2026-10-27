## TPC-C, 1 shard, 2 threads, 3 runs per configuration (medians)

ALEX tables: `item,stock,warehouse,district`. Everything else stays on Masstree in every configuration.

| Metric | Masstree | ALEX, built by inserts | ALEX, rebuilt |
| :--- | ---: | ---: | ---: |
| Throughput (txn/s) | 119088 | 141462 | 140830 |
| Throughput range over runs | 106216–126895 | 106436–144840 | 98145–145962 |
| Committed transactions | 3613902 | 4297855 | 4269585 |
| Abort rate (aborts/s) | 16.9706 | 16.1281 | 15.3708 |
| NewOrder latency (ms) | 0.0191307 | 0.015465 | 0.0155701 |
| Payment latency (ms) | 0.00592898 | 0.0058731 | 0.00586115 |
| Delivery latency (ms) | 0.0614423 | 0.0595922 | 0.0590162 |
| OrderStatus latency (ms) | 0.00704689 | 0.00719577 | 0.00691786 |
| StockLevel latency (ms) | 0.0703716 | 0.0484153 | 0.0490687 |
| Load time (ms) | 2158.08 | 2148.72 | 2137.89 |
| RSS after loading (MiB) | 307.3 | 316.0 | 313.1 |
| Peak RSS (MiB) | 2590.8 | 3023.5 | 3014.9 |

Mako's own average-latency figure overflows, so it is omitted. Peak RSS grows with the
number of committed transactions (each NewOrder inserts rows), so it is not an index
comparison on its own.

ALEX rebuild and index size (from run 1 of "ALEX, rebuilt"):

```
[learned-index-mako] rebuilt 7 ALEX indexes in 11.6 ms
[learned-index-mako] alex table=district_0 keys=10 index_bytes=440 bytes_per_key=44.0
[learned-index-mako] alex table=district_1 keys=10 index_bytes=440 bytes_per_key=44.0
[learned-index-mako] alex table=stock_0 keys=100000 index_bytes=2303784 bytes_per_key=23.0
[learned-index-mako] alex table=stock_1 keys=100000 index_bytes=2303784 bytes_per_key=23.0
[learned-index-mako] alex table=warehouse_0 keys=1 index_bytes=248 bytes_per_key=248.0
[learned-index-mako] alex table=warehouse_1 keys=1 index_bytes=248 bytes_per_key=248.0
[learned-index-mako] alex table=item keys=100000 index_bytes=2303784 bytes_per_key=23.0
```
