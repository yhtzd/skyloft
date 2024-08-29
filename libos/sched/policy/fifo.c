/*
 * fifo.c: a fifo scheduler implemented with fixed runqueue and load balance
 */

#include <skyloft/sched.h>
#include <skyloft/sched/policy/fifo.h>
#include <skyloft/sync.h>
#include <skyloft/task.h>

#include <utils/assert.h>
#include <utils/hash.h>
#include <utils/log.h>

__thread struct fifo_rq *this_rq;
struct fifo_rq *rqs[USED_CPUS];

int fifo_sched_init_percpu(void *percpu_data)
{
    struct fifo_rq *rq = (struct fifo_rq *)percpu_data;

    // TODO: multiple apps
    if (current_app_id() == 0) {
        rqs[current_cpu_id()] = this_rq = rq;
        spin_lock_init(&rq->lock);
        rq->k = thisk();
        rq->head = rq->tail = 0;
        list_head_init(&rq->overflow);
    }

    return 0;
}

static void put_task(struct fifo_rq *rq, struct task *task)
{
    uint32_t rq_head;
    int flags;

    assert(task != NULL);

    local_irq_save(flags);
    rq_head = atomic_load_acq(&rq->head);
    if (unlikely(rq->tail - rq_head >= RUNTIME_RQ_SIZE)) {
        spin_lock(&rq->lock);
        list_add_tail(&rq->overflow, &task->link);
        spin_unlock(&rq->lock);
        local_irq_restore(flags);
        return;
    }
    RQ_TAIL(rq) = task;
    atomic_store_rel(&rq->tail, rq->tail + 1);
    local_irq_restore(flags);
}

int fifo_sched_spawn(struct task *task, int cpu)
{
    if (task == NULL || cpu < 0 || cpu > USED_CPUS)
        return -1;
    put_task(this_rq(), task);
    return 0;
}

void fifo_sched_yield()
{
    put_task(this_rq(), task_self());
}

void fifo_sched_wakeup(struct task *task)
{
    put_task(this_rq(), task);
}

static bool steal_task(struct fifo_rq *l, struct fifo_rq *r)
{
    struct task *task = NULL;
    uint32_t avail, rq_head, i;

    if (!spin_try_lock(&r->lock))
        return false;

    /* try to steal directly from the runqueue */
    avail = atomic_load_acq(&r->tail) - r->head;
    if (avail) {
        /* steal half the tasks */
        avail = div_up(avail, 2);
        rq_head = r->head;
        for (i = 0; i < avail; i++) {
            l->tasks[i] = r->tasks[rq_head++ & RQ_SIZE_MASK];
        }
        atomic_store_rel(&r->head, rq_head);
        spin_unlock(&r->lock);
        l->tail = avail;
        ADD_STAT(TASKS_STOLEN, avail);
        return true;
    }

    /* check for overflow tasks */
    task = list_pop(&r->overflow, struct task, link);
    if (task)
        goto done;

    /* check for softirqs */
    task = softirq_task(r->k, SOFTIRQ_MAX_BUDGET);
    if (task)
        goto done;

done:
    if (task) {
        l->tasks[l->tail++] = task;
        ADD_STAT(TASKS_STOLEN, 1);
    }
    spin_unlock(&r->lock);
    return task != NULL;
}

#if defined(SKYLOFT_DPDK) && defined(UTIMER)
#define WORKER_CPUS (USED_CPUS - 2)
#elif !defined(SKYLOFT_DPDK) && !defined(UTIMER)
#define WORKER_CPUS (USED_CPUS)
#else
#define WORKER_CPUS (USED_CPUS - 1)
#endif

void fifo_sched_balance()
{
    int cpu = current_cpu_id();
    struct fifo_rq *l = cpu_rq(cpu);
    int i, sibling, idx;
    uint32_t start_idx;

    assert_spin_lock_held(&l->lock);
    assert(RQ_IS_EMPTY(l));

    l->head = l->tail = 0;

    // sibling = cpu_sibling(cpu);
    // if (sibling >= 0 && steal_task(l, cpu_rq(sibling)))
    //     return;

    /* try to steal from every kthread */
    start_idx = rand_crc32c((uintptr_t)l);
    for (i = 0; i < WORKER_CPUS; i++) {
        idx = (start_idx + i) % WORKER_CPUS;
        if (idx != cpu && steal_task(l, cpu_rq(idx)))
            return;
    }
}
