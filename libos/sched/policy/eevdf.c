/*
 * eevdf.c: EEVDF-like scheduler
 */

#include <skyloft/platform.h>
#include <skyloft/sched.h>
#include <skyloft/sched/policy/eevdf.h>
#include <skyloft/sync.h>

#include <utils/log.h>
#include <utils/minmax.h>

#define __node_2_task(node) rb_entry((node), struct eevdf_task, run_node)
#define scale_load(w)       ((w) << SCHED_FIXEDPOINT_SHIFT)
#define scale_load_down(w)                                 \
    ({                                                     \
        unsigned long __w = (w);                           \
                                                           \
        if (__w)                                           \
            __w = max(2UL, __w >> SCHED_FIXEDPOINT_SHIFT); \
        __w;                                               \
    })

#ifdef CONFIG_SCHED_DEBUG
#define SCHED_WARN_ON(x) WARN_ONCE(x, #x)
#else
#define SCHED_WARN_ON(x) ({ (void)(x), 0; })
#endif

/* TICK_NSEC is the time between ticks in nsec assuming SHIFTED_HZ */
#define TICK_NSEC ((NSEC_PER_SEC + TIMER_HZ / 2) / TIMER_HZ)

/*
 * Minimal preemption granularity for CPU-bound tasks:
 * (default: 0.75 msec * (1 + ilog(ncpus)), units: nanoseconds)
 */
__nsec sysctl_sched_base_slice = 12500ULL;

DEFINE_PERCPU(struct eevdf_rq *, percpu_rqs);

/* global counter for next CPU */
static atomic_int TARGET_CPU = 0;
extern __thread int g_logic_cpu_id;

static bool update_deadline(struct eevdf_rq *eevdf_rq, struct eevdf_task *task);
static inline struct eevdf_task *__pick_root_task(struct eevdf_rq *eevdf_rq);
static inline struct eevdf_task *__pick_first_task(struct eevdf_rq *eevdf_rq);

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

/*
 * delta /= w
 */
static inline uint64_t calc_delta_fair(uint64_t delta, struct eevdf_task *task)
{
    if (unlikely(task->load.weight != NICE_0_LOAD))
        delta = __calc_delta(delta, NICE_0_LOAD, &task->load);

    return delta;
}

static inline uint64_t min_vruntime(uint64_t min_vruntime, uint64_t vruntime)
{
    int64_t delta = (int64_t)(vruntime - min_vruntime);
    if (delta < 0)
        min_vruntime = vruntime;

    return min_vruntime;
}

static inline bool task_before(const struct eevdf_task *a, const struct eevdf_task *b)
{
    /*
     * Tiebreak on vruntime seems unnecessary since it can
     * hardly happen.
     */
    return (int64_t)(a->deadline - b->deadline) < 0;
}

static inline int64_t task_key(struct eevdf_rq *eevdf_rq, struct eevdf_task *task)
{
    return (int64_t)(task->vruntime - eevdf_rq->min_vruntime);
}
/*
 * Compute virtual time from the per-task service numbers:
 *
 * Fair schedulers conserve lag:
 *
 *   \Sum lag_i = 0
 *
 * Where lag_i is given by:
 *
 *   lag_i = S - s_i = w_i * (V - v_i)
 *
 * Where S is the ideal service time and V is it's virtual time counterpart.
 * Therefore:
 *
 *   \Sum lag_i = 0
 *   \Sum w_i * (V - v_i) = 0
 *   \Sum w_i * V - w_i * v_i = 0
 *
 * From which we can solve an expression for V in v_i (which we have in
 * task->vruntime):
 *
 *       \Sum v_i * w_i   \Sum v_i * w_i
 *   V = -------------- = --------------
 *          \Sum w_i            W
 *
 * Specifically, this is the weighted average of all task virtual runtimes.
 *
 * [[ NOTE: this is only equal to the ideal scheduler under the condition
 *          that join/leave operations happen at lag_i = 0, otherwise the
 *          virtual time has non-contiguous motion equivalent to:
 *
 *	      V +-= lag_i / W
 *
 *	    Also see the comment in place_task() that deals with this. ]]
 *
 * However, since v_i is uint64_t, and the multiplication could easily overflow
 * transform it into a relative form that uses smaller quantities:
 *
 * Substitute: v_i == (v_i - v0) + v0
 *
 *     \Sum ((v_i - v0) + v0) * w_i   \Sum (v_i - v0) * w_i
 * V = ---------------------------- = --------------------- + v0
 *                  W                            W
 *
 * Which we track using:
 *
 *                    v0 := eevdf_rq->min_vruntime
 * \Sum (v_i - v0) * w_i := eevdf_rq->avg_vruntime
 *              \Sum w_i := eevdf_rq->avg_load
 *
 * Since min_vruntime is a monotonic increasing variable that closely tracks
 * the per-task service, these deltas: (v_i - v), will be in the order of the
 * maximal (virtual) lag induced in the system due to quantisation.
 *
 * Also, we use scale_load_down() to reduce the size.
 *
 * As measured, the max (key * weight) value was ~44 bits for a kernel build.
 */
