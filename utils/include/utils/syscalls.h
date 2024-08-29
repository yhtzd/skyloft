#pragma once

#include <sys/syscall.h>
#include <unistd.h>

static inline pid_t _gettid()
{
    return syscall(SYS_gettid);
}
