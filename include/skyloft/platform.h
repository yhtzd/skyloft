#pragma once

#include <numa.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

#include <skyloft/params.h>
#include <utils/assert.h>
#include <utils/defs.h>

int platform_init();
int platform_init_percpu();
int cpubind_init_percpu();

/* CPU */

static inline int hw_cpu_id(int cpu_id)
{
    declear_cpu_array(int, hw_cpu_list, USED_CPUS);
    if (cpu_id >= USED_CPUS) {
        return -1;
    }
    return hw_cpu_list[cpu_id];
}

static inline int cpu_sibling(int cpu_id)
{
    declear_cpu_array(int, cpu_siblings, USED_CPUS);
    return cpu_siblings[cpu_id];
}

static inline int cpu_numa_node(int cpu_id)
{
    return numa_node_of_cpu(hw_cpu_id(cpu_id));
}

static inline pid_t _gettid()
{
    return syscall(SYS_gettid);
}

int bind_to_cpu(int tid, int cpu_id);
int unbind_cpus(int tid);

/* Memory */

typedef unsigned int mem_key_t;

void touch_mapping(void *base, size_t len, size_t pgsize);
void *mem_map_anom(void *base, size_t len, size_t pgsize, int node);
void *mem_map_shm_file(const char *path, void *base, size_t len, size_t pgsize, int node);
void *mem_map_shm(mem_key_t key, void *base, size_t len, size_t pgsize, bool exclusive);
int mem_unmap_shm(void *base);

typedef unsigned long physaddr_t; /* physical addresses */
typedef unsigned long virtaddr_t; /* virtual addresses */

int mem_lookup_page_phys_addrs(void *addr, size_t len, size_t pgsize, physaddr_t *maddrs);

static inline int mem_lookup_page_phys_addr(void *addr, size_t pgsize, physaddr_t *paddr)
{
    return mem_lookup_page_phys_addrs(addr, pgsize, pgsize, paddr);
}

physaddr_t mem_virt2phys(void *addr);

/* Remote FD */

static inline int pidfd_open(pid_t pid)
{
    return syscall(SYS_pidfd_open, pid, 0);
}

static inline int pidfd_getfd(int pidfd, int targetfd)
{
    return syscall(SYS_pidfd_getfd, pidfd, targetfd, 0);
}

/* Kernel module API */

int skyloft_park_on_cpu(int cpu);
int skyloft_wakeup(pid_t target_tid);
int skyloft_switch_to(pid_t target_tid);

/* User interrupt */
#ifdef SKYLOFT_UINTR

#include <utils/uintr.h>

#define UVEC 1

static __always_inline __attribute__((target("general-regs-only"))) int uintr_index()
{
    extern __thread int g_uintr_index;
    return g_uintr_index;
}

static inline uint64_t rdfsbase()
{
    uint64_t val;
    asm volatile("rdfsbase %0" : "=r"(val));
    return val;
}

#define local_irq_disable _clui
#define local_irq_enable  _stui
#define local_irq_enabled _testui

#define local_irq_save(flags) \
    do {                      \
        flags = _testui();    \
        _clui();              \
    } while (0)
#define local_irq_restore(flags) \
    do {                         \
        if (flags)               \
            _stui();             \
    } while (0)

int skyloft_setup_device_uintr(int flags);
int skyloft_timer_set_hz(int hz);
#else
#define local_irq_disable()
#define local_irq_enable()
#define local_irq_enabled()      false
#define local_irq_save(flags)    ((void)flags)
#define local_irq_restore(flags) ((void)flags)
#endif
