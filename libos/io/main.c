/*
 * main.c: a thread running on the CPU dedicated for network I/O
 */

#include <errno.h>
#include <stdio.h>

#include <skyloft/global.h>
#include <skyloft/io.h>
#include <skyloft/mm.h>
#include <skyloft/params.h>
#include <skyloft/platform.h>
#include <skyloft/sched.h>

#include <utils/defs.h>
#include <utils/init.h>
#include <utils/log.h>
#include <utils/lrpc.h>

#define LOG_INTERVAL_US (3000 * 1000)

struct iothread *io;
physaddr_t *page_paddrs;

int str_to_ip(const char *str, uint32_t *addr)
{
    uint8_t a, b, c, d;
    if (sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4) {
        return -EINVAL;
    }

    *addr = MAKE_IP_ADDR(a, b, c, d);
    return 0;
}

int str_to_mac(const char *str, struct eth_addr *addr)
{
    size_t i;
    static const char *fmts[] = {"%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", "%hhx-%hhx-%hhx-%hhx-%hhx-%hhx",
                                 "%hhx%hhx%hhx%hhx%hhx%hhx"};

    for (i = 0; i < ARRAY_SIZE(fmts); i++) {
        if (sscanf(str, fmts[i], &addr->addr[0], &addr->addr[1], &addr->addr[2], &addr->addr[3],
                   &addr->addr[4], &addr->addr[5]) == 6) {
            return 0;
        }
    }
    return -EINVAL;
}

static size_t cal_chan_size(int nr)
{
    size_t ret = 0, q;

    /* RX queues */
    q = sizeof(struct lrpc_msg) * IO_PKTQ_SIZE;
    q = align_up(q, CACHE_LINE_SIZE);
    ret += q * nr;

    /* TX packet queues */
    q = sizeof(struct lrpc_msg) * IO_PKTQ_SIZE;
    q = align_up(q, CACHE_LINE_SIZE);
    ret += q * nr;

    /* TX command queues */
    q = sizeof(struct lrpc_msg) * IO_CMDQ_SIZE;
    q = align_up(q, CACHE_LINE_SIZE);
    ret += q * nr;

    /* TX egress pool */
    ret = align_up(ret, PGSIZE_2MB);
    ret += EGRESS_POOL_SIZE(proc->nr_ks);
    ret = align_up(ret, PGSIZE_2MB);

    return ret;
}

int iothread_init(void)
{
    int i, ret = 0;
    void *base;
    size_t len, nr_pages;
    struct kthread *k;
    char *ptr;

    io = (struct iothread *)malloc(sizeof(struct iothread));

    /* default configurations */
    str_to_ip(IO_ADDR, &io->addr);
    str_to_mac(IO_MAC, &io->mac);
    str_to_ip(IO_GATEWAY, &io->gateway);
    str_to_ip(IO_NETMASK, &io->netmask);

    /* map communication shared memory for command queues and egress packets */
    len = cal_chan_size(proc->nr_ks);
    base = mem_map_anom(NULL, len, PGSIZE_2MB, 0);
    if (base == MAP_FAILED) {
        log_err("ioqueues: mem_map_shm() failed");
        return -1;
    }

    ptr = base;
    for (i = 0; i < proc->nr_ks; i++) {
        k = &proc->all_ks[i];
        mbufq_init(&k->txpktq_overflow);
        mbufq_init(&k->txcmdq_overflow);
        lrpc_init(&k->rxq, (struct lrpc_msg *)ptr, IO_PKTQ_SIZE);
        ptr += align_up(sizeof(struct lrpc_msg) * IO_PKTQ_SIZE, CACHE_LINE_SIZE);
        lrpc_init(&k->txpktq, (struct lrpc_msg *)ptr, IO_PKTQ_SIZE);
        ptr += align_up(sizeof(struct lrpc_msg) * IO_PKTQ_SIZE, CACHE_LINE_SIZE);
        lrpc_init(&k->txcmdq, (struct lrpc_msg *)ptr, IO_CMDQ_SIZE);
        ptr += align_up(sizeof(struct lrpc_msg) * IO_CMDQ_SIZE, CACHE_LINE_SIZE);
    }

    ptr = (char *)align_up((uintptr_t)ptr, PGSIZE_2MB);
    io->tx_region.base = ptr;
    io->tx_region.len = EGRESS_POOL_SIZE(proc->nr_ks);

    /* initialize the table of physical page addresses */
    nr_pages = div_up(io->tx_region.len, PGSIZE_2MB);
    page_paddrs = malloc(nr_pages * sizeof(physaddr_t));
    ret =
        mem_lookup_page_phys_addrs(io->tx_region.base, io->tx_region.len, PGSIZE_2MB, page_paddrs);
    if (ret)
        return ret;

    proc->max_overflows = EGRESS_POOL_SIZE(proc->nr_ks) / MBUF_DEFAULT_LEN;
    proc->nr_overflows = 0;
    proc->overflow_queue = malloc(sizeof(uint64_t) * proc->max_overflows);

    return ret;
}

static const struct init_entry init_handlers[] = {
    /* dataplane */
    INITIALIZER(dpdk, init),
    INITIALIZER(rx, init),
    INITIALIZER(tx, init),
    INITIALIZER(dpdk_late, init),

    /* network stack */
    INITIALIZER(net, init),
    INITIALIZER(arp, init),
    INITIALIZER(trans, init),
    INITIALIZER(arp, init_late),
};

__noreturn void iothread_main()
{
    assert(current_cpu_id() == IO_CPU);

    BUG_ON(run_init_handlers(init_handlers, sizeof(init_handlers) / sizeof(struct init_entry)));
    atomic_store_rel(&proc->ready, true);
    log_info("io: initialized on CPU %d(%d)", current_cpu_id(), hw_cpu_id(current_cpu_id()));

#ifdef SKYLOFT_STAT
    uint64_t next_log_time = now_us();
#endif

    for (;;) {
        rx_burst();

        cmd_rx_burst();

        tx_drain_completions(proc, IO_OVERFLOW_BATCH_DRAIN);

        tx_burst();

#ifdef SKYLOFT_STAT
        if (now_us() > next_log_time) {
            print_stats();
            dpdk_print_eth_stats();
            next_log_time += LOG_INTERVAL_US;
        }
#endif
    }
}
