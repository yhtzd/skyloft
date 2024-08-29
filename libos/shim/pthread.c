#include <errno.h>

#include <skyloft/sched.h>
#include <skyloft/sync/sync.h>
#include <skyloft/task.h>
#include <skyloft/uapi/pthread.h>
#include <skyloft/uapi/task.h>
#include <utils/assert.h>
#include <utils/defs.h>
#include <utils/log.h>
#include <utils/spinlock.h>

struct join_handle {
    void *(*fn)(void *);
    void *args;
    void *retval;
    spinlock_t lock;
    struct task *waiter;
    bool detached;
};

static void __trampoline(void *arg)
{
    struct join_handle *j = arg;

    j->retval = j->fn(j->args);
    spin_lock_np(&j->lock);
    if (j->detached) {
        spin_unlock_np(&j->lock);
        return;
    }
    if (j->waiter != NULL) {
        task_enqueue(0, j->waiter);
    }
    j->waiter = task_self();
    task_block(&j->lock);
}

int sl_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*fn)(void *), void *arg)
{
    struct join_handle **handle = (struct join_handle **)thread;
    struct join_handle *j;
    struct task *task;

    task = task_create_with_buf(__trampoline, (void **)&j, sizeof(struct join_handle));
    if (unlikely(!task))
        return -ENOMEM;

    j->fn = fn;
    j->args = arg;
    spin_lock_init(&j->lock);
    j->waiter = NULL;
    j->detached = false;

    if (handle)
        *handle = j;

    if (unlikely(task_enqueue(current_cpu_id(), task))) {
        task_free(task);
        return -EAGAIN;
    }

    return 0;
}

int sl_pthread_join(pthread_t thread, void **retval)
{
    struct join_handle *j = (struct join_handle *)thread;

    spin_lock_np(&j->lock);
    if (j->detached) {
        spin_unlock_np(&j->lock);
        return -EINVAL;
    }
    if (j->waiter == NULL) {
        j->waiter = task_self();
        task_block(&j->lock);
        spin_lock_np(&j->lock);
    }
    if (retval)
        *retval = j->retval;
    spin_unlock_np(&j->lock);
    return 0;
}

int sl_pthread_detach(pthread_t thread)
{
    struct join_handle *j = (struct join_handle *)thread;

    spin_lock_np(&j->lock);
    if (j->detached) {
        spin_unlock_np(&j->lock);
        return -EINVAL;
    }
    j->detached = true;
    if (j->waiter)
        task_wakeup(j->waiter);
    spin_unlock_np(&j->lock);
    return 0;
}

int sl_pthread_yield(void)
{
    task_yield();
    return 0;
}

pthread_t sl_pthread_self()
{
    return align_down(stack_top(task_self()->stack) - sizeof(struct join_handle), RSP_ALIGNMENT);
}

void __attribute__((noreturn)) sl_pthread_exit(void *retval)
{
    task_exit(retval);
}

int sl_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *mutexattr)
{
    mutex_init((mutex_t *)mutex);
    return 0;
}

int sl_pthread_mutex_lock(pthread_mutex_t *mutex)
{
    mutex_lock((mutex_t *)mutex);
    return 0;
}

int sl_pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    return mutex_try_lock((mutex_t *)mutex) ? 0 : EBUSY;
}

int sl_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    mutex_unlock((mutex_t *)mutex);
    return 0;
}

int sl_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    return 0;
}

int sl_pthread_cond_init(pthread_cond_t *__restrict cond,
                         const pthread_condattr_t *__restrict cond_attr)
{
    condvar_init((condvar_t *)cond);
    return 0;
}

int sl_pthread_cond_signal(pthread_cond_t *cond)
{
    condvar_signal((condvar_t *)cond);
    return 0;
}

int sl_pthread_cond_broadcast(pthread_cond_t *cond)
{
    condvar_broadcast((condvar_t *)cond);
    return 0;
}

int sl_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    condvar_wait((condvar_t *)cond, (mutex_t *)mutex);
    return 0;
}

int sl_pthread_cond_destroy(pthread_cond_t *cond)
{
    return 0;
}