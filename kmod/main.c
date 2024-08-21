#include <linux/cpumask.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/mman.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/isolation.h>

#include <asm/apic.h>
#include <asm/io.h>
#include <asm/irq_vectors.h>

#define UINTR_ENABLE (defined(CONFIG_X86_USER_INTERRUPTS) && defined(SKYLOFT_UINTR))

#if UINTR_ENABLE
#include <asm/skyloft.h>
#include <asm/uintr.h>
#endif

#include <skyloft/uapi/dev.h>

#define MAX_CPUS 128

static int cpu_list_len;
static uint cpu_list[MAX_CPUS];
module_param_array(cpu_list, uint, &cpu_list_len, S_IRUGO);

static struct cpumask skyloft_cpu_mask;

static inline uint64_t now_tsc(void)
{
    uint32_t lo, hi;
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi)::"rcx");
    return ((uint64_t)hi << 32) | lo;
}

static int skyloft_open(struct inode *inode, struct file *file)
{
    pr_info("skyloft: skyloft_open\n");
    return 0;
}

static int skyloft_release(struct inode *inode, struct file *file)
{
    pr_info("skyloft: skyloft_release\n");
    return 0;
}

#if UINTR_ENABLE
static int skyloft_setup_device_uintr(int flags)
{
    pr_info("skyloft: skyloft_setup_device_uintr on CPU %d, flags=%d\n", smp_processor_id(), flags);

    if (!current->thread.upid_activated) {
        pr_warn("skyloft: not a user interrupt receiver\n");
        return -EINVAL;
    }

    current->thread.upid_ctx->suppressed = true;
    current->thread.upid_ctx->timer_uintr = !!(flags & SKYLOFT_SETUP_DEVICE_UINTR_FLAGS_TIMER);
    return 0;
}

static int skyloft_ipi_send(int cpu)
{
    unsigned long flags;
    pr_debug_ratelimited("skyloft: send IPI from CPU %d to %d\n", smp_processor_id(), cpu);

    local_irq_save(flags);
    apic->send_IPI(cpu, UINTR_SKYLOFT_VECTOR);
    local_irq_restore(flags);
    return 0;
}

#define APIC_TIMER_PERIOD 25000000
#define APIC_DIVISOR      1

int skyloft_timer_set_hz(u32 hz)
{
    unsigned long flags;
    u32 clock = APIC_TIMER_PERIOD / hz / APIC_DIVISOR;
    u32 actual_hz = APIC_TIMER_PERIOD / APIC_DIVISOR / clock;

    if (!is_skyloft_enabled()) {
        pr_warn("skyloft: skyloft is not enabled on CPU %d\n", smp_processor_id());
        return -EINVAL;
    }

    if (clock == 0) {
        pr_warn("skyloft: too high timer frequency\n");
        return -EINVAL;
    }

    pr_info("skyloft: set timer frequency to %d Hz (TMICT=%d, actual=%d)\n", hz, clock, actual_hz);

    local_irq_save(flags);
    apic_write(APIC_LVTT, APIC_LVT_TIMER_PERIODIC | UINTR_SKYLOFT_VECTOR);
    apic_write(APIC_TDCR, APIC_TDR_DIV_1);
    apic_write(APIC_TMICT, clock);
    pr_info("skyloft: APIC_TMICT 0x%x APIC_TMCCT 0x%x APIC_TMDCR 0x%x APIC_LVTT 0x%x\n",
            apic_read(APIC_TMICT), apic_read(APIC_TMCCT), apic_read(APIC_TDCR),
            apic_read(APIC_LVTT));
    local_irq_restore(flags);
    return 0;
}

#define IPI_SEND_COUNT 10000000

static int skyloft_ipi_bench(int cpu)
{
    uint64_t start, end;
    int count = IPI_SEND_COUNT;
    int ipi_recv_count = per_cpu(irq_stat, cpu).skyloft_timer_spurious_count;

    pr_info("skyloft: bench IPI send from CPU %d to %d\n", smp_processor_id(), cpu);

    start = now_tsc();
    while (count--) {
        apic->send_IPI(cpu, UINTR_SKYLOFT_VECTOR);
    }
    end = now_tsc();

    ipi_recv_count = per_cpu(irq_stat, cpu).skyloft_timer_spurious_count - ipi_recv_count;
    pr_info("skyloft: skyloft_ipi_bench: total=%lld, avg=%lld (cycles), ipi_recv_cnt = %d\n", end - start,
             (end - start) / IPI_SEND_COUNT, ipi_recv_count);
    return 0;
}
#endif

static struct task_struct *ksched_lookup_task(pid_t pid)
{
    return pid_task(find_vpid(pid), PIDTYPE_PID);
}

