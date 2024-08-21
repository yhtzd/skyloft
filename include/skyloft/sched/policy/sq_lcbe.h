#pragma once

#include <skyloft/params.h>
#include <skyloft/percpu.h>

#include <utils/assert.h>
#include <utils/bitmap.h>
#include <utils/defs.h>
#include <utils/queue.h>
#include <utils/time.h>

#include "dummy.h"

#define SQ_UVEC 1
#define SQ_LC   0 /* LC default app ID */
#define SQ_BE   1 /* BE default app ID */

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

struct sq_cpu {
    struct sq_worker lc;
    struct sq_worker be;
    bool need_sched;
    bool is_lc;
};

struct sq_params {
    int num_workers;
    int preemption_quantum;
    int guaranteed_cpus;
    int adjust_quantum;
    double congestion_thresh;
};

struct sq_dispatcher {
    /* Centric queue */
    queue_t pending_tasks;
    /* Maximum number of workers */
    int num_workers;
    /* If not set, tasks will run to complete */
    __nsec preemption_quantum;
    /* BE process ID */
    pid_t be_pid;
    /* Dispatcher will wait for ready flags */
    bool be_ready[USED_CPUS];
    bool lc_ready[USED_CPUS];
    /* CPUs used by LC. */
    DEFINE_BITMAP(lc_cpus, USED_CPUS);
    unsigned int lc_nr_cpus;
    /* Guaranteed CPUs used by LC */
    unsigned int lc_guaranteed_cpus;
    /* Core allocation */
    __nsec last_adjust;
    __nsec adjust_quantum;
    /* Congestion threshold */
    double congestion_thresh;
};

struct sq_task {
    /* Time when task is created and pushed */
    __nsec ingress;
    /* Time when task is popped */
    __nsec start;
    /* Elapsed running time */
    __nsec active;
};
BUILD_ASSERT(sizeof(struct sq_task) <= POLICY_TASK_DATA_SIZE);

DECLARE_PERCPU(struct sq_cpu *, sq_cpus);

static inline struct sq_cpu *this_sq_cpu() { return percpu_get(sq_cpus); }

static inline struct sq_worker *this_worker()
{
    struct sq_cpu *cpu = this_sq_cpu();
    return cpu->is_lc ? &cpu->lc : &cpu->be;
}

static inline struct sq_cpu *sq_cpu(int cpu_id) { return percpu_get_remote(sq_cpus, cpu_id); }

static inline struct sq_worker *cpu_worker(int cpu_id)
{
    struct sq_cpu *cpu = sq_cpu(cpu_id);
    return cpu->is_lc ? &cpu->lc : &cpu->be;
}

static inline void init_worker(struct sq_worker *worker)
{
    worker->cur_task = NULL;
    worker->state = WORKER_IDLE;
}

struct task *sq_sched_pick_next();
void sq_sched_finish_task(struct task *task);
void sq_sched_poll();
int sq_sched_init(void *data);
int sq_sched_init_percpu(void *percpu_data);
int sq_sched_spawn(struct task *task, int cpu);
int sq_sched_set_params(void *params);
void sq_sched_dump_tasks();

#define sq_sched_init_task     dummy_sched_init_task
#define sq_sched_block         dummy_sched_block
#define sq_sched_wakeup        dummy_sched_wakeup
#define sq_sched_percpu_lock   dummy_sched_percpu_lock
#define sq_sched_percpu_unlock dummy_sched_percpu_unlock
#define sq_sched_balance       dummy_sched_balance

#define SCHED_DATA_SIZE        (sizeof(struct sq_dispatcher))
#define SCHED_PERCPU_DATA_SIZE (sizeof(struct sq_worker))