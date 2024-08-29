/*
 * timer.h: simulated per-CPU timer
 */

#pragma once

#include <skyloft/params.h>

#include <utils/defs.h>
#include <utils/time.h>

struct utimer {
    /* uintr sender index */
    int uintr_index[USED_CPUS];
    /* if cpu has been preempted */
    __nsec deadline[USED_CPUS];
};

__noreturn void utimer_main(void);
