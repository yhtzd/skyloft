/*
 * hook.h - support for symbol replacement
 */

#pragma once

#include <dlfcn.h>

#include <skyloft/uapi/task.h>
#include <utils/defs.h>

#define HOOK1(__fn, __ret, __arg1)                 \
    __ret __fn(__arg1 __a1)                        \
    {                                              \
        static __ret (*real_##__fn)(__arg1);       \
        if (unlikely(!real_##__fn)) {              \
            real_##__fn = dlsym(RTLD_NEXT, #__fn); \
        }                                          \
        __ret __t = real_##__fn(__a1);             \
        return __t;                                \
    }

#define HOOK1_NORET(__fn, __arg1)                  \
    void __fn(__arg1 __a1)                         \
    {                                              \
        static void (*real_##__fn)(__arg1);        \
        if (unlikely(!real_##__fn)) {              \
            real_##__fn = dlsym(RTLD_NEXT, #__fn); \
        }                                          \
        real_##__fn(__a1);                         \
    }

#define HOOK2(__fn, __ret, __arg1, __arg2)           \
    __ret __fn(__arg1 __a1, __arg2 __a2)             \
    {                                                \
        static __ret (*real_##__fn)(__arg1, __arg2); \
        if (unlikely(!real_##__fn)) {                \
            real_##__fn = dlsym(RTLD_NEXT, #__fn);   \
        }                                            \
        __ret __t = real_##__fn(__a1, __a2);         \
        return __t;                                  \
    }

#define HOOK3(__fn, __ret, __arg1, __arg2, __arg3)           \
    __ret __fn(__arg1 __a1, __arg2 __a2, __arg3 __a3)        \
    {                                                        \
        static __ret (*real_##__fn)(__arg1, __arg2, __arg3); \
        if (unlikely(!real_##__fn)) {                        \
            real_##__fn = dlsym(RTLD_NEXT, #__fn);           \
        }                                                    \
        __ret __t = real_##__fn(__a1, __a2, __a3);           \
        return __t;                                          \
    }

#define HOOK4(__fn, __ret, __arg1, __arg2, __arg3, __arg4)           \
    __ret __fn(__arg1 __a1, __arg2 __a2, __arg3 __a3, __arg4 __a4)   \
    {                                                                \
        static __ret (*real_##__fn)(__arg1, __arg2, __arg3, __arg4); \
        if (unlikely(!real_##__fn)) {                                \
            real_##__fn = dlsym(RTLD_NEXT, #__fn);                   \
        }                                                            \
        __ret __t = real_##__fn(__a1, __a2, __a3, __a4);             \
        return __t;                                                  \
    }
