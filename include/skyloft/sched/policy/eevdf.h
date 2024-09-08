#pragma once

#include <skyloft/percpu.h>
#include <skyloft/task.h>

#include <utils/defs.h>
#include <utils/rbtree.h>
#include <utils/spinlock.h>
#include <utils/time.h>

#include "dummy.h"
#include "utils/log.h"

/* EEVDF load weight: weight * inv_weight = 2^32 */
struct load_weight {
    uint64_t weight;
    uint32_t inv_weight;
};

/* EEVDF percpu state */
struct eevdf_rq {
    spinlock_t lock;
    uint8_t pad0[60];
    /* sum of load of all tasks */
    struct load_weight load;
    /* rbtree root with leftmost node cached */
    struct rb_root_cached tasks_timeline;
    uint32_t nr_running;
    int64_t avg_vruntime;
    uint64_t avg_load;
    uint64_t min_vruntime;
    struct eevdf_task *curr;
} __aligned_cacheline;

struct eevdf_task {
    int last_run;
    struct load_weight load;
    /* rbtree link */
    struct rb_node run_node;
    uint64_t deadline;
    uint64_t min_vruntime;
    /* task states */
    bool on_rq;
    /* task scheduled time */
    __nsec exec_start;
    __nsec sum_exec_runtime;
    __nsec prev_sum_exec_runtime;
    uint64_t vruntime;
    int64_t vlag;
    uint64_t slice;
} __aligned_cacheline;

#define WEIGHT_IDLEPRIO 3
#define WMULT_IDLEPRIO  1431655765
#define WMULT_CONST     (~0U)
#define WMULT_SHIFT     32
#define NICE_0_SHIFT    10
#define NICE_0_LOAD     (1L << NICE_0_SHIFT)

#define SCHED_FIXEDPOINT_SHIFT 10
#define NICE_0_LOAD_SHIFT      (SCHED_FIXEDPOINT_SHIFT + SCHED_FIXEDPOINT_SHIFT)

DECLARE_PERCPU(struct eevdf_rq *, percpu_rqs);

#define this_rq()           percpu_get(percpu_rqs)
#define cpu_rq(cpu)         percpu_get_remote(percpu_rqs, cpu)
#define eevdf_task_of(task) ((struct eevdf_task *)task->policy_task_data)
#define task_of(task)       (container_of((void *)task, struct task, policy_task_data))

#define eevdf_sched_init       dummy_sched_init
#define eevdf_sched_set_params dummy_sched_set_params
#define eevdf_sched_poll       dummy_sched_poll
#define eevdf_sched_dump_tasks dummy_sched_dump_tasks
#define eevdf_sched_balance    dummy_sched_balance

static inline void eevdf_sched_percpu_lock(int cpu)
{
    // log_debug("%s: %d", __func__, cpu);
    spin_lock(&cpu_rq(cpu)->lock);
}

static inline void eevdf_sched_percpu_unlock(int cpu)
{
    spin_unlock(&cpu_rq(cpu)->lock);
    // log_debug("%s: %d", __func__, cpu);
}

static inline int eevdf_sched_init_percpu(void *percpu_data)
{
    struct eevdf_rq *eevdf_rq = percpu_data;
    percpu_get(percpu_rqs) = eevdf_rq;
    spin_lock_init(&eevdf_rq->lock);
    eevdf_rq->curr = NULL;
    eevdf_rq->tasks_timeline = RB_ROOT_CACHED;
    eevdf_rq->min_vruntime = (uint64_t)(-(1LL << 20));
    eevdf_rq->nr_running = 0;
    eevdf_rq->load.weight = eevdf_rq->load.inv_weight = 0;
    return 0;
}

struct task *eevdf_sched_pick_next();
int eevdf_sched_spawn(struct task *, int);
void eevdf_sched_yield();
void eevdf_sched_wakeup(struct task *);
void eevdf_sched_block();
bool eevdf_sched_preempt();
int eevdf_sched_init_task(struct task *);
void eevdf_sched_finish_task(struct task *);
