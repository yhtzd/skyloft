/*
 * smalloc.c - a simple malloc implementation built on top of the base
 * libary slab and thread-local cache allocator
 */

#include <errno.h>

#include <skyloft/mm/page.h>
#include <skyloft/mm/slab.h>
#include <skyloft/mm/tcache.h>
#include <skyloft/percpu.h>

#define SMALLOC_MAG_SIZE 8
#define SMALLOC_BITS     15
#define SMALLOC_MIN_SIZE SLAB_MIN_SIZE
#define SMALLOC_MAX_SIZE (SMALLOC_MIN_SIZE << (SMALLOC_BITS - 1))
BUILD_ASSERT(SMALLOC_MIN_SIZE >= SLAB_MIN_SIZE);

static struct slab smalloc_slabs[SMALLOC_BITS];
static struct tcache *smalloc_tcaches[SMALLOC_BITS];
static DEFINE_PERCPU(struct tcache_percpu, smalloc_pts[SMALLOC_BITS]);

/**
 * smalloc_size_to_idx - converts a size to a cache index
 * @size: the size of the item to allocate
 *
 * Returns the smalloc cache index.
 */
static inline int smalloc_size_to_idx(size_t size)
{
    return 64 - __builtin_ctz(SMALLOC_MIN_SIZE) - __builtin_clzl((size - 1) | SMALLOC_MIN_SIZE);
}

static const char *slab_names[SMALLOC_BITS] = {
    "smalloc (16 B)",  "smalloc (32 B)",  "smalloc (64 B)",  "smalloc (128 B)",  "smalloc (256 B)",
    "smalloc (512 B)", "smalloc (1 KB)",  "smalloc (2 KB)",  "smalloc (4 KB)",   "smalloc (8 KB)",
    "smalloc (16 KB)", "smalloc (32 KB)", "smalloc (64 KB)", "smalloc (128 KB)", "smalloc (256 KB)",
};

/**
 * smalloc - allocates memory (non-inlined path)
 * @size: the size of the item
 *
 * Returns an item or NULL if out of memory.
 */
void *smalloc(size_t size)
{
    struct tcache_percpu *pt;
    void *item;

    if (unlikely(size > SMALLOC_MAX_SIZE))
        return NULL;

    preempt_disable();
    pt = &percpu_get(smalloc_pts[smalloc_size_to_idx(size)]);
    item = tcache_alloc(pt);
    preempt_enable();

    return item;
}

/**
 * __szmalloc - allocates zeroed memory (non-inlined path)
 * @size: the size of the item
 *
 * Returns an item or NULL if out of memory.
 */
void *__szalloc(size_t size)
{
    void *item = smalloc(size);
    if (unlikely(!item))
        return NULL;

    memset(item, 0, size);
    return item;
}

/*
 * sfree - frees memory back to the generic allocator
 * @item: the item to free
 */
void sfree(void *item)
{
    struct slab_node *n = addr_to_page(item)->snode;
    struct tcache_percpu *pt;

    preempt_disable();
    pt = &percpu_get(smalloc_pts[smalloc_size_to_idx(n->size)]);
    tcache_free(pt, item);
    preempt_enable();
}

/**
 * smalloc_init - initializes slab malloc
 *
 * NOTE: requires slab to be initialized first
 */
int smalloc_init(void)
{
    int i, ret;

    for (i = 0; i < SMALLOC_BITS; i++) {
        ret = slab_create(&smalloc_slabs[i], slab_names[i], (SMALLOC_MIN_SIZE << i),
                          SLAB_FLAG_FALSE_OKAY);
        if (ret)
            return ret;

        smalloc_tcaches[i] = slab_create_tcache(&smalloc_slabs[i], SMALLOC_MAG_SIZE);
        if (!smalloc_tcaches[i])
            return -ENOMEM;
    }

    return 0;
}

/**
 * smalloc_init_percpu - initializes slab malloc (per-CPU)
 */
int smalloc_init_percpu(void)
{
    int i;

    for (i = 0; i < SMALLOC_BITS; i++)
        tcache_init_percpu(smalloc_tcaches[i], &percpu_get(smalloc_pts[i]));

    return 0;
}
