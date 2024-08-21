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

#define RUN_TIME_MS 1000    // 1s
#define TIMER_HZ    1000000 // 1us
#define WORKS       2e9
#define UVEC        1

#define now now_tsc

static uint64_t recv_lat = 0;
static volatile int counter = 0;

static int dev_fd;
static int uintr_index;

static void __attribute__((interrupt))
__attribute__((target("general-regs-only", "inline-all-stringops")))
uintr_handler(struct __uintr_frame *ui_frame, unsigned long long vector)
{
    _senduipi(uintr_index);
    asm volatile("" : : :"rdx", "rcx", "rsi", "rdi",
        "r8", "r9", "r10", "r11");
    counter++;
}

int main(int argc, char **argv)
{
    int ret = 0;

    bind_to_cpu(MASTER_CPU);
    printf("run on CPU: %d\n", sched_getcpu());

    uint64_t work_time = now();
    dummy_work(WORKS);
    work_time = now() - work_time;
    printf("work time = %ld cycles\n", work_time);

    ret = uintr_register_handler(uintr_handler, 0);
    if (ret < 0) {
        printf("Failed to register a handler %d\n", ret);
        exit(EXIT_FAILURE);
    }

    uintr_index = uintr_register_self(UVEC, 0);
    if (uintr_index < 0) {
        printf("Failed to register the sender for self\n");
        exit(EXIT_FAILURE);
    }
    dev_fd = open("/dev/skyloft", O_RDWR);
    if (dev_fd < 0) {
        printf("Failed to open the device\n");
        exit(EXIT_FAILURE);
    }

    ret = ioctl(dev_fd, SKYLOFT_IO_SETUP_DEVICE_UINTR, 1); // flags=1: bind timer
    if (ret < 0) {
        perror("Failed to register skyloft\n");
        exit(EXIT_FAILURE);
    }

    ret = ioctl(dev_fd, SKYLOFT_IO_TIMER_SET_HZ, TIMER_HZ);
    if (ret < 0) {
        perror("Failed to set timer frequency\n");
        exit(EXIT_FAILURE);
    }

    _senduipi(uintr_index);
    _stui();
    recv_lat = now();
    dummy_work(WORKS);
    recv_lat = now() - recv_lat - work_time;
    _clui();

    printf("count = %d\n", counter);
    recv_lat = DIV_ROUND(recv_lat, counter);
    printf("recv latency = %ld cycles\n", recv_lat);

    /** clean up */
    uintr_unregister_handler(0);

    return 0;
}
