#include <errno.h>
#include <fcntl.h>
#include <numa.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <skyloft/global.h>
#include <skyloft/io.h>
#include <skyloft/mm.h>
#include <skyloft/params.h>
#include <skyloft/percpu.h>
#include <skyloft/platform.h>
#include <skyloft/sched.h>
#include <skyloft/sync.h>
#include <skyloft/task.h>
#include <skyloft/uapi/dev.h>
#include <skyloft/uapi/task.h>
#include <utils/init.h>
#include <utils/log.h>
#include <utils/spinlock.h>
#include <utils/time.h>

__usec g_boot_time_us;
int g_app_id;
__thread int g_logic_cpu_id;
__thread bool thread_init_done;

struct metadata *shm_metadata;
struct proc *shm_apps;
struct proc *proc;

static thread_fn_t saved_app_main;
static void *saved_app_arg;
static atomic_int all_init_done;

static initializer_fn_t global_init_hook;
static initializer_fn_t percpu_init_hook;
static initializer_fn_t late_init_hook;

static const struct init_entry init_handlers[] = {
    INITIALIZER(global, init),
    INITIALIZER(signal, init),
    INITIALIZER(platform, init),

    /* memory management */
    INITIALIZER(page, init),
    INITIALIZER(slab, init),
    INITIALIZER(smalloc, init),

    /* scheduler */
    INITIALIZER(sched, init),
    INITIALIZER(proc, init),
#ifdef SKYLOFT_DPDK
    INITIALIZER(iothread, init),
#endif
};

static const struct init_entry init_handlers_percpu[] = {
    /* memory memangement */
    INITIALIZER(percpu, init),
    INITIALIZER(page, init_percpu),
    INITIALIZER(smalloc, init_percpu),

    /* scheduler */
    INITIALIZER(sched, init_percpu),
    INITIALIZER(timer, init_percpu),
};

static const struct init_entry late_init_handlers_percpu[] = {
    /* platform */
    INITIALIZER(cpubind, init_percpu),
    INITIALIZER(platform, init_percpu),
};

int global_init()
{
    int ret = 0;

    spin_lock(&shm_metadata->lock);

    if (!shm_metadata->nr_apps)
        shm_metadata->boot_time_us = now_us();
    g_boot_time_us = shm_metadata->boot_time_us;

    if (shm_metadata->nr_apps >= MAX_APPS) {
        log_err("Too many apps %d", shm_metadata->nr_apps);
        ret = -EOVERFLOW;
        goto out;
    }

    g_app_id = shm_metadata->nr_apps++;
    proc = &shm_apps[g_app_id];
    proc->id = g_app_id;
    log_info("global_init: APP ID %d", g_app_id);
out:
    spin_unlock(&shm_metadata->lock);
    return ret;
}

int proc_init()
{
    int i, ret = 0;

    proc->pid = getpid();
    proc->exited = false;
    proc->ready = false;
#ifdef SKYLOFT_DPDK
    proc->nr_ks = USED_CPUS - 1;
#else
    proc->nr_ks = USED_CPUS;
#endif
    for (i = 0; i < USED_CPUS; i++) {
        struct kthread *k = &proc->all_ks[i];

        spin_lock_init(&k->lock);
        k->cpu = i;
        k->node = cpu_numa_node(i);
        k->app = proc->id;
        k->parked = false;
        memset(k->stats, 0, sizeof(k->stats));
    }

    return ret;
}

int cpubind_init_percpu(void)
{
    if (is_daemon()) {
        bind_to_cpu(0, g_logic_cpu_id);
    } else {
        BUG_ON(skyloft_park_on_cpu(g_logic_cpu_id));
    }

    log_info("CPU %d(%d): node = %d, tid = %d %p", g_logic_cpu_id, hw_cpu_id(g_logic_cpu_id),
             thisk()->node, _gettid(), localk);

    return 0;
}

