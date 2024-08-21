/*
 * sq.c: single queue c-FCFS scheduler
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <skyloft/params.h>
#include <skyloft/sched.h>
#include <skyloft/sched/policy/sq_lcbe.h>
#include <skyloft/task.h>
#include <skyloft/uapi/task.h>
#include <utils/atomic.h>
#include <utils/bitmap.h>
#include <utils/log.h>
#include <utils/uintr.h>

struct sq_dispatcher *global_dispatcher;
DEFINE_PERCPU(struct sq_cpu *, sq_cpus);
static volatile bool lc_worker_preempted[USED_CPUS];
static volatile bool be_worker_preempted[USED_CPUS];

static bool dispatcher_ready = false;

static inline struct sq_task *sq_task_of(struct task *task)
{
    return (struct sq_task *)&task->policy_task_data;
}

static inline __nsec task_latency(struct task *task)
{
    return now_ns() - sq_task_of(task)->ingress;
}

static inline double task_slo(struct task *task)
{
    return (double)sq_task_of(task)->active / task_latency(task);
}

extern __thread struct task *__curr;
struct task *sq_sched_pick_next()
{
    /* We use this flag to keep the state consistent */
    struct sq_cpu *cpu = this_sq_cpu();
    if (atomic_load_acq(&cpu->need_sched)) {
        log_debug("need sched! %p", cpu);
        cpu->is_lc = !cpu->is_lc;
        atomic_store_rel(&cpu->need_sched, false);
    }

    struct sq_worker *worker = this_worker();
    if (current_cpu_id() == 0 || !cpu->is_lc) {
        return worker->cur_task;
    } else {
        if (atomic_load_acq(&worker->state) == WORKER_QUEUING) {
            /* enter twice from BE to LC */
            if (current_app_id() == SQ_LC) {
                worker->start_time = now_ns();
                sq_task_of(worker->cur_task)->start = now_ns();
                atomic_store_rel(&worker->state, WORKER_RUNNING);
            }
            return worker->cur_task;
        } else
            return NULL;
    }
}

void sq_sched_yield()
{
    struct sq_cpu *cpu;

    /* Switch APP on CPU 0 */
    if (current_cpu_id() == 0) {
        cpu = this_sq_cpu();
        if (current_app_id() == SQ_BE) {
            cpu->is_lc = true;
            global_dispatcher->be_ready[0] = true;
        } else {
            cpu->is_lc = false;
        }
        cpu->lc.state = WORKER_QUEUING;
    }
}

void sq_sched_finish_task(struct task *task)
{
    struct sq_worker *worker = this_worker();

    /*
     * LC task: finished by worker and released by dispatcher
     * BE task: running in background; only task on CPU 0 finishes
     */
    atomic_store_rel(&worker->state, WORKER_FINISHED);

    /* Collect BE workers */
    if (current_cpu_id() == 0) {
        worker->cur_task = NULL;
        this_sq_cpu()->is_lc = true;
        this_sq_cpu()->lc.state = WORKER_QUEUING;
    } else if (current_app_id() == SQ_BE) {
        log_debug("BE task %d finished %lu %lu", worker->cur_task->id,
                  now_ns() - sq_task_of(worker->cur_task)->start,
                  sq_task_of(worker->cur_task)->active);
        worker->cur_task = NULL;
    }
}

int sq_sched_spawn(struct task *task, int cpu_id)
{
    /*
     * LC task: worker task will pushed back to global queue
     * BE task: do nothing; each worker only has one background task
     */
    if (current_app_id() == SQ_BE && current_cpu_id() != 0)
        return 0;

    if (current_cpu_id() != 0) {
        log_err("%s: must be called on the dispatcher (CPU 0)", __func__);
        return -1;
    }

    if (!dispatcher_ready) {
        struct sq_cpu *cpu = sq_cpu(cpu_id);
        if (cpu_id == 0) {
            if (!cpu->be.cur_task) {
                log_debug("BE comes");
                cpu->be.cur_task = task;
            } else if (!cpu->lc.cur_task) {
                log_debug("LC comes");
                cpu->lc.cur_task = task;
                cpu->lc.state = WORKER_QUEUING;
                dispatcher_ready = true;
            } else {
                log_err("%s: Too many tasks", __func__);
                return -EINVAL;
            }
        } else {
            task->allow_preempt = true;
            cpu->be.cur_task = task;
        }
        return 0;
    } else {
        task->skip_free = true;
        task->allow_preempt = true;
        sq_task_of(task)->ingress = now_ns();
        sq_task_of(task)->active = 0;
        return queue_push(&global_dispatcher->pending_tasks, task);
    }
}

static bool is_congested(void)
{
    if (queue_is_empty(&global_dispatcher->pending_tasks))
        return false;

    struct task *task = queue_head(&global_dispatcher->pending_tasks);

    if (sq_task_of(task)->active && task_slo(task) < global_dispatcher->congestion_thresh) {
        log_debug("Congested %lu %lu", sq_task_of(task)->active, task_latency(task));
        return true;
    }

    return false;
}

