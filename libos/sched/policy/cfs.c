/*
 * cfs.c: CFS-like scheduler
 */

#include <skyloft/platform.h>
#include <skyloft/sched.h>
#include <skyloft/sched/policy/cfs.h>
#include <skyloft/sync.h>

#include <utils/log.h>

/*
 * Targeted preemption latency for CPU-bound tasks:
 * (default: 6ms * (1 + ilog(ncpus)), units: nanoseconds)
 */
__nsec sysctl_sched_latency = 50000ULL;
/*
 * Minimal preemption granularity for CPU-bound tasks:
 * (default: 0.75 msec * (1 + ilog(ncpus)), units: nanoseconds)
 */
__nsec sysctl_sched_min_granularity = 12500ULL;
/*
 * is kept at sysctl_sched_latency / sysctl_sched_min_granularity
 */
static unsigned int sched_nr_latency = 4;

DEFINE_PERCPU(struct cfs_rq *, percpu_rqs);

/* global counter for next CPU */
static atomic_int TARGET_CPU = 0;
extern __thread int g_logic_cpu_id;

static void __update_inv_weight(struct load_weight *lw)
{
    unsigned long w;

    if (likely(lw->inv_weight))
        return;

    w = lw->weight;

    if (unlikely(w >= WMULT_CONST))
        lw->inv_weight = 1;
    else if (unlikely(!w))
        lw->inv_weight = WMULT_CONST;
    else
        lw->inv_weight = WMULT_CONST / w;
}

static inline void update_load_add(struct load_weight *lw, unsigned long inc)
{
    lw->weight += inc;
    lw->inv_weight = 0;
}

static inline void update_load_sub(struct load_weight *lw, unsigned long dec)
{
    lw->weight -= dec;
    lw->inv_weight = 0;
}

static inline void update_load_set(struct load_weight *lw, unsigned long w)
{
    lw->weight = w;
    lw->inv_weight = 0;
}

static uint64_t __calc_delta(uint64_t delta_exec, unsigned long weight, struct load_weight *lw)
{
    uint64_t fact = weight;
    int shift = WMULT_SHIFT;

    __update_inv_weight(lw);

    if (unlikely(fact >> 32)) {
        while (fact >> 32) {
            fact >>= 1;
            shift--;
        }
    }

    /* hint to use a 32x32->64 mul */
    fact = (uint64_t)(uint32_t)fact * lw->inv_weight;

    while (fact >> 32) {
        fact >>= 1;
        shift--;
    }

    return mul_u64_u32_shr(delta_exec, fact, shift);
}

static inline uint64_t calc_delta_fair(uint64_t delta, struct cfs_task *task)
{
    if (unlikely(task->load.weight != NICE_0_LOAD))
        delta = __calc_delta(delta, NICE_0_LOAD, &task->load);

    return delta;
}

static inline uint64_t max_vruntime(uint64_t max_vruntime, uint64_t vruntime)
{
    int64_t delta = (int64_t)(vruntime - max_vruntime);
    if (delta > 0)
        max_vruntime = vruntime;

    return max_vruntime;
}

static inline uint64_t min_vruntime(uint64_t min_vruntime, uint64_t vruntime)
{
    int64_t delta = (int64_t)(vruntime - min_vruntime);
    if (delta < 0)
        min_vruntime = vruntime;

    return min_vruntime;
}

static void update_min_vruntime(struct cfs_rq *cfs_rq)
{
    struct cfs_task *curr = cfs_rq->curr, *task;
    struct rb_node *leftmost = rb_first_cached(&cfs_rq->tasks_timeline);
    uint64_t vruntime = cfs_rq->min_vruntime;

    if (curr) {
        if (curr->on_rq)
            vruntime = curr->vruntime;
        else
            curr = NULL;
    }

    if (leftmost) { /* non-empty tree */
        task = rb_entry(leftmost, struct cfs_task, run_node);

        if (!curr)
            vruntime = task->vruntime;
        else
            vruntime = min_vruntime(vruntime, task->vruntime);
    }

    /* ensure we never gain time by being placed backwards. */
    cfs_rq->min_vruntime = max_vruntime(cfs_rq->min_vruntime, vruntime);
}

static inline void update_curr(struct cfs_rq *cfs_rq)
{
    struct cfs_task *curr = cfs_rq->curr;
    __nsec now = now_ns();
    __nsec delta_exec;

    if (unlikely(!curr))
        return;

    delta_exec = now - curr->exec_start;
    if (unlikely((int64_t)delta_exec <= 0))
        return;

    curr->exec_start = now;
    curr->sum_exec_runtime += delta_exec;
    curr->vruntime += calc_delta_fair(delta_exec, curr);
    update_min_vruntime(cfs_rq);
}

/*
 * The idea is to set a period in which each task runs once.
 *
 * When there are too many tasks (sched_nr_latency) we have to stretch
 * this period because otherwise the slices get too small.
 *
 * p = (nr <= nl) ? l : l * nr / nl
 */
