#include <errno.h>
#include <fcntl.h>
#include <linux/mman.h>
#include <numaif.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <skyloft/mm.h>
#include <skyloft/params.h>
#include <skyloft/platform.h>
#include <utils/assert.h>
#include <utils/log.h>

long mbind(void *start, size_t len, int mode, const unsigned long *nmask, unsigned long maxnode,
           unsigned flags)
{
    return syscall(__NR_mbind, start, len, mode, nmask, maxnode, flags);
}

static void sigbus_error(int sig) { panic("couldn't map pages"); }

void touch_mapping(void *base, size_t len, size_t pgsize)
{
    sighandler_t s;
    char *pos;

    /*
     * Unfortunately mmap() provides no error message if MAP_POPULATE fails
     * because of insufficient memory. Therefore, we manually force a write
     * on each page to make sure the mapping was successful.
     */
    s = signal(SIGBUS, sigbus_error);
    for (pos = (char *)base; pos < (char *)base + len; pos += pgsize) ACCESS_ONCE(*pos);
    signal(SIGBUS, s);
}

static void *__mem_map_common(void *base, size_t len, size_t pgsize, int flags, int fd,
                              unsigned long *mask, int numa_policy)
{
    void *addr;

    flags |= MAP_POPULATE;
    if (fd == -1)
        flags |= MAP_ANONYMOUS;
    if (base)
        flags |= MAP_FIXED;

    len = align_up(len, pgsize);

    switch (pgsize) {
    case PGSIZE_4KB:
        break;
    case PGSIZE_2MB:
        flags |= MAP_HUGETLB;
#ifdef MAP_HUGE_2MB
        flags |= MAP_HUGE_2MB;
#endif
        break;
    case PGSIZE_1GB:
#ifdef MAP_HUGE_1GB
        flags |= MAP_HUGETLB | MAP_HUGE_1GB;
#else
        return MAP_FAILED;
#endif
        break;
    default: /* fail on other sizes */
        return MAP_FAILED;
    }

    addr = mmap(base, len, PROT_READ | PROT_WRITE, flags, fd, 0);
    if (addr == MAP_FAILED) {
        log_err("failed %s", strerror(errno));
        return MAP_FAILED;
    }

    BUILD_ASSERT(sizeof(unsigned long) * 8 >= MAX_NUMA);
    if (mbind(addr, len, numa_policy, mask ? mask : NULL, mask ? MAX_NUMA + 1 : 0,
              MPOL_MF_STRICT | MPOL_MF_MOVE)) {
        log_err("failed %s", strerror(errno));
        goto fail;
    }

    touch_mapping(addr, len, pgsize);
    return addr;

fail:
    munmap(addr, len);
    return MAP_FAILED;
}

/**
 * mem_map_anom - map anonymous memory pages
 * @base: the base address (or NULL for automatic)
 * @len: the length of the mapping
 * @pgsize: the page size
 * @node: the NUMA node
 *
 * Returns the base address, or MAP_FAILED if out of memory
 */
void *mem_map_anom(void *base, size_t len, size_t pgsize, int node)
{
    unsigned long mask = (1 << node);
    return __mem_map_common(base, len, pgsize, MAP_PRIVATE, -1, &mask, MPOL_BIND);
}

/**
 * mem_map_shm - maps a System V shared memory segment backed with a file
 * @path: the file path to the shared memory backing file
 * @base: the base address to map the shared segment (or automatic if NULL)
 * @len: the length of the mapping
 * @pgsize: the size of each page
 * @node: the NUMA node
 *
 * Returns a pointer to the mapping, or NULL if the mapping failed.
 */
void *mem_map_shm_file(const char *path, void *base, size_t len, size_t pgsize, int node)
{
    int fd = open(path, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        return MAP_FAILED;
    }

    len = align_up(len, pgsize);
    if (ftruncate(fd, len) < 0) {
        return MAP_FAILED;
    }

    unsigned long mask = (1 << node);
    return __mem_map_common(base, len, pgsize, MAP_SHARED, fd, &mask, MPOL_BIND);
}

/**
 * mem_map_shm - maps a System V shared memory segment
 * @key: the unique key that identifies the shared region (e.g. use ftok())
 * @base: the base address to map the shared segment (or automatic if NULL)
 * @len: the length of the mapping
 * @pgsize: the size of each page
 * @exclusive: ensure this call creates the shared segment
 *
 * Returns a pointer to the mapping, or NULL if the mapping failed.
 */