static __noreturn void *percpu_entry(void *arg)
{
    int ret, cpu_id = (int)(size_t)arg;
    extern __thread struct kthread *localk;
    struct kthread *k;

    g_logic_cpu_id = cpu_id;

    localk = &proc->all_ks[cpu_id];
    k = thisk();
    assert(k == localk);
    k->tid = _gettid();

    ret = run_init_handlers(init_handlers_percpu,
                            sizeof(init_handlers_percpu) / sizeof(struct init_entry));
    if (ret < 0) {
        log_err("Failed to init on CPU %d: %d", cpu_id, ret);
        exit(EXIT_FAILURE);
    }

    /* TODO: run it on IO_CPU if DPDK disabled */
    if (percpu_init_hook && cpu_id != IO_CPU) {
        ret = percpu_init_hook();
        if (ret) {
            log_err("percpu_init_hook(): failed with %d", ret);
            exit(EXIT_FAILURE);
        }
    }

    if (saved_app_main && !cpu_id) {
        if (task_spawn(0, saved_app_main, saved_app_arg, RUNTIME_LARGE_STACK_SIZE) < 0) {
            panic("Cannot spawn main thread");
        }
    }

#ifndef SCHED_PERCPU
    /* synchronize */
    thread_init_done = true;
    atomic_fetch_add(&all_init_done, 1);
    while (atomic_load(&all_init_done) < USED_CPUS);
#endif

    ret = run_init_handlers(late_init_handlers_percpu,
                            sizeof(late_init_handlers_percpu) / sizeof(struct init_entry));
    if (ret < 0) {
        log_err("Failed to init on CPU %d: %d", cpu_id, ret);
        exit(EXIT_FAILURE);
    }

    if (!cpu_id && late_init_hook) {
        ret = late_init_hook();
        if (ret) {
            log_err("late_init_hook(): failed with %d", ret);
            exit(EXIT_FAILURE);
        }
    }

#ifdef SCHED_PERCPU
    /* synchronize */
    thread_init_done = true;
    atomic_fetch_add(&all_init_done, 1);
    while (atomic_load(&all_init_done) < USED_CPUS);
#endif

#ifdef SKYLOFT_DPDK
    /* CPU reserved for I/O */
    if (cpu_id == IO_CPU) {
        iothread_main();
    } else {
        while (!atomic_load_acq(&proc->ready));

        ret = net_init_percpu();
        if (ret < 0) {
            log_err("net_init_percpu(): failed with %d", ret);
        }

        start_schedule();
    }
#else
    start_schedule();
#endif
}

int __api sl_set_initializers(initializer_fn_t global, initializer_fn_t percpu,
                              initializer_fn_t late)
{
    global_init_hook = global;
    percpu_init_hook = percpu;
    late_init_hook = late;
    return 0;
}

int __api sl_libos_start(thread_fn_t entry, void *arg)
{
    int ret = 0;
    long int i;

    saved_app_main = entry;
    saved_app_arg = arg;

    shm_metadata = mem_map_shm_file(SHM_META_PATH, NULL, sizeof(struct metadata), PGSIZE_4KB, 0);
    if (shm_metadata == MAP_FAILED)
        return -ENOMEM;

    shm_apps = mem_map_shm_file(SHM_APPS_PATH, NULL, sizeof(struct proc) * MAX_APPS, PGSIZE_4KB, 0);
    if (shm_apps == MAP_FAILED)
        return -ENOMEM;

    ret = run_init_handlers(init_handlers, sizeof(init_handlers) / sizeof(struct init_entry));
    if (ret < 0) {
        log_err("Failed to init: %d", ret);
        return -1;
    }

    if (global_init_hook) {
        ret = global_init_hook();
        if (ret) {
            log_err("global_init_hook(): failed with %d", ret);
            return ret;
        }
    }

    for (i = 1; i < USED_CPUS; i++) {
        pthread_create(&proc->all_ks[i].ph, NULL, percpu_entry, (void *)i);
    }

    percpu_entry((void *)0);
}
