# Skyloft

Skyloft: A General High-Efficient Scheduling Framework in User Space

## Overview

<div align="left">
<img src=docs/imgs/overview.jpg width=60% />
</div>

### Layout

- `apps/`: Benchmark real-world applications
- `docs/`: Documents and images
- `synthetic/`: Benchmark c-FCFS and PS scheduling policies
    - `rocksdb/`: Latency-critical application
    - `antagonist/`: Batch application
- `kmod/`: Skyloft kernel module
- `libos/`: Skyloft main code
    - `io/`: IO thread
    - `net/`: Network stack
    - `shim/`: Shim layer for POSIX APIs
    - `sync/`: Synchronization primitives
    - `mm/`: Memory management
    - `sched/`: Schedulers
- `utils/`: Useful tools
- `scripts/`: Setup machine; run experiments
- `microbench/`: Microbenchmarks and prototypes
- `paper_results/`: Experimental results in the paper
