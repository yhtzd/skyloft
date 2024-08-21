#pragma once

#include <unistd.h>
#include <sys/syscall.h>

static inline pid_t _gettid()
{
    return syscall(SYS_gettid);
}
