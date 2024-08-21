#!/bin/bash

sudo mkdir mnt
sudo dd if=/dev/zero of=./rootfs.ext4 bs=1M count=32
sudo mkfs.ext4 rootfs.ext4
sudo mount rootfs.ext4 mnt

sudo cp -r $1/_install/* mnt

cd mnt
sudo mkdir -p sys mnt sys etc/init.d dev tmp proc

cd dev
sudo mknod console c 5 1
sudo mknod null c 1 3
sudo mknod tty1 c 4 1
sudo mknod ttyS0 c 4 64

cd ../
sudo echo "proc    /proc   proc    defaults        0       0" > etc/fstab
sudo echo "tmpfs   /tmp    tmpfs   defaults        0       0" >> etc/fstab
sudo echo "sysfs   /sys    sysfs   defaults        0       0" >> etc/fstab
sudo chmod 755 etc/fstab

sudo echo "/bin/mount -a" > etc/init.d/rcS
sudo echo "mount -o remount, rw /" >> etc/init.d/rcS
sudo echo "mkdir -p /dev/pts" >> etc/init.d/rcS
sudo echo "mount -t devpts devpts /dev/pts" >> etc/init.d/rcS
sudo echo "mdev -s" >> etc/init.d/rcS
sudo chmod 755 etc/init.d/rcS

sudo echo "::sysinit:/etc/init.d/rcS" > etc/inittab
sudo echo "::respawn:-/bin/sh" >> etc/inittab
sudo echo "::askfirst:-/bin/sh" >> etc/inittab
sudo echo "::ctrlaltdel:/bin/umount -a -r" >> etc/inittab
sudo chmod 755 etc/inittab

cd ..
sudo umount mnt
sudo rm -rf mnt

gzip --best -c rootfs.ext4 > rootfs.img.gz
rm -rf rootfs.ext4