static uint64_t __sched_period(unsigned long nr_running)
{
    if (unlikely(nr_running > sched_nr_latency))
        return nr_running * sysctl_sched_min_granularity;
    else
        return sysctl_sched_latency;
}

/*
 * We calculate the wall-time slice from the period by taking a part
 * proportional to the weight.
 *
 * s = p * wi / sum(wi)
 */
static uint64_t sched_slice(struct cfs_rq *cfs_rq, struct cfs_task *task)
{
    uint64_t slice;
    struct load_weight *load;
    struct load_weight lw;

    slice = __sched_period(cfs_rq->nr_running + !task->on_rq);

    load = &cfs_rq->load;
    if (unlikely(!task->on_rq)) {
        lw = cfs_rq->load;
        update_load_add(&lw, task->load.weight);
        load = &lw;
    }

    return __calc_delta(slice, task->load.weight, load);
}

/*
 * We calculate the vruntime slice of a to-be-inserted task.
 *
 * vs = s * NICE_0 / w
 */
static uint64_t sched_vslice(struct cfs_rq *cfs_rq, struct cfs_task *task)
{
    return calc_delta_fair(sched_slice(cfs_rq, task), task);
}

static void place_task(struct cfs_rq *cfs_rq, struct cfs_task *task, bool init)
{
    uint64_t vruntime = cfs_rq->min_vruntime;

    /*
     * The 'current' period is already promised to the current tasks,
     * however the extra weight of the new task will slow them down a
     * little, place the new task so that it fits in the slot that
     * stays open at the end.
     */
    if (init)
        vruntime += sched_vslice(cfs_rq, task);

    /* sleeps up to a single latency don't count. */
    if (!init)
        /*
         * Halve their sleep time's effect, to allow
         * for a gentler effect of sleepers:
         */
        vruntime -= sysctl_sched_latency >> 1;

    /* ensure we never gain time by being placed backwards. */
    task->vruntime = max_vruntime(task->vruntime, vruntime);
}

static inline struct cfs_task *__pick_first_task(struct cfs_rq *cfs_rq)
{
    return rb_entry(rb_first_cached(&cfs_rq->tasks_timeline), struct cfs_task, run_node);
}

static inline void __dequeue_task(struct cfs_rq *cfs_rq, struct cfs_task *task)
{
    rb_erase_cached(&task->run_node, &cfs_rq->tasks_timeline);
}

static inline bool __vruntime_lt(struct rb_node *a, const struct rb_node *b)
{
    return (int64_t)(rb_entry(a, struct cfs_task, run_node)->vruntime -
                     rb_entry(b, struct cfs_task, run_node)->vruntime) < 0;
}

static inline void __enqueue_task(struct cfs_rq *cfs_rq, struct cfs_task *task)
{
    rb_add_cached(&task->run_node, &cfs_rq->tasks_timeline, &__vruntime_lt);
}

static inline void enqueue_update(struct cfs_rq *cfs_rq, struct cfs_task *task)
{
    update_load_add(&cfs_rq->load, task->load.weight);
    cfs_rq->nr_running++;
}

static void enqueue_task(struct cfs_rq *cfs_rq, struct cfs_task *task, bool wakeup)
{
    bool curr = (task == cfs_rq->curr);
    assert_spin_lock_held(&cfs_rq->lock);

    if (!wakeup && curr)
        task->vruntime += cfs_rq->min_vruntime;
    update_curr(cfs_rq);
    enqueue_update(cfs_rq, task);

    if (!wakeup && !curr)
        task->vruntime += cfs_rq->min_vruntime;

    /* compensate before waking up task  */
    if (wakeup)
        place_task(cfs_rq, task, false);

    if (!curr)
        __enqueue_task(cfs_rq, task);

    task->on_rq = true;
    log_debug("%s: rq=%p task=%p curr=%p nr=%d", __func__, cfs_rq, task, cfs_rq->curr,
              cfs_rq->nr_running);
}

static inline void dequeue_update(struct cfs_rq *cfs_rq, struct cfs_task *task)
{
    update_load_sub(&cfs_rq->load, task->load.weight);
    cfs_rq->nr_running--;
}

static void dequeue_task(struct cfs_rq *cfs_rq, struct cfs_task *task, bool sleep)
{
    assert_spin_lock_held(&cfs_rq->lock);

    update_curr(cfs_rq);
    dequeue_update(cfs_rq, task);

    if (task != cfs_rq->curr)
        __dequeue_task(cfs_rq, task);
    task->on_rq = false;

    /* normalize the entity after updating the min_vruntime */
    if (!sleep)
        task->vruntime -= cfs_rq->min_vruntime;

    update_min_vruntime(cfs_rq);
    log_debug("%s: %p %p %d", __func__, task, cfs_rq->curr, cfs_rq->nr_running);
}

