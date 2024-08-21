/*
 * tx.c - the transmission path for the I/O kernel (runtimes -> network)
 */

#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_mempool.h>

#include <skyloft/io.h>
#include <skyloft/sched.h>
#include <utils/log.h>

#define TX_PREFETCH_STRIDE 2

/*
 * Private data stored in egress mbufs, used to send completions to runtimes.
 */
struct tx_pktmbuf_priv {
    struct proc *p;
    struct kthread *t;
    unsigned long completion_data;
};

static inline struct tx_pktmbuf_priv *tx_pktmbuf_get_priv(struct rte_mbuf *buf)
{
    return (struct tx_pktmbuf_priv *)(((char *)buf) + sizeof(struct rte_mbuf));
}

/*
 * Prepare rte_mbuf struct for transmission.
 */
static void tx_prepare_tx_mbuf(struct rte_mbuf *buf, const struct tx_net_hdr *net_hdr,
                               struct kthread *t)
{
    struct tx_pktmbuf_priv *priv_data;
    uint32_t page_number;

    /* initialize mbuf to point to net_hdr->payload */
    buf->buf_addr = (char *)net_hdr->payload;
    // rte_mbuf_iova_set(buf, (rte_iova_t)buf->buf_addr);
    page_number = PGN_2MB((uintptr_t)buf->buf_addr - (uintptr_t)io->tx_region.base);
    rte_mbuf_iova_set(buf, page_paddrs[page_number] + PGOFF_2MB(buf->buf_addr));
    buf->data_off = 0;
    rte_mbuf_refcnt_set(buf, 1);

    buf->buf_len = net_hdr->len;
    buf->pkt_len = net_hdr->len;
    buf->data_len = net_hdr->len;

    buf->ol_flags = 0;
    if (net_hdr->olflags != 0) {
        if (net_hdr->olflags & OLFLAG_IP_CHKSUM)
            buf->ol_flags |= RTE_MBUF_F_TX_IP_CKSUM;
        if (net_hdr->olflags & OLFLAG_TCP_CHKSUM)
            buf->ol_flags |= RTE_MBUF_F_TX_TCP_CKSUM;
        if (net_hdr->olflags & OLFLAG_IPV4)
            buf->ol_flags |= RTE_MBUF_F_TX_IPV4;
        if (net_hdr->olflags & OLFLAG_IPV6)
            buf->ol_flags |= RTE_MBUF_F_TX_IPV6;

        buf->l4_len = sizeof(struct rte_tcp_hdr);
        buf->l3_len = sizeof(struct rte_ipv4_hdr);
        buf->l2_len = RTE_ETHER_HDR_LEN;
    }

    /* initialize the private data, used to send completion events */
    priv_data = tx_pktmbuf_get_priv(buf);
    priv_data->p = proc;
    priv_data->t = t;
    priv_data->completion_data = net_hdr->completion_data;
}

/*
 * Send a completion event to the runtime for the mbuf pointed to by obj.
 */
bool tx_send_completion(void *obj)
{
    struct rte_mbuf *buf;
    struct tx_pktmbuf_priv *priv_data;
    struct kthread *t;
    struct proc *p;

    buf = (struct rte_mbuf *)obj;
    priv_data = tx_pktmbuf_get_priv(buf);
    p = priv_data->p;

    /* during initialization, the mbufs are enqueued for the first time */
    if (unlikely(!p))
        return true;

    /* check if runtime is still registered */
    if (unlikely(p->exited))
        return true; /* no need to send a completion */

    /* send completion to runtime */
    t = priv_data->t;
    if (!t->parked) {
        if (likely(lrpc_send(&t->rxq, RX_NET_COMPLETE, priv_data->completion_data)))
            return true;
    } else {
        if (likely(
                rx_send_to_runtime(p, p->next_rr++, RX_NET_COMPLETE, priv_data->completion_data)))
            return true;
    }

    if (unlikely(p->nr_overflows == p->max_overflows)) {
        log_warn("tx: Completion overflow queue is full");
        return false;
    }
    p->overflow_queue[p->nr_overflows++] = priv_data->completion_data;
    log_debug("tx: failed to send completion to runtime");

    return true;
}

int tx_drain_completions(struct proc *p, int n)
{
    int i = 0;
    while (p->nr_overflows > 0 && i < n) {
        if (!rx_send_to_runtime(p, p->next_rr++, RX_NET_COMPLETE,
                                p->overflow_queue[--p->nr_overflows])) {
            p->nr_overflows++;
            break;
        }
        i++;
    }
    return i;
}

static int tx_drain_queue(struct kthread *t, int n, const struct tx_net_hdr **hdrs)
{
    int i;

    for (i = 0; i < n; i++) {
        uint64_t cmd;
        unsigned long payload;

        if (!lrpc_recv(&t->txpktq, &cmd, &payload))
            break;

        /* TODO: need to kill the process? */
        BUG_ON(cmd != TXPKT_NET_XMIT);

        // hdrs[i] = shmptr_to_ptr(&io->tx_region, payload, sizeof(struct tx_net_hdr));
        hdrs[i] = (struct tx_net_hdr *)payload;
        /* TODO: need to kill the process? */
        BUG_ON(!hdrs[i]);
    }

    return i;
}

/*
 * Process a batch of outgoing packets.
 */
