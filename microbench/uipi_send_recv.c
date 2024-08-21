#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <skyloft/uapi/dev.h>
#include <utils/time.h>
#include <utils/uintr.h>
#include <utils/cpu.h>

#include "common.h"

#define MASTER_CPU 2
#define SLAVE_CPU  3

#define WARMUP  10000
#define ITER    10000000
#define WORKS   2e9
#define UVEC    1

#define now now_tsc

static uint64_t send_lat = 0;
static uint64_t recv_lat = 0;

static volatile int counter = 0;
static volatile int init_ok = 0;
static volatile int send_ok = 0;
static int uintr_fd;
static int uintr_index;
static int dev_fd;

static void __attribute__((interrupt))
__attribute__((target("general-regs-only", "inline-all-stringops")))
uintr_handler(struct __uintr_frame *ui_frame, unsigned long long vector)
{
    asm volatile("" : : :"rdx", "rcx", "rsi", "rdi",
        "r8", "r9", "r10", "r11");
    counter++;
}

static void *sender_thread(void *arg)
{
    bind_to_cpu(SLAVE_CPU);
    printf("sender run on CPU: %d\n", sched_getcpu());

    while (!init_ok);

    send_lat = now();
    for (int i = 0; i < ITER; i++) {
        _senduipi(uintr_index);
    }
    send_lat = now() - send_lat;
    send_ok = 1;

    return NULL;
}

static void bench_rdtsc()
{
    uint64_t sum = 0;
    for (int i = 0; i < ITER; i++) {
        uint64_t a = now_tsc();
        uint64_t b = now_tsc();
        sum += b - a;
    }
    printf("rdtsc latency = %ld cycles\n", DIV_ROUND(sum, ITER));
}

int main(int argc, char **argv)
{
    int ret = 0;

    // Create the sender thread before binding CPU.
    pthread_t sender;
    pthread_create(&sender, NULL, sender_thread, NULL);

    bind_to_cpu(MASTER_CPU);
    printf("run on CPU: %d\n", sched_getcpu());

    uint64_t work_time = now();
    dummy_work(WORKS);
    work_time = now() - work_time;
    printf("work time = %ld cycles\n", work_time);

    dev_fd = open("/dev/skyloft", O_RDWR);
    if (dev_fd < 0) {
        printf("Failed to open the device\n");
        exit(EXIT_FAILURE);
    }

    bench_rdtsc();

    ret = uintr_register_handler(uintr_handler, 0);
    if (ret < 0) {
        printf("Failed to register a handler %d\n", ret);
        exit(EXIT_FAILURE);
    }

    uintr_fd = uintr_vector_fd(UVEC, 0);
    if (uintr_fd < 0) {
        printf("Failed to create a vector\n");
        exit(EXIT_FAILURE);
    }

    uintr_index = uintr_register_sender(uintr_fd, 0);
    if (uintr_index < 0) {
        printf("Failed to register a sender %d\n", uintr_index);
        exit(EXIT_FAILURE);
    }

    init_ok = 1;

    _stui();
    recv_lat = now();
    dummy_work(WORKS);
    recv_lat = now() - recv_lat - work_time;
    _clui();

    assert(send_ok);

    printf("count = %d\n", counter);
    send_lat = DIV_ROUND(send_lat, ITER);
    recv_lat = DIV_ROUND(recv_lat, counter);
    printf("send latency = %ld cycles\n", send_lat);
    printf("recv latency = %ld cycles\n", recv_lat);

    /** clean up */
    uintr_unregister_handler(0);

    return 0;
}
