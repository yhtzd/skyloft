/*
 * test_timer.c - tests task timer
 */

#include <stdio.h>

#include <skyloft/sync/sync.h>
#include <skyloft/sync/timer.h>
#include <skyloft/task.h>
#include <skyloft/uapi/task.h>
#include <utils/defs.h>
#include <utils/log.h>
#include <utils/time.h>

#define WORKERS 2
#define N       1000

static void work_handler(void *arg)
{
    waitgroup_t *wg_parent = (waitgroup_t *)arg;
    int i;

    for (i = 0; i < N; i++) {
        timer_sleep(2000);
    }

    waitgroup_done(wg_parent);
}

static void main_handler(void *arg)
{
    waitgroup_t wg;
    double timeouts_per_second;
    uint64_t start_us;
    int i, ret;

    waitgroup_init(&wg);
    waitgroup_add(&wg, WORKERS);
    start_us = now_us();
    for (i = 1; i < WORKERS + 1; i++) {
        ret = sl_task_spawn_oncpu(i, work_handler, (void *)&wg, 0);
        BUG_ON(ret);
    }

    waitgroup_wait(&wg);
    timeouts_per_second = (double)(WORKERS * N) / ((now_us() - start_us) * 0.000001);
    printf("handled %f timeouts / second\n", timeouts_per_second);
}

int main(int argc, char *argv[])
{
    int ret = 0;

    ret = sl_libos_start(main_handler, NULL);
    if (ret) {
        printf("failed to start libos: %d\n", ret);
        return ret;
    }

    return 0;
}
