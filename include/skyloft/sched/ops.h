/*
 * ops.h: Skyloft general operations
 */

#pragma once

#if defined(SKYLOFT_SCHED_FIFO)
#include "policy/fifo.h"
#define SCHED_NAME        "fifo"
#define SCHED_OP(op_name) fifo_##op_name
#elif defined(SKYLOFT_SCHED_FIFO2)
#include "policy/rr.h"
#define SCHED_NAME        "fifo"
#define SCHED_OP(op_name) fifo_##op_name
#elif defined(SKYLOFT_SCHED_CFS)
#include "policy/cfs.h"
#define SCHED_NAME        "cfs"
#define SCHED_OP(op_name) cfs_##op_name
#elif defined(SKYLOFT_SCHED_SQ)
#include "policy/sq.h"
#define SCHED_NAME        "sq"
#define SCHED_OP(op_name) sq_##op_name
#elif defined(SKYLOFT_SCHED_SQ_LCBE)
#include "policy/sq_lcbe.h"
#define SCHED_NAME        "sq"
#define SCHED_OP(op_name) sq_##op_name
#endif

#ifndef SCHED_DATA_SIZE
#define SCHED_DATA_SIZE (2 * 1024 * 1024)
#endif

#ifndef SCHED_PERCPU_DATA_SIZE
#define SCHED_PERCPU_DATA_SIZE (2 * 1024 * 1024)
#endif

#define __sched_name SCHED_NAME

static inline int __sched_init(void *data) { return SCHED_OP(sched_init)(data); }
static inline int __sched_init_percpu(void *percpu_data)
{
    return SCHED_OP(sched_init_percpu)(percpu_data);
}

static inline int __sched_init_task(struct task *task) { return SCHED_OP(sched_init_task)(task); }
static inline void __sched_finish_task(struct task *task) { SCHED_OP(sched_finish_task)(task); }

static inline int __sched_spawn(struct task *task, int cpu)
{
    return SCHED_OP(sched_spawn)(task, cpu);
}

static inline struct task *__sched_pick_next() { return SCHED_OP(sched_pick_next)(); }
static inline void __sched_block() { SCHED_OP(sched_block)(); }
static inline void __sched_wakeup(struct task *task) { SCHED_OP(sched_wakeup)(task); }
static inline void __sched_yield() { SCHED_OP(sched_yield)(); }
static inline void __sched_percpu_lock(int cpu) { SCHED_OP(sched_percpu_lock)(cpu); }
static inline void __sched_percpu_unlock(int cpu) { SCHED_OP(sched_percpu_unlock)(cpu); }

static inline void __sched_balance() { SCHED_OP(sched_balance)(); }
static inline void __sched_poll() { SCHED_OP(sched_poll)(); }

static __always_inline __attribute__((target("general-regs-only"))) bool __sched_preempt()
{
    return SCHED_OP(sched_preempt)();
}

static inline int __sched_set_params(void *params) { return SCHED_OP(sched_set_params)(params); }
static inline void __sched_dump_tasks() { SCHED_OP(sched_dump_tasks)(); }
