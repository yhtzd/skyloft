/*
 * rcu.c - support for read-copy-update
 *
 * The main challenge of RCU is determining when it's safe to free objects. The
 * strategy here is to maintain a per-kthread counter. Whenever the scheduler is
 * entered or exited, the counter is incremented. When the count is even, we
 * know that either the scheduler loop is still running or the kthread is
 * parked. When the count is odd, we know a uthread is currently running. We can
 * safely free objects by reading each kthread's counter and then waiting until
 * each kthread count is either even & >= the previous value (to detect parking)
 * or odd & > the previous value (to detect rescheduling).
 *
 * FIXME: Freeing objects is expensive with this minimal implementation. This
 * should be fine as long as RCU updates are rare. The Linux Kernel uses several
 * more optimized strategies that we may want to consider in the future.
 */

#include <skyloft/params.h>
#include <skyloft/sync/rcu.h>
#include <skyloft/sync/sync.h>
#include <skyloft/sync/timer.h>
#include <skyloft/task.h>
#include <skyloft/uapi/task.h>
#include <utils/defs.h>
#include <utils/log.h>
#include <utils/spinlock.h>

/* the time RCU waits before checking if it can free objects */
#define RCU_SLEEP_PERIOD (10 * USEC_PER_MSEC)

/* Protects @rcu_head. */
static DEFINE_SPINLOCK(rcu_lock);
/* The head of the list of objects waiting to be freed */
static struct rcu_head *rcu_head;
static bool rcu_worker_launched;
uint32_t *rcu_gen_percpu[USED_CPUS];

#ifdef DEBUG
__thread int rcu_read_count;
#endif /* DEBUG */

static void rcu_worker(void *arg)
{
    struct rcu_head *head, *next;
    unsigned int last_rcu_gen[USED_CPUS];
    unsigned int gen;
    int i;

    log_info("rcu: rcu worker %p", arg);

    while (true) {
        /* check if any RCU objects are waiting to be freed */
        spin_lock_np(&rcu_lock);
        if (!rcu_head) {
            spin_unlock_np(&rcu_lock);
            timer_sleep(RCU_SLEEP_PERIOD);
            continue;
        }
        head = rcu_head;
        rcu_head = NULL;
        spin_unlock_np(&rcu_lock);

        /* read the RCU generation counters */
        for (i = 0; i < USED_CPUS; i++) last_rcu_gen[i] = atomic_load_acq(rcu_gen_percpu[i]);

        while (true) {
            /* wait for RCU generation counters to increase */
            timer_sleep(RCU_SLEEP_PERIOD);

            /* read the RCU generation counters again */
            for (i = 0; i < USED_CPUS; i++) {
                gen = atomic_load_acq(rcu_gen_percpu[i]);
                /* wait for a quiescent period (all passes context switch) */
                if ((gen & 0x1) == 0x1 && gen == last_rcu_gen[i]) {
                    break;
                }
            }

            /* did any of the RCU generation checks fail? */
            if (i != USED_CPUS)
                continue;

            /* actually free the RCU objects */
            while (head) {
                next = head->next;
                head->func(head);
                head = next;
            }

            break;
        }
    }
}

/**
 * rcu_free - frees an RCU object after the quiescent period
 * @head: the RCU head structure embedded within the object
 * @func: the release method
 */
void rcu_free(struct rcu_head *head, rcu_callback_t func)
{
    bool launch_worker = false;

    head->func = func;

    spin_lock_np(&rcu_lock);
    if (unlikely(!rcu_worker_launched))
        launch_worker = rcu_worker_launched = true;
    head->next = rcu_head;
    rcu_head = head;
    spin_unlock_np(&rcu_lock);

    if (unlikely(launch_worker))
        BUG_ON(sl_task_spawn(rcu_worker, head, 0));
}

struct sync_arg {
    struct rcu_head rcu;
    struct task *task;
};

static void synchronize_rcu_finish(struct rcu_head *head)
{
    struct sync_arg *tmp = container_of(head, struct sync_arg, rcu);
    task_wakeup(tmp->task);
}

/**
 * synchronize_rcu - blocks until it is safe to free an RCU object
 *
 * WARNING: Can only be called from thread context.
 */
void synchronize_rcu(void)
{
    bool launch_worker = false;
    struct sync_arg tmp;
    struct task *task = task_self();

    tmp.rcu.func = synchronize_rcu_finish;
    tmp.task = task;

    spin_lock_np(&rcu_lock);
    if (unlikely(!rcu_worker_launched))
        launch_worker = rcu_worker_launched = true;
    tmp.rcu.next = rcu_head;
    rcu_head = &tmp.rcu;
    if (unlikely(launch_worker))
        BUG_ON(sl_task_spawn(rcu_worker, NULL, 0));
    task_block(&rcu_lock);
}
