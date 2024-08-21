/*
 * log.c - the logging system
 */

#include <execinfo.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <utils/defs.h>
#include <utils/log.h>
#include <utils/time.h>
#include <utils/uintr.h>

#define MAX_LOG_LEN 4096

/* stored here to avoid pushing too much on the stack */
static __thread char buf[MAX_LOG_LEN];

extern __thread int g_logic_cpu_id;

static inline int log_color(int level)
{
    switch (level) {
    case LOG_CRIT:
        return 31; // red
    case LOG_ERR:
        return 31; // red
    case LOG_WARN:
        return 33; // yellow
    case LOG_NOTICE:
        return 35; // magenta
    case LOG_INFO:
        return 32; // green
    case LOG_DEBUG:
        return 36; // cyan
    default:
        return 0;
    }
}

void logk(int level, const char *fmt, ...)
{
    int flags;
    va_list ptr;

    local_irq_save(flags);

    int cpu = g_logic_cpu_id;
    __usec us = monotonic_us();
    sprintf(buf, "\x1b[37m[%ld.%06d %d] \x1b[%dm", us / USEC_PER_SEC, (int)(us % USEC_PER_SEC), cpu,
            log_color(level));

    off_t off = strlen(buf);
    va_start(ptr, fmt);
    vsnprintf(buf + off, MAX_LOG_LEN - off, fmt, ptr);
    va_end(ptr);
    printf("%s\x1b[m\n", buf);

    if (level <= LOG_ERR)
        fflush(stdout);

    local_irq_restore(flags);
}

#define MAX_CALL_DEPTH 256
void logk_backtrace(void)
{
    void *buf[MAX_CALL_DEPTH];
    const int calls = backtrace(buf, ARRAY_SIZE(buf));
    backtrace_symbols_fd(buf, calls, 1);
}

void logk_bug(bool fatal, const char *expr, const char *file, int line, const char *func)
{
    logk(fatal ? LOG_CRIT : LOG_WARN, "%s: %s:%d ASSERTION '%s' FAILED IN '%s'",
         fatal ? "FATAL" : "WARN", file, line, expr, func);

    if (fatal) {
        logk_backtrace();
        exit(EXIT_FAILURE);
    }
}