int cfs_sched_init_task(struct task *t)
{
    struct cfs_task *task = cfs_task_of(t);
    memset(task, 0, sizeof(struct cfs_task));
    /* guarantee task always has weight */
    update_load_set(&task->load, NICE_0_LOAD);
    t->allow_preempt = true;
    return 0;
}

void cfs_sched_finish_task(struct task *t)
{
    struct cfs_rq *cfs_rq = this_rq();
    struct cfs_task *task = cfs_task_of(t);

    assert_local_irq_disabled();

    spin_lock(&cfs_rq->lock);
    dequeue_task(cfs_rq, task, false);
    cfs_rq->curr = NULL; /* never touch it again */
    spin_unlock(&cfs_rq->lock);
}

static inline void __put_task(struct cfs_rq *cfs_rq, struct cfs_task *task)
{
    spin_lock(&cfs_rq->lock);
    if (!task->on_rq)
        enqueue_task(cfs_rq, task, false);
    spin_unlock(&cfs_rq->lock);
}

static void __fork_task(struct cfs_rq *cfs_rq, struct cfs_task *task)
{
    struct cfs_task *curr = cfs_rq->curr;
    update_curr(cfs_rq);
    if (curr)
        task->vruntime = curr->vruntime;
    place_task(cfs_rq, task, true);
    task->vruntime -= cfs_rq->min_vruntime;
}

static inline int find_target_cpu(struct cfs_task *task, bool new_task)
{
    if (new_task) {
        return atomic_fetch_add(&TARGET_CPU, 1) % USED_CPUS;
    } else {
        return task->last_run;
    }
}

int cfs_sched_spawn(struct task *t, int cpu)
{
    struct cfs_task *task = cfs_task_of(t);
    struct cfs_rq *cfs_rq = cpu_rq(find_target_cpu(task, true));

    __fork_task(cfs_rq, task);
    __put_task(cfs_rq, task);

    return 0;
}

static void __set_next_task(struct cfs_rq *cfs_rq, struct cfs_task *task)
{
    if (task->on_rq)
        __dequeue_task(cfs_rq, task);
    task->exec_start = now_ns();
    task->last_run = g_logic_cpu_id;
    task->prev_sum_exec_runtime = task->sum_exec_runtime;
    cfs_rq->curr = task;
}

struct task *cfs_sched_pick_next()
{
    struct cfs_rq *rq = this_rq();
    struct cfs_task *task;

    assert_spin_lock_held(&rq->lock);

    if (!rq->nr_running)
        return NULL;

    task = __pick_first_task(rq);
    __set_next_task(rq, task);

    return task_of(task);
}

void cfs_sched_yield()
{
    struct cfs_rq *rq = this_rq();
    struct cfs_task *prev = rq->curr;
    assert(this_rq()->curr == cfs_task_of(task_self()));

    spin_lock(&rq->lock);
    if (prev->on_rq) {
        update_curr(rq);
        __enqueue_task(rq, prev);
    }
    rq->curr = NULL;
    spin_unlock(&rq->lock);
}

void cfs_sched_wakeup(struct task *t)
{
    struct cfs_task *task = cfs_task_of(t);
    struct cfs_rq *rq = cpu_rq(find_target_cpu(task, false));

    spin_lock(&rq->lock);
    if (!task->on_rq)
        enqueue_task(rq, task, true);
    spin_unlock(&rq->lock);
}

void cfs_sched_block()
{
    struct cfs_rq *rq = this_rq();

    assert(rq->curr->on_rq);

    spin_lock(&rq->lock);
    dequeue_task(rq, rq->curr, true);
    rq->curr = NULL;
    spin_unlock(&rq->lock);
}

static bool check_preempt_tick(struct cfs_rq *cfs_rq, struct cfs_task *curr)
{
    struct cfs_task *first;
    __nsec delta_exec, ideal_runtime;
    int64_t delta;

    ideal_runtime = sched_slice(cfs_rq, curr);
    delta_exec = curr->sum_exec_runtime - curr->prev_sum_exec_runtime;
    if (delta_exec > ideal_runtime)
        return true;
    if (delta_exec < sysctl_sched_min_granularity)
        return false;

    first = __pick_first_task(cfs_rq);
    delta = curr->vruntime - first->vruntime;
    if (delta < 0)
        return false;
    if ((uint64_t)delta > ideal_runtime)
        return true;

    return false;
}

bool cfs_sched_preempt()
{
    bool resched = false;
    struct cfs_rq *cfs_rq = this_rq();

    assert_local_irq_disabled();

    spin_lock(&cfs_rq->lock);
    update_curr(cfs_rq);
    if (cfs_rq->nr_running > 1)
        resched = check_preempt_tick(cfs_rq, cfs_rq->curr);
    spin_unlock(&cfs_rq->lock);

    log_debug("%s return %d", __func__, resched);

    return resched;
}
