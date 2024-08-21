#pragma once

#include <skyloft/percpu.h>
#include <skyloft/task.h>

#include <utils/defs.h>
#include <utils/rbtree.h>
#include <utils/spinlock.h>
#include <utils/time.h>

#include "dummy.h"

/* CFS load weight: weight * inv_weight = 2^32 */
struct load_weight {
    uint64_t weight;
    uint32_t inv_weight;
};

/* CFS percpu state */
struct cfs_rq {
    spinlock_t lock;
    /* sum of load of all tasks */
    struct load_weight load;
    uint32_t nr_running;
    /* rbtree root with leftmost node cached */
    struct rb_root_cached tasks_timeline;
    uint64_t min_vruntime;
    struct cfs_task *curr;
} __aligned_cacheline;

struct cfs_task {
    int last_run;
    struct load_weight load;
    /* rbtree link */
    struct rb_node run_node;
    /* task states */
    bool on_rq;
    /* task scheduled time */
    __nsec exec_start;
    __nsec sum_exec_runtime;
    __nsec prev_sum_exec_runtime;
    uint64_t vruntime;
} __aligned_cacheline;

#define WEIGHT_IDLEPRIO 3
#define WMULT_IDLEPRIO  1431655765
#define WMULT_CONST     (~0U)
#define WMULT_SHIFT     32
#define NICE_0_SHIFT    10
#define NICE_0_LOAD     (1L << NICE_0_SHIFT)

DECLARE_PERCPU(struct cfs_rq *, percpu_rqs);

#define this_rq()         percpu_get(percpu_rqs)
#define cpu_rq(cpu)       percpu_get_remote(percpu_rqs, cpu)
#define cfs_task_of(task) ((struct cfs_task *)task->policy_task_data)
#define task_of(task)     (container_of((void *)task, struct task, policy_task_data))

#define cfs_sched_init       dummy_sched_init
#define cfs_sched_set_params dummy_sched_set_params
#define cfs_sched_poll       dummy_sched_poll
#define cfs_sched_dump_tasks dummy_sched_dump_tasks
#define cfs_sched_balance    dummy_sched_balance

static inline void cfs_sched_percpu_lock(int cpu) { spin_lock(&cpu_rq(cpu)->lock); }

static inline void cfs_sched_percpu_unlock(int cpu) { spin_unlock(&cpu_rq(cpu)->lock); }

static inline int cfs_sched_init_percpu(void *percpu_data)
{
    struct cfs_rq *cfs_rq = percpu_data;
    percpu_get(percpu_rqs) = cfs_rq;
    cfs_rq->curr = NULL;
    cfs_rq->tasks_timeline = RB_ROOT_CACHED;
    cfs_rq->min_vruntime = (uint64_t)(-(1LL << 20));
    cfs_rq->nr_running = 0;
    cfs_rq->load.weight = cfs_rq->load.inv_weight = 0;
    return 0;
}

struct task *cfs_sched_pick_next();
int cfs_sched_spawn(struct task *, int);
void cfs_sched_yield();
void cfs_sched_wakeup(struct task *);
bool cfs_sched_preempt();
int cfs_sched_init_task(struct task *);
void cfs_sched_finish_task(struct task *);
