#include <errno.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include <skyloft/params.h>
#include <skyloft/percpu.h>
#include <skyloft/platform.h>
#include <skyloft/sched.h>
#include <skyloft/sched/ops.h>
#include <skyloft/task.h>

#include <utils/assert.h>
#include <utils/atomic.h>
#include <utils/defs.h>
#include <utils/log.h>
#include <utils/time.h>

#define SHM_SCHED_DATA_HUGE_PATH      "/mnt/huge/skyloft_sched_data_huge"
#define SHM_SCHED_DATA_HUGE_BASE_ADDR 0x300000000000UL

static void *huge_pages_base;
static void *shm_sched_data;
static void *shm_sched_data_percpu[USED_CPUS];

__thread struct task *__curr, *__idle;
__thread struct kthread *localk;
__thread volatile unsigned int preempt_cnt;
static __thread uint32_t rcu_gen;
extern __thread int g_logic_cpu_id;

static inline void switch_to_app(void *arg)
{
    struct task *next = arg;
    int target_app = next->app_id;
    int target_tid = shm_apps[target_app].all_ks[current_cpu_id()].tid;
    int ret = 0;

retry:
    if (unlikely(atomic_load_acq(&shm_apps[target_app].exited))) {
        log_warn("App %d has exited", target_app);
        atomic_store(&shm_metadata->apps[current_cpu_id()], proc->id);
        return;
    }

    /* trace the application running on this CPU */
    atomic_store(&shm_metadata->apps[current_cpu_id()], target_app);

    /* switch userspace through kernel */
    ret = skyloft_switch_to(target_tid);

    if (unlikely(ret)) {
        log_warn("Failed to switch to app %d: %d", target_tid, ret);
        goto retry;
    }
}

/**
  fast_schedule - (fastpath) switch directly to the next task
 */
static __always_inline void fast_schedule()
{
    struct task *prev = __curr, *next;

    assert_local_irq_disabled();
    assert(__curr != NULL);

    __sched_percpu_lock(g_logic_cpu_id);
    next = __sched_pick_next();
    /* slow path: switch to idle and run schedule() */
    if (unlikely(!next)) {
        log_debug("%s: (%d,%d) -> ", __func__, prev->app_id, prev->id);
        __context_switch_to_idle(&prev->rsp, __idle->rsp);
        return;
    }
    __sched_percpu_unlock(g_logic_cpu_id);

    log_debug("%s: (%d,%d) -> (%d,%d)", __func__, prev->app_id, prev->id, next->app_id, next->id);

    /* increment the RCU generation number (odd is in task) */
    atomic_store_rel(&rcu_gen, rcu_gen + 2);
    assert((rcu_gen & 0x1) == 0x1);

#if defined(SKYLOFT_TIMER) && !defined(SKYLOFT_UINTR) && !defined(SKYLOFT_DPDK)
    softirq_run(SOFTIRQ_MAX_BUDGET);
#endif

    assert(task_is_runnable(next));
    /* check if we're switching into the same task as before */
    if (unlikely(next == prev)) {
        next->stack_busy = false;
        return;
    }

    /* task must be scheduled atomically */
    if (unlikely(atomic_load_acq(&next->stack_busy))) {
        /* wait until the scheduler finishes switching stacks */
        while (atomic_load_acq(&next->stack_busy)) cpu_relax();
    }

    if (unlikely(next->app_id != prev->app_id)) {
        /* switch to idle first */
        atomic_store_rel(&prev->stack_busy, false);
        switch_to_app(next);
        return;
    }

    /* switch stacks and enter the next task */
    __curr = next;
    if (next->init) {
        next->init = false;
        __context_switch_init(&prev->rsp, next->rsp, &prev->stack_busy);
    } else
        __context_switch(&prev->rsp, next->rsp, &prev->stack_busy);
}

/**
 * schedule - (slowpath) idle task
 */
