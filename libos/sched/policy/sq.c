/*
 * sq.c: single queue c-FCFS scheduler
 */

#include <errno.h>
#include <string.h>

#include <skyloft/params.h>
#include <skyloft/sched.h>
#include <skyloft/sched/policy/sq.h>
#include <skyloft/task.h>
#include <skyloft/uapi/task.h>
#include <utils/atomic.h>
#include <utils/log.h>
#include <utils/uintr.h>

static struct sq_dispatcher *global_dispatcher;

DEFINE_PERCPU(struct sq_worker *, workers);

static volatile bool worker_ready[USED_CPUS];
static volatile bool worker_preempted[USED_CPUS];
static bool dispatcher_ready = false;

int sq_sched_spawn(struct task *task, int cpu)
{
    if (current_cpu_id() != 0) {
        log_err("%s: must be called on the dispatcher (CPU 0)", __func__);
        return -1;
    }

    if (!dispatcher_ready) {
        dispatcher_ready = true;
        this_worker()->cur_task = task;
        atomic_store_rel(&this_worker()->state, WORKER_QUEUING);
        return 0;
    } else {
        task->skip_free = true;
        task->allow_preempt = true;
        return queue_push(&global_dispatcher->pending_tasks, task);
    }
}

void sq_sched_poll()
{
    struct sq_worker *worker;
    enum sq_worker_state worker_state;
    struct task *task;
    int i;

    if (current_cpu_id() == 0) {
        for (i = 1; i < global_dispatcher->num_workers + 1; i++) {
            worker = cpu_worker(i);
            worker_state = atomic_load_acq(&worker->state);
            // log_debug("Poll %p %d %d", worker, i, worker->uintr_index);

            if (worker_state == WORKER_RUNNING) {
                if (global_dispatcher->preemption_quantum && !worker_preempted[i] &&
                    now_ns() > worker->start_time + global_dispatcher->preemption_quantum) {
                    log_debug("! %d %p start %.3lf now %.3lf", i, worker->cur_task,
                              (double)worker->start_time / NSEC_PER_USEC,
                              (double)now_ns() / NSEC_PER_USEC);
                    _senduipi(worker->uintr_index);
                    /* Avoid preempting more times. */
                    worker_preempted[i] = true;
                }
                continue;
            }

            /* Previous task may have not been dequeued. */
            if (worker_state == WORKER_QUEUING)
                continue;

            if (worker_state == WORKER_FINISHED) {
                log_debug("worker %d %p finished", i, worker->cur_task);
                /* All tasks are created and freed by dispatcher. */
                task_free(worker->cur_task);
            } else if (worker_state == WORKER_PREEMPTED) {
                log_debug("worker %d %p preempted", i, worker->cur_task);
                /* A preempted worker must have a task. */
                assert(worker->cur_task != NULL);
                queue_push(&global_dispatcher->pending_tasks, worker->cur_task);
            }

            task = queue_pop(&global_dispatcher->pending_tasks);
            if (task) {
                log_debug("worker %d %p started", i, task);
                worker->cur_task = task;
                atomic_store_rel(&worker->state, WORKER_QUEUING);
                worker_preempted[i] = false;
            } else {
                atomic_store_rel(&worker->state, WORKER_IDLE);
            }
        }
    }
}

int sq_sched_set_params(void *params)
{
    struct sq_params *p = params;
    log_info("sq_sched_set_params: num_workers=%d preemption_quantum=%d", p->num_workers,
             p->preemption_quantum);

    if (p->num_workers >= 0 && p->num_workers < USED_CPUS) {
        global_dispatcher->num_workers = p->num_workers;
        global_dispatcher->preemption_quantum = p->preemption_quantum * NSEC_PER_USEC;
        return 0;
    }

    return -EINVAL;
}

int sq_sched_init(void *data)
{
    struct sq_dispatcher *dispatcher = data;
    dispatcher->num_workers = USED_CPUS;
    queue_init(&dispatcher->pending_tasks);
    global_dispatcher = dispatcher;
    memset((void *)worker_ready, 0, sizeof(bool) * USED_CPUS);
    memset((void *)worker_preempted, 0, sizeof(bool) * USED_CPUS);
    return 0;
}

int sq_sched_init_percpu(void *percpu_data)
{
    int ret, i;
    struct sq_worker *worker = percpu_data;

    worker->cur_task = NULL;
    worker->state = WORKER_IDLE;
    percpu_get(workers) = worker;

    if (current_cpu_id() == 0) {
        for (i = 1; i < USED_CPUS; i++) {
            while (!atomic_load_acq(&worker_ready[i]));

            ret = uintr_register_sender(cpu_worker(i)->uintr_fd, 0);
            if (ret < 0) {
                log_err("failed to register interrupt sender\n");
                return -1;
            }
            cpu_worker(i)->uintr_index = ret;
            log_debug("worker %p %d %d", cpu_worker(i), i, ret);
        }
        log_info("SQ dispatcher registered as a sender for all workers.");
    } else {
        extern void uintr_handler();
        ret = uintr_register_handler(uintr_handler, 0);
        if (ret < 0) {
            log_err("failed to register interrupt handler\n");
            return -1;
        }

        worker->uintr_fd = uintr_vector_fd(SQ_UVEC, 0);
        if (worker->uintr_fd < 0) {
            log_err("failed to register interrupt vector\n");
            return -1;
        }

        atomic_store_rel(&worker_ready[current_cpu_id()], true);
        local_irq_disable();
    }

    return 0;
}
