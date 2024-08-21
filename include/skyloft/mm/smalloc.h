/*
 * smalloc.h - malloc() based on the SLAB and thread-local item caches
 */

#pragma once

#include <string.h>

#include <utils/defs.h>

#define __smalloc_attr __malloc __assume_aligned(16)

void *smalloc(size_t size) __smalloc_attr;
void *__szalloc(size_t size) __smalloc_attr;
void sfree(void *item);
int smalloc_init(void);
int smalloc_init_percpu(void);

/**
 * szalloc - allocates zeroed memory
 * @size: the size of the item
 *
 * Returns an item or NULL if out of memory.
 */
static __always_inline void *szalloc(size_t size)
{
    if (__builtin_constant_p(size)) {
        void *item = smalloc(size);
        if (unlikely(!item))
            return NULL;
        memset(item, 0, size);
        return item;
    }
    return __szalloc(size);
}

/**
 * smalloc_array - allocates a contiguous array of items
 * @n: the number of items
 * @size: the size of each item
 *
 * Returns an item array, or NULL if out of memory.
 */
static __always_inline void *smalloc_array(size_t n, size_t size)
{
    return smalloc(n * size);
}
