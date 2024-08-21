#pragma once

#include <skyloft/params.h>
#include <skyloft/percpu.h>
#include <skyloft/task.h>
#include <utils/list.h>

#include "dummy.h"

struct fifo_rq {
    uint32_t head;
    atomic_int tail;
    struct kthread *k;
    atomic_int num_tasks;
    uint8_t pad0[44];
    /* cache line 2~5 */
    struct task *tasks[RUNTIME_RQ_SIZE];
} __aligned_cacheline;

struct fifo_task {
    int last_run;
    int quan;
};

#define fifo_task_of(task) ((struct fifo_task *)(task->policy_task_data))
DECLARE_PERCPU(struct fifo_rq *, rqs);
#define this_rq()   percpu_get(rqs)
#define cpu_rq(cpu) percpu_get_remote(rqs, cpu)

#define RQ_SIZE_MASK (RUNTIME_RQ_SIZE - 1)

struct task *fifo_sched_pick_next();
int fifo_sched_init_percpu(void *percpu_data);
int fifo_sched_spawn(struct task *task, int cpu);
void fifo_sched_yield();
void fifo_sched_wakeup(struct task *task);
bool fifo_sched_preempt();

static inline int fifo_sched_init_task(struct task *task)
{
    task->allow_preempt = true;
    return 0;
}

#define fifo_sched_init          dummy_sched_init
#define fifo_sched_finish_task   dummy_sched_finish_task
#define fifo_sched_block         dummy_sched_block
#define fifo_sched_set_params    dummy_sched_set_params
#define fifo_sched_poll          dummy_sched_poll
#define fifo_sched_dump_tasks    dummy_sched_dump_tasks
#define fifo_sched_percpu_lock   dummy_sched_percpu_lock
#define fifo_sched_percpu_unlock dummy_sched_percpu_unlock
#define fifo_sched_balance       dummy_sched_balance
