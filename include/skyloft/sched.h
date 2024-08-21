#pragma once

#include <net/mbuf.h>
#include <skyloft/global.h>
#include <skyloft/params.h>
#include <skyloft/platform.h>
#include <skyloft/stat.h>

#include <utils/defs.h>
#include <utils/kref.h>
#include <utils/lrpc.h>
#include <utils/shm.h>
#include <utils/time.h>

struct kthread;
struct task;
struct proc;

__noreturn void start_schedule(void);
__noreturn __noinline void schedule();

/* init handlers */
int sched_init_percpu(void);
int sched_init(void);
int proc_init(void);

/* softirq APIs */
struct task *softirq_task(struct kthread *k, int budget);
bool softirq_run(int budget);

/* task APIs */
int task_spawn(int cpu_id, void (*fn)(void *arg), void *arg, int stack_size);
int task_enqueue(int cpu_id, struct task *task);
void task_yield();
void task_wakeup(struct task *);
void task_block(spinlock_t *lock);
__noreturn void task_exit(void *code);

/* assembly helper routines from switch.S */
extern void __context_switch(uint64_t *prev_stack, uint64_t next_stack, uint8_t *prev_stack_busy);
extern void __context_switch_to_idle(uint64_t *prev_stack, uint64_t idle_stack);
extern void __context_switch_from_idle(uint64_t next_stack);
extern void __context_switch_to_fn_nosave(void (*fn)(void), uint64_t idle_stack);

struct kthread {
    /* 1st cacheline */
    spinlock_t lock;
    /* pthread handle */
    pthread_t ph;
    pid_t tid;
    /* app id */
    int app;
    /* logic cpu id */
    int cpu;
    /* numa node id */
    int node;
    /* kernel thread states */
    bool parked;
    /* RCU generation number */
    uint8_t pad0[28];

    /* 2nd cacheline */
    struct mbufq txpktq_overflow;
    struct mbufq txcmdq_overflow;
    uint8_t pad1[32];

    /* 3rd cacheline */
    /* kernel thread local timer */
    spinlock_t timer_lock;
    int32_t nr_timers;
    struct timer_idx *timers;
    uint8_t pad2[48];

    /* 4th-6th cacheline */
    /* per-CPU communication channel */
    struct lrpc_chan rxq;
    struct lrpc_chan txpktq;
    struct lrpc_chan txcmdq;

    /* 7th cacheline */
    /* statistics counters */
    uint64_t stats[STAT_NR];
} __aligned_cacheline;

BUILD_ASSERT(offsetof(struct kthread, txpktq_overflow) == 64);
BUILD_ASSERT(offsetof(struct kthread, timer_lock) == 128);
BUILD_ASSERT(offsetof(struct kthread, rxq) == 192);
BUILD_ASSERT(offsetof(struct kthread, stats) == 192 + 64 * 6);

struct proc {
    /* global application identification */
    int id;
    pid_t pid;
    int nr_ks;

    /* boot time (us) */
    __nsec boot_time;

    /* process states */
    volatile bool ready;
    volatile bool exited;

    /* kernel threads bound to CPU */
    struct kthread all_ks[USED_CPUS];
    struct kthread *active_ks[USED_CPUS];
    atomic_int nr_active;
    uint32_t next_rr;

    /* overflow queue for completion data */
    uint32_t max_overflows;
    uint32_t nr_overflows;
    uint64_t *overflow_queue; /* a pionter to daemon space */
} __aligned_cacheline;

extern struct proc *proc;
extern __thread struct kthread *localk;
extern __thread volatile unsigned int preempt_cnt;

/**
 * task_self - current running task
 */
static __always_inline struct task *task_self()
{
    extern __thread struct task *__curr;
    assert(__curr != NULL);
    return __curr;
}

/**
 * preempt_disable - disables preemption
 *
 * Can be nested.
 */
static __always_inline void preempt_disable(void) { preempt_cnt++; }

/**
 * preempt_enable - reenables preemption
 *
 * Can be nested.
 */
static __always_inline void preempt_enable(void) { preempt_cnt--; }

/**
 * preempt_enabled - returns true if preemption is enabled
 */
static __always_inline __attribute__((target("general-regs-only"))) bool preempt_enabled(void)
{
    return !preempt_cnt;
}

#define assert_preempt_disabled()   assert(!(local_irq_enabled() && preempt_enabled()))
#define assert_local_irq_disabled() assert(!local_irq_enabled())

/**
 * thisk - returns the per-kernel-thread data
 */
static __always_inline __attribute__((target("general-regs-only"))) struct kthread *thisk(void)
{
    return localk;
}

/**
 * cpuk - returns the per-kernel-thread data of the cpu
 */
static inline struct kthread *cpuk(int cpu_id) { return &proc->all_ks[cpu_id]; }

/**
 * getk - returns the per-kernel-thread data and disables preemption
 *
 * WARNING: If you're using myk() instead of getk(), that's a bug if preemption
 * is enabled. The local kthread can change at anytime.
 */
static __always_inline struct kthread *getk(void)
{
    preempt_disable();
    return localk;
}

/**
 * putk - reenables preemption after calling getk()
 */
static __always_inline void putk(void) { preempt_enable(); }

static inline int current_app_id() { return proc->id; }

static inline int current_cpu_id() { return thisk()->cpu; }

static inline struct proc *current_app() { return &shm_apps[current_app_id()]; }

static inline bool is_daemon() { return current_app_id() == DAEMON_APP_ID; }

static inline int current_numa_node() { return thisk()->node; }
