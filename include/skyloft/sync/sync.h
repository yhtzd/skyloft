/*
 * sync.h - support for synchronization
 */

#pragma once

#include <skyloft/platform.h>
#include <skyloft/sched.h>

#include <utils/list.h>
#include <utils/spinlock.h>

/*
 * Mutex support
 */

typedef struct {
    bool held;
    spinlock_t waiter_lock;
    struct list_head waiters;
} mutex_t;

bool mutex_try_lock(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);
void mutex_init(mutex_t *m);

/**
 * mutex_held - is the mutex currently held?
 * @m: the mutex to check
 */
static inline bool mutex_held(mutex_t *m) { return m->held; }

/**
 * assert_mutex_held - asserts that a mutex is currently held
 * @m: the mutex that must be held
 */
static inline void assert_mutex_held(mutex_t *m) { assert(mutex_held(m)); }

/*
 * Condition variable support
 */

typedef struct {
    spinlock_t waiter_lock;
    struct list_head waiters;
} condvar_t;

void condvar_wait(condvar_t *cv, mutex_t *m);
void condvar_signal(condvar_t *cv);
void condvar_broadcast(condvar_t *cv);
void condvar_init(condvar_t *cv);

/*
 * Barrier support
 */

typedef struct {
    spinlock_t lock;
    int waiting;
    int count;
    struct list_head waiters;
} barrier_t;

void barrier_init(barrier_t *b, int count);
bool barrier_wait(barrier_t *b);

/*
 * Spin lock support
 */

/**
 * spin_lock_np - takes a spin lock and disables preemption
 * @l: the spin lock
 */
static inline void spin_lock_np(spinlock_t *l)
{
    preempt_disable();
    spin_lock(l);
}

/**
 * spin_try_lock_np - takes a spin lock if its available and disables preemption
 * @l: the spin lock
 *
 * Returns true if successful, otherwise fail.
 */
static inline bool spin_try_lock_np(spinlock_t *l)
{
    preempt_disable();
    if (spin_try_lock(l))
        return true;

    preempt_enable();
    return false;
}

/**
 * spin_unlock_np - releases a spin lock and re-enables preemption
 * @l: the spin lock
 */
static inline void spin_unlock_np(spinlock_t *l)
{
    spin_unlock(l);
    preempt_enable();
}

#define spin_lock_irqsave(lock, flags) \
    do {                               \
        local_irq_save(flags);         \
        spin_lock(lock);               \
    } while (0)

#define spin_unlock_irqrestore(lock, flags) \
    do {                                    \
        spin_unlock(lock);                  \
        local_irq_restore(flags);           \
    } while (0)

/*
 * Wait group support
 */

struct waitgroup {
    spinlock_t lock;
    int cnt;
    struct list_head waiters;
};

typedef struct waitgroup waitgroup_t;

void waitgroup_add(waitgroup_t *wg, int cnt);
void waitgroup_wait(waitgroup_t *wg);
void waitgroup_init(waitgroup_t *wg);

/**
 * waitgroup_done - notifies the wait group that one waiting event completed
 * @wg: the wait group to complete
 */
static inline void waitgroup_done(waitgroup_t *wg) { waitgroup_add(wg, -1); }

int futex_wait(int *uaddr, int val);
int futex_wake(int *uaddr, int val);
