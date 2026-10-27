## TPC-C, 1 shard, 2 threads, 5 runs per engine (medians)

ALEX tables: `item,stock,warehouse,district`. Everything else stays on Masstree in both configurations.

| Metric | Masstree | ALEX tables |
| :--- | ---: | ---: |
| Throughput (txn/s) | 100223 | 131963 |
| Throughput range over runs | 96842–116593 | 82635–143040 |
| Committed transactions | 3059710 | 4002330 |
| Abort rate (aborts/s) | 15.3952 | 15.2659 |
| NewOrder latency (ms) | 0.0222251 | 0.0167492 |
| Payment latency (ms) | 0.00689921 | 0.00618515 |
| Delivery latency (ms) | 0.0721337 | 0.0623506 |
| OrderStatus latency (ms) | 0.0081129 | 0.00731365 |
| StockLevel latency (ms) | 0.0875991 | 0.0522305 |
| Load time (ms) | 2684.89 | 2232.58 |
| RSS after loading (MiB) | 288.8 | 303.2 |
| Peak RSS (MiB) | 2237.8 | 2837.0 |

Mako's own average-latency figure overflows, so it is omitted. Peak RSS grows with the
number of committed transactions (each NewOrder inserts rows), so it is not an index
comparison on its own.

ALEX index size at exit (from run 1):

```
[learned-index-mako] alex table=district_0 keys=10 index_bytes=456 bytes_per_key=45.6
[learned-index-mako] alex table=district_1 keys=10 index_bytes=456 bytes_per_key=45.6
[learned-index-mako] alex table=stock_0 keys=100000 index_bytes=2341160 bytes_per_key=23.4
[learned-index-mako] alex table=stock_1 keys=100000 index_bytes=2341160 bytes_per_key=23.4
[learned-index-mako] alex table=warehouse_0 keys=1 index_bytes=232 bytes_per_key=232.0
[learned-index-mako] alex table=warehouse_1 keys=1 index_bytes=232 bytes_per_key=232.0
[learned-index-mako] alex table=item keys=100000 index_bytes=2341160 bytes_per_key=23.4
```
