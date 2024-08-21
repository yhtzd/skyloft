#pragma once

static inline int dummy_sched_init(void *data) { return 0; }
static inline int dummy_sched_init_percpu(void *percpu_data) { return 0; }
static inline int dummy_sched_init_task(struct task *task) { return 0; }
static inline void dummy_sched_finish_task(struct task *task) {}

static inline int dummy_sched_spawn(struct task *task, int cpu) { return 0; }
static inline struct task *dummy_sched_pick_next() { return 0; }
static inline void dummy_sched_block() {}
static inline void dummy_sched_wakeup(struct task *task) {}
static inline void dummy_sched_yield() {}
static inline void dummy_sched_percpu_lock(int cpu) {}
static inline void dummy_sched_percpu_unlock(int cpu) {}

static inline void dummy_sched_balance() {}
static inline bool dummy_sched_preempt() { return false; }
static inline void dummy_sched_poll() {}

static inline int dummy_sched_set_params(void *params) { return 0; }
static inline void dummy_sched_dump_tasks() {}
