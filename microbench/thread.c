#include <stdatomic.h>
#include <stdio.h>

#include <skyloft/sync.h>
#include <skyloft/task.h>
#include <skyloft/uapi/task.h>

#include <utils/time.h>

#include "common.h"
#include "skyloft/sync/sync.h"

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

static void bench_yield()
{
    atomic_store(&counter, 0);

    sl_task_spawn(thread_yield_fn, NULL, 0);
    thread_yield_fn(NULL);

    while (atomic_load(&counter) < 2) {
        sl_task_yield();
    }
}

static void bench_mutex()
{
    mutex_t m;
    volatile unsigned long foo = 0;

    mutex_init(&m);

    for (int i = 0; i < ROUNDS; ++i) {
        mutex_lock(&m);
        foo++;
        mutex_unlock(&m);
    }
}

mutex_t m;
condvar_t cv;
bool dir = false;

static void thread_ping_fn(void *)
{
    mutex_lock(&m);
    for (int i = 0; i < ROUNDS / 2; ++i) {
        while (dir) condvar_wait(&cv, &m);
        dir = true;
        condvar_signal(&cv);
    }
    mutex_unlock(&m);
}

static void bench_condvar()
{
    mutex_init(&m);
    condvar_init(&cv);

    sl_task_spawn(thread_ping_fn, NULL, 0);

    mutex_lock(&m);
    for (int i = 0; i < ROUNDS / 2; ++i) {
        while (!dir) condvar_wait(&cv, &m);
        dir = false;
        condvar_signal(&cv);
    }
    mutex_unlock(&m);

    sl_task_yield();
}

void app_main(void *arg)
{
    bench_one("yield", bench_yield, ROUNDS);
    bench_one("spawn", bench_spawn, ROUNDS);
    bench_one("mutex", bench_mutex, ROUNDS);
    bench_one("condvar", bench_condvar, ROUNDS);
}

int main(int argc, char *argv[])
{
    printf("Skyloft threading microbenchmark\n");
    sl_libos_start(app_main, NULL);
}
