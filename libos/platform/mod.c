#include <utils/log.h>

extern int platform_cpu_init();
extern int platform_dev_init();
#ifdef SKYLOFT_UINTR
extern int platform_uintr_init_percpu();
#endif

int platform_init()
{
    if (platform_cpu_init() < 0) {
        log_err("platform_cpu_init failed");
        return -1;
    }

    if (platform_dev_init() < 0) {
        log_err("platform_dev_init failed");
        return -1;
    }

    return 0;
}

int platform_init_percpu()
{
#if defined(SKYLOFT_UINTR) && !defined(SKYLOFT_SCHED_SQ) && !defined(SKYLOFT_SCHED_SQ_LCBE)
    if (platform_uintr_init_percpu() < 0) {
        log_err("platform_uintr_init_percpu failed");
        return -1;
    }
#endif
    return 0;
}
