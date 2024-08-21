#include <skyloft/sched.h>
#include <skyloft/sync/sync.h>
#include <skyloft/task.h>
#include <skyloft/uapi/task.h>
#include <utils/log.h>

#include <errno.h>

/*
 * Mutex support
 */

/**
 * mutex_try_lock - attempts to acquire a mutex
 * @m: the mutex to acquire
 *
 * Returns true if the acquire was successful.
 */
bool mutex_try_lock(mutex_t *m)
{
    spin_lock_np(&m->waiter_lock);
    if (m->held) {
        spin_unlock_np(&m->waiter_lock);
        return false;
    }
    m->held = true;
    spin_unlock_np(&m->waiter_lock);
    return true;
}

/**
 * mutex_lock - acquires a mutex
 * @m: the mutex to acquire
 */
void mutex_lock(mutex_t *m)
{
    struct task *task;

    spin_lock_np(&m->waiter_lock);
    task = task_self();
    if (!m->held) {
        m->held = true;
        spin_unlock_np(&m->waiter_lock);
        return;
    }
    list_add_tail(&m->waiters, &task->link);
    task_block(&m->waiter_lock);
}

/**
 * mutex_unlock - releases a mutex
 * @m: the mutex to release
 */
void mutex_unlock(mutex_t *m)
{
    struct task *task;

    spin_lock_np(&m->waiter_lock);
    task = list_pop(&m->waiters, struct task, link);
    if (!task) {
        m->held = false;
        spin_unlock_np(&m->waiter_lock);
        return;
    }
    spin_unlock_np(&m->waiter_lock);
    task_wakeup(task);
}

/**
 * mutex_init - initializes a mutex
 * @m: the mutex to initialize
 */
void mutex_init(mutex_t *m)
{
    m->held = false;
    spin_lock_init(&m->waiter_lock);
    list_head_init(&m->waiters);
}

/*
 * Condition variable support
 */

/**
 * condvar_wait - waits for a condition variable to be signalled
 * @cv: the condition variable to wait for
 * @m: the currently held mutex that projects the condition
 */
void condvar_wait(condvar_t *cv, mutex_t *m)
{
    struct task *task = task_self();

    assert_mutex_held(m);
    spin_lock_np(&cv->waiter_lock);
    mutex_unlock(m);
    list_add_tail(&cv->waiters, &task->link);
    task_block(&cv->waiter_lock);
    mutex_lock(m);
}

/**
 * condvar_signal - signals a thread waiting on a condition variable
 * @cv: the condition variable to signal
 */
void condvar_signal(condvar_t *cv)
{
    struct task *task;

    spin_lock_np(&cv->waiter_lock);
    task = list_pop(&cv->waiters, struct task, link);
    spin_unlock_np(&cv->waiter_lock);
    if (task)
        task_wakeup(task);
}

/**
 * condvar_broadcast - signals all waiting threads on a condition variable
 * @cv: the condition variable to signal
 */
void condvar_broadcast(condvar_t *cv)
{
    struct task *task;
    struct list_head tmp;

    list_head_init(&tmp);

    spin_lock_np(&cv->waiter_lock);
    list_append_list(&tmp, &cv->waiters);
    spin_unlock_np(&cv->waiter_lock);

    while (true) {
        task = list_pop(&tmp, struct task, link);
        if (!task)
            break;
        task_wakeup(task);
    }
}

/**
 * condvar_init - initializes a condition variable
 * @cv: the condition variable to initialize
 */
void condvar_init(condvar_t *cv)
{
    spin_lock_init(&cv->waiter_lock);
    list_head_init(&cv->waiters);
}

/*
 * Barrier support
 */

/**
 * barrier_init - initializes a barrier
 * @b: the wait group to initialize
 * @count: number of threads that must wait before releasing
 */
void barrier_init(barrier_t *b, int count)
{
    spin_lock_init(&b->lock);
    list_head_init(&b->waiters);
    b->count = count;
    b->waiting = 0;
}

