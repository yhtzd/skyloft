/*
 * bitmap.h - a library for bit array manipulation
 */

#include <utils/log.h>

struct init_entry {
    const char *name;
    int (*init)(void);
};

#define INITIALIZER(name, suffix)      \
    {                                  \
        __cstr(name), &name##_##suffix \
    }

static inline int run_init_handlers(const struct init_entry *h, int n)
{
    int i, ret;

    for (i = 0; i < n; i++) {
        ret = h[i].init();
        if (ret) {
            log_err("\t%s init failed, ret = %d", h[i].name, ret);
            return ret;
        }
    }

    return 0;
}