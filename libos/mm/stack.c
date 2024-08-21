/*
 * stack.c - allocates and manages per-thread stacks
 */

#include <errno.h>
#include <sys/mman.h>

#include <skyloft/mm.h>
#include <skyloft/percpu.h>
#include <skyloft/task.h>
#include <utils/atomic.h>
#include <utils/log.h>

#define STACK_BASE_ADDR 0x200000000000UL

static struct tcache *stack_tcache;
DEFINE_PERCPU(struct tcache_percpu, stack_percpu);

static struct stack *stack_create(void *base)
{
    void *stack_addr;
    struct stack *s;

    stack_addr = mmap(base, sizeof(struct stack), PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack_addr == MAP_FAILED)
        return NULL;

    s = (struct stack *)stack_addr;

    return s;
}

/* WARNING: the contents of the stack may be lost after reclaiming. */
static void stack_reclaim(struct stack *s)
{
    int ret;
    ret = madvise(s->payload, RUNTIME_STACK_SIZE, MADV_DONTNEED);
    WARN_ON_ONCE(ret);
}

static DEFINE_SPINLOCK(stack_lock);
static int free_stack_count;
static struct stack *free_stacks[MAX_TASKS];
static atomic_long stack_pos = STACK_BASE_ADDR;

static void stack_tcache_free(struct tcache *tc, int nr, void **items)
{
    int i;

    /* try to release the backing memory first */
    for (i = 0; i < nr; i++) stack_reclaim((struct stack *)items[i]);

    /* then make the stacks available for reallocation */
    spin_lock(&stack_lock);
    for (i = 0; i < nr; i++) free_stacks[free_stack_count++] = items[i];
    BUG_ON(free_stack_count >= MAX_TASKS + TCACHE_DEFAULT_MAG_SIZE);
    spin_unlock(&stack_lock);
}

static int stack_tcache_alloc(struct tcache *tc, int nr, void **items)
{
    void *base;
    int i = 0;

    spin_lock(&stack_lock);
    while (free_stack_count && i < nr) {
        items[i++] = free_stacks[--free_stack_count];
    }
    spin_unlock(&stack_lock);

    for (; i < nr; i++) {
        base = (void *)atomic_fetch_add(&stack_pos, sizeof(struct stack));
        items[i] = stack_create(base);
        if (unlikely(!items[i]))
            goto fail;
    }

    return 0;

fail:
    log_err("stack: failed to allocate stack memory");
    stack_tcache_free(tc, i, items);
    return -ENOMEM;
}

static const struct tcache_ops stack_tcache_ops = {
    .alloc = stack_tcache_alloc,
    .free = stack_tcache_free,
};

/**
 * stack_init_thread - intializes per-thread state
 * Returns 0 (always successful).
 */
int stack_init_percpu(void)
{
    tcache_init_percpu(stack_tcache, &percpu_get(stack_percpu));
    return 0;
}

/**
 * stack_init - initializes the stack allocator
 * Returns 0 if successful, or -ENOMEM if out of memory.
 */
int stack_init(void)
{
    stack_tcache = tcache_create("runtime_stacks", &stack_tcache_ops, TCACHE_DEFAULT_MAG_SIZE,
                                 sizeof(struct stack));
    if (!stack_tcache)
        return -ENOMEM;
    return 0;
}