static void avg_vruntime_add(struct eevdf_rq *eevdf_rq, struct eevdf_task *task)
{
    unsigned long weight = scale_load_down(task->load.weight);
    int64_t key = task_key(eevdf_rq, task);

    eevdf_rq->avg_vruntime += key * weight;
    eevdf_rq->avg_load += weight;
}

static void avg_vruntime_sub(struct eevdf_rq *eevdf_rq, struct eevdf_task *task)
{
    unsigned long weight = scale_load_down(task->load.weight);
    int64_t key = task_key(eevdf_rq, task);

    eevdf_rq->avg_vruntime -= key * weight;
    eevdf_rq->avg_load -= weight;
}

static inline void avg_vruntime_update(struct eevdf_rq *eevdf_rq, int64_t delta)
{
    /*
     * v' = v + d ==> avg_vruntime' = avg_runtime - d*avg_load
     */
    eevdf_rq->avg_vruntime -= eevdf_rq->avg_load * delta;
}

/*
 * Specifically: avg_runtime() + 0 must result in task_eligible() := true
 * For this to be so, the result of this function must have a left bias.
 */
uint64_t avg_vruntime(struct eevdf_rq *eevdf_rq)
{
    struct eevdf_task *curr = eevdf_rq->curr;
    int64_t avg = eevdf_rq->avg_vruntime;
    long load = eevdf_rq->avg_load;

    if (curr && curr->on_rq) {
        unsigned long weight = scale_load_down(curr->load.weight);

        avg += task_key(eevdf_rq, curr) * weight;
        load += weight;
    }

    if (load) {
        /* sign flips effective floor / ceiling */
        if (avg < 0)
            avg -= (load - 1);
        avg = div_s64(avg, load);
    }

    return eevdf_rq->min_vruntime + avg;
}

/*
 * lag_i = S - s_i = w_i * (V - v_i)
 *
 * However, since V is approximated by the weighted average of all entities it
 * is possible -- by addition/removal/reweight to the tree -- to move V around
 * and end up with a larger lag than we started with.
 *
 * Limit this to either double the slice length with a minimum of TICK_NSEC
 * since that is the timing granularity.
 *
 * EEVDF gives the following limit for a steady state system:
 *
 *   -r_max < lag < max(r_max, q)
 *
 * XXX could add max_slice to the augmented data to track this.
 */
static int64_t task_lag(uint64_t avruntime, struct eevdf_task *task)
{
    int64_t vlag, limit;

    vlag = avruntime - task->vruntime;
    limit = calc_delta_fair(max_t(uint64_t, 2 * task->slice, TICK_NSEC), task);

    return clamp(vlag, -limit, limit);
}

static void update_task_lag(struct eevdf_rq *eevdf_rq, struct eevdf_task *task)
{
    SCHED_WARN_ON(!task->on_rq);

    task->vlag = task_lag(avg_vruntime(eevdf_rq), task);
}

