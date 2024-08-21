# Skyloft SOSP '24 Artifact Evaluation

## 1. Overview

### 1.1 Artifact Directory Layout

### 1.2 Main Experiments

| Experiments        | Figure/Table              | Runtime | Description                                                                          |
| ------------------ | ------------------------- | ------- | ------------------------------------------------------------------------------------ |
| schbench           | Figure 4 & Figure 5       |         | Per-CPU scheduling wakeup latency                                                    |
| synthetic-single   | Figure 6(a)               |         | Tail latency for a single synthetic latency-critical (LC) workload                   |
| synthetic-multiple | Figure 6(b) & Figure 6(c) |         | Tail latency and CPU share for co-located synthetic LC and best-effort (BE) workload |
| memcached          | Figure 7(a)               |         | Tail latency for a Memcached server                                                  |
| preempt            | Table 5                   |         | Preemption mechanism overhead                                                        |

## 2. System Requirements

### 2.1 Hardware

Server:

- CPU: Intel CPU with User Interrupts support (i.e. 4th Gen or later Intel® Xeon® Scalable processor).
- Network: Intel NIC supported by DPDK ixgbe driver.

Client:

- Network: Same as server side.

To achieve low latency and stable results, TurboBoost, CPU idle states, CPU frequency scaling and transparent hugepages need to be disabled.

For reference, we used the following setup when conducting evaluations in the paper. We recommend using the same CPU and NIC, since results might depend on core number and clock frequency, etc.:

Server:

- CPU: 2x Intel(R) Xeon(R) Gold 5418Y @ 2.0 GHz
- RAM: 4x 32GB DDR5 RDIMM @ 4400MT/s
- Storage: 1TB PCIe 4.0 NVMe SSD
- NIC: Intel(R) 82599ES NIC
- MB: Super Micro X13DEI

Client:

- CPU: 2x Intel(R) Xeon(R) E5-2683 v4 @ 2.1 GHz
- RAM: 4x 32GB DDR4 RDIMM @ 2400MT/s
- Storage: 1TB SATA SSD
- NIC: Intel(R) 82599ES NIC
- MB: Super Micro X10DRI

The NICs need to be inserted to the PCIe slots connected to the CPU0. The server and the client are directly connected with an SFP+ fiber-optic cable.

### 2.2 Software

- Linux: We recommend using Ubuntu 22.04 LTS.
- Python: for plotting figures; `numpy` and `matplotlib` packages are required.
- git
- GCC
- CMake

Other software dependencies are provided in our GitHub repositories, and installation steps are covered in latter sections:

- Linux kernel
- DPDK
- schbench
- RocksDB
- Memcached

## 3. Getting Started

**Following instructions are for the Server.**

### 3.0 Check Requirements

Make sure the CPU supports `UINTR`. If it does, the output of the following command is not empty:

```sh
$ cat /proc/cpuinfo | grep -w uintr
flags           : fpu vme de ... uintr md_clear serialize tsxldtrk ...
```

### 3.1 Clone the AE Repository

```sh
$ git clone https://github.com/yhtzd/skyloft-sosp24-ae.git
```

### 3.2 Build and Install the Kernel

```sh
$ git clone -b skyloft --depth 1 https://github.com/yhtzd/uintr-linux-kernel.git
$ cd uintr-linux-kernel
$ ./build.sh
```

### 3.3 Configure Kernel Commandline Parameters

Take GRUB as example:

1. Open `/etc/default/grub`, add or modify the line:

   ```config
   GRUB_CMDLINE_LINUX="isolcpus=0-23,48-71 nohz_full=0-23,48-71 intel_iommu=off nopat watchdog_thresh=0"
   ```

   This isolates all cores on CPU0 for running `Skyloft`. Core numbers for `isolcpus` and `nohz_full` should be adjusted according to the specific CPU model. You may use `numactl --hardware` or `lstopo` to check which cores belong to CPU0. This also disables IOMMU for `Skyloft`'s network stack to work.
2. Generate the new grub config file and reboot your system:

   ```sh
   $ sudo update-grub2
   $ sudo reboot
   ```

   Remember to select the kernel installed in the previous step when rebooting.
3. Verify the kernel and parameters are applied (the kernel version string might be different):

   ```sh
   $ cat /proc/cmdline
   BOOT_IMAGE=/vmlinuz-6.0.0-skyloft-nohzfull+ root=UUID=3f07ca35-20a0-41df-a6c3-96786074c290 ro isolcpus=0-23,48-71 nohz_full=0-23,48-71 intel_iommu=off nopat watchdog_thresh=0 quiet splash console=tty0 console=ttyS0,115200n8 vt.handoff=7
   $ uname -r
   6.0.0-skyloft-nohzfull+
   ```

### 3.4 Download Skyloft

```sh
$ git clone https://github.com/yhtzd/skyloft.git
$ cd skyloft
$ git submodule update --init --recursive
```

### 3.5 Setup Hosts

Disable CPU frequency scaling so all cores are running at base clock, and setup hugepages:

```sh
$ cd skyloft/scripts
$ ./install_deps.sh
$ ./disable_cpufreq_scaling.sh -c 0-23
$ sudo ./setup_host.sh
```

### 3.6 Install DPDK

Install DPDK v22.11 on the machine:

```sh
$ git clone https://github.com/yhtzd/dpdk.git
$ cd dpdk
$ meson build
$ cd build
$ ninja
$ sudo meson install
```

And show the NIC status:

```sh
$ sudo dpdk-devbind.py -s

Other Network devices
=====================
0000:2a:00.0 '82599ES 10-Gigabit SFI/SFP+ Network Connection 10fb' unused=ixgbe,vfio-pci,uio_pci_generic
```

then bind the NIC to the UIO driver:

```sh
$ sudo dpdk-devbind.py --bind=uio_pci_generic 2a:00.0 --force
```

### 3.7 Install Skyloft kernel module

Make sure you have completed the previous steps:

```sh
$ cd skyloft/kmod
$ UINTR=1 make
$ make insmod
```

**Following instructions are for the Client.**

### 3.8 Setup

In this step, we use the DPDK submodule of Shenango.

```sh
# Download Shenango
$ git clone https://github.com/yhtzd/shenango-client.git
$ cd shenango-client
# Download and build DPDK
$ ./dpdk.sh
# Setup hugepages
$ ./scripts/setup_machine
# Setup igb_uio driver
$ cd dpdk
$ ./usertools/dpdk-setup.sh
# Then type 38, 45 and 62
# Bind the uio driver
$ sudo ./usertools/dpdk-devbind.py -s

Network devices using kernel driver
===================================
0000:02:00.0 '82599ES 10-Gigabit SFI/SFP+ Network Connection 10fb' if=ens2f0 drv=ixgbe unused=igb_uio,uio_pci_generic

$ sudo ./usertools/dpdk-devbind.py --bind=igb_uio 02:00.1

# Build Shenango
$ make clean && make -j

# Build synthetic client
$ cd apps/synthetic
$ cargo build
```

## 4. Run Experiments

Each expriment needs different parameters, such as number of CPU cores, preemption quantum, and which CPU to be used for IO activities. These paramerter profiles are stored in `skyloft/scripts/params`, and merged with defaults at compile time by the build script (`skyloft/scripts/build.sh`).

### 4.1 schbench

The `schbench` experiment shows `Skyloft`'s per-CPU scheduler performance, by comparing the 99% wakeup latency of the `schbench` schduler benchmarking tool. This experiment uses 24 CPU cores, running different number of worker threads, ranging from 8 to 96.

The parameters of each build target are listed as follows:

| Build Target         | Schedule Policy | Preemption Quantum |
| -------------------- | --------------- | ------------------ |
| schbench-cfs-50us    | CFS             | 50us               |
| schbench-rr-50us  | RR              | 50us               |
| schbench-rr-200us | RR              | 200us              |
| schbench-rr-1ms   | RR              | 1ms                |
| schbench-rr       | FIFO            | No preemption      |

To run this experiment, take `schbench-cfs-50us` as an example:

```sh
cd skyloft
./scripts/build.sh schbench-cfs-50us
./scripts/bench/schbench.sh cfs-50us
```

The results are written into `results_24_cfs-50us` folder. Data from different number of worker threads are summarized in the `all.csv` file, and the output of each run are stored in `.txt` files.

To plot the figures, move `all.csv` file to the `results/schbench/skyloft_cfs50us` directory (change this path according to the build target), then run the plot script:

```sh
mv results_24_cfs-50us/all.csv results/schbench/skyloft_cfs50us/
cd scripts/plots
python3 plot_schbench.py
python3 plot_schbench2.py
```

The figures are saved in `scripts/plots/schbench.pdf` and `scripts/plots/schbench2.pdf`, corresponding to the Figure 4 and Figure 5 in the paper.

### 4.2 synthetic-single

The `run_synthetic_lc.sh` script runs a single synthetic LC app (`shinjuku`), iterating over different target throughput, with the following parameters:

- Workers: 20
- Dispatcher: 1
- Load distribution: bimodal, 99.5% 4us, 5% 10000us
- Preemption quantum: 30 μs

```sh
$ cd skyloft/scripts
$ ./build synthetic-sq
$ ./run_synthetic_lc.sh
```

The experiment data is saved in a file named `data-lc` in CSV format. Each column corresponds to the following data:

| Target Throughput | Measured Throughput | Min. Latency | 50% | 99% | 99.5% | 99.9% | Max. Latency |
| ----------------- | ------------------- | ------------ | --- | --- | ----- | ----- | ------------ |

To plot the output, move and rename the `data-lc` file to `skyloft/results/synthetic/99.5-4-0.5-10000/shinjuku-30us`, and run `plot_synthetic.py` (this script plots both `synthetic-single` and `synthetic-multiple` figures):

```sh
$ mv data-lc ../results/synthetic/99.5-4-0.5-10000/skyloft-30us
$ cd plots
$ python3 plot_synthetic.py
```

