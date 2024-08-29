SIGNAL ?=
DPDK ?= 1
TIMER ?= 1
UINTR ?= 0
SCHED ?= fifo
DAEMON ?=
DEBUG ?=
STAT ?= 0
LOG ?= info
FXSAVE ?= 0

CC ?= gcc
CFLAGS := -Wall -O2 -D_GNU_SOURCE
CMAKE_ARGS ?=

CMAKE_ARGS += -DSCHED_POLICY=$(SCHED)
CMAKE_ARGS += \
	-DSIGNAL=$(SIGNAL) \
	-DDPDK=$(DPDK) \
	-DTIMER=$(TIMER) \
	-DUINTR=$(UINTR) \
	-DDAEMON=$(DAEMON) \
	-DDEBUG=$(DEBUG) \
	-DSTAT=$(STAT) \
	-DLOG_LEVEL=$(LOG) \
	-DFXSAVE=$(FXSAVE)
CMAKE_ARGS += -DCMAKE_INSTALL_PREFIX=install

all: build

build:
	mkdir -p build
	cd build && cmake .. $(CMAKE_ARGS) && make VERBOSE=1 -j4

install: build
	cd build && make install

kmod:
	cd kmod && make

insmod:
	cd kmod && make insmod

tests:
	cd tests && make

memcached: install
	cd build && make memcached VERBOSE=1

zstd: install
	cd build && make zstd VERBOSE=1

schbench: install
	cd build && make schbench VERBOSE=1

rocksdb: install
	cd build && make rocksdb_server VERBOSE=1

microbench: install
	cd build && make microbench VERBOSE=1

fmt:
	@clang-format --style=file -i $(shell find utils/ libos/ apps/ synthetic/ -iname '*.c' -o -iname '*.cc' -o -iname '*.h')

clean:
	make -C build clean

distclean: clean
	rm -rf build
	make -C tests clean
	make -C kmod clean

.PHONY: all clean fmt kmod build tests install