/*
 * Task is eligible once it received less service than it ought to have,
 * eg. lag >= 0.
 *
 * lag_i = S - s_i = w_i*(V - v_i)
 *
 * lag_i >= 0 -> V >= v_i
 *
 *     \Sum (v_i - v)*w_i
 * V = ------------------ + v
 *          \Sum w_i
 *
 * lag_i >= 0 -> \Sum (v_i - v)*w_i >= (v_i - v)*(\Sum w_i)
 *
 * Note: using 'avg_vruntime() > task->vruntime' is inaccurate due
 *       to the loss in precision caused by the division.
 */
static int vruntime_eligible(struct eevdf_rq *eevdf_rq, uint64_t vruntime)
{
    struct eevdf_task *curr = eevdf_rq->curr;
    int64_t avg = eevdf_rq->avg_vruntime;
    long load = eevdf_rq->avg_load;

    if (curr && curr->on_rq) {
        unsigned long weight = scale_load_down(curr->load.weight);

        avg += task_key(eevdf_rq, curr) * weight;
        load += weight;
    }

    return avg >= (int64_t)(vruntime - eevdf_rq->min_vruntime) * load;
}

static inline int task_eligible(struct eevdf_rq *eevdf_rq, struct eevdf_task *task)
{
    return vruntime_eligible(eevdf_rq, task->vruntime);
}

static uint64_t __update_min_vruntime(struct eevdf_rq *eevdf_rq, uint64_t vruntime)
{
    uint64_t min_vruntime = eevdf_rq->min_vruntime;
    /*
     * open coded max_vruntime() to allow updating avg_vruntime
     */
    int64_t delta = (int64_t)(vruntime - min_vruntime);
    if (delta > 0) {
        avg_vruntime_update(eevdf_rq, delta);
        min_vruntime = vruntime;
    }
    return min_vruntime;
}

static void update_min_vruntime(struct eevdf_rq *eevdf_rq)
{
    struct eevdf_task *task = __pick_root_task(eevdf_rq);
    struct eevdf_task *curr = eevdf_rq->curr;
    uint64_t vruntime = eevdf_rq->min_vruntime;

    if (curr) {
        if (curr->on_rq)
            vruntime = curr->vruntime;
        else
            curr = NULL;
    }

    if (task) {
        if (!curr)
            vruntime = task->min_vruntime;
        else
            vruntime = min_vruntime(vruntime, task->min_vruntime);
    }

    /* ensure we never gain time by being placed backwards. */
    eevdf_rq->min_vruntime = __update_min_vruntime(eevdf_rq, vruntime);
}

#define vruntime_gt(field, lse, rse) ({ (int64_t)((lse)->field - (rse)->field) > 0; })

static inline void __min_vruntime_update(struct eevdf_task *task, struct rb_node *node)
{
    if (node) {
        struct eevdf_task *rse = __node_2_task(node);
        if (vruntime_gt(min_vruntime, task, rse))
            task->min_vruntime = rse->min_vruntime;
    }
}

/*
 * task->min_vruntime = min(task->vruntime, {left,right}->min_vruntime)
 */
static inline bool min_vruntime_update(struct eevdf_task *task, bool exit)
{
    uint64_t old_min_vruntime = task->min_vruntime;
    struct rb_node *node = &task->run_node;

    task->min_vruntime = task->vruntime;
    __min_vruntime_update(task, node->rb_right);
    __min_vruntime_update(task, node->rb_left);

    return task->min_vruntime == old_min_vruntime;
}

RB_DECLARE_CALLBACKS(static, min_vruntime_cb, struct eevdf_task, run_node, min_vruntime,
                     min_vruntime_update);

static inline bool update_curr(struct eevdf_rq *eevdf_rq)
{
    struct eevdf_task *curr = eevdf_rq->curr;
    __nsec now = now_ns();
    int64_t delta_exec;
    bool resched = false;

    if (unlikely(!curr))
        return resched;

    delta_exec = now - curr->exec_start;
    if (unlikely(delta_exec <= 0))
        return resched;

    curr->exec_start = now;
    curr->sum_exec_runtime += delta_exec;
    curr->vruntime += calc_delta_fair(delta_exec, curr);
    resched = update_deadline(eevdf_rq, curr);
    update_min_vruntime(eevdf_rq);
    return resched;
}

