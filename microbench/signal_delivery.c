#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>

#include <utils/cpu.h>
#include <utils/time.h>

#include "common.h"

#define MASTER_CPU 25
#define SLAVE_CPU  26

#define ITER    1000000

#define now  now_tsc

static volatile int recv_count = 0;
static volatile int init_ok = 0;
static volatile int send_ok = 0;

static uint64_t time_before_send, send_recv_lat;
static pid_t receiver_tid;

void signal_handler()
{
    send_recv_lat += now() - time_before_send;
    recv_count++;
}

void setup_signal_handler(void)
{
    struct sigaction action;
    action.sa_flags = 0;
    action.sa_handler = signal_handler;
    sigaction(SIGUSR1, &action, NULL);
}

static inline void do_send(int iter, pid_t target)
{
    for (int i = 0; i < iter; i++) {
        time_before_send = now();
        kill(target, SIGUSR1);
        while (recv_count <= i);
    }
}

static void *sender_thread(void *arg)
{
    bind_to_cpu(SLAVE_CPU);
    printf("sender %d run on CPU: %d\n", _gettid(), sched_getcpu());

    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGUSR1);
    sigprocmask(SIG_BLOCK, &signal_set, NULL);

    while (!init_ok);

    do_send(ITER, receiver_tid);
    send_ok = 1;

    return NULL;
}

int main()
{
    // Create the sender thread before binding CPU.
    pthread_t sender;
    pthread_create(&sender, NULL, sender_thread, NULL);

    bind_to_cpu(MASTER_CPU);
    receiver_tid = _gettid();
    printf("receiver %d run on CPU: %d\n", receiver_tid, sched_getcpu());

    setup_signal_handler();
    init_ok = 1;

    while (recv_count < ITER);

    printf("sent %d signals, received %d signals\n", ITER, recv_count);
    printf("send-recv latency = %ld cycles\n", DIV_ROUND(send_recv_lat, recv_count));

    return 0;
}
