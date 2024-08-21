#include <skyloft/global.h>
#include <skyloft/params.h>
#include <skyloft/sched.h>
#include <skyloft/stat.h>
#include <skyloft/uapi/task.h>
#include <stdatomic.h>
#include <utils/atomic.h>
#include <utils/log.h>

void __real_exit(int code);

void __wrap_exit(int code)
{
    int i, target_tid;

    if (!shm_apps || !current_app())
        goto real_exit;

    if (atomic_flag_test_and_set(&current_app()->exited))
        return;

    log_info("Exiting libos ...");

#ifdef SKYLOFT_STAT
    print_stats();
#endif

    spin_lock(&shm_metadata->lock);
    shm_metadata->nr_apps--;
    spin_unlock(&shm_metadata->lock);

    /* bind to other CPUs that enable timer interrupts to make wakeup and exit successful */
    unbind_cpus(0);

    if (!is_daemon()) {
        for (i = 0; i < USED_CPUS; i++) {
            target_tid = shm_apps[DAEMON_APP_ID].all_ks[i].tid;
            atomic_store(&shm_metadata->apps[i], DAEMON_APP_ID);
            skyloft_wakeup(target_tid);
        }
    } else if (shm_metadata->nr_apps == 0) {
        unlink(SHM_META_PATH);
        unlink(SHM_APPS_PATH);
    } else {
        log_warn("Daemon should not exit");
    }

real_exit:
    __real_exit(code);
}
