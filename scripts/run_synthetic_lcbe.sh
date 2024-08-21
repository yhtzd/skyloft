#!/bin/bash

# set -x

LC_APP=shinjuku
LC_OUT_FILE=./data-lc
LC_GUARANTEED_CPUS=4
BE_APP=antagonist
BE_OUT_FILE=./out-be
BE_DATA_FILE=./data-be
NUM_WORKERS=20
RUN_TIME=5

BIN_DIR=$(dirname "$0")/../build/bin

loads="$(seq 0.1 0.1 0.7) $(seq 0.8 0.02 0.9)"
# loads="$(seq 0.8 0.1 0.8)"

mkdir -p /tmp/skyloft_synthetic
rm $LC_OUT_FILE $BE_OUT_FILE $BE_DATA_FILE
touch  $LC_OUT_FILE $BE_OUT_FILE $BE_DATA_FILE

for i in $loads; do
    echo "Load: $i"
    sudo rm -rf /dev/shm/skyloft_* /mnt/huge/skyloft_*
    sudo ipcrm -a > /dev/null 2>&1
    # gdb --args ${BIN_DIR}/$APP --run_time=5 \
    sudo ${BIN_DIR}/$LC_APP \
        --get_service_time=4000 \
        --range_query_service_time=10000000 \
        --fake_work \
        --load=$i \
        --range_query_ratio=0.005 \
        --preemption_quantum=30 \
        --run_time=$RUN_TIME \
        --num_workers=$NUM_WORKERS \
        --guaranteed_cpus=$LC_GUARANTEED_CPUS \
        --output_path=$LC_OUT_FILE &
        # --detailed_print
    sleep 2
    sudo ${BIN_DIR}/$BE_APP \
        --run_time=$RUN_TIME \
        --num_workers=$NUM_WORKERS \
        --output_path=$BE_OUT_FILE
    sleep 2

    rps=$(tail -n 1 $LC_OUT_FILE | cut -d ',' -f 2)
    share=$(tail -n 1 $BE_OUT_FILE)
    echo "$rps,$share"
    echo "$rps,$share" >> $BE_DATA_FILE
done
