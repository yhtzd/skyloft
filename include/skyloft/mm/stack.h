#pragma once

#include <skyloft/mm/tcache.h>
#include <skyloft/percpu.h>

DECLARE_PERCPU(struct tcache_percpu, stack_percpu);

/**
 * stack_alloc - allocates a stack
 *
 * Stack allocation is extremely cheap, think less than taking a lock.
 *
 * Returns an unitialized stack.
 */
static inline struct stack *stack_alloc(void) { return tcache_alloc(&percpu_get(stack_percpu)); }

/**
 * stack_free - frees a stack
 * @s: the stack to free
 */
static inline void stack_free(struct stack *s)
{
    tcache_free(&percpu_get(stack_percpu), (void *)s);
}

int stack_init_percpu();
int stack_init();
