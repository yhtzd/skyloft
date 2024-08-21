/*
 * rr.c: a round-robin scheduler
 */

#include <skyloft/params.h>
#include <skyloft/percpu.h>
#include <skyloft/platform.h>
#include <skyloft/sched/policy/rr.h>
#include <skyloft/sync.h>
#include <skyloft/task.h>

#include <utils/assert.h>
#include <utils/atomic.h>
#include <utils/hash.h>
#include <utils/list.h>
#include <utils/log.h>

DEFINE_PERCPU(struct fifo_rq *, rqs);

static atomic_int TARGET_CPU = 0;

static int find_target_cpu(struct task *task, bool new_task)
{
    if (new_task) {
        int cpu = atomic_fetch_add(&TARGET_CPU, 1) % USED_CPUS;
        return cpu;
    } else {
        return fifo_task_of(task)->last_run;
    }
}

static void put_task(struct fifo_rq *rq, struct task *task)
{
    unsigned int tail = atomic_fetch_add_explicit(&rq->tail, 1, memory_order_acquire);
    if (tail >= atomic_load_acq(&rq->head) + RUNTIME_RQ_SIZE) {
        panic("runqueue full");
    }
    atomic_store_rel(&rq->tasks[tail & RQ_SIZE_MASK], task);
}

struct task *fifo_sched_pick_next()
{
    struct fifo_rq *rq = this_rq();
    unsigned int head = atomic_load_relax(&rq->head);
    struct task *task = (struct task *)atomic_exchange_explicit(
        (atomic_ullong *)&rq->tasks[head & RQ_SIZE_MASK], NULL, memory_order_acquire);
    if (!task) {
        goto done;
    }
    atomic_store_rel(&rq->head, head + 1);
    fifo_task_of(task)->last_run = current_cpu_id();

done:
    return task;
}

int fifo_sched_spawn(struct task *task, int cpu)
{
    if (task == NULL || cpu < 0 || cpu > USED_CPUS)
        return -1;
    fifo_task_of(task)->quan = 0;
    cpu = find_target_cpu(task, true);
    put_task(cpu_rq(cpu), task);
    atomic_inc(&cpu_rq(cpu)->num_tasks);
    return 0;
}

void fifo_sched_yield() { put_task(this_rq(), task_self()); }

void fifo_sched_wakeup(struct task *task)
{
    int cpu = find_target_cpu(task, false);
    struct fifo_rq *rq = cpu_rq(cpu);
    put_task(rq, task);
    atomic_inc(&rq->num_tasks);
}

int fifo_sched_init_percpu(void *percpu_data)
{
    struct fifo_rq *rq = (struct fifo_rq *)percpu_data;

    percpu_get(rqs) = rq;

    if (current_app_id() == 0) {
        rq->k = thisk();
        rq->head = rq->tail = 0;
        rq->num_tasks = 0;
    }

    return 0;
}

bool fifo_sched_preempt()
{
    struct fifo_task *task = fifo_task_of(task_self());
    if (++task->quan >= PREEMPT_QUAN) {
        task->quan = 0;
        return true;
    }
    return false;
}
