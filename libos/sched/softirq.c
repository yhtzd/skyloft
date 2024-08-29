/*
 * softirq.c - handles high priority events (timers, ingress packets, etc.)
 */

#include <net/mbuf.h>
#include <skyloft/io.h>
#include <skyloft/params.h>
#include <skyloft/sched.h>
#include <skyloft/sched/ops.h>
#include <skyloft/sync/timer.h>
#include <skyloft/task.h>
#include <skyloft/uapi/task.h>

#include <utils/defs.h>
#include <utils/log.h>
#include <utils/lrpc.h>

struct softirq_work {
    /* packets received  */
    int recv_cnt;
    /* packets completed (sent by I/O thread) */
    int compl_cnt;
    /* budget left for timer */
    int timer_budget;
    struct kthread *k;
    struct rx_net_hdr *recv_reqs[SOFTIRQ_MAX_BUDGET];
    struct mbuf *compl_reqs[SOFTIRQ_MAX_BUDGET];
};

static void softirq_gather_work(struct softirq_work *w, struct kthread *k, int budget)
{
    int recv_cnt = 0;
    int compl_cnt = 0;

    budget = MIN(budget, SOFTIRQ_MAX_BUDGET);
#ifdef SKYLOFT_DPDK
    while (budget-- > 0) {
        uint64_t cmd;
        uint64_t payload;

        if (!lrpc_recv(&k->rxq, &cmd, &payload))
            break;

        switch (cmd) {
        case RX_NET_RECV:
            w->recv_reqs[recv_cnt] = (struct rx_net_hdr *)payload;
            recv_cnt++;
            break;
        case RX_NET_COMPLETE:
            w->compl_reqs[compl_cnt++] = (struct mbuf *)payload;
            break;
        default:
            log_err("net: invalid RXQ cmd '%ld'", cmd);
        }
    }
#endif

    w->k = k;
    w->recv_cnt = recv_cnt;
    w->compl_cnt = compl_cnt;
    w->timer_budget = budget;
}

static inline bool softirq_pending(struct kthread *k)
{
#ifdef SKYLOFT_DPDK
    return !lrpc_empty(&k->rxq) || timer_needed(k);
#else
    return timer_needed(k);
#endif
}

static void softirq_fn(void *arg)
{
    struct softirq_work *w = arg;
    uint64_t elapsed;
    STAT_CYCLES_BEGIN(elapsed);

#ifdef SKYLOFT_DPDK
    int i;
    /* complete TX requests and free packets */
    for (i = 0; i < w->compl_cnt; i++) mbuf_free(w->compl_reqs[i]);

    /* deliver new RX packets to the runtime */
    net_rx_softirq(w->recv_reqs, w->recv_cnt);
#endif

    /* handle any pending timeouts */
    if (timer_needed(w->k))
        timer_softirq(w->k, w->timer_budget);

    ADD_STAT_CYCLES(SOFTIRQ_CYCLES, elapsed);
}

/**
 * softirq_task - creates a closure for softirq handling
 * @k: the kthread from which to take RX queue commands
 * @budget: the maximum number of events to process
 *
 * Returns a task that handles receive processing when executed or
 * NULL if no receive processing work is available.
 */
struct task *softirq_task(struct kthread *k, int budget)
{
    struct task *t;
    struct softirq_work *w;

    /* check if there's any work available */
    if (!softirq_pending(k))
        return NULL;

    t = task_create_with_buf(softirq_fn, (void **)&w, sizeof(struct softirq_work));
    if (unlikely(!t))
        return NULL;

    softirq_gather_work(w, k, budget);

    return t;
}

/**
 * softirq_run - handles softirq processing in the current task
 * @budget: the maximum number of events to process
 */
bool softirq_run(int budget)
{
    struct kthread *k;
    struct softirq_work w;

    k = getk();
    /* check if there's any work available */
    if (!softirq_pending(k)) {
        putk();
        return false;
    }

#ifdef SKYLOFT_DPDK
    __sched_percpu_lock(k->cpu);
    softirq_gather_work(&w, k, budget);
    __sched_percpu_unlock(k->cpu);
    softirq_fn(&w);
#else
    w.k = k;
    w.timer_budget = MIN(budget, SOFTIRQ_MAX_BUDGET);
    softirq_fn(&w);
#endif

    putk();
    return true;
}
