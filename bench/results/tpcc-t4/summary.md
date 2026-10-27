## TPC-C, 1 shard, 4 threads, 3 runs per configuration (medians)

ALEX tables: `item,stock,warehouse,district`. Everything else stays on Masstree in every configuration.

| Metric | Masstree | ALEX, built by inserts | ALEX, rebuilt |
| :--- | ---: | ---: | ---: |
| Throughput (txn/s) | 147336 | 206948 | 211893 |
| Throughput range over runs | 142968–147434 | 165533–243043 | 154693–232498 |
| Committed transactions | 4535804 | 6327518 | 6480782 |
| Abort rate (aborts/s) | 23.2732 | 25.1183 | 25.8622 |
| NewOrder latency (ms) | 0.0300122 | 0.0206036 | 0.0200001 |
| Payment latency (ms) | 0.00918489 | 0.00790417 | 0.00766599 |
| Delivery latency (ms) | 0.0978515 | 0.0811706 | 0.0782248 |
| OrderStatus latency (ms) | 0.0106128 | 0.00912 | 0.00947423 |
| StockLevel latency (ms) | 0.130277 | 0.0727182 | 0.0733965 |
| Load time (ms) | 4211.81 | 5006.12 | 5032.53 |
| RSS after loading (MiB) | 518.4 | 549.1 | 525.3 |
| Peak RSS (MiB) | 3412.1 | 4557.1 | 4666.6 |

Mako's own average-latency figure overflows, so it is omitted. Peak RSS grows with the
number of committed transactions (each NewOrder inserts rows), so it is not an index
comparison on its own.

ALEX rebuild and index size (from run 1 of "ALEX, rebuilt"):

```
[learned-index-mako] rebuilt 13 ALEX indexes in 32.0 ms
[learned-index-mako] alex table=district_0 keys=10 index_bytes=440 bytes_per_key=44.0
[learned-index-mako] alex table=district_1 keys=10 index_bytes=440 bytes_per_key=44.0
[learned-index-mako] alex table=district_2 keys=10 index_bytes=440 bytes_per_key=44.0
[learned-index-mako] alex table=district_3 keys=10 index_bytes=440 bytes_per_key=44.0
[learned-index-mako] alex table=stock_0 keys=100000 index_bytes=2303784 bytes_per_key=23.0
[learned-index-mako] alex table=stock_1 keys=100000 index_bytes=2303784 bytes_per_key=23.0
[learned-index-mako] alex table=stock_2 keys=100000 index_bytes=2303784 bytes_per_key=23.0
[learned-index-mako] alex table=stock_3 keys=100000 index_bytes=2303784 bytes_per_key=23.0
[learned-index-mako] alex table=warehouse_0 keys=1 index_bytes=248 bytes_per_key=248.0
[learned-index-mako] alex table=warehouse_1 keys=1 index_bytes=248 bytes_per_key=248.0
[learned-index-mako] alex table=warehouse_2 keys=1 index_bytes=248 bytes_per_key=248.0
[learned-index-mako] alex table=warehouse_3 keys=1 index_bytes=248 bytes_per_key=248.0
[learned-index-mako] alex table=item keys=100000 index_bytes=2303784 bytes_per_key=23.0
```