static bool update_deadline(struct eevdf_rq *eevdf_rq, struct eevdf_task *task)
{
    if ((int64_t)(task->vruntime - task->deadline) < 0)
        return false;

    /*
     * For EEVDF the virtual time slope is determined by w_i (iow.
     * nice) while the request time r_i is determined by
     * sysctl_sched_base_slice.
     */
    task->slice = sysctl_sched_base_slice;

    /*
     * EEVDF: vd_i = ve_i + r_i / w_i
     */
    task->deadline = task->vruntime + calc_delta_fair(task->slice, task);

    /*
     * The task has consumed its request, reschedule.
     */
    if (eevdf_rq->nr_running > 1) {
        return true;
        //     resched_curr(rq_of(eevdf_rq));
        //     clear_buddies(eevdf_rq, task);
    }

    return false;
}

static void place_task(struct eevdf_rq *eevdf_rq, struct eevdf_task *task, bool init)
{
    uint64_t vslice, vruntime = avg_vruntime(eevdf_rq);
    int64_t lag = 0;

    task->slice = sysctl_sched_base_slice;
    vslice = calc_delta_fair(task->slice, task);

    // SCHED_FEAT(PLACE_LAG, true)
    /*
     * Due to how V is constructed as the weighted average of entities,
     * adding tasks with positive lag, or removing tasks with negative lag
     * will move 'time' backwards, this can screw around with the lag of
     * other tasks.
     *
     * EEVDF: placement strategy #1 / #2
     */
    if (eevdf_rq->nr_running) {
        struct eevdf_task *curr = eevdf_rq->curr;
        unsigned long load;

        lag = task->vlag;

        /*
         * If we want to place a task and preserve lag, we have to
         * consider the effect of the new task on the weighted
         * average and compensate for this, otherwise lag can quickly
         * evaporate.
         *
         * Lag is defined as:
         *
         *   lag_i = S - s_i = w_i * (V - v_i)
         *
         * To avoid the 'w_i' term all over the place, we only track
         * the virtual lag:
         *
         *   vl_i = V - v_i <=> v_i = V - vl_i
         *
         * And we take V to be the weighted average of all v:
         *
         *   V = (\Sum w_j*v_j) / W
         *
         * Where W is: \Sum w_j
         *
         * Then, the weighted average after adding an task with lag
         * vl_i is given by:
         *
         *   V' = (\Sum w_j*v_j + w_i*v_i) / (W + w_i)
         *      = (W*V + w_i*(V - vl_i)) / (W + w_i)
         *      = (W*V + w_i*V - w_i*vl_i) / (W + w_i)
         *      = (V*(W + w_i) - w_i*l) / (W + w_i)
         *      = V - w_i*vl_i / (W + w_i)
         *
         * And the actual lag after adding an task with vl_i is:
         *
         *   vl'_i = V' - v_i
         *         = V - w_i*vl_i / (W + w_i) - (V - vl_i)
         *         = vl_i - w_i*vl_i / (W + w_i)
         *
         * Which is strictly less than vl_i. So in order to preserve lag
         * we should inflate the lag before placement such that the
         * effective lag after placement comes out right.
         *
         * As such, invert the above relation for vl'_i to get the vl_i
         * we need to use such that the lag after placement is the lag
         * we computed before dequeue.
         *
         *   vl'_i = vl_i - w_i*vl_i / (W + w_i)
         *         = ((W + w_i)*vl_i - w_i*vl_i) / (W + w_i)
         *
         *   (W + w_i)*vl'_i = (W + w_i)*vl_i - w_i*vl_i
         *                   = W*vl_i
         *
         *   vl_i = (W + w_i)*vl'_i / W
         */
        load = eevdf_rq->avg_load;
        if (curr && curr->on_rq)
            load += scale_load_down(curr->load.weight);

        lag *= load + scale_load_down(task->load.weight);
        if (WARN_ON_ONCE(!load))
            load = 1;
        lag = div_s64(lag, load);
    }

    task->vruntime = vruntime - lag;

    // SCHED_FEAT(PLACE_LAG, true)
    /*
     * When joining the competition; the existing tasks will be,
     * on average, halfway through their slice, as such start tasks
     * off with half a slice to ease into the competition.
     */
    if (init)
        vslice /= 2;

    /*
     * EEVDF: vd_i = ve_i + r_i/w_i
     */
    task->deadline = task->vruntime + vslice;
    log_debug("%s: rq: %p, task: %p, init: %d, slice: %lu, vrt: %lu, ddl: %lu", __func__, eevdf_rq,
              task, init, task->slice, task->vruntime, task->deadline);
}

