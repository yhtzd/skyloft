#include <assert.h>
#include <stdio.h>

#include <skyloft/uapi/params.h>
#include <skyloft/uapi/task.h>
#include <utils/time.h>

void do_work(void *arg)
{
    int cpu_id = sl_current_cpu_id();
    assert(cpu_id == (int)(uintptr_t)arg);
    printf("Hello from CPU %d\n", cpu_id);

    for (int i = 0; i < 10; i++) {

        __nsec before = now_ns();
        __nsec dur_ns = 1000 * NSEC_PER_MSEC;

        printf("running (%d, %d): iter=%d\n", sl_current_app_id(), sl_current_task_id(), i);
        for (;;) {
            if (now_ns() - before > dur_ns)
                break;
        }

        sl_task_yield();
    }
}

void app_main(void *arg)
{
    printf("%d\n", USED_CPUS);
    for (size_t i = 1; i < USED_CPUS; i++) {
        sl_task_spawn_oncpu(i, do_work, (void *)i, 0);
    }
    do_work((void *)0);
}

int main(int argc, char *argv[])
{
    printf("Hello, world!\n");
    sl_libos_start(app_main, (void *)2333);
}