bool tx_burst(void)
{
    const struct tx_net_hdr *hdrs[IO_TX_BURST_SIZE];
    static struct rte_mbuf *bufs[IO_TX_BURST_SIZE];
    struct kthread *threads[IO_TX_BURST_SIZE];
    unsigned int i, j, ret;
    static unsigned int pos = 0, n_pkts = 0, n_bufs = 0;
    struct kthread *t;

    /*
     * Poll each kthread in each runtime until all have been polled or we
     * have PKT_BURST_SIZE pkts.
     */
    for (i = 0; i < proc->nr_ks; i++) {
        t = &proc->all_ks[(pos + i) % proc->nr_ks];
        // if (t->parked)
        //     continue;
        if (n_pkts >= IO_TX_BURST_SIZE)
            goto full;
        ret = tx_drain_queue(t, IO_TX_BURST_SIZE - n_pkts, &hdrs[n_pkts]);
        for (j = n_pkts; j < n_pkts + ret; j++) threads[j] = t;
        n_pkts += ret;
    }

    if (n_pkts == 0)
        return false;

    pos++;

full:

    /* allocate mbufs */
    if (n_pkts - n_bufs > 0) {
        ret = rte_mempool_get_bulk(io->tx_mbuf_pool, (void **)&bufs[n_bufs], n_pkts - n_bufs);
        if (unlikely(ret)) {
            log_warn("tx: error getting %d mbufs from mempool", n_pkts - n_bufs);
            return true;
        }
    }

    /* fill in packet metadata */
    for (i = n_bufs; i < n_pkts; i++) {
        if (i + TX_PREFETCH_STRIDE < n_pkts)
            prefetch(hdrs[i + TX_PREFETCH_STRIDE]);
        tx_prepare_tx_mbuf(bufs[i], hdrs[i], threads[i]);
    }

    n_bufs = n_pkts;

    /* finally, send the packets on the wire */
    ret = rte_eth_tx_burst(io->port_id, 0, bufs, n_pkts);
    log_debug("tx: transmitted %d packet(s) on port %d", ret, io->port_id);

    /* apply back pressure if the NIC TX ring was full */
    if (unlikely(ret < n_pkts)) {
        n_pkts -= ret;
        for (i = 0; i < n_pkts; i++) bufs[i] = bufs[ret + i];
    } else {
        n_pkts = 0;
    }

    n_bufs = n_pkts;
    return true;
}

/*
 * Zero out private data for a packet
 */

static void tx_pktmbuf_priv_init(struct rte_mempool *mp, void *opaque, void *obj, unsigned obj_idx)
{
    struct rte_mbuf *buf = obj;
    struct tx_pktmbuf_priv *data = tx_pktmbuf_get_priv(buf);
    memset(data, 0, sizeof(*data));
}

/*
 * Create and initialize a packet mbuf pool for holding struct mbufs and
 * handling completion events. Actual buffer memory is separate, in shared
 * memory.
 */
static struct rte_mempool *tx_pktmbuf_completion_pool_create(const char *name, unsigned n,
                                                             uint16_t priv_size, int socket_id)
{
    struct rte_mempool *mp;
    struct rte_pktmbuf_pool_private mbp_priv;
    unsigned elt_size;
    int ret;

    if (RTE_ALIGN(priv_size, RTE_MBUF_PRIV_ALIGN) != priv_size) {
        log_err("tx: mbuf priv_size=%u is not aligned", priv_size);
        rte_errno = EINVAL;
        return NULL;
    }
    elt_size = sizeof(struct rte_mbuf) + (unsigned)priv_size;
    mbp_priv.mbuf_data_room_size = 0;
    mbp_priv.mbuf_priv_size = priv_size;

    mp = rte_mempool_create_empty(name, n, elt_size, 0, sizeof(struct rte_pktmbuf_pool_private),
                                  socket_id, 0);
    if (mp == NULL)
        return NULL;

    ret = rte_mempool_set_ops_byname(mp, "completion", NULL);
    if (ret != 0) {
        log_err("tx: error setting mempool handler");
        rte_mempool_free(mp);
        rte_errno = -ret;
        return NULL;
    }
    rte_pktmbuf_pool_init(mp, &mbp_priv);

    ret = rte_mempool_populate_default(mp);
    if (ret < 0) {
        rte_mempool_free(mp);
        rte_errno = -ret;
        return NULL;
    }

    rte_mempool_obj_iter(mp, rte_pktmbuf_init, NULL);
    rte_mempool_obj_iter(mp, tx_pktmbuf_priv_init, NULL);

    return mp;
}

/*
 * Initialize tx state.
 */
int tx_init()
{
    int ret;

    /* register completion ops */
    extern struct rte_mempool_ops ops_completion;
    if ((ret = rte_mempool_register_ops(&ops_completion)) < 0) {
        log_err("tx: couldn't register completion ops");
        return ret;
    }

    /* create a mempool to hold struct rte_mbufs and handle completions */
    io->tx_mbuf_pool = tx_pktmbuf_completion_pool_create(
        "TX_MBUF_POOL", IO_NUM_COMPLETIONS, sizeof(struct tx_pktmbuf_priv), rte_socket_id());
    if (io->tx_mbuf_pool == NULL) {
        log_err("tx: couldn't create tx mbuf pool");
        return -1;
    }

    return 0;
}