static inline struct eevdf_task *__pick_root_task(struct eevdf_rq *eevdf_rq)
{
    struct rb_node *root = eevdf_rq->tasks_timeline.rb_root.rb_node;

    if (!root)
        return NULL;

    return __node_2_task(root);
}

static inline struct eevdf_task *__pick_first_task(struct eevdf_rq *eevdf_rq)
{
    struct rb_node *left = rb_first_cached(&eevdf_rq->tasks_timeline);

    if (!left)
        return NULL;

    return __node_2_task(left);
}

static inline void __dequeue_task(struct eevdf_rq *eevdf_rq, struct eevdf_task *task)
{
    rb_erase_augmented_cached(&task->run_node, &eevdf_rq->tasks_timeline, &min_vruntime_cb);
    avg_vruntime_sub(eevdf_rq, task);
}

static inline bool __deadline_less(struct rb_node *a, const struct rb_node *b)
{
    return (int64_t)(rb_entry(a, struct eevdf_task, run_node)->deadline -
                     rb_entry(b, struct eevdf_task, run_node)->deadline) < 0;
}

/*
 * Enqueue an task into the rb-tree:
 */
static inline void __enqueue_task(struct eevdf_rq *eevdf_rq, struct eevdf_task *task)
{
    avg_vruntime_add(eevdf_rq, task);
    task->min_vruntime = task->vruntime;
    rb_add_augmented_cached(&task->run_node, &eevdf_rq->tasks_timeline, __deadline_less,
                            &min_vruntime_cb);
}

static void __set_next_task(struct eevdf_rq *eevdf_rq, struct eevdf_task *task)
{
    if (task->on_rq) {
        __dequeue_task(eevdf_rq, task);
        /*
         * HACK, stash a copy of deadline at the point of pick in vlag,
         * which isn't used until dequeue.
         */
        task->vlag = task->deadline;
    }
    task->exec_start = now_ns();
    task->last_run = g_logic_cpu_id;
    task->prev_sum_exec_runtime = task->sum_exec_runtime;
    eevdf_rq->curr = task;
}

static inline void enqueue_update(struct eevdf_rq *eevdf_rq, struct eevdf_task *task)
{
    update_load_add(&eevdf_rq->load, task->load.weight);
    eevdf_rq->nr_running++;
}

static void enqueue_task(struct eevdf_rq *eevdf_rq, struct eevdf_task *task)
{
    // log_debug("%s enter: rq=%p task=%p", __func__, eevdf_rq, task);
    bool curr = (task == eevdf_rq->curr);
    assert_spin_lock_held(&eevdf_rq->lock);

    if (curr)
        place_task(eevdf_rq, task, 0);
    update_curr(eevdf_rq);

    /*
     * XXX now that the entity has been re-weighted, and it's lag adjusted,
     * we can place the entity.
     */
    if (!curr)
        place_task(eevdf_rq, task, 0);
    enqueue_update(eevdf_rq, task);

    if (!curr)
        __enqueue_task(eevdf_rq, task);

    task->on_rq = true;
    log_debug("%s exit: rq=%p task=%p curr=%p nr=%d\n", __func__, eevdf_rq, task, eevdf_rq->curr,
              eevdf_rq->nr_running);
}

static inline void dequeue_update(struct eevdf_rq *eevdf_rq, struct eevdf_task *task)
{
    update_load_sub(&eevdf_rq->load, task->load.weight);
    eevdf_rq->nr_running--;
}