__noreturn __noinline void schedule()
{
    struct task *next;
    uint64_t elapsed;

    assert_local_irq_disabled();

    /* unmark busy for the stack of the previous task */
    if (__curr != NULL) {
        atomic_store_rel(&__curr->stack_busy, false);
        __curr = NULL;
    }

    /* increment the RCU generation number (even is in scheduler) */
    atomic_store_rel(&rcu_gen, rcu_gen + 1);
    assert((rcu_gen & 0x1) == 0x0);

    STAT_CYCLES_BEGIN(elapsed);
again:
    next = __sched_pick_next();
    if (unlikely(!next)) {
#if defined(SKYLOFT_SCHED_CFS) || defined(SKYLOFT_SCHED_EEVDF)
        // log_debug("%s: again, unlocking", __func__);
        __sched_percpu_unlock(g_logic_cpu_id);
#endif
#ifdef SCHED_PERCPU
#ifdef SKYLOFT_DPDK
        if ((next = softirq_task(localk, SOFTIRQ_MAX_BUDGET))) {
            ADD_STAT(LOCAL_SPAWNS, 1);
            goto done;
        }
#else
        /* check for softirqs */
        softirq_run(SOFTIRQ_MAX_BUDGET);
#endif
        /* optional load balance */
        __sched_balance();
#if defined(SKYLOFT_SCHED_CFS) || defined(SKYLOFT_SCHED_EEVDF)
        __sched_percpu_lock(g_logic_cpu_id);
#endif
#endif
        goto again;
    }
done:
    /* release the lock */
    __sched_percpu_unlock(g_logic_cpu_id);

    log_debug("%s: -> (%d,%d)", __func__, next->app_id, next->id);

    /* udpate stat counters */
    ADD_STAT_CYCLES(IDLE_CYCLES, elapsed);
    ADD_STAT(IDLE, 1);

    /* increment the RCU generation number (odd is in task) */
    atomic_store_rel(&rcu_gen, rcu_gen + 1);
    assert((rcu_gen & 0x1) == 0x1);

    /* task must be scheduled atomically */
    if (unlikely(atomic_load_acq(&next->stack_busy))) {
        /* wait until the scheduler finishes switching stacks */
        while (atomic_load_acq(&next->stack_busy)) cpu_relax();
    }

    ADD_STAT(SWITCH_TO, 1);
    if (unlikely(next->app_id != current_app_id())) {
        switch_to_app(next);

        /* this function should not return, so we call `schedule` again */
        schedule();
    }

    /* switch stacks and enter the next task */
    __curr = next;
    if (next->init) {
        next->init = false;
        __context_switch_from_idle_init(next->rsp);
    } else
        __context_switch_from_idle(next->rsp);
}

__noreturn void start_schedule(void)
{
    atomic_store_rel(&rcu_gen, 1);
    __sched_percpu_lock(g_logic_cpu_id);
    schedule();
}

int sched_init_percpu()
{
    if (sched_task_init_percpu() < 0) {
        log_err("sched_task_init_percpu failed");
        return -1;
    }

    void *data_percpu = shm_sched_data_percpu[g_logic_cpu_id];
    log_debug("sched: shm_sched_data_percpu[%d]: %p", current_cpu_id(), data_percpu);

    if (__sched_init_percpu(data_percpu) < 0) {
        log_err("sched: init policy percpu failed");
        return -1;
    }

    struct task *task = task_create_idle();
    if (!task) {
        log_err("sched: create idle task failed");
        return -1;
    }

    __curr = NULL;
    __idle = task;

    extern uint32_t *rcu_gen_percpu[USED_CPUS];
    rcu_gen_percpu[g_logic_cpu_id] = &rcu_gen;

    return 0;
}

static int sched_shm_map()
{
    int i;
    size_t task_size, policy_size, policy_percpu_size;
    off_t policy_off, policy_percpu_off;
    size_t huge_pages_size = 0;

    task_size = TASK_SIZE_PER_APP * MAX_APPS;
    policy_size = align_up(SCHED_DATA_SIZE, PGSIZE_2MB);
    policy_percpu_size = align_up(SCHED_PERCPU_DATA_SIZE, PGSIZE_2MB);

    huge_pages_size = task_size;
    policy_off = huge_pages_size;
    huge_pages_size += policy_size;
    policy_percpu_off = huge_pages_size;
    huge_pages_size += policy_percpu_size * USED_CPUS;

    huge_pages_base =
        mem_map_shm_file(SHM_SCHED_DATA_HUGE_PATH, (void *)SHM_SCHED_DATA_HUGE_BASE_ADDR,
                         huge_pages_size, PGSIZE_2MB, 0);
    if (huge_pages_base != (void *)SHM_SCHED_DATA_HUGE_BASE_ADDR) {
        log_err("sched: open shm %s failed", SHM_SCHED_DATA_HUGE_PATH);
        return -ENOMEM;
    }

    shm_sched_data = (void *)(huge_pages_base + policy_off);
    for (i = 0; i < USED_CPUS; i++)
        shm_sched_data_percpu[i] =
            (void *)(huge_pages_base + policy_percpu_off + i * policy_percpu_size);

    return 0;
}

