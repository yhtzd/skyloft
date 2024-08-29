CC := gcc

SKYLOFT_DIR ?=

CFLAGS += -Wall -g -D_GNU_SOURCE -DSKYLOFT_UINTR -muintr -O3
CFLAGS += -I../utils/include -I../include
LDLIBS += $(SKYLOFT_DIR)/lib/libskyloft.a $(SKYLOFT_DIR)/lib/libutils.a
LDFLAGS += -T $(SKYLOFT_DIR)/lib/libos.ld -lnuma -lpthread
LDFLAGS += $(shell pkg-config --libs libdpdk)

TARGETS := \
	uipi_send_recv \
	uipi_delivery \
	utimer_recv \
	signal_send_recv \
	signal_delivery \
	setitimer_recv \
	kipi_send_recv \
	fxsave \
	thread

all: $(TARGETS)
	ln -sf ./thread ../build/bin/thread

%: %.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) $(LDFLAGS)

clean:
	rm -rf $(TARGETS)