static void dequeue_task(struct eevdf_rq *eevdf_rq, struct eevdf_task *task)
{
    assert_spin_lock_held(&eevdf_rq->lock);

    update_curr(eevdf_rq);

    update_task_lag(eevdf_rq, task);
    if (task != eevdf_rq->curr)
        __dequeue_task(eevdf_rq, task);
    task->on_rq = false;
    dequeue_update(eevdf_rq, task);

    update_min_vruntime(eevdf_rq);
    log_debug("%s: task: %p, curr: %p, nr_run: %d", __func__, task, eevdf_rq->curr,
              eevdf_rq->nr_running);
}

int eevdf_sched_init_task(struct task *t)
{
    // log_debug("%s: %p", __func__, t);
    struct eevdf_task *task = eevdf_task_of(t);
    memset(task, 0, sizeof(struct eevdf_task));
    task->slice = sysctl_sched_base_slice;
    /* guarantee task always has weight */
    update_load_set(&task->load, NICE_0_LOAD);
    t->allow_preempt = true;
    log_debug("%s: return\n", __func__);
    return 0;
}

void eevdf_sched_finish_task(struct task *t)
{
    // log_debug("%s: %p\n", __func__, t);
    struct eevdf_rq *eevdf_rq = this_rq();
    struct eevdf_task *task = eevdf_task_of(t);

    assert_local_irq_disabled();

    spin_lock(&eevdf_rq->lock);
    dequeue_task(eevdf_rq, task);
    eevdf_rq->curr = NULL; /* never touch it again */
    spin_unlock(&eevdf_rq->lock);
    log_debug("%s: exit\n", __func__);
}

static inline void __put_task(struct eevdf_rq *eevdf_rq, struct eevdf_task *task)
{
    log_debug("%s: rq: %p, task: %p", __func__, eevdf_rq, task);
    spin_lock(&eevdf_rq->lock);
    if (!task->on_rq)
        enqueue_task(eevdf_rq, task);
    spin_unlock(&eevdf_rq->lock);
    // log_debug("%s: exit, rq: %p, task: %p\n", __func__, eevdf_rq, task);
}

static void __fork_task(struct eevdf_rq *eevdf_rq, struct eevdf_task *task)
{
    struct eevdf_task *curr = eevdf_rq->curr;
    if (curr)
        update_curr(eevdf_rq);
    place_task(eevdf_rq, task, true);
}

static inline int find_target_cpu(struct eevdf_task *task, bool new_task)
{
    if (new_task) {
        return atomic_fetch_add(&TARGET_CPU, 1) % USED_CPUS;
    } else {
        return task->last_run;
    }
}

int eevdf_sched_spawn(struct task *t, int cpu)
{
    struct eevdf_task *task = eevdf_task_of(t);
    int target_cpu = find_target_cpu(task, true);
    struct eevdf_rq *eevdf_rq = cpu_rq(target_cpu);
    log_debug("%s: t: %p, cpu: %d, target_cpu: %d", __func__, t, cpu, target_cpu);

    __fork_task(eevdf_rq, task);
    __put_task(eevdf_rq, task);
    // log_debug("%s: exit, t: %p, cpu: %d\n", __func__, t, cpu);

    return 0;
}

/*
 * Earliest Eligible Virtual Deadline First
 *
 * In order to provide latency guarantees for different request sizes
 * EEVDF selects the best runnable task from two criteria:
 *
 *  1) the task must be eligible (must be owed service)
 *
 *  2) from those tasks that meet 1), we select the one
 *     with the earliest virtual deadline.
 *
 * We can do this in O(log n) time due to an augmented RB-tree. The
 * tree keeps the entries sorted on deadline, but also functions as a
 * heap based on the vruntime by keeping:
 *
 *  task->min_vruntime = min(task->vruntime, task->{left,right}->min_vruntime)
 *
 * Which allows tree pruning through eligibility.
 */
static struct eevdf_task *pick_eevdf(struct eevdf_rq *eevdf_rq)
{
    // log_debug("%s: %p", __func__, eevdf_rq);
    struct rb_node *node = eevdf_rq->tasks_timeline.rb_root.rb_node;
    struct eevdf_task *task = __pick_first_task(eevdf_rq);
    struct eevdf_task *curr = eevdf_rq->curr;
    struct eevdf_task *best = NULL;

