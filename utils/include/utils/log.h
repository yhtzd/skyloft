/*
 * log.h - the logging service
 */

#pragma once

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void logk(int level, const char *fmt, ...) __attribute__((__format__(__printf__, 2, 3)));
extern void logk_backtrace(void);

#if defined(LOG_LEVEL_DEBUG)
#define MAX_LOG_LEVEL LOG_DEBUG
#elif defined(LOG_LEVEL_INFO)
#define MAX_LOG_LEVEL LOG_INFO
#elif defined(LOG_LEVEL_NOTICE)
#define MAX_LOG_LEVEL LOG_NOTICE
#elif defined(LOG_LEVEL_WARN)
#define MAX_LOG_LEVEL LOG_WARN
#elif defined(LOG_LEVEL_ERR)
#define MAX_LOG_LEVEL LOG_ERR
#elif defined(LOG_LEVEL_CRIT)
#define MAX_LOG_LEVEL LOG_CRIT
#else
#define MAX_LOG_LEVEL LOG_CRIT
#endif

#define do_logk(level, fmt, ...)             \
    do {                                     \
        if (level <= MAX_LOG_LEVEL)          \
            logk(level, fmt, ##__VA_ARGS__); \
    } while (0)

/* forces format checking */
#define no_logk(level, fmt, ...)             \
    do {                                     \
        if (0)                               \
            logk(level, fmt, ##__VA_ARGS__); \
    } while (0)

enum {
    LOG_CRIT = 1,   /* critical */
    LOG_ERR = 2,    /* error */
    LOG_WARN = 3,   /* warning */
    LOG_NOTICE = 4, /* significant normal condition */
    LOG_INFO = 5,   /* informational */
    LOG_DEBUG = 6,  /* debug */
};

#define log_crit(fmt, ...)   do_logk(LOG_CRIT, fmt, ##__VA_ARGS__)
#define log_err(fmt, ...)    do_logk(LOG_ERR, fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...)   do_logk(LOG_WARN, fmt, ##__VA_ARGS__)
#define log_notice(fmt, ...) do_logk(LOG_NOTICE, fmt, ##__VA_ARGS__)
#define log_info(fmt, ...)   do_logk(LOG_INFO, fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...)  do_logk(LOG_DEBUG, fmt, ##__VA_ARGS__)

#define log_once(level, fmt, ...)               \
    ({                                          \
        static bool __once;                     \
        if (unlikely(!__once)) {                \
            __once = true;                      \
            do_logk(level, fmt, ##__VA_ARGS__); \
        }                                       \
    })

#define log_crit_once(fmt, ...)   log_once(LOG_CRIT, fmt, ##__VA_ARGS__)
#define log_err_once(fmt, ...)    log_once(LOG_ERR, fmt, ##__VA_ARGS__)
#define log_warn_once(fmt, ...)   log_once(LOG_WARN, fmt, ##__VA_ARGS__)
#define log_notice_once(fmt, ...) log_once(LOG_NOTICE, fmt, ##__VA_ARGS__)
#define log_info_once(fmt, ...)   log_once(LOG_INFO, fmt, ##__VA_ARGS__)
#define log_debug_once(fmt, ...)  log_once(LOG_DEBUG, fmt, ##__VA_ARGS__)

#define log_first_n(level, num, fmt, ...)       \
    ({                                          \
        static int __n = (num);                 \
        if (__n > 0) {                          \
            __n--;                              \
            do_logk(level, fmt, ##__VA_ARGS__); \
        }                                       \
    })

#define log_crit_first_n(num, fmt, ...)   log_first_n(LOG_CRIT, num, fmt, ##__VA_ARGS__)
#define log_err_first_n(num, fmt, ...)    log_first_n(LOG_ERR, num, fmt, ##__VA_ARGS__)
#define log_warn_first_n(num, fmt, ...)   log_first_n(LOG_WARN, num, fmt, ##__VA_ARGS__)
#define log_notice_first_n(num, fmt, ...) log_first_n(LOG_NOTICE, num, fmt, ##__VA_ARGS__)
#define log_info_first_n(num, fmt, ...)   log_first_n(LOG_INFO, num, fmt, ##__VA_ARGS__)
#define log_debug_first_n(num, fmt, ...)  log_first_n(LOG_DEBUG, num, fmt, ##__VA_ARGS__)

#define panic(fmt, ...)                     \
    do {                                    \
        logk(LOG_CRIT, fmt, ##__VA_ARGS__); \
        exit(EXIT_FAILURE);                 \
    } while (0)

#ifdef __cplusplus
}
#endif
