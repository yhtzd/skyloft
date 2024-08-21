#pragma once

#include <skyloft/sched.h>

/*
 * These are per-kthread stat counters. It's recommended that most counters be
 * monotonically increasing, as that decouples the counters from any particular
 * collection time period. However, it may not be possible to represent all
 * counters this way.
 *
 * Don't use these enums directly. Instead, use the STAT() macro.
 */
enum {
    /* scheduler counters */
    STAT_LOCAL_SPAWNS = 0,
    STAT_SWITCH_TO,
    STAT_TASKS_STOLEN,
    STAT_IDLE,
    STAT_IDLE_CYCLES,
    // STAT_WAKEUP,
    // STAT_WAKEUP_CYCLES,
    STAT_SOFTIRQS_LOCAL,
    STAT_SOFTIRQ_CYCLES,
    STAT_ALLOC,
    STAT_ALLOC_CYCLES,
    STAT_RX,
    STAT_TX,
#ifdef SKYLOFT_UINTR
    STAT_UINTR,
    // STAT_UINTR_CYCLES,
#endif

    /* total number of counters */
    STAT_NR,
};

static const char *STAT_STR[] = {
    "local_spawns",
    "switch_to",
    "tasks_stolen",
    "idle",
    "idle_cycles",
    // "wakeup",
    // "wakeup_cycles",
    "softirqs_local",
    "softirq_cycles",
    "alloc",
    "alloc_cycles",
    "rx",
    "tx",
#ifdef SKYLOFT_UINTR
    "uintr",
    // "uintr_cycles",
#endif
};

BUILD_ASSERT(ARRAY_SIZE(STAT_STR) == STAT_NR);

static inline const char *stat_str(int idx)
{
    return STAT_STR[idx];
}

/**
 * STAT - gets a stat counter
 *
 * e.g. STAT(DROPS)++;
 *
 * Deliberately could race with preemption.
 */
#define ADD_STAT_FORCE(counter, val) (thisk()->stats[STAT_##counter] += val)
#ifdef SKYLOFT_STAT
#define ADD_STAT(counter, val) ADD_STAT_FORCE(counter, val)
#define STAT_CYCLES_BEGIN(timer) ({ \
    timer = now_tsc(); \
})
#define ADD_STAT_CYCLES(counter, timer) ({ \
    ADD_STAT_FORCE(counter, now_tsc() - timer); \
})
#else
#define ADD_STAT(counter, val)
#define STAT_CYCLES_BEGIN(timer) ((void)timer)
#define ADD_STAT_CYCLES(counter, timer)
#endif

void print_stats(void);
