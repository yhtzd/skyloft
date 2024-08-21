#!/bin/bash

echo 0 > /proc/sys/kernel/hung_task_timeout_secs
echo 1 > /sys/module/rcupdate/parameters/rcu_cpu_stall_suppress
echo never > /sys/kernel/mm/transparent_hugepage/enabled
echo never > /sys/kernel/mm/transparent_hugepage/defrag

chmod -R 777 /dev/hugepages

echo 8192 | tee /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages
mkdir -p /mnt/huge || true
mount -t hugetlbfs -opagesize=2M nodev /mnt/huge
chmod 777 /mnt/huge

modprobe uio_pci_generic
