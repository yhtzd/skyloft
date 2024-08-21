#pragma once

#include <skyloft/params.h>
#include <skyloft/task.h>
#include <utils/list.h>

#include "dummy.h"

struct fifo_rq {
    struct kthread *k;
    uint32_t head, tail;
    struct list_head overflow;
    uint8_t pad0[32];
    spinlock_t lock;
    uint8_t pad1[60];
    /* cache line 2~5 */
    struct task *tasks[RUNTIME_RQ_SIZE];
} __aligned_cacheline;

extern __thread struct fifo_rq *this_rq;
extern struct fifo_rq *rqs[USED_CPUS];

#define this_rq()   (this_rq)
#define cpu_rq(cpu) (rqs[cpu])

#define RQ_SIZE_MASK    (RUNTIME_RQ_SIZE - 1)
#define RQ_HEAD(rq)     ((rq)->tasks[(rq)->head & RQ_SIZE_MASK])
#define RQ_TAIL(rq)     ((rq)->tasks[(rq)->tail & RQ_SIZE_MASK])
#define RQ_IS_EMPTY(rq) ((rq)->head == (rq)->tail)

static inline struct task *fifo_sched_pick_next()
{
    struct task *task;
    struct fifo_rq *rq = this_rq();

    if (RQ_IS_EMPTY(rq))
        return NULL;

    task = RQ_HEAD(rq);
    rq->head++;

    return task;
}

static inline void fifo_sched_percpu_lock(int cpu) { spin_lock(&cpu_rq(cpu)->lock); }

static inline void fifo_sched_percpu_unlock(int cpu) { spin_unlock(&cpu_rq(cpu)->lock); }

static __always_inline __attribute__((target("general-regs-only"))) bool fifo_sched_preempt()
{
    return true;
}

#define fifo_sched_init        dummy_sched_init
#define fifo_sched_init_task   dummy_sched_init_task
#define fifo_sched_finish_task dummy_sched_finish_task
#define fifo_sched_block       dummy_sched_block
#define fifo_sched_set_params  dummy_sched_set_params
#define fifo_sched_poll        dummy_sched_poll
#define fifo_sched_dump_tasks  dummy_sched_dump_tasks

int fifo_sched_init_percpu(void *percpu_data);
int fifo_sched_spawn(struct task *task, int cpu);
void fifo_sched_yield();
void fifo_sched_wakeup(struct task *task);
void fifo_sched_balance();
