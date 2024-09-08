/*
 * timer.c - support for timers
 *
 * So far we use a D-ary heap just like the Go runtime. We may want to consider
 * adding a lower-resolution shared timer wheel as well.
 */

#include <errno.h>
#include <limits.h>

#include <skyloft/params.h>
#include <skyloft/sched.h>
#include <skyloft/sync/sync.h>
#include <skyloft/sync/timer.h>
#include <skyloft/task.h>
#include <skyloft/uapi/task.h>
#include <utils/log.h>
#include <utils/time.h>

/* the arity of the heap */
#define D 4

/**
 * is_valid_heap - checks that the timer heap is a valid min heap
 * @heap: the timer heap
 * @n: the number of timers in the heap
 *
 * Returns true if valid, false otherwise.
 */
static bool is_valid_heap(struct timer_idx *heap, int n)
{
    int i, p;

    /* check that each timer's deadline is later or equal to its parent's
     * deadline */
    for (i = n - 1; i > 1; i--) {
        p = (i - 1) / D;
        if (heap[p].deadline_us > heap[i].deadline_us)
            return false;
    }

    return true;
}

/**
 * timer_heap_is_valid - checks that this kthread's timer heap is a
 * valid min heap
 * @k: the kthread
 */
static void assert_timer_heap_is_valid(struct kthread *k)
{
    assert(is_valid_heap(k->timers, k->nr_timers));
}

static void sift_up(struct timer_idx *heap, int i)
{
    struct timer_idx tmp = heap[i];
    int p;

    while (i > 0) {
        p = (i - 1) / D;
        if (tmp.deadline_us >= heap[p].deadline_us)
            break;
        heap[i] = heap[p];
        heap[i].e->idx = i;
        heap[p] = tmp;
        heap[p].e->idx = p;
        i = p;
    }
}

static void sift_down(struct timer_idx *heap, int i, int n)
{
    struct timer_idx tmp = heap[i];
    uint64_t w;
    int c, j;

    while (1) {
        w = tmp.deadline_us;
        c = INT_MAX;
        for (j = (i * D + 1); j <= (i * D + D); j++) {
            if (j >= n)
                break;
            if (heap[j].deadline_us < w) {
                w = heap[j].deadline_us;
                c = j;
            }
        }
        if (c == INT_MAX)
            break;
        heap[i] = heap[c];
        heap[i].e->idx = i;
        heap[c] = tmp;
        heap[c].e->idx = c;
        i = c;
    }
}

/**
 * timer_merge - merges a timer heap from another kthread into our timer heap
 * @r: the remote kthread whose timer heap we will absorb
 */
void timer_merge(struct kthread *r)
{
    struct kthread *k = thisk();
    int i;

    spin_lock(&k->timer_lock);
    spin_lock(&r->timer_lock);

    if (r->nr_timers == 0) {
        spin_unlock(&r->timer_lock);
        goto done;
    }

    /* move all timers from r to the end of our array */
    for (i = 0; i < r->nr_timers; i++) {
        k->timers[k->nr_timers] = r->timers[i];
        k->timers[k->nr_timers].e->idx = k->nr_timers;
        k->timers[k->nr_timers].e->k = k;
        k->nr_timers++;

        if (k->nr_timers >= MAX_TIMERS)
            BUG();
    }
    r->nr_timers = 0;
    spin_unlock(&r->timer_lock);

    /*
     * Restore heap order by sifting each non-leaf element downward,
     * starting from the bottom of the heap and working upward (runs in
     * linear time).
     */
    for (i = k->nr_timers / D; i >= 0; i--) sift_down(k->timers, i, k->nr_timers);

done:
    spin_unlock(&k->timer_lock);
}

/**
 * timer_earliest_deadline - return the first deadline for this kthread or 0 if
 * there are no active timers.
 */
uint64_t timer_earliest_deadline()
{
    struct kthread *k = thisk();
    uint64_t deadline_us;

    /* deliberate race condition */
    if (k->nr_timers == 0)
        deadline_us = 0;
    else
        deadline_us = k->timers[0].deadline_us;

    return deadline_us;
}

static void timer_start_locked(struct timer_entry *e, uint64_t deadline_us)
{
    struct kthread *k = thisk();
    int i;

    assert_spin_lock_held(&k->timer_lock);

    /* can't insert a timer twice! */
    BUG_ON(e->armed);

    i = k->nr_timers++;
    if (k->nr_timers >= MAX_TIMERS) {
        /* TODO: support unlimited timers */
        BUG();
    }

    k->timers[i].deadline_us = deadline_us;
    k->timers[i].e = e;
    e->idx = i;
    e->k = k;
    sift_up(k->timers, i);
    e->armed = true;
}