static int pick_cpu(void)
{
    /*
    1. try to allocate a hyperthread pair core
    2. try the core that we most recently ran on
    3. the core is busy and no core available, should we preempt it?
    4. pick the lowest available core
    5. no cores available, take from the first bursting proc
    */
    return bitmap_find_next_cleared(global_dispatcher->lc_cpus, USED_CPUS, 0);
}

/* core allocation */
static void adjust_cpus(void)
{
    int cpu;

    if (now_ns() <= global_dispatcher->last_adjust + global_dispatcher->adjust_quantum)
        return;

    if (!is_congested())
        return;

    cpu = pick_cpu();
    if (cpu == USED_CPUS)
        return;

    /* LC will never give the CPU back to BE. */
    if (!atomic_load_acq(&be_worker_preempted[cpu])) {
        log_debug("LC asks for %d", cpu);
        _senduipi(cpu_worker(cpu)->uintr_index);
        be_worker_preempted[cpu] = true;
    }

    global_dispatcher->last_adjust = now_ns();
}

bool sq_sched_preempt()
{
    struct sq_cpu *cpu = this_sq_cpu();
    struct sq_worker *worker = this_worker();
    if (atomic_load_acq(&worker->state) == WORKER_RUNNING) {
        sq_task_of(worker->cur_task)->active += now_ns() - sq_task_of(worker->cur_task)->start;
        if (!cpu->is_lc) {
            cpu->need_sched = true;
            bitmap_set(global_dispatcher->lc_cpus, current_cpu_id());
            global_dispatcher->lc_nr_cpus++;
        } else {
            atomic_store_rel(&worker->state, WORKER_PREEMPTED);
        }

        return true;
    } else {
        return false;
    }
}

void sq_sched_poll()
{
    struct sq_cpu *cpu;
    struct sq_worker *worker;
    enum sq_worker_state worker_state;
    struct task *task;
    int i;

    if (current_cpu_id() == 0) {
        while (!atomic_load_acq(&global_dispatcher->be_ready[0])) task_yield();

        adjust_cpus();

        for (i = 1; i < USED_CPUS; i++) {
            cpu = sq_cpu(i);

            if (bitmap_test(global_dispatcher->lc_cpus, i)) {
                worker = &cpu->lc;
                worker_state = atomic_load_acq(&worker->state);

                if (worker_state == WORKER_RUNNING) {
                    if (global_dispatcher->preemption_quantum && !lc_worker_preempted[i] &&
                        now_ns() > worker->start_time + global_dispatcher->preemption_quantum) {
                        log_debug("! %d %d start %.3lf now %.3lf", i, worker->cur_task->id,
                                  (double)worker->start_time / NSEC_PER_USEC,
                                  (double)now_ns() / NSEC_PER_USEC);
                        _senduipi(worker->uintr_index);
                        /* Avoid preempting more times. */
                        lc_worker_preempted[i] = true;
                    }
                    continue;
                }

                /* Previous task may have not been dequeued. */
                if (worker_state == WORKER_QUEUING)
                    continue;

                if (worker_state == WORKER_FINISHED) {
                    log_debug("%d worker %d finished", i, worker->cur_task->id);
                    /* All tasks are created and freed by dispatcher. */
                    task_free(worker->cur_task);
                } else if (worker_state == WORKER_PREEMPTED) {
                    log_debug("%d worker %d preempted", i, worker->cur_task->id);
                    /* A preempted worker must have a task. */
                    queue_push(&global_dispatcher->pending_tasks, worker->cur_task);
                }

                task = queue_pop(&global_dispatcher->pending_tasks);
                if (task) {
                    log_debug("%d worker %d started", i, task->id);
                    worker->cur_task = task;
                    lc_worker_preempted[i] = false;
                    atomic_store_rel(&worker->state, WORKER_QUEUING);
                } else {
                    atomic_store_rel(&worker->state, WORKER_IDLE);
                }
            } else { /* Run BE workers */
                worker = &cpu->be;
                worker_state = atomic_load_acq(&worker->state);

                if (worker_state == WORKER_IDLE)
                    atomic_store_rel(&worker->state, WORKER_RUNNING);

                if (cpu->is_lc && !atomic_load_acq(&cpu->need_sched)) {
                    log_debug("Run BE %d %d", i, cpu->is_lc);
                    atomic_store_rel(&cpu->need_sched, true);
                }
            }
        }
    }
}

int sq_sched_set_params(void *params)
{
    struct sq_params *p = params;
    log_info("sq_sched_set_params: num_workers=%d preemption_quantum=%d guaranteed_cpus=%d "
             "congestion_thresh=%.3lf",
             p->num_workers, p->preemption_quantum, p->guaranteed_cpus, p->congestion_thresh);

    if (p->num_workers >= 0 && p->num_workers < USED_CPUS) {
        global_dispatcher->num_workers = p->num_workers;
        global_dispatcher->preemption_quantum = p->preemption_quantum * NSEC_PER_USEC;
        global_dispatcher->lc_guaranteed_cpus = p->guaranteed_cpus;
        for (unsigned int i = 0; i < global_dispatcher->lc_guaranteed_cpus + 1; i++)
            bitmap_set(global_dispatcher->lc_cpus, i);
        global_dispatcher->lc_nr_cpus = global_dispatcher->lc_guaranteed_cpus;
        global_dispatcher->adjust_quantum = p->adjust_quantum * NSEC_PER_USEC;
        global_dispatcher->congestion_thresh = p->congestion_thresh;
        return 0;
    }

    return -EINVAL;
}

