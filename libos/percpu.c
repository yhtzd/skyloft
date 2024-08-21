#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <skyloft/global.h>
#include <skyloft/mm.h>
#include <skyloft/params.h>
#include <skyloft/platform.h>
#include <skyloft/sched.h>

#include <utils/log.h>
#include <utils/spinlock.h>

unsigned int thread_count;
__thread void *percpu_ptr;

define_cpu_array(void *, percpu_offsets, USED_CPUS);

extern const char __percpu_start[];
extern const char __percpu_end[];

static int alloc_percpu_data(void)
{
    void *addr;
    size_t len = __percpu_end - __percpu_start;

    /* no percpu data */
    if (!len)
        return 0;

    addr = mem_map_anom(NULL, len, PGSIZE_2MB, current_numa_node());
    if (addr == MAP_FAILED)
        return -ENOMEM;

    memset(addr, 0, len);
    percpu_ptr = addr;
    percpu_offsets[current_cpu_id()] = addr;
    return 0;
}

/**
 * percpu_init - initializes a thread
 *
 * Returns 0 if successful, otherwise fail.
 */
int percpu_init(void)
{
    int ret;

    if ((ret = alloc_percpu_data()) < 0)
        return ret;

    return 0;
}
