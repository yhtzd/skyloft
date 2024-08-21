CC := gcc

CFLAGS += -Wall -g -D_GNU_SOURCE -DSKYLOFT_UINTR -muintr -O3
CFLAGS += -I../utils/include -I../include
LDLIBS += -lpthread

TARGETS := uipi_send_recv uipi_delivery utimer_recv signal_send_recv signal_delivery setitimer_recv kipi_send_recv

all: $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -rf $(TARGETS)