int sched_init()
{
    int ret;

    log_info("sched: scheduling policy %s", __sched_name);

    if ((ret = sched_shm_map()) < 0) {
        log_err("sched: map huge pages failed %d", ret);
        return ret;
    }

    if ((ret = sched_task_init(huge_pages_base + current_app_id() * TASK_SIZE_PER_APP)) < 0) {
        log_err("sched: init task failed %d", ret);
        return ret;
    }

    if ((ret = __sched_init(shm_sched_data)) < 0) {
        log_err("sched: init policy failed");
        return ret;
    }

    return 0;
}

/* task APIs */

int task_spawn(int cpu_id, thread_fn_t fn, void *arg, int stack_size)
{
    struct task *task;
    int ret;
    int flags;

    ADD_STAT(LOCAL_SPAWNS, 1);

    task = task_create(fn, arg);
    if (unlikely(!task))
        return -ENOMEM;

    local_irq_save(flags);
    ret = __sched_spawn(task, cpu_id);
    if (unlikely(ret)) {
        log_warn("sched: %s failed to spawn task on %d", __func__, cpu_id);
        task_free(task);
    }
    local_irq_restore(flags);
    return ret;
}

int task_enqueue(int cpu_id, struct task *task)
{
    int flags, ret;
    ADD_STAT(LOCAL_SPAWNS, 1);
    local_irq_save(flags);
    ret = __sched_spawn(task, cpu_id);
    local_irq_restore(flags);
    return ret;
}

/**
 * task_yield - yield the current running task
 */
void task_yield()
{
    int flags;
#ifdef SCHED_PERCPU
    softirq_run(SOFTIRQ_MAX_BUDGET);
#endif
    local_irq_save(flags);
    atomic_store_rel(&__curr->stack_busy, true);
    __sched_yield();
    fast_schedule();
    local_irq_restore(flags);
}

/**
 * task_wakeup - wake up a BLOCKED task
 */
void task_wakeup(struct task *task)
{
    int flags;
    assert(task_is_blocked(task));
    local_irq_save(flags);
    task->state = TASK_RUNNABLE;
    __sched_wakeup(task);
    local_irq_restore(flags);
}

/**
 * task_block - marks a task as BLOCKED
 * @lock: the lock to be released
 */
void task_block(spinlock_t *lock)
{
    int flags;

    assert_preempt_disabled();
    assert_spin_lock_held(lock);

    local_irq_save(flags);
    preempt_enable();

    __curr->state = TASK_BLOCKED;
    __curr->stack_busy = true;
    spin_unlock(lock);
    __sched_block();
    fast_schedule();
    local_irq_restore(flags);
}

static void __task_exit()
{
    /* task stack might be freed */
    __sched_finish_task(__curr);
    if (!__curr->skip_free)
        task_free(__curr);
    __curr = NULL;

    __sched_percpu_lock(g_logic_cpu_id);
    schedule();
}

/**
 * task_exit - release the current running task and run schedule()
 * @code: exit code or return code of the function
 */
__noreturn void task_exit(void *code)
{
    /* disable preemption before scheduling */
    local_irq_disable();
    __context_switch_to_fn_nosave(__task_exit, __idle->rsp);
}

/* API implementations */

const char *__api sl_sched_policy_name()
{
    return __sched_name;
}

int __api sl_current_task_id()
{
    return __curr->id;
}

int __api sl_sched_set_params(void *params)
{
    return __sched_set_params(params);
}

void __api sl_sched_poll()
{
    __sched_poll();
}

int __api sl_task_spawn(thread_fn_t fn, void *arg, int stack_size)
{
    return task_spawn(g_logic_cpu_id, fn, arg, stack_size);
}

int __api sl_task_spawn_oncpu(int cpu_id, thread_fn_t fn, void *arg, int stack_size)
{
    return task_spawn(cpu_id, fn, arg, stack_size);
}

void __api sl_task_yield()
{
    task_yield();
}

__noreturn void __api sl_task_exit(void *code)
{
    task_exit(code);
}

void __api sl_dump_tasks()
{
    __sched_dump_tasks();
}

#ifdef SKYLOFT_UINTR

void __attribute__((target("general-regs-only"))) __attribute__((interrupt))
uintr_handler(struct __uintr_frame *ui_frame, unsigned long long vector)
{
    /* reset UPID.PIR */
#if defined(SCHED_PERCPU) && !defined(UTIMER)
    _senduipi(uintr_index());
#endif
    ADD_STAT(UINTR, 1);
    /* check if rescheduling needed */
    if (__sched_preempt()) {
        if (preempt_enabled() && __curr->allow_preempt) {
            task_yield();
        }
    }
}

#endif