    /*
     * We can safely skip eligibility check if there is only one task
     * in this eevdf_rq, saving some cycles.
     */
    if (eevdf_rq->nr_running == 1)
        return curr && curr->on_rq ? curr : task;

    if (curr && (!curr->on_rq || !task_eligible(eevdf_rq, curr)))
        curr = NULL;

    // SCHED_FEAT(RUN_TO_PARITY, true)
    /*
     * Once selected, run a task until it either becomes non-eligible or
     * until it gets a new slice. See the HACK in set_next_task().
     */
    if (curr && curr->vlag == curr->deadline)
        return curr;

    /* Pick the leftmost task if it's eligible */
    if (task && task_eligible(eevdf_rq, task)) {
        best = task;
        goto found;
    }

    /* Heap search for the EEVD task */
    while (node) {
        struct rb_node *left = node->rb_left;

        /*
         * Eligible entities in left subtree are always better
         * choices, since they have earlier deadlines.
         */
        if (left && vruntime_eligible(eevdf_rq, __node_2_task(left)->min_vruntime)) {
            node = left;
            continue;
        }

        task = __node_2_task(node);

        /*
         * The left subtree either is empty or has no eligible
         * task, so check the current node since it is the one
         * with earliest deadline that might be eligible.
         */
        if (task_eligible(eevdf_rq, task)) {
            best = task;
            break;
        }

        node = node->rb_right;
    }
found:
    if (!best || (curr && task_before(curr, best)))
        best = curr;

    // log_debug("%s: best: %p", __func__, best);
    return best;
}

struct task *eevdf_sched_pick_next()
{
    struct eevdf_rq *rq = this_rq();
    struct eevdf_task *task;
    struct eevdf_task *curr = rq->curr;

    assert_spin_lock_held(&rq->lock);
    if (curr) {
        if (curr->on_rq)
            update_curr(rq);
        else
            curr = NULL;
    }

    if (!rq->nr_running)
        return NULL;

    task = pick_eevdf(rq);
    if (!task) {
        return NULL;
    }
    __set_next_task(rq, task);

    return task_of(task);
}

void eevdf_sched_yield()
{
    struct eevdf_rq *rq = this_rq();
    struct eevdf_task *prev = rq->curr;
    // log_debug("%s: rq: %p, curr: %p, self: %p", __func__, this_rq(), this_rq()->curr,
    //           eevdf_task_of(task_self()));
    assert(this_rq()->curr == eevdf_task_of(task_self()));

    spin_lock(&rq->lock);
    if (prev->on_rq) {
        update_curr(rq);
        prev->deadline += calc_delta_fair(prev->slice, prev);
        __enqueue_task(rq, prev);
    }
    rq->curr = NULL;
    spin_unlock(&rq->lock);
    // log_debug("%s: exit, rq: %p", __func__, rq);
}

void eevdf_sched_wakeup(struct task *t)
{
    // log_debug("%s: task: %p", __func__, t);
    struct eevdf_task *task = eevdf_task_of(t);
    struct eevdf_rq *rq = cpu_rq(find_target_cpu(task, false));

    spin_lock(&rq->lock);
    if (!task->on_rq)
        enqueue_task(rq, task);
    spin_unlock(&rq->lock);
    log_debug("%s: rq: %p, task: %p \n", __func__, rq, task);
}

void eevdf_sched_block()
{
    // log_debug("%s: enter", __func__);
    struct eevdf_rq *rq = this_rq();

    assert(rq->curr->on_rq);

    spin_lock(&rq->lock);
    dequeue_task(rq, rq->curr);
    rq->curr = NULL;
    spin_unlock(&rq->lock);
    // log_debug("%s: exit, rq: %p", __func__, rq);
}

bool eevdf_sched_preempt()
{
    // log_debug("%s: enter", __func__);
    bool resched = false;
    struct eevdf_rq *eevdf_rq = this_rq();

    assert_local_irq_disabled();

    spin_lock(&eevdf_rq->lock);
    resched = update_curr(eevdf_rq);
    spin_unlock(&eevdf_rq->lock);

    // log_debug("%s: rq: %p, return %d\n", __func__, eevdf_rq, resched);

    return resched;
}
