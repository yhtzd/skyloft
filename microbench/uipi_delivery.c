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

#define WARMUP 10000
#define ITER   1000000
#define UVEC   1

static uint64_t time_before_send;
static uint64_t send_recv_lat = 0;

static volatile int counter = 0;
static volatile int init_ok = 0;
static int uintr_fd;
static int uintr_index;
static volatile int dev_fd;

static void __attribute__((interrupt))
__attribute__((target("general-regs-only", "inline-all-stringops")))
uintr_handler(struct __uintr_frame *ui_frame, unsigned long long vector)
{
    if (vector == UVEC) {
        send_recv_lat += now_tsc() - time_before_send;
        counter++;
    }
}


static void *sender_thread(void *arg)
{
    bind_to_cpu(SLAVE_CPU);
    printf("Sender runs on %d\n", sched_getcpu());

    while (!init_ok);

    for (int i = 0; i < ITER; i++) {
        time_before_send = now_tsc();
        _senduipi(uintr_index);
        while (counter <= i);
    }

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
    printf("Receiver runs on CPU: %d\n", sched_getcpu());

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
    while (counter < ITER);

    send_recv_lat = DIV_ROUND(send_recv_lat, ITER);
    printf("count = %d\n", counter);
    printf("delivery latency = %ld cycles\n", send_recv_lat);

    /** clean up */
    uintr_unregister_handler(0);

    return 0;
}
