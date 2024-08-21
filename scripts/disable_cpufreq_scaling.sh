#!/bin/bash

cpu_list="2,3,26,27"

function usage() {
    echo "Usage: $0 [OPTIONS] <TAG>"
    echo ""
    echo "Options:"
    echo "    -h                      Display this message"
    echo "    -c <CPU_LIST>           List of CPUs"
    echo "    -a                      Select all CPUs"
    echo ""
}

while getopts "c:a" opt
do
   case "$opt" in
      c) cpu_list="$OPTARG";;
      a) cpu_list=$(cat /sys/devices/system/cpu/online);;
      h | ?) usage; exit 0 ;;
   esac
done

cpu_list=$(echo $cpu_list | perl -pe 's/(\d+)-(\d+)/join(",", $1..$2)/eg')

echo $cpu_list

for cpu in ${cpu_list//,/ }
do
    sudo cpufreq-set -c ${cpu} -g performance
    freq=$(cat /sys/devices/system/cpu/cpu${cpu}/cpufreq/scaling_cur_freq)
    echo "Set CPU ${cpu} frequency to ${freq} kHz"
done