int sq_sched_init(void *data)
{
    struct sq_dispatcher *dispatcher = data;
    global_dispatcher = dispatcher;
    if (current_app_id() == SQ_LC) {
        dispatcher->num_workers = USED_CPUS;
        queue_init(&dispatcher->pending_tasks);
        memset((void *)lc_worker_preempted, 0, sizeof(bool) * USED_CPUS);
        memset((void *)be_worker_preempted, 0, sizeof(bool) * USED_CPUS);
        memset((void *)dispatcher->lc_ready, 0, sizeof(int) * USED_CPUS);
        memset((void *)dispatcher->be_ready, 0, sizeof(int) * USED_CPUS);
        bitmap_init(dispatcher->lc_cpus, USED_CPUS, false);
        dispatcher->last_adjust = now_ns();
    } else {
        dispatcher->be_pid = getpid();
        log_debug("BE pid %d", dispatcher->be_pid);
    }
    return 0;
}

extern void uintr_handler();
static inline int uipi_receiver(struct sq_worker *worker)
{
    int ret = 0;

    if ((ret = uintr_register_handler(uintr_handler, 0)) < 0) {
        log_err("failed to register interrupt handler\n");
        goto out;
    }

    if ((ret = uintr_vector_fd(SQ_UVEC, 0)) < 0) {
        log_err("failed to register interrupt vector\n");
        goto out;
    }
    worker->uintr_fd = ret;

out:
    return ret;
}

static inline int uipi_sender(struct sq_worker *worker)
{
    int ret;

    if ((ret = uintr_register_sender(worker->uintr_fd, 0)) < 0) {
        log_err("failed to register interrupt sender\n");
        return ret;
    }
    worker->uintr_index = ret;

    return 0;
}

int sq_sched_init_percpu(void *percpu_data)
{
    int ret = 0, i, pidfd;
    struct sq_cpu *cpu = percpu_data;

    if (current_app_id() == SQ_LC) {
        init_worker(&cpu->lc);
        init_worker(&cpu->be);
        /* First APP is LC. */
        cpu->is_lc = true;
    }
    percpu_get(sq_cpus) = cpu;

#ifdef SKYLOFT_UINTR
    if (current_cpu_id() == 0) {
        if (current_app_id() == SQ_LC) {
            /* LC dispatcher can access fd of LC worker in the same process. */
            for (i = 1; i < USED_CPUS; i++) {
                while (!atomic_load_acq(&global_dispatcher->lc_ready[i]));
                if ((ret = uipi_sender(&sq_cpu(i)->lc)) < 0)
                    goto out;
            }
            log_info("SQ dispatcher registered as a sender for all LC workers.");

            /* LC dispatcher cannot directly access fd of BE worker. The fd is shared by BE process.
             */
            for (i = 1; i < USED_CPUS; i++) {
                while (!atomic_load_acq(&global_dispatcher->be_ready[i]));
                if (i == 1) {
                    if ((ret = pidfd_open(global_dispatcher->be_pid)) < 0) {
                        log_err("Failed to open a process fd: %s", strerror(errno));
                        goto out;
                    }
                    pidfd = ret;
                }

                if ((ret = pidfd_getfd(pidfd, sq_cpu(i)->be.uintr_fd)) < 0) {
                    log_err("Failed to steal an fd from another process: %s", strerror(errno));
                    goto out;
                }
                log_debug("old fd %d new fd %d", sq_cpu(i)->be.uintr_fd, ret);
                sq_cpu(i)->be.uintr_fd = ret;
                if ((ret = uipi_sender(&sq_cpu(i)->be)) < 0)
                    goto out;
            }
            log_info("SQ dispatcher registered as a sender for all BE workers.");

            while (!atomic_load_acq(&sq_cpu(0)->be.cur_task));
        }
    } else {
        if (current_app_id() == SQ_LC) {
            ret = uipi_receiver(&cpu->lc);
            atomic_store_rel(&global_dispatcher->lc_ready[current_cpu_id()], 1);
        } else {
            ret = uipi_receiver(&cpu->be);
            atomic_store_rel(&global_dispatcher->be_ready[current_cpu_id()], 1);
        }
        local_irq_disable();
    }
#endif

out:
    return ret;
}

void sq_sched_dump_tasks()
{
    int i;
    struct sq_cpu *cpu;

    printf("Core Allocation Status:\n");
    printf("\t0 Dispatcher\n");
    for (i = 1; i < USED_CPUS; i++) {
        cpu = sq_cpu(i);

        if (cpu->is_lc) {
            printf("\t%d LC\n", i);
        } else {
            printf("\t%d BE\n", i);
        }
    }
}