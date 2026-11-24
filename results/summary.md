```
date: 2026-11-15T10:53:23Z
kernel: Linux 6.6.87.2-microsoft-standard-WSL2
cpu:  Intel(R) Core(TM) Ultra 7 155H
governor: n/a
thp: always [madvise] never
compiler: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
```

### SPSC queues

| queue | Mmsg/s (median) | one-way p50 ns | p99 ns | p99.9 ns |
|---|---|---|---|---|
| mutex | 7.61 | 377.893 | 2857.34 | 6387.4 |
| naive | 9.05 | 199.288 | 335.868 | 1512.56 |
| padded | 23.61 | 167.77 | 241.313 | 567.003 |
| cached | 37.84 | 136.251 | 241.313 | 377.893 |
| cached+batch | 41.53 | - | - | - |

### Order book implementations (single thread)

| book | Mmsg/s | p50 ns | p90 | p99 | p99.9 | max | allocs/msg |
|---|---|---|---|---|---|---|---|
| map+list+unordered_map (baseline) | 7.59 | 110 | 399 | 756 | 1428 | 5072490 | 0.986 |
| ladder+bitmap, std::unordered_map ids | 10.27 | 89 | 273 | 504 | 714 | 2316980 | 0.493 |
| ladder+bitmap, flat-hash ids | 41.64 | 60 | 162 | 283 | 399 | 1936160 | 0.000 |

### Pipeline max throughput (no per-message timing)

| queue | book | Mmsg/s | e2e p50 ns | p99 | p99.9 | apply p50 | p99 | p99.9 | allocs |
|---|---|---|---|---|---|---|---|---|---|
| mutex | ladder | 2.59 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| naive | ladder | 5.12 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| padded | ladder | 11.76 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| cached | ladder | 12.99 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| cached | map | 3.93 | 0 | 0 | 0 | 0 | 0 | 0 | 3943814 |
| cached | stdhash | 6.83 | 0 | 0 | 0 | 0 | 0 | 0 | 1971123 |

### Pipeline end-to-end, paced

| queue | book | Mmsg/s | e2e p50 ns | p99 | p99.9 | apply p50 | p99 | p99.9 | allocs |
|---|---|---|---|---|---|---|---|---|---|
| cached | map | 1.00 | 588 | 9300750 | 18601500 | 273 | 1219 | 3364 | 3943814 |
| cached | ladder | 1.00 | 482 | 7895750 | 10985400 | 199 | 565 | 838 | 0 |

### Paced, unpinned

| queue | book | Mmsg/s | e2e p50 ns | p99 | p99.9 | apply p50 | p99 | p99.9 | allocs |
|---|---|---|---|---|---|---|---|---|---|
| cached | ladder | 1.00 | 483 | 3959280 | 8607140 | 199 | 567 | 840 | 0 |

### Paced, pinned + hugepages

| queue | book | Mmsg/s | e2e p50 ns | p99 | p99.9 | apply p50 | p99 | p99.9 | allocs |
|---|---|---|---|---|---|---|---|---|---|
| cached | ladder | 1.00 | 462 | 2926540 | 8951780 | 189 | 546 | 756 | 0 |
