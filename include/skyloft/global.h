#ifndef _GLOBAL_H_
#define _GLOBAL_H_

#include <stdbool.h>
#include <sys/types.h>

#include <skyloft/params.h>

#include <utils/atomic.h>
#include <utils/spinlock.h>

#define DAEMON_APP_ID 0

#define SHM_META_PATH    "/dev/shm/skyloft_meta"
#define SHM_APPS_PATH    "/dev/shm/skyloft_apps"
#define SHM_INGRESS_PATH "/mnt/huge/skyloft_ingress"
#define SHM_INGRESS_KEY  0x696d736b /* "imsk" */
#define SHM_INGRESS_SIZE 0x20000000

struct metadata {
    spinlock_t lock;
    volatile int nr_apps;
    volatile uint64_t boot_time_us;
    /* maps cpu to app */
    volatile atomic_int apps[USED_CPUS];
};

extern struct metadata *shm_metadata;
extern struct proc *shm_apps;

int global_init(void);

#endif // _GLOBAL_H_
