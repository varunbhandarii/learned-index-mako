## TPC-C, 1 shard, 4 threads, 3 runs per engine (medians)

ALEX tables: `item,stock,warehouse,district`. Everything else stays on Masstree in both configurations.

| Metric | Masstree | ALEX tables |
| :--- | ---: | ---: |
| Throughput (txn/s) | 184264 | 215850 |
| Throughput range over runs | 168715–187181 | 211733–222658 |
| Committed transactions | 5628145 | 6643721 |
| Abort rate (aborts/s) | 28.2545 | 26.4788 |
| NewOrder latency (ms) | 0.0238503 | 0.0204575 |
| Payment latency (ms) | 0.00768682 | 0.00748666 |
| Delivery latency (ms) | 0.0778567 | 0.074611 |
| OrderStatus latency (ms) | 0.0094349 | 0.00848231 |
| StockLevel latency (ms) | 0.100769 | 0.0666899 |
| Load time (ms) | 4371.57 | 4468.54 |
| RSS after loading (MiB) | 519.1 | 517.7 |
| Peak RSS (MiB) | 4108.8 | 4763.4 |

Mako's own average-latency figure overflows, so it is omitted. Peak RSS grows with the
number of committed transactions (each NewOrder inserts rows), so it is not an index
comparison on its own.

ALEX index size at exit (from run 1):

```
[learned-index-mako] alex table=district_0 keys=10 index_bytes=456 bytes_per_key=45.6
[learned-index-mako] alex table=district_1 keys=10 index_bytes=456 bytes_per_key=45.6
[learned-index-mako] alex table=district_2 keys=10 index_bytes=456 bytes_per_key=45.6
[learned-index-mako] alex table=district_3 keys=10 index_bytes=456 bytes_per_key=45.6
[learned-index-mako] alex table=stock_0 keys=100000 index_bytes=2341160 bytes_per_key=23.4
[learned-index-mako] alex table=stock_1 keys=100000 index_bytes=2341160 bytes_per_key=23.4
[learned-index-mako] alex table=stock_2 keys=100000 index_bytes=2341160 bytes_per_key=23.4
[learned-index-mako] alex table=stock_3 keys=100000 index_bytes=2341160 bytes_per_key=23.4
[learned-index-mako] alex table=warehouse_0 keys=1 index_bytes=232 bytes_per_key=232.0
[learned-index-mako] alex table=warehouse_1 keys=1 index_bytes=232 bytes_per_key=232.0
[learned-index-mako] alex table=warehouse_2 keys=1 index_bytes=232 bytes_per_key=232.0
[learned-index-mako] alex table=warehouse_3 keys=1 index_bytes=232 bytes_per_key=232.0
[learned-index-mako] alex table=item keys=100000 index_bytes=2341160 bytes_per_key=23.4
```
