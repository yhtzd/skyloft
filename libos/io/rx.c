/*
 * rx.c - the receive path for the I/O kernel (network -> runtimes)
 */

#include <rte_common.h>
#include <rte_dev.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_mbuf.h>

#include <skyloft/global.h>
#include <skyloft/io.h>
#include <skyloft/params.h>
#include <skyloft/platform.h>
#include <skyloft/sched.h>
#include <utils/log.h>
#include <utils/shm.h>

#define MBUF_CACHE_SIZE    250
#define RX_PREFETCH_STRIDE 2

/*
 * Prepend rx_net_hdr preamble to ingress packets.
 */
static struct rx_net_hdr *rx_prepend_rx_preamble(struct rte_mbuf *buf)
{
    struct rx_net_hdr *net_hdr;
    uint64_t masked_ol_flags;

    net_hdr = (struct rx_net_hdr *)rte_pktmbuf_prepend(buf, (uint16_t)sizeof(*net_hdr));
    RTE_ASSERT(net_hdr != NULL);

    net_hdr->completion_data = (unsigned long)buf;
    net_hdr->len = rte_pktmbuf_pkt_len(buf) - sizeof(*net_hdr);
    net_hdr->rss_hash = buf->hash.rss;
    masked_ol_flags = buf->ol_flags & RTE_MBUF_F_RX_IP_CKSUM_MASK;
    if (masked_ol_flags == RTE_MBUF_F_RX_IP_CKSUM_GOOD)
        net_hdr->csum_type = CHECKSUM_TYPE_UNNECESSARY;
    else
        net_hdr->csum_type = CHECKSUM_TYPE_NEEDED;
    net_hdr->csum = 0; /* unused for now */

    return net_hdr;
}

/**
 * rx_send_to_runtime - enqueues a command to an RXQ for a runtime
 * @p: the runtime's proc structure
 * @hash: the 5-tuple hash for the flow the command is related to
 * @cmd: the command to send
 * @payload: the command payload to send
 *
 * Returns true if the command was enqueued, otherwise a thread is not running
 * and can't be woken or the queue was full.
 */
bool rx_send_to_runtime(struct proc *p, uint32_t hash, uint64_t cmd, uint64_t payload)
{
    struct kthread *t;

    if (p->nr_ks > 0) {
        /* load balance between active threads */
        t = &p->all_ks[hash % p->nr_ks];
    } else {
        BUG();
    }

    return lrpc_send(&t->rxq, cmd, payload);
}

static bool rx_send_pkt_to_runtime(struct proc *p, struct rx_net_hdr *hdr)
{
    return rx_send_to_runtime(p, hdr->rss_hash, RX_NET_RECV, (uint64_t)hdr);
}

static void rx_one_pkt(struct rte_mbuf *buf)
{
    struct rte_ether_hdr *ptr_mac_hdr;
    struct rte_ether_addr *ptr_dst_addr;
    struct rx_net_hdr *net_hdr;
    int n = 0;

    ptr_mac_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);
    ptr_dst_addr = &ptr_mac_hdr->dst_addr;
    log_debug("rx: rx packet with MAC %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8
              " %02" PRIx8,
              ptr_dst_addr->addr_bytes[0], ptr_dst_addr->addr_bytes[1], ptr_dst_addr->addr_bytes[2],
              ptr_dst_addr->addr_bytes[3], ptr_dst_addr->addr_bytes[4],
              ptr_dst_addr->addr_bytes[5]);

    /* handle unicast destinations (send to a single runtime) */
    if (likely(rte_is_unicast_ether_addr(ptr_dst_addr))) {
        net_hdr = rx_prepend_rx_preamble(buf);

        if (!rx_send_pkt_to_runtime(proc, net_hdr)) {
            log_debug("rx: failed to send unicast packet to runtime");
            rte_pktmbuf_free(buf);
        }
        return;
    }

    /* handle broadcast destinations (send to all runtimes) */
    if (rte_is_broadcast_ether_addr(ptr_dst_addr)) {
        net_hdr = rx_prepend_rx_preamble(buf);

        if (rx_send_pkt_to_runtime(proc, net_hdr)) {
            n++;
        } else {
            log_debug("rx: failed to enqueue broadcast packet to runtime");
        }

        if (!n) {
            rte_pktmbuf_free(buf);
            return;
        }
        rte_mbuf_refcnt_update(buf, n - 1);
        return;
    }

    rte_pktmbuf_free(buf);
}

/*
 * Process a batch of incoming packets.
 */
bool rx_burst(void)
{
    struct rte_mbuf *bufs[IO_RX_BURST_SIZE];
    uint16_t nb_rx, i;

    /* retrieve packets from NIC queue */
    nb_rx = rte_eth_rx_burst(io->port_id, 0, bufs, IO_RX_BURST_SIZE);
    if (nb_rx > 0)
        log_debug("rx: received %d packet(s) on port %d", nb_rx, io->port_id);

    for (i = 0; i < nb_rx; i++) {
        if (i + RX_PREFETCH_STRIDE < nb_rx) {
            prefetch(rte_pktmbuf_mtod(bufs[i + RX_PREFETCH_STRIDE], char *));
        }
        rx_one_pkt(bufs[i]);
    }

    return nb_rx > 0;
}

/*
 * Initialize rx state.
 */
int rx_init()
{
    /* create a mempool in shared memory to hold the rx mbufs */
    io->rx_mbuf_pool = rte_pktmbuf_pool_create("RX_MBUF_POOL", IO_NUM_MBUFS, MBUF_CACHE_SIZE, 0,
                                               RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (io->rx_mbuf_pool == NULL) {
        return -ENOMEM;
    }

    return 0;
}
