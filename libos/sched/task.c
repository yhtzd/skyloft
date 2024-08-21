#include "skyloft/sched.h"
#include <errno.h>

#include <skyloft/mm.h>
#include <skyloft/params.h>
#include <skyloft/percpu.h>
#include <skyloft/sched/ops.h>
#include <skyloft/task.h>

#include <utils/assert.h>
#include <utils/atomic.h>
#include <utils/defs.h>
#include <utils/log.h>
#include <utils/spinlock.h>

atomic_int task_count;
static atomic_int task_id_allocator;

static __always_inline void __task_init(struct task *t, struct stack *s)
{
    extern int g_app_id;
    t->app_id = g_app_id;
    t->stack = s;
    t->stack_busy = false;
    t->state = TASK_RUNNABLE;
    t->allow_preempt = false;
    t->skip_free = false;
#if DEBUG
    t->id = atomic_inc(&task_id_allocator);
#endif
    __sched_init_task(t);
}

#ifdef SCHED_PERCPU

/* per-CPU task allocator */

static struct slab thread_slab;
static struct tcache *task_tcache;
static DEFINE_PERCPU(struct tcache_percpu, task_percpu);

static __always_inline struct task *__task_create(bool _idle)
{
    struct task *t;
    struct stack *s;

    preempt_disable();
    t = tcache_alloc(&percpu_get(task_percpu));
    if (unlikely(!t)) {
        preempt_enable();
        return NULL;
    }

    s = stack_alloc();
    if (unlikely(!s)) {
        tcache_free(&percpu_get(task_percpu), t);
        preempt_enable();
        return NULL;
    }
    preempt_enable();

    __task_init(t, s);

    log_debug("%s %p %p %d", __func__, t, t->stack, t->id);

    return t;
}

static __always_inline void __task_free(struct task *t)
{
    stack_free(t->stack);
    tcache_free(&percpu_get(task_percpu), t);
}

static __always_inline int __task_alloc_init_percpu()
{
    tcache_init_percpu(task_tcache, &percpu_get(task_percpu));
    stack_init_percpu();

    return 0;
}

static __always_inline int __task_alloc_init(void *base)
{
    int ret;

    if ((ret = stack_init()) < 0) {
        log_err("sched_task_init: failed to init stack");
        return ret;
    }

    ret = slab_create(&thread_slab, "runtime_threads", sizeof(struct task), 0);
    if (ret)
        return ret;

    task_tcache = slab_create_tcache(&thread_slab, TCACHE_DEFAULT_MAG_SIZE);
    if (!task_tcache) {
        log_err("sched_task_init: failed to create task tcache");
        slab_destroy(&thread_slab);
        return -ENOMEM;
    }

    return 0;
}

#else

/* centralized task allocator */

struct task_cache {
    atomic_int head, tail;
    struct task *task[MAX_TASKS_PER_APP];
    struct stack *stack[MAX_TASKS_PER_APP];
} __aligned_cacheline;
BUILD_ASSERT(is_power_of_two(MAX_TASKS_PER_APP));
#define TASK_CACHE_MASK (MAX_TASKS_PER_APP - 1)

static struct task_cache task_cache;

static __always_inline struct task *__task_create(bool idle)
{
    struct task *t;
    struct stack *s;
    int i;

    assert_local_irq_disabled();
    BUG_ON(task_cache.head < task_cache.tail);

    i = atomic_inc(&task_cache.head) & TASK_CACHE_MASK;
    t = task_cache.task[i];
    s = task_cache.stack[i];

    __task_init(t, s);

    log_debug("%s %p %p %d", __func__, t, t->stack, t->id);

    return t;
}

static __always_inline void __task_free(struct task *t)
{
    int i = atomic_inc(&task_cache.tail) & TASK_CACHE_MASK;
    task_cache.stack[i] = t->stack;
    task_cache.task[i] = t;
}

static __always_inline int __task_alloc_init(void *base)
{
    int i;
    void *stack_base = base + MAX_TASKS_PER_APP * sizeof(struct task);

    for (i = 0; i < MAX_TASKS_PER_APP; i++) {
        task_cache.task[i] = base + i * sizeof(struct task);
        task_cache.stack[i] = stack_base + i * sizeof(struct stack);
    }

    return 0;
}

static __always_inline int __task_alloc_init_percpu() { return 0; }

#endif

struct task *task_create(thread_fn_t fn, void *arg)
{
    uint64_t *rsp;
    struct task *task;
    struct switch_frame *frame;

    task = __task_create(false);
    if (unlikely(!task))
        return NULL;

    rsp = (uint64_t *)stack_top(task->stack);
    *--rsp = (uint64_t)task_exit;
    frame = (struct switch_frame *)rsp - 1;
    frame->rip = (uint64_t)fn;
    frame->rdi = (uint64_t)arg;
    frame->rbp = 0;
    task->rsp = (uint64_t)frame;

    return task;
}

struct task *task_create_with_buf(thread_fn_t fn, void **buf, size_t buf_len)
{

    uint64_t rsp, *ptr;
    struct switch_frame *frame;

    struct task *task = __task_create(false);
    if (unlikely(!task))
        return NULL;

    rsp = stack_top(task->stack);
    rsp -= buf_len;
    rsp = align_down(rsp, RSP_ALIGNMENT);
    *buf = (void *)rsp;
    ptr = (uint64_t *)rsp;
    *--ptr = (uint64_t)task_exit;
    frame = (struct switch_frame *)ptr - 1;
    frame->rip = (uint64_t)fn;
    frame->rdi = (uint64_t)*buf;
    frame->rbp = 0;
    task->rsp = (uint64_t)frame;

    return task;
}

struct task *task_create_idle()
{
    struct task *task;

    task = __task_create(true);
    if (unlikely(!task))
        return NULL;

    task->state = TASK_IDLE;
    task->rsp = (uint64_t)stack_top(task->stack) - 8;
    atomic_inc(&task_count);
    return task;
}

void task_free(struct task *t)
{
    __task_free(t);
    atomic_dec(&task_count);

    log_debug("%s %p %p %d", __func__, t, t->stack, t->id);
}

int sched_task_init_percpu() { return __task_alloc_init_percpu(); }

int sched_task_init(void *base)
{
    int ret;

    if ((ret = __task_alloc_init(base)) < 0)
        return ret;

    atomic_init(&task_id_allocator, 0);

    return 0;
}
