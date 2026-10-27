## Index microbenchmark: 1,000,000 keys, 3 runs per configuration (medians)

Lookups are uniform random point lookups. Memory is the growth in resident memory
while building the index, with freed pages returned to the OS first; values are
pointers, so it is the index alone.

### dense 4-byte ids (like TPC-C item)

| Index | Lookup, 1 thread (ns) | Lookups/s, 4 threads (M) | Build (ms) | Memory (bytes/key) |
| :--- | ---: | ---: | ---: | ---: |
| masstree | 467.3 | 9.79 | 71.4 | 23.3 |
| alex | 306.8 | 6.77 | 204.2 | 25.0 (ALEX counts 22.5) |
| alex-rebuilt | 166.0 | 6.92 | 255.1 | 24.1 (ALEX counts 23.0) |

### dense 8-byte (warehouse, item) (like TPC-C stock)

| Index | Lookup, 1 thread (ns) | Lookups/s, 4 threads (M) | Build (ms) | Memory (bytes/key) |
| :--- | ---: | ---: | ---: | ---: |
| masstree | 472.9 | 9.8 | 60.7 | 23.3 |
| alex | 470.9 | 6.07 | 207.4 | 25.6 (ALEX counts 24.8) |
| alex-rebuilt | 168.8 | 7.39 | 303.7 | 25.1 (ALEX counts 23.0) |

### random 8-byte keys

| Index | Lookup, 1 thread (ns) | Lookups/s, 4 threads (M) | Build (ms) | Memory (bytes/key) |
| :--- | ---: | ---: | ---: | ---: |
| masstree | 550.3 | 7.71 | 549.2 | 33.7 |
| alex | 268.3 | 6.7 | 414.8 | 25.2 (ALEX counts 23.4) |
| alex-rebuilt | 202.3 | 6.84 | 575.3 | 24.1 (ALEX counts 23.0) |
