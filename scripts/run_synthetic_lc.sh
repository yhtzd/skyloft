#!/bin/bash

set -x

LC_APP=shinjuku
LC_OUT_FILE=./data-lc
NUM_WORKERS=20
RUN_TIME=5

BIN_DIR=$(dirname "$0")/../build/bin

loads="$(seq 0.1 0.1 0.7) $(seq 0.8 0.02 0.9)"
# loads="$(seq 0.8 0.1 0.8)"

rm $LC_OUT_FILE
touch $LC_OUT_FILE
mkdir -p /tmp/skyloft_synthetic

for i in $loads; do
    echo "Load: $i"
    sudo rm -rf /dev/shm/skyloft_* /mnt/huge/skyloft_*
    sudo ipcrm -a > /dev/null 2>&1
    # gdb --args ${BIN_DIR}/$APP --run_time=5 \
    ${BIN_DIR}/$LC_APP \
        --get_service_time=4000 \
        --range_query_service_time=10000000 \
        --fake_work \
        --load=$i \
        --range_query_ratio=0.005 \
        --preemption_quantum=30 \
        --run_time=$RUN_TIME \
        --num_workers=$NUM_WORKERS \
        --output_path=$LC_OUT_FILE
        # --detailed_print
done
