## TPC-C, 1 shard, 1 threads, 3 runs per configuration (medians)

ALEX tables: `item,stock,warehouse,district`. Everything else stays on Masstree in every configuration.

| Metric | Masstree | ALEX, built by inserts | ALEX, rebuilt |
| :--- | ---: | ---: | ---: |
| Throughput (txn/s) | 59917.3 | 76539.8 | 73375.7 |
| Throughput range over runs | 57028–62631 | 65034–80834 | 72922–80354 |
| Committed transactions | 1819020 | 2319157 | 2224140 |
| Abort rate (aborts/s) | 0 | 0 | 0 |
| NewOrder latency (ms) | 0.0192373 | 0.0144517 | 0.0152127 |
| Payment latency (ms) | 0.00575776 | 0.00532276 | 0.00550778 |
| Delivery latency (ms) | 0.061529 | 0.0555625 | 0.0566994 |
| OrderStatus latency (ms) | 0.00731883 | 0.00671982 | 0.00687063 |
| StockLevel latency (ms) | 0.0690336 | 0.0433033 | 0.0452082 |
| Load time (ms) | 1170.52 | 1258.8 | 1304.6 |
| RSS after loading (MiB) | 182.8 | 177.2 | 180.0 |
| Peak RSS (MiB) | 1320.7 | 1635.4 | 1579.3 |

Mako's own average-latency figure overflows, so it is omitted. Peak RSS grows with the
number of committed transactions (each NewOrder inserts rows), so it is not an index
comparison on its own.

ALEX rebuild and index size (from run 1 of "ALEX, rebuilt"):

```
[learned-index-mako] rebuilt 4 ALEX indexes in 5.2 ms
[learned-index-mako] alex table=district_0 keys=10 index_bytes=440 bytes_per_key=44.0
[learned-index-mako] alex table=stock_0 keys=100000 index_bytes=2303784 bytes_per_key=23.0
[learned-index-mako] alex table=warehouse_0 keys=1 index_bytes=248 bytes_per_key=248.0
[learned-index-mako] alex table=item keys=100000 index_bytes=2303784 bytes_per_key=23.0
```
