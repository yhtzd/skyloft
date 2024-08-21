#include <stdatomic.h>
#include <stdio.h>

#include <skyloft/task.h>
#include <skyloft/uapi/task.h>
#include <utils/time.h>

#include "bench_common.h"

#define ROUNDS  10000000
#define ROUNDS2 10000

static atomic_int counter = 0;

static void null_fn(void *)
{
    atomic_fetch_add(&counter, 1);
}

static void thread_yield_fn(void *)
{
    for (int i = 0; i < ROUNDS / 2; ++i) sl_task_yield();
    atomic_fetch_add(&counter, 1);
}

static void bench_spawn()
{
    for (int i = 0; i < ROUNDS; ++i) {
        atomic_store(&counter, 0);
        sl_task_spawn(null_fn, NULL, 0);
        sl_task_yield();
    }
}

#ifndef SKYLOFT_SCHED_SQ
static void bench_spawn2()
{
    atomic_store(&counter, 0);
    for (int i = 0; i < ROUNDS2; ++i) {
        sl_task_spawn(null_fn, NULL, 0);
    }
    for (int i = 0; i < ROUNDS2; ++i) {
        sl_task_yield();
    }
}
#endif

static void bench_yield()
{
    atomic_store(&counter, 0);

    sl_task_spawn(thread_yield_fn, NULL, 0);
    thread_yield_fn(NULL);

    while (atomic_load(&counter) < 2) {
        sl_task_yield();
    }
}

static void bench_task_create()
{
    for (int i = 0; i < ROUNDS2; i++) task_create(null_fn, NULL);
}

void app_main(void *arg)
{
    bench_one("yield", bench_yield, ROUNDS);
    bench_one("spawn", bench_spawn, ROUNDS);
#ifndef SKYLOFT_SCHED_SQ
    bench_one("spawn2", bench_spawn2, ROUNDS2);
#endif
    bench_one("task_create", bench_task_create, ROUNDS2);
}

int main(int argc, char *argv[])
{
    printf("Skyloft micro-benchmarks\n");
    sl_libos_start(app_main, NULL);
}