void *mem_map_shm(mem_key_t key, void *base, size_t len, size_t pgsize, bool exclusive)
{
    void *addr;
    int shmid, flags = IPC_CREAT | 0777;

    BUILD_ASSERT(sizeof(mem_key_t) == sizeof(key_t));

    switch (pgsize) {
    case PGSIZE_4KB:
        break;
    case PGSIZE_2MB:
        flags |= SHM_HUGETLB;
#ifdef SHM_HUGE_2MB
        flags |= SHM_HUGE_2MB;
#endif
        break;
    case PGSIZE_1GB:
#ifdef SHM_HUGE_1GB
        flags |= SHM_HUGETLB | SHM_HUGE_1GB;
#else
        return MAP_FAILED;
#endif
        break;
    default: /* fail on other sizes */
        return MAP_FAILED;
    }

    if (exclusive)
        flags |= IPC_EXCL;

    shmid = shmget(key, len, flags);
    if (shmid == -1)
        return MAP_FAILED;

    addr = shmat(shmid, base, 0);
    if (addr == MAP_FAILED)
        return MAP_FAILED;

    touch_mapping(addr, len, pgsize);
    return addr;
}

/**
 * mem_unmap_shm - detach a shared memory mapping
 * @addr: the base address of the mapping
 *
 * Returns 0 if successful, otherwise fail.
 */
int mem_unmap_shm(void *addr)
{
    if (shmdt(addr) == -1)
        return -errno;
    return 0;
}

#define PAGEMAP_PGN_MASK       0x7fffffffffffffULL
#define PAGEMAP_FLAG_PRESENT   (1ULL << 63)
#define PAGEMAP_FLAG_SWAPPED   (1ULL << 62)
#define PAGEMAP_FLAG_FILE      (1ULL << 61)
#define PAGEMAP_FLAG_SOFTDIRTY (1ULL << 55)

/**
 * mem_lookup_page_phys_addrs - determines the physical address of pages
 * @addr: a pointer to the start of the pages (must be @size aligned)
 * @len: the length of the mapping
 * @pgsize: the page size (4KB, 2MB, or 1GB)
 * @paddrs: a pointer store the physical addresses (of @nr elements)
 *
 * Returns 0 if successful, otherwise failure.
 */
int mem_lookup_page_phys_addrs(void *addr, size_t len, size_t pgsize, physaddr_t *paddrs)
{
    uintptr_t pos;
    uint64_t tmp;
    int fd, i = 0, ret = 0;

    /*
     * 4 KB pages could be swapped out by the kernel, so it is not
     * safe to get a machine address. If we later decide to support
     * 4KB pages, then we need to mlock() the page first.
     */
    if (pgsize == PGSIZE_4KB)
        return -EINVAL;

    fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) {
        log_warn("%s(): cannot open /proc/self/pagemap: %s", __func__, strerror(errno));
        return -EIO;
    }

    for (pos = (uintptr_t)addr; pos < (uintptr_t)addr + len; pos += pgsize) {
        if (lseek(fd, pos / PGSIZE_4KB * sizeof(uint64_t), SEEK_SET) == (off_t)-1) {
            log_warn("%s(): seek error in /proc/self/pagemap: %s", __func__, strerror(errno));
            ret = -EIO;
            goto out;
        }
        if (read(fd, &tmp, sizeof(uint64_t)) <= 0) {
            log_warn("%s(): cannot read /proc/self/pagemap: %s", __func__, strerror(errno));
            ret = -EIO;
            goto out;
        }
        if (!(tmp & PAGEMAP_FLAG_PRESENT)) {
            ret = -ENODEV;
            goto out;
        }

        paddrs[i++] = (tmp & PAGEMAP_PGN_MASK) * PGSIZE_4KB;
    }

out:
    close(fd);
    return ret;
}

physaddr_t mem_virt2phys(void *addr)
{
    physaddr_t pa;
    int fd, ret;
    int page_size;
    uint64_t virt_pfn;
    off_t offset;
    uint64_t page;
    char pagemap[32];

    page_size = getpagesize();

    sprintf(pagemap, "/proc/self/pagemap");
    fd = open(pagemap, O_RDONLY);
    if (fd < 0) {
        log_warn("%s(): cannot open %s: %s", __func__, pagemap, strerror(errno));
        return -1;
    }

    virt_pfn = (uint64_t)addr / page_size;
    offset = sizeof(uint64_t) * virt_pfn;
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        log_warn("%s(): seek error in %s: %s", __func__, pagemap, strerror(errno));
        close(fd);
        return -1;
    }

    ret = read(fd, &page, sizeof(uint64_t));
    close(fd);
    if (ret < 0) {
        log_warn("%s(): cannot read %s: %s", __func__, pagemap, strerror(errno));
        close(fd);
        return -1;
    } else if (ret != sizeof(uint64_t)) {
        log_warn("%s(): read %d bytes from %s "
                 "but expected %lx",
                 __func__, ret, pagemap, sizeof(uint64_t));
        return -1;
    }

    log_info("%s %lx", __func__, page);
    /*
     * the pfn (page frame number) are bits 0-54 (see
     * pagemap.txt in linux Documentation)
     */
    if ((page & PAGEMAP_PGN_MASK) == 0)
        return -1;

    pa = ((page & PAGEMAP_PGN_MASK) * page_size) + ((unsigned long)addr % page_size);

    return pa;
}