/**
 * timer_start - arms a timer
 * @e: the timer entry to start
 * @deadline_us: the deadline in microseconds
 *
 * @e must have been initialized with timer_init().
 */
void timer_start(struct timer_entry *e, uint64_t deadline_us)
{
    struct kthread *k = thisk();

    spin_lock_np(&k->timer_lock);
    timer_start_locked(e, deadline_us);
    spin_unlock_np(&k->timer_lock);
    putk();
}

/**
 * timer_cancel - cancels a timer
 * @e: the timer entry to cancel
 *
 * Returns true if the timer was successfully cancelled, otherwise it has
 * already fired or was never armed.
 */
bool timer_cancel(struct timer_entry *e)
{
    struct kthread *k;
    uint32_t last;

try_again:
    preempt_disable();
    k = atomic_load_acq(&e->k);

    spin_lock_np(&k->timer_lock);

    if (e->k != k) {
        /* Timer was merged to a different heap */
        spin_unlock_np(&k->timer_lock);
        goto try_again;
    }

    if (!e->armed) {
        spin_unlock_np(&k->timer_lock);
        return false;
    }
    e->armed = false;

    last = --k->nr_timers;
    if (e->idx == last) {
        spin_unlock_np(&k->timer_lock);
        return true;
    }

    k->timers[e->idx] = k->timers[last];
    k->timers[e->idx].e->idx = e->idx;
    sift_up(k->timers, e->idx);
    sift_down(k->timers, e->idx, k->nr_timers);
    spin_unlock_np(&k->timer_lock);
    return true;
}

static void timer_finish_sleep(unsigned long arg)
{
    task_wakeup((struct task *)arg);
}

static void __timer_sleep(uint64_t deadline_us)
{
#ifdef SKYLOFT_TIMER
    struct kthread *k = thisk();
    struct timer_entry e;

    timer_init(&e, timer_finish_sleep, (unsigned long)task_self());

    spin_lock_np(&k->timer_lock);
    timer_start_locked(&e, deadline_us);
    task_block(&k->timer_lock);
#else
    while (now_us() < deadline_us) {
        task_yield();
    }
#endif
}

/**
 * timer_sleep_until - sleeps until a deadline
 * @deadline_us: the deadline time in microseconds
 */
void timer_sleep_until(uint64_t deadline_us)
{
    if (unlikely(now_us() >= deadline_us))
        return;

    __timer_sleep(deadline_us);
}

/**
 * timer_sleep - sleeps for a duration
 * @duration_us: the duration time in microseconds
 */
void timer_sleep(uint64_t duration_us)
{
    __timer_sleep(now_us() + duration_us);
}

void __api sl_sleep(int secs)
{
    return timer_sleep(secs * USEC_PER_SEC);
}

void __api sl_usleep(int usecs)
{
    return timer_sleep(usecs);
}

/**
 * timer_softirq - handles expired timers
 * @k: the kthread to check
 * @budget: the maximum number of timers to handle
 */
void timer_softirq(struct kthread *k, unsigned int budget)
{
    struct timer_entry *e;
    uint64_t now;
    int i;

    spin_lock_np(&k->timer_lock);
    assert_timer_heap_is_valid(k);

    now = now_us();
    while ((budget-- > 0) && k->nr_timers > 0 && k->timers[0].deadline_us <= now) {
        i = --k->nr_timers;
        e = k->timers[0].e;
        if (i > 0) {
            k->timers[0] = k->timers[i];
            k->timers[0].e->idx = 0;
            sift_down(k->timers, 0, i);
        }
        spin_unlock_np(&k->timer_lock);

        /* execute the timer handler */
        e->fn(e->arg);

        spin_lock_np(&k->timer_lock);
        now = now_us();
    }

    spin_unlock_np(&k->timer_lock);
}

/**
 * timer_init_percpu - initializes percpu timer state
 *
 * Returns 0 if successful, otherwise fail.
 */
int timer_init_percpu(void)
{
    struct kthread *k = thisk();

    k->timers = aligned_alloc(CACHE_LINE_SIZE,
                              align_up(sizeof(struct timer_idx) * MAX_TIMERS, CACHE_LINE_SIZE));
    if (!k->timers)
        return -ENOMEM;

    return 0;
}
