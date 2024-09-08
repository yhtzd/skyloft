#!/bin/bash

tag=$1
dir=results_24_$tag

bin=$(dirname "$0")/../../$base/build/bin/schbench
cmd="$bin -n10 -F128 -m1 -r10 -i5"

mkdir -p $dir
echo "cores,wake99,rps50,lat99" > $dir/all.csv

cores=$(seq 8 8 96)

for i in $cores; do
    echo "Running with $i cores"
    output="$dir/$i.txt"
    sudo rm -rf /dev/shm/skyloft_* /mnt/huge/skyloft_*
    sudo ipcrm -a > /dev/null 2>&1
    sleep 5
    timeout 20 $cmd -t$i 2>&1 | tee $output

    wake=$(cat $output | grep -a "* 99.0th" | tail -n2 | head -n1 | awk '{print $3}')
    lat=$(cat $output | grep -a "* 99.0th" | tail -n1 | awk '{print $3}')
    rps=$(cat $output | grep -a "* 50.0th" | tail -n1 | awk '{print $3}')
    echo "$i,$wake,$rps,$lat"
    echo "$i,$wake,$rps,$lat" >> $dir/all.csv
done
