/*
 * commands.c - dataplane commands to/from runtimes
 */

#include <rte_mbuf.h>

#include <skyloft/io.h>
#include <skyloft/sched.h>
#include <utils/log.h>
#include <utils/lrpc.h>

static int cmd_drain_queue(struct kthread *t, struct rte_mbuf **bufs, int n)
{
    int i, n_bufs = 0;

    for (i = 0; i < n; i++) {
        uint64_t cmd;
        unsigned long payload;

        if (!lrpc_recv(&t->txcmdq, &cmd, &payload))
            break;

        switch (cmd) {
        case TXCMD_NET_COMPLETE:
            bufs[n_bufs++] = (struct rte_mbuf *)payload;
            break;

        default:
            /* kill the runtime? */
            BUG();
        }
    }

    return n_bufs;
}

/*
 * Process a batch of commands from runtimes.
 */
bool cmd_rx_burst(void)
{
    struct rte_mbuf *bufs[IO_CMD_BURST_SIZE];
    int i, n_bufs = 0, idx;
    static unsigned int pos = 0;

    /*
     * Poll each thread in each runtime until all have been polled or we
     * have processed CMD_BURST_SIZE commands.
     */
    for (i = 0; i < proc->nr_ks; i++) {
        idx = (pos + i) % proc->nr_ks;

        if (n_bufs >= IO_CMD_BURST_SIZE)
            break;
        n_bufs += cmd_drain_queue(&proc->all_ks[idx], &bufs[n_bufs], IO_CMD_BURST_SIZE - n_bufs);
    }

    pos++;
    for (i = 0; i < n_bufs; i++) rte_pktmbuf_free(bufs[i]);
    return n_bufs > 0;
}
