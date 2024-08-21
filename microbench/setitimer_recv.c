#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>

#include <utils/cpu.h>
#include <utils/time.h>

#include "common.h"

#define MASTER_CPU 25

#define ITER    1000000
#define WORKS   2e9

#define now  now_tsc

static volatile int recv_count = 0;

static uint64_t recv_lat;

void signal_handler()
{
    recv_count++;
}

void setup_signal_handler(void)
{
    struct sigaction action;
    action.sa_flags = 0;
    action.sa_handler = signal_handler;
    sigaction(SIGALRM, &action, NULL);

    struct itimerval timer;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 5;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 1;
    setitimer(ITIMER_REAL, &timer, NULL);
}

void clear_signal_handler(void)
{
    sigaction(SIGALRM, NULL, NULL);
}

int main()
{
    bind_to_cpu(MASTER_CPU);
    printf("run on CPU: %d\n", sched_getcpu());

    uint64_t work_time = now();
    dummy_work(WORKS);
    work_time = now() - work_time;
    printf("work time = %ld cycles\n", work_time);

    setup_signal_handler();

    recv_lat = now();
    dummy_work(WORKS);
    recv_lat = now() - recv_lat - work_time;
    clear_signal_handler();

    printf("count = %d\n", recv_count);
    printf("recv latency = %ld cycles\n", DIV_ROUND(recv_lat, recv_count));

    return 0;
}
