/*
 * ops.h - useful x86_64 instructions
 */

#pragma once

#include <features.h>

static inline void cpu_relax(void)
{
#if __GNUC_PREREQ(10, 0)
#if __has_builtin(__builtin_ia32_pause)
    __builtin_ia32_pause();
#endif
#else
    asm volatile("pause");
#endif
}
