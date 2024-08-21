#pragma once

#include <utils/syscalls.h>

static inline int bind_to_cpu(int cpu_id)
{
    cpu_set_t cpuset;
    pid_t tid = _gettid();

    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    return sched_setaffinity(tid, sizeof(cpu_set_t), &cpuset);
}
