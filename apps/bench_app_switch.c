#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include <skyloft/uapi/task.h>
#include <utils/atomic.h>

#include "bench_common.h"

#define SHM_SIZE 4096
#define SHM_PATH "/skyloft_app_bench"

#define NUM_APPS 2
#define ROUNDS   1000000

static int shm_fd;

void *open_shared_memory()
{
    shm_fd = shm_open(SHM_PATH, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        printf("shm_open failed: %d\n", errno);
        return NULL;
    }

    if (ftruncate(shm_fd, SHM_SIZE) < 0) {
        printf("ftruncate failed: %d\n", errno);
        return NULL;
    }

    void *addr = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (addr == MAP_FAILED) {
        return NULL;
    }

    return addr;
}

void close_shared_memory(void *addr)
{
    munmap(addr, SHM_SIZE);
    close(shm_fd);
    shm_unlink(SHM_PATH);
}

void bench_yield_between_apps()
{
    for (int i = 0; i < ROUNDS / NUM_APPS; ++i) {
        sl_task_yield();
    }
}

void app_main(void *arg)
{
    atomic_int *flag = open_shared_memory();
    assert(flag);
    atomic_inc(flag);

    printf("Wait for other apps to start ...\n");
    while (atomic_load_acq(flag) < NUM_APPS) {
        sl_task_yield();
    }
    printf("All apps entered, start benchmarking ...\n");

    bench_one("yield_between_apps", bench_yield_between_apps, ROUNDS);

    atomic_dec(flag);
    close_shared_memory(flag);
}

int main(int argc, char *argv[])
{
    printf("Benmark for yield between apps\n");
    sl_libos_start(app_main, NULL);
}
