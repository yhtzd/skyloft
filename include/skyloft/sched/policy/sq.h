/*
 * sq.h: single queue c-FCFS scheduler
 */

#pragma once

#include <skyloft/params.h>
#include <skyloft/percpu.h>
#include <skyloft/platform.h>

#include <utils/defs.h>
#include <utils/queue.h>
#include <utils/time.h>

#include "dummy.h"

#define SQ_UVEC 1

enum sq_worker_state {
    WORKER_IDLE,
    WORKER_QUEUING,
    WORKER_RUNNING,
    WORKER_PREEMPTED,
    WORKER_FINISHED,
};

struct sq_worker {
    enum sq_worker_state state;
    struct task *cur_task;
    __nsec start_time;
    uint8_t pad0[40];
    int uintr_fd;
    int uintr_index;
} __aligned_cacheline;

struct sq_params {
    int num_workers;
    int preemption_quantum;
};

struct sq_dispatcher {
    /* Centric queue */
    queue_t pending_tasks;
    /* Maximum number of workers */
    int num_workers;
    /* If not set, tasks will run to complete */
    int preemption_quantum;
};

DECLARE_PERCPU(struct sq_worker *, workers);

#define this_worker()   percpu_get(workers)
#define cpu_worker(cpu) percpu_get_remote(workers, (cpu))

static inline bool sq_sched_preempt()
{
    struct sq_worker *worker = this_worker();
    if (atomic_load_acq(&worker->state) == WORKER_RUNNING) {
        atomic_store_rel(&worker->state, WORKER_PREEMPTED);
        return true;
    } else {
        return false;
    }
}

static inline struct task *sq_sched_pick_next()
{
    struct sq_worker *worker = this_worker();
    if (atomic_load_acq(&worker->state) == WORKER_QUEUING) {
        worker->start_time = now_ns();
        atomic_store_rel(&worker->state, WORKER_RUNNING);
        return worker->cur_task;
    } else
        return NULL;
}

static inline void sq_sched_finish_task(struct task *task)
{
    atomic_store_rel(&this_worker()->state, WORKER_FINISHED);
}

static inline void sq_sched_yield()
{
    struct sq_worker *worker = this_worker();
    if (atomic_load_acq(&worker->state) == WORKER_RUNNING)
        atomic_store_rel(&worker->state, WORKER_QUEUING);
}

void sq_sched_poll();
int sq_sched_init(void *data);
int sq_sched_init_percpu(void *percpu_data);
int sq_sched_spawn(struct task *task, int cpu);
int sq_sched_set_params(void *params);

#define sq_sched_init_task     dummy_sched_init_task
#define sq_sched_block         dummy_sched_block
#define sq_sched_wakeup        dummy_sched_wakeup
#define sq_sched_percpu_lock   dummy_sched_percpu_lock
#define sq_sched_percpu_unlock dummy_sched_percpu_unlock
#define sq_sched_balance       dummy_sched_balance
#define sq_sched_dump_tasks    dummy_sched_dump_tasks

#define SCHED_DATA_SIZE        (sizeof(struct sq_dispatcher))
#define SCHED_PERCPU_DATA_SIZE (sizeof(struct sq_worker))
