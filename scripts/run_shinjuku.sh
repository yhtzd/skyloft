#!/bin/bash

APP=shinjuku

BIN_DIR=$(dirname "$0")/../build/bin

loads="$(seq 0.1 0.1 0.7) $(seq 0.8 0.02 0.9)"
# loads="$(seq 0.8 0.1 0.8)"

mkdir -p /tmp/skyloft_synthetic

for i in $loads; do
    echo "Load: $i"
    sudo rm -rf /dev/shm/skyloft_* /mnt/huge/skyloft_*
    # gdb --args ${BIN_DIR}/$APP --run_time=5 \
    ${BIN_DIR}/$APP --run_time=5 \
        --num_workers=20 \
        --get_service_time=4000 \
        --range_query_service_time=10000000 \
        --range_query_ratio=0.005 \
        --load=$i \
        --fake_work \
        --preemption_quantum=30 \
        --output_path=./data
        # --detailed_print
done
