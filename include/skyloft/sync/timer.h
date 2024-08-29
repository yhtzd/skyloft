/*
 * timer.h - support for timers
 */

#pragma once

#include <skyloft/sched.h>


typedef void (*timer_fn_t)(uint64_t arg);

struct timer_entry {
    bool armed;
    uint32_t idx;
    timer_fn_t fn;
    uint64_t arg;
    struct kthread *k;
};

/**
 * timer_init - initializes a timer
 * @e: the timer entry to initialize
 * @fn: the timer handler (called when the timer fires)
 * @arg: an argument passed to the timer handler
 */
static inline void timer_init(struct timer_entry *e, timer_fn_t fn, unsigned long arg)
{
    e->armed = false;
    e->fn = fn;
    e->arg = arg;
}

void timer_start(struct timer_entry *e, uint64_t deadline_us);
bool timer_cancel(struct timer_entry *e);

/*
 * High-level API
 */

void timer_sleep_until(uint64_t deadline_us);
void timer_sleep(uint64_t duration_us);

struct timer_idx {
    uint64_t deadline_us;
    struct timer_entry *e;
};

void timer_softirq(struct kthread *k, unsigned int budget);
void timer_merge(struct kthread *r);
uint64_t timer_earliest_deadline(void);

/**
 * timer_needed - returns true if pending timers have to be handled
 * @k: the kthread to check
 */
static inline bool timer_needed(struct kthread *k)
{
    /* deliberate race condition */
    return k->nr_timers > 0 && k->timers[0].deadline_us <= now_us();
}

int timer_init_percpu(void);