#include <fcntl.h>
#include <sys/mman.h>
#ifdef SKYLOFT_SIGNAL_SWITCH
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#endif

#include <skyloft/platform.h>
#include <skyloft/uapi/dev.h>
#include <utils/log.h>

#ifdef SKYLOFT_SIGNAL_SWITCH
static sigset_t signal_set;
#else
static int dev_fd;
#endif

#ifdef SKYLOFT_SIGNAL_SWITCH
static inline int wait_for_signal()
{
    int sig;
    int ret = sigwait(&signal_set, &sig);
    if (ret) {
        log_warn("sigwait failed");
    }
    return ret;
}
#endif

int platform_dev_init()
{
#ifdef SKYLOFT_SIGNAL_SWITCH
    log_info("platform: switching mode: signal");
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGUSR1);
    return 0;
#else
    log_info("platform: switching mode: kmod");
    dev_fd = open(SKYLOFT_DEV_PATH, O_RDWR | O_SYNC);
    return dev_fd;
#endif
}

int skyloft_park_on_cpu(int cpu)
{
#ifdef SKYLOFT_SIGNAL_SWITCH
    int ret = 0;
    if (cpu >= 0) {
        ret = bind_to_cpu(0, cpu);
    }
    wait_for_signal();
    return ret;
#else
    return ioctl(dev_fd, SKYLOFT_IO_PARK, cpu >= 0 ? hw_cpu_id(cpu) : -1);
#endif
}

int skyloft_wakeup(pid_t target_tid)
{
#ifdef SKYLOFT_SIGNAL_SWITCH
    int ret = kill(target_tid, SIGUSR1);
    if (ret) {
        errno = -ESRCH;
    }
    return ret;
#else
    return ioctl(dev_fd, SKYLOFT_IO_WAKEUP, target_tid);
#endif
}

int skyloft_switch_to(pid_t target_tid)
{
#ifdef SKYLOFT_SIGNAL_SWITCH
    int ret = kill(target_tid, SIGUSR1);
    if (ret) {
        log_warn("send signal to thread %d failed: %d %d\n", target_tid, ret, errno);
    } else {
        wait_for_signal();
    }
    return ret;
#else
    return ioctl(dev_fd, SKYLOFT_IO_SWITCH_TO, target_tid);
#endif
}

#ifdef SKYLOFT_UINTR
int skyloft_setup_device_uintr(int flags)
{
    return ioctl(dev_fd, SKYLOFT_IO_SETUP_DEVICE_UINTR, flags);
}

int skyloft_timer_set_hz(int hz)
{
    return ioctl(dev_fd, SKYLOFT_IO_TIMER_SET_HZ, hz);
}
#endif
