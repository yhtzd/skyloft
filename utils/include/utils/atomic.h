/*
 * atomic.h - utilities for atomic memory ops
 *
 * With the exception of *_read and *_write, consider these operations full
 * barriers.
 */

#pragma once

#include <stdatomic.h>

#define atomic_load_relax(ptr)     __atomic_load_n(ptr, __ATOMIC_RELAXED)
#define atomic_load_acq(ptr)       __atomic_load_n(ptr, __ATOMIC_ACQUIRE)
#define atomic_load_con(ptr)       __atomic_load_n(ptr, __ATOMIC_CONSUME)
#define atomic_store_rel(ptr, val) __atomic_store_n(ptr, val, __ATOMIC_RELEASE)
#define atomic_inc(ptr)            atomic_fetch_add(ptr, 1)
#define atomic_dec(ptr)            atomic_fetch_sub(ptr, 1)
#define atomic_dec_zero(ptr)       (atomic_dec(ptr) == 1)
