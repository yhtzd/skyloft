#include <skyloft/params.h>
#include <skyloft/sched.h>
#include <skyloft/sched/utimer.h>
#include <skyloft/stat.h>

#include <utils/log.h>
#include <utils/time.h>
#include <utils/uintr.h>

#ifdef UTIMER

#define NSEC_PER_TICK (NSEC_PER_SEC / TIMER_HZ)

static struct utimer utimer;

static int utimer_init(void)
{
    int i, ret;

    for (i = 0; i < proc->nr_ks; i++) {
        ret = uintr_register_sender(proc->all_ks[i].uintr_fd, 0);
        if (ret < 0) {
            log_err("%s: failed to register uintr sender to %d", __func__, i);
            return ret;
        }

        utimer.uintr_index[i] = ret;
        utimer.deadline[i] = now_ns() + NSEC_PER_TICK;
    }

    return 0;
}

__noreturn void utimer_main(void)
{
    int i;
    uint64_t stat;

    utimer_init();

    log_info("utimer: initialized on CPU %d(%d)", current_cpu_id(), hw_cpu_id(current_cpu_id()));

    while (true) {
        for (i = 0; i < proc->nr_ks; i++) {
            STAT_CYCLES_BEGIN(stat);
            if (now_ns() > utimer.deadline[i]) {
                _senduipi(utimer.uintr_index[i]);
                utimer.deadline[i] = now_ns() + NSEC_PER_TICK;
                ADD_STAT(UTIMER_SENDS, 1);
                ADD_STAT_CYCLES(UTIMER_CYCLES, stat);
            }
        }
    }
}

#endif