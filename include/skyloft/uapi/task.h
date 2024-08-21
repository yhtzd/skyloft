/*
 * task.h - support for skyloft custom APIs
 */

#ifndef _SKYLOFT_UAPI_TASK_H_
#define _SKYLOFT_UAPI_TASK_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define __api

typedef void (*thread_fn_t)(void *arg);
typedef int (*initializer_fn_t)(void);

static inline int __api sl_current_app_id()
{
    extern int g_app_id;
    return g_app_id;
}

static inline int __api sl_current_cpu_id()
{
    extern __thread int g_logic_cpu_id;
    return g_logic_cpu_id;
}

int __api sl_current_task_id();

int __api sl_task_spawn(thread_fn_t fn, void *arg, int stack_size);
int __api sl_task_spawn_oncpu(int cpu_id, thread_fn_t fn, void *arg, int stack_size);
void __api sl_task_yield();
void __attribute__((noreturn)) __api sl_task_exit(int code);

const char *__api sl_sched_policy_name();
int __api sl_sched_set_params(void *params);
void __api sl_sched_poll();

int __api sl_set_initializers(initializer_fn_t global, initializer_fn_t percpu,
                              initializer_fn_t late);
int __api sl_libos_start(thread_fn_t app_main, void *arg);
int __api sl_libos_start_daemon();

void __api sl_dump_tasks();

void __api sl_sleep(int secs);
void __api sl_usleep(int usecs);

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

#define FUTEX_PRIVATE_FLAG   128
#define FUTEX_CLOCK_REALTIME 256
#define FUTEX_CMD_MASK       ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME)

#define FUTEX_WAIT_PRIVATE (FUTEX_WAIT | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAKE_PRIVATE (FUTEX_WAKE | FUTEX_PRIVATE_FLAG)

struct timespec;

int __api sl_futex(int *uaddr, int op, int val, const struct timespec *timeout, int *uaddr2,
                    int val3);

#ifdef __cplusplus
}
#endif

#endif
