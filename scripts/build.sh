#!/bin/bash

script_dir=$(dirname $(readlink -f $0))

app_name=$1
sched_policy=$2
preempt_quantum=$3

if [[ $app_name =~ synthetic-(.*) ]]; then
    params_file=$script_dir/params/shinjuku.params
    build_cmd="make install SCHED=${BASH_REMATCH[1]} UINTR=1 DPDK=0"
elif [[ $app_name =~ schbench-([a-zA-Z0-9]+).* ]]; then
    params_file=$script_dir/params/$app_name.params
    build_cmd="make schbench SCHED=${BASH_REMATCH[1]} UINTR=1 DPDK=0 LOG=warn"
elif [ $app_name == "memcached" ]; then
    params_file=$script_dir/params/$app_name.params
    build_cmd="make memcached SCHED=fifo UINTR=0 DPDK=1"
elif [[ $app_name =~ rocksdb-server-([a-zA-Z0-9]+) ]]; then
    params_file=$script_dir/params/$app_name.params
    build_cmd="make rocksdb SCHED=fifo UINTR=1 DPDK=1 FXSAVE=1"
elif [[ $app_name =~ rocksdb-server-([a-zA-Z0-9]+)-utimer ]]; then
    params_file=$script_dir/params/$app_name.params
    build_cmd="make rocksdb SCHED=fifo UINTR=1 DPDK=1 FXSAVE=1"
elif [ $app_name == "rocksdb-server" ]; then
    params_file=$script_dir/params/$app_name.params
    build_cmd="make rocksdb SCHED=fifo UINTR=0 DPDK=1"
elif [ $app_name == "microbench" ]; then
    params_file=$script_dir/params/microbench.params
    build_cmd="make microbench SCHED=rr UINTR=0 DPDK=0"
fi

# Generate parameters

output_params=$script_dir/../params.h.in
default_params=$script_dir/params/default.params
if [[ ! -f $params_file ]]; then
    echo "Params file $params_file does not exist."
    exit 1
fi
cat $default_params > $output_params
while IFS= read -r line; do
    if [[ $line =~ ^#define[[:space:]]+([a-zA-Z_][a-zA-Z_0-9]*) ]]; then
        macro_name=${BASH_REMATCH[1]}
        echo "#undef $macro_name" >> $output_params
        echo "$line" >> $output_params
    fi
done < $params_file

# Build the application

eval $build_cmd
