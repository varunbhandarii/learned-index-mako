## Index microbenchmark: 1,000,000 keys, 3 runs per configuration (medians)

Lookups are uniform random point lookups. Memory is the growth in resident memory
while building the index, with freed pages returned to the OS first; values are
pointers, so it is the index alone.

### dense 4-byte ids (like TPC-C item)

| Index | Lookup, 1 thread (ns) | Lookups/s, 4 threads (M) | Build (ms) | Memory (bytes/key) |
| :--- | ---: | ---: | ---: | ---: |
| masstree | 331.8 | 12.24 | 48.2 | 23.3 |
| alex | 247.5 | 18.17 | 168.0 | 24.9 (ALEX counts 22.5) |
| alex-rebuilt | 127.1 | 23.43 | 199.3 | 24.0 (ALEX counts 23.0) |

### dense 8-byte (warehouse, item) (like TPC-C stock)

| Index | Lookup, 1 thread (ns) | Lookups/s, 4 threads (M) | Build (ms) | Memory (bytes/key) |
| :--- | ---: | ---: | ---: | ---: |
| masstree | 346.1 | 12.06 | 44.8 | 23.3 |
| alex | 342.4 | 11.3 | 211.7 | 25.6 (ALEX counts 24.8) |
| alex-rebuilt | 110.4 | 21.71 | 260.3 | 25.0 (ALEX counts 23.0) |

### random 8-byte keys

| Index | Lookup, 1 thread (ns) | Lookups/s, 4 threads (M) | Build (ms) | Memory (bytes/key) |
| :--- | ---: | ---: | ---: | ---: |
| masstree | 440.6 | 9.6 | 445.0 | 33.7 |
| alex | 200.4 | 20.58 | 313.5 | 25.2 (ALEX counts 23.4) |
| alex-rebuilt | 136.7 | 20.44 | 419.0 | 23.9 (ALEX counts 23.0) |