/**
 * barrier_wait - waits on a barrier
 * @b: the barrier to wait on
 *
 * Returns true if the calling thread releases the barrier
 */
bool barrier_wait(barrier_t *b)
{
    struct task *task;
    struct list_head tmp;

    list_head_init(&tmp);

    spin_lock_np(&b->lock);

    if (++b->waiting >= b->count) {
        list_append_list(&tmp, &b->waiters);
        b->waiting = 0;
        spin_unlock_np(&b->lock);
        while (true) {
            task = list_pop(&tmp, struct task, link);
            if (!task)
                break;
            task_wakeup(task);
        }
        return true;
    }

    task = task_self();
    list_add_tail(&b->waiters, &task->link);
    task_block(&b->lock);
    return false;
}

/*
 * Wait group support
 */

/**
 * waitgroup_add - adds or removes waiters from a wait group
 * @wg: the wait group to update
 * @cnt: the count to add to the waitgroup (can be negative)
 *
 * If the wait groups internal count reaches zero, the waiting thread (if it
 * exists) will be signalled. The wait group must be incremented at least once
 * before calling waitgroup_wait().
 */
void waitgroup_add(waitgroup_t *wg, int cnt)
{
    struct task *task;
    struct list_head tmp;

    list_head_init(&tmp);

    spin_lock_np(&wg->lock);
    wg->cnt += cnt;
    BUG_ON(wg->cnt < 0);
    if (wg->cnt == 0)
        list_append_list(&tmp, &wg->waiters);
    spin_unlock_np(&wg->lock);

    while (true) {
        task = list_pop(&tmp, struct task, link);
        if (!task)
            break;
        task_wakeup(task);
    }
}

/**
 * waitgroup_wait - waits for the wait group count to become zero
 * @wg: the wait group to wait on
 */
void waitgroup_wait(waitgroup_t *wg)
{
    struct task *task;

    spin_lock_np(&wg->lock);
    task = task_self();
    if (wg->cnt == 0) {
        spin_unlock_np(&wg->lock);
        return;
    }
    list_add_tail(&wg->waiters, &task->link);
    task_block(&wg->lock);
}

/**
 * waitgroup_init - initializes a wait group
 * @wg: the wait group to initialize
 */
void waitgroup_init(waitgroup_t *wg)
{
    spin_lock_init(&wg->lock);
    list_head_init(&wg->waiters);
    wg->cnt = 0;
}

struct futex {
    int *uaddr;
    struct list_node link;
    struct task *task;
};

DEFINE_LIST_HEAD(futex_list);
DEFINE_SPINLOCK(futex_lock);

int futex_wait(int *uaddr, int val)
{
    struct futex futex = {.uaddr = uaddr, .task = task_self()};

    spin_lock_np(&futex_lock);
    if (*uaddr != val) {
        spin_unlock_np(&futex_lock);
        return -EAGAIN;
    }

    list_add_tail(&futex_list, &futex.link);
    task_block(&futex_lock);
    return 0;
}

int futex_wake(int *uaddr, int val)
{
    int count = 0;
    struct futex *f;

    spin_lock_np(&futex_lock);
    list_for_each(&futex_list, f, link)
    {
        if (f->uaddr == uaddr) {
            list_del(&f->link);
            task_wakeup(f->task);
            if (++count == val)
                break;
        }
    }
    spin_unlock_np(&futex_lock);
    return count;
}

int __api sl_futex(int *uaddr, int op, int val, const struct timespec *timeout, int *uaddr2,
                   int val3)
{
    int cmd = op & FUTEX_CMD_MASK;
    switch (cmd) {
    case FUTEX_WAIT:
        if (timeout) {
            log_warn("FUTEX_WAIT with timeout is not supported");
        }
        int ret = futex_wait(uaddr, val);
        return ret;
    case FUTEX_WAKE:
        return futex_wake(uaddr, val);
    default:
        return -1;
    }
}

void init_sync()
{
    spin_lock_init(&futex_lock);
    list_head_init(&futex_list);
}
