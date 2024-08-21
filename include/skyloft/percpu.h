/*
 * percpu.h - percpu data and other utilities
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <skyloft/params.h>
#include <utils/defs.h>

/* used to define percpu variables */
#define DEFINE_PERCPU(type, name) \
    typeof(type) __percpu_##name __percpu __attribute__((section(".percpu,\"\",@nobits#")))

/* used to make percpu variables externally available */
#define DECLARE_PERCPU(type, name) extern DEFINE_PERCPU(type, name)

extern __thread void *percpu_ptr;
extern unsigned int thread_count;
extern const char __percpu_start[];

declear_cpu_array(void *, percpu_offsets, USED_CPUS);

/**
 * percpu_get_remote - get a percpu variable on a specific CPU
 * @var: the percpu variable
 * @cpu_id: the CPU id
 *
 * Returns a percpu variable.
 */
#define percpu_get_remote(var, cpu_id)                                                          \
    (*((__force typeof(__percpu_##var) *)((uintptr_t) & __percpu_##var +                        \
                                                            (uintptr_t)percpu_offsets[cpu_id] - \
                                                            (uintptr_t)__percpu_start)))

static inline void *__percpu_get(void __percpu *key)
{
    return (__force void *)((uintptr_t)key + (uintptr_t)percpu_ptr - (uintptr_t)__percpu_start);
}

/**
 * percpu_get - get the local percpu variable
 * @var: the percpu variable
 *
 * Returns a percpu variable.
 */
#define percpu_get(var) (*((typeof(__percpu_##var) *)(__percpu_get(&__percpu_##var))))

/**
 * thread_is_active - is the thread initialized?
 * @thread: the thread id
 *
 * Returns true if yes, false if no.
 */
#define thread_is_active(thread) (percpu_offsets[thread] != NULL)

static inline int __thread_next_active(int thread)
{
    while (thread < (int)thread_count) {
        if (thread_is_active(++thread))
            return thread;
    }

    return thread;
}

/**
 * for_each_thread - iterates over each thread
 * @thread: the thread id
 */
#define for_each_thread(thread) \
    for ((thread) = -1; (thread) = __thread_next_active(thread), (thread) < thread_count;)

extern __thread bool thread_init_done;

extern int percpu_init(void);