static int skyloft_park_on_cpu(int cpu_id)
{
    int ret = 0;
    pr_info("skyloft: skyloft_park_on_cpu %d: %d -> %d\n", current->pid, smp_processor_id(),
            cpu_id);

    if (cpu_id >= 0) {
        if (cpu_id >= num_possible_cpus() || !cpumask_test_cpu(cpu_id, &skyloft_cpu_mask)) {
            pr_warn("skyloft: CPU %d is not bound to skyloft\n", cpu_id);
            ret = -EBUSY;
            goto out;
        }

        ret = set_cpus_allowed_ptr(current, cpumask_of(cpu_id));
        if (ret) {
            pr_warn("skyloft: failed to bind to CPU %d\n", cpu_id);
            goto out;
        }
    }

    if (task_is_running(current)) {
        __set_current_state(TASK_INTERRUPTIBLE);
    }

out:
    schedule();
    return ret;
}

static int skyloft_wakeup(pid_t pid)
{
    struct task_struct *p;

    rcu_read_lock();
    p = ksched_lookup_task(pid);

    if (!p) {
        rcu_read_unlock();
        return -ESRCH;
    }

    if (task_is_running(p)) {
        rcu_read_unlock();
        return -EBUSY;
    }

    wake_up_process(p);
    rcu_read_unlock();
    return 0;
}

static int skyloft_switch_to(pid_t target_tid)
{
    int err;
    unsigned long flags;

    local_irq_save(flags);

    if (target_tid > 0) {
        if ((err = skyloft_wakeup(target_tid))) {
            local_irq_restore(flags);
            return err;
        }
    }

    __set_current_state(TASK_INTERRUPTIBLE);
    local_irq_restore(flags);
    schedule();
    return 0;
}

static void skyloft_bind_cpu(void *info)
{
#if UINTR_ENABLE
    pr_info("skyloft: bind on CPU %d\n", smp_processor_id());
    if (skyloft_enable()) {
        pr_warn("skyloft: failed to bind on CPU %d\n", smp_processor_id());
        return;
    }
#endif
}

static void skyloft_unbind_cpu(void *info)
{
#if UINTR_ENABLE
    pr_info("skyloft: unbind on CPU %d\n", smp_processor_id());
    if (skyloft_disable()) {
        pr_warn("skyloft: failed to unbind on CPU %d\n", smp_processor_id());
        return;
    }
#endif
}

static long skyloft_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
    case SKYLOFT_IO_PARK:
        return skyloft_park_on_cpu(arg);
    case SKYLOFT_IO_WAKEUP:
        return skyloft_wakeup(arg);
    case SKYLOFT_IO_SWITCH_TO:
        return skyloft_switch_to(arg);
#if UINTR_ENABLE
    case SKYLOFT_IO_IPI_BENCH:
        return skyloft_ipi_bench(arg);
    case SKYLOFT_IO_SETUP_DEVICE_UINTR:
        return skyloft_setup_device_uintr(arg);
    case SKYLOFT_IO_TIMER_SET_HZ:
        return skyloft_timer_set_hz((u32)arg);
    case SKYLOFT_IO_IPI_SEND:
        return skyloft_ipi_send(arg);
#endif
    default:
        return -EINVAL;
    }
}

static const struct file_operations skyloft_fops = {
    .owner = THIS_MODULE,
    .open = skyloft_open,
    .release = skyloft_release,
    .unlocked_ioctl = skyloft_ioctl,
};

struct miscdevice skyloft_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = SKYLOFT_DEV_NAME,
    .fops = &skyloft_fops,
};

static int __init skyloft_init(void)
{
    int err;

    if (cpu_list_len == 0) {
        pr_err("skyloft: parameter `cpu_list` is empty\n");
        return -EINVAL;
    }

    for (int i = 0; i < cpu_list_len; i++) {
        if (cpu_list[i] >= num_possible_cpus()) {
            pr_err("skyloft: invalid cpu id %d\n", cpu_list[i]);
            return -EINVAL;
        }
        if (housekeeping_cpu(cpu_list[i], HK_TYPE_DOMAIN)) {
            pr_err("skyloft: cpu %d is not isolated\n", cpu_list[i]);
            return -EINVAL;
        }
        cpumask_set_cpu(cpu_list[i], &skyloft_cpu_mask);
    }

    err = misc_register(&skyloft_device);
    if (err) {
        pr_err("skyloft: cannot register misc device\n");
        return err;
    }

    preempt_disable();
    on_each_cpu_mask(&skyloft_cpu_mask, skyloft_bind_cpu, NULL, 1);
    preempt_enable();

    pr_info("skyloft: module initialized\n");
    return 0;
}

static void __exit skyloft_exit(void)
{
    misc_deregister(&skyloft_device);

    preempt_disable();
    on_each_cpu_mask(&skyloft_cpu_mask, skyloft_unbind_cpu, NULL, 1);
    preempt_enable();

    pr_info("skyloft: module exited\n");
}

module_init(skyloft_init);
module_exit(skyloft_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yuekai Jia");
MODULE_DESCRIPTION("Skyloft kernel module.");
MODULE_VERSION("0.01");
