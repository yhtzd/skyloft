/*
 * shm.h - shared memory communication
 */

#pragma once

#include <limits.h>

#include <utils/assert.h>

/*
 * Shared memory pointer support. These are pointers that are passed across
 * address spaces, so the mapped regions will have different base offsets.
 */

typedef uintptr_t shmptr_t;

/* shared memory pointers have a special non-zero NULL value */
#define SHMPTR_NULL ULONG_MAX

struct shm_region {
    void *base;
    size_t len;
};

/**
 * ptr_to_shmptr - converts a normal pointer to a shared memory pointer
 * @r: the shared memory region the pointer resides in
 * @ptr: the normal pointer to convert
 * @len: the size of the object
 *
 * Returns a shared memory pointer.
 */
static inline shmptr_t ptr_to_shmptr(struct shm_region *r, void *ptr, size_t len)
{
    assert((uintptr_t)r->base <= (uintptr_t)ptr);
    assert((uintptr_t)ptr + len <= (uintptr_t)r->base + r->len);
    return (uintptr_t)ptr - (uintptr_t)r->base;
}

/**
 * shmptr_to_ptr - converts a shared memory pointer to a normal pointer
 * @r: the shared memory region the shared memory pointer resides in
 * @shmptr: the shared memory pointer
 * @len: the size of the object
 *
 * Returns a normal pointer, or NULL if the shared memory pointer is outside
 * the region.
 */
static inline void *shmptr_to_ptr(struct shm_region *r, shmptr_t shmptr, size_t len)
{
    /* WARNING: could wrap around! */
    if (unlikely(ULONG_MAX - shmptr < r->len || shmptr + len > r->len))
        return NULL;
    return (void *)(shmptr + (uintptr_t)r->base);
}
