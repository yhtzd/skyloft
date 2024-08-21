#!/bin/bash

APP=shinjuku

BIN_DIR=$(dirname "$0")/../build/bin

if [ ! -z "$1" ]; then
    APP=$1
    shift 1;
fi

ROCKSDB_DIR=/tmp/skyloft_rocksdb_$(whoami)
OUTPUT_DIR=/tmp/skyloft_experiment_$(whoami)

mkdir -p ${OUTPUT_DIR}
sudo rm -rf /dev/shm/skyloft_* /mnt/huge/skyloft_*
sudo ipcrm -a > /dev/null 2>&1

sudo ${BIN_DIR}/$APP --rocksdb_path=${ROCKSDB_DIR} --output_path=${OUTPUT_DIR} $@
