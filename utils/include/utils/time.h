#ifndef _TIME_H_
#define _TIME_H_

#include <stdint.h>
#include <time.h>
#include <unistd.h>

typedef uint64_t __sec;
typedef uint64_t __nsec;
typedef uint64_t __usec;

#define NSEC_PER_SEC  (1000000000UL)
#define NSEC_PER_MSEC (1000000UL)
#define NSEC_PER_USEC (1000UL)
#define USEC_PER_SEC  (1000000UL)
#define USEC_PER_MSEC (1000UL)
#define MSEC_PER_SEC  (1000UL)

#define time_nsec_to_sec(ns)  ((ns) / NSEC_PER_SEC)
#define time_nsec_to_msec(ns) ((ns) / NSEC_PER_MSEC)
#define time_nsec_to_usec(ns) ((ns) / NSEC_PER_USEC)

#define time_sec_to_nsec(sec)   ((sec) * NSEC_PER_SEC)
#define time_msec_to_nsec(msec) ((msec) * NSEC_PER_MSEC)
#define time_usec_to_nsec(usec) ((usec) * NSEC_PER_USEC)

extern __usec g_boot_time_us;

static inline __nsec now_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
}

static inline __usec now_us()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * USEC_PER_SEC + ts.tv_nsec / NSEC_PER_USEC;
}

/// Return monotonic time in microseconds since system boot.
static inline __usec monotonic_us()
{
    return now_us() - g_boot_time_us;
}

static inline uint64_t now_tsc()
{
    uint32_t lo, hi;
    asm volatile("rdtscp" : "=a"(lo), "=d"(hi)::"rcx");
    return ((uint64_t)hi << 32) | lo;
}

static inline void spin_until(__nsec deadline)
{
    while (now_ns() < deadline);
}

static inline void spin(__nsec duration)
{
    spin_until(now_ns() + duration);
}

#endif // _TIME_H_
