#!/bin/bash

APP=hello

BIN_DIR=$(dirname "$0")/../build/bin

if [ ! -z "$1" ]; then
    APP=$1
    shift 1;
fi

sudo rm -rf /dev/shm/skyloft_* /mnt/huge/skyloft_*
sudo ipcrm -a > /dev/null 2>&1

sudo gdb --args ${BIN_DIR}/$APP $@