The figures are saved in `synthetic-a.pdf` (Figure 6(a)), `synthetic-b.pdf` (Figure 6(b)), and `synthetic-c.pdf` (Figure 6(c)).

### 4.3 synthetic-multiple

The `run_synthetic_lcbe.sh` script runs both a LC app and a BE app (`antagonist`), with the parameters same as the previous experiment. The `Skyloft` is built with `sq_lcbe` sheduling policy.

```sh
$ cd skyloft/scripts
$ ./build synthetic-sq_lcbe
$ ./run_synthetic_lcbe.sh
```

After running the script, the terminal output might be messed, and need a `reset` command to recover.

The experiment data of the LC app is saved in the `data-lc` file, which is the same. The data of BE app is in the `data-be` file, with columns defined as follows:

| Measured Throughput of the LC App. | CPU share of the BE App. |
| ---------------------------------- | ------------------------ |

The plotting procedure is similar:

```sh
$ mv data-lc ../results/synthetic/99.5-4-0.5-10000-lcbe/skyloft-30us-lc
$ mv data-be ../results/synthetic/99.5-4-0.5-10000-lcbe/skyloft-30us-be
$ cd plots
$ python3 plot_synthetic.py
```

The figures are saved in `synthetic-a.pdf` (Figure 6(a)), `synthetic-b.pdf` (Figure 6(b)), and `synthetic-c.pdf` (Figure 6(c)).

### 4.4 memcached

Build and run the Skyloft memcached on Server:

```sh
$ cd skyloft/scripts
$ ./build memcached
$ ./run.sh memcached -p 11211 -t 4 -u root
```

Run the Shenango client on Client:

```sh
$ sudo ./iokerneld
$ numactl -N 0 -- \
   ./apps/synthetic/target/release/synthetic \
   10.3.3.3:11211 \
   --config client.config \
   --threads 32 \
   --mode runtime-client \
   --protocol memcached \
   --samples 30 --start_mpps 0 --mpps 3.0 \
   --transport tcp \
   --runtime 1000000000
```

### 4.6 preempt

First, build benchmarks for various preemption mechanism:

```sh
cd microbench
make
```

Entries in Table 5 can be obtained by running the following commands:

| Entries in Table 5   | Command |
| ------------------ | ------- |
| Signal Send/Recv   | `./signal_send_recv` |
| Signal Delivery    | `./signal_delivery` |
| User IPI Send/Recv | `./uipi_send_recv` |
| User IPI Delivery  | `./uipi_delivery` |
| `setitimer` Recv   | `./setitimer_recv` |
| User timer interrupt Recv | `./utimer_recv` |
| Kernel IPI Send/Recv | `./kipi_send_recv` |

For the Kernel IPI benchmark, you need to get the output of the kernel module by `demsg`:

```console
$ ./kipi_send_recv
run on CPU: 2
sender run on CPU: 3
work time = 1999826054 cycles
kipi recv total latency = 4370643528 cycles
$ sudo dmesg
...
[42868.588566] skyloft: skyloft_ipi_bench: total=4370647834, avg=437 (cycles), ipi_recv_cnt = 2761085
...
```

The send time is `437` cycles, and the receive time is `4370647834 / 2761085 = 1582` cycles.

## 5. Related Work

### 5.1 ghOSt

First, install the [kernel](https://github.com/google/ghost-kernel). On Ubuntu 18.04, install the generic version 5.11, and then in the `ghost-kernel` directory, run `make oldconfig` and then install.

The `[ghost-userspace](https://github.com/yhtzd/ghost-userspace)` project uses Bazel as the build tool. When attempting to build, the following error occurs:

```log
ERROR: external/subpar/compiler/BUILD:31:10: in py_binary rule @@subpar//compiler:compiler:
Traceback (most recent call last):
    File "/virtual_builtins_bzl/common/python/py_binary_bazel.bzl", line 38, column 36, in _py_binary_impl
    File "/virtual_builtins_bzl/common/python/py_executable_bazel.bzl", line 97, column 37, in py_executable_bazel_impl
    File "/virtual_builtins_bzl/common/python/py_executable.bzl", line 108, column 25, in py_executable_base_impl
    File "/virtual_builtins_bzl/common/python/py_executable.bzl", line 189, column 13, in _validate_executable
Error in fail: It is not allowed to use Python 2
```

Directly modifying the `BUILD` file to change the default from PY2 to PY3 resolves the error.

To install `gcc-9`, execute the following commands, as the default `gcc` version is too low to support C++20:

```sh
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt update
sudo apt install gcc-9 g++-9
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-9 60 --slave /usr/bin/g++ g++ /usr/bin/g++-9
```

To install `clang-12`, use the following commands:

```sh
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 12
```

During `sudo apt update`, an issue occurs where an AMD machine attempts to pull ARM64 images. Specify the architecture `[arch=amd64,i386]` in the `source.list`.

An error `error: use of undeclared identifier 'BPF_F_MMAPABLE'` occurs. To resolve this, in the `ghost-kernel` directory, run `sudo make headers_install INSTALL_HDR_PATH=/usr` to overwrite the existing Linux headers.

