#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#include "bench_common.h"

#define ROUNDS 100000

static pthread_t thread;

static void *null_fn(void *)
{
    return NULL;
}

static void *thread_yield_fn(void *)
{
    for (int i = 0; i < ROUNDS / 2; ++i) sched_yield();
    return NULL;
}

static void bench_spawn()
{
    for (int i = 0; i < ROUNDS; ++i) {
        pthread_create(&thread, NULL, null_fn, NULL);
        pthread_join(thread, NULL);
    }
}

static void bench_yield()
{
    pthread_create(&thread, NULL, thread_yield_fn, NULL);
    thread_yield_fn(NULL);

    pthread_join(thread, NULL);
}

int main(int argc, char *argv[])
{
    printf("PThread micro-benchmarks\n");
    bench_one("yield", bench_yield, ROUNDS);
    bench_one("spawn", bench_spawn, ROUNDS);
}
