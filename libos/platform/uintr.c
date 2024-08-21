#ifdef SKYLOFT_UINTR

#include <skyloft/global.h>
#include <skyloft/platform.h>
#include <skyloft/uapi/task.h>
#include <utils/log.h>
#include <utils/uintr.h>

extern void uintr_handler();

__thread int g_uintr_index;

int platform_uintr_init_percpu(void)
{
#ifdef SKYLOFT_DPDK
    if (sl_current_cpu_id() == IO_CPU)
        return 0;
#endif

    int ret = uintr_register_handler(uintr_handler, 0);
    if (ret < 0) {
        log_err("uintr: failed to register a handler %d", ret);
        return -1;
    }

    g_uintr_index = uintr_register_self(UVEC, 0);
    if (g_uintr_index < 0) {
        log_err("uintr: failed to register the sender for self");
        return -1;
    }

    ret = skyloft_setup_device_uintr(1);
    if (ret < 0) {
        log_err("uintr: failed to map device interrupts to userspace");
        return -1;
    }

    ret = skyloft_timer_set_hz(TIMER_HZ);
    if (ret < 0) {
        log_err("uintr: failed to set timer frequency");
        return -1;
    }

    local_irq_disable();
    _senduipi(g_uintr_index);

    log_info("uintr: CPU %d registered uintr index %d", sl_current_cpu_id(), g_uintr_index);

    return 0;
}

#endif
