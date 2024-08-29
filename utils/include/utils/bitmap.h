/*
 * bitmap.h - a library for bit array manipulation
 */

#pragma once

#include <assert.h>
#include <string.h>

#include <utils/atomic.h>
#include <utils/defs.h>

#define BITS_PER_LONG           (sizeof(long) * 8)
#define BITMAP_LONG_SIZE(nbits) div_up(nbits, BITS_PER_LONG)

#define DEFINE_BITMAP(name, nbits) unsigned long name[BITMAP_LONG_SIZE(nbits)]

typedef unsigned long *bitmap_ptr;

#define BITMAP_POS_IDX(pos)   ((pos) / BITS_PER_LONG)
#define BITMAP_POS_SHIFT(pos) ((pos) % BITS_PER_LONG)

/**
 * bitmap_set - sets a bit in the bitmap
 * @bits: the bitmap
 * @pos: the bit number
 */
static inline void bitmap_set(unsigned long *bits, int pos)
{
    bits[BITMAP_POS_IDX(pos)] |= (1ul << BITMAP_POS_SHIFT(pos));
}

/**
 * bitmap_clear - clears a bit in the bitmap
 * @bits: the bitmap
 * @pos: the bit number
 */
static inline void bitmap_clear(unsigned long *bits, int pos)
{
    bits[BITMAP_POS_IDX(pos)] &= ~(1ul << BITMAP_POS_SHIFT(pos));
}

/**
 * bitmap_test - tests if a bit is set in the bitmap
 * @bits: the bitmap
 * @pos: the bit number
 *
 * Returns true if the bit is set, otherwise false.
 */
static inline bool bitmap_test(unsigned long *bits, int pos)
{
    return (bits[BITMAP_POS_IDX(pos)] & (1ul << BITMAP_POS_SHIFT(pos))) != 0;
}

/**
 * bitmap_atomic_set - atomically sets a bit in the bitmap
 * @bits: the bitmap
 * @pos: the bit number
 */
static inline void bitmap_atomic_set(unsigned long *bits, int pos)
{
    atomic_fetch_or(&bits[BITMAP_POS_IDX(pos)], (1ul << BITMAP_POS_SHIFT(pos)));
}

/**
 * bitmap_atomic_test_and_set - atomically tests and sets a bit in the bitmap
 * @bits: the bitmap
 * @pos; the bit number
 */
static inline bool bitmap_atomic_test_and_set(unsigned long *bits, int pos)
{
    unsigned long bit = (1ul << BITMAP_POS_SHIFT(pos));
    return (atomic_fetch_or(&bits[BITMAP_POS_IDX(pos)], bit) & bit) != 0;
}

/**
 * bitmap_atomic_clear - atomically clears a bit in the bitmap
 * @bits: the bitmap
 * @pos: the bit number
 */
static inline void bitmap_atomic_clear(unsigned long *bits, int pos)
{
    atomic_fetch_and(&bits[BITMAP_POS_IDX(pos)], ~(1ul << BITMAP_POS_SHIFT(pos)));
}

/**
 * bitmap_atomic_test - atomically tests a bit in the bitmap
 * @bits: the bitmap
 * @pos: the bit number
 */
static inline bool bitmap_atomic_test(unsigned long *bits, int pos)
{
    return (atomic_load_acq(&bits[BITMAP_POS_IDX(pos)]) & (1ul << BITMAP_POS_SHIFT(pos))) != 0;
}

/**
 * bitmap_init - initializes a bitmap
 * @bits: the bitmap
 * @nbits: the number of total bits
 * @state: if true, all bits are set, otherwise all bits are cleared
 */
static inline void bitmap_init(unsigned long *bits, int nbits, bool state)
{
    memset(bits, state ? 0xff : 0, BITMAP_LONG_SIZE(nbits) * sizeof(long));
}

extern int bitmap_find_next_set(unsigned long *bits, int nbits, int pos);
extern int bitmap_find_next_cleared(unsigned long *bits, int nbits, int pos);

/**
 * bitmap_for_each_set - generates a loop iteration over each set bit
 * @bits: the bitmap
 * @nbits: the number of total bits
 * @pos: the bit position (int)
 */
#define bitmap_for_each_set(bits, nbits, pos) \
    for ((pos) = -1; (pos) = bitmap_find_next_set((bits), (nbits), ((pos) + 1)), (pos) < (nbits);)

/**
 * bitmap_for_each_cleared - generates a loop iteration over each cleared bit
 * @bits: the bitmap
 * @nbits: the number of total bits
 * @pos: the bit position (int)
 */
#define bitmap_for_each_cleared(bits, nbits, pos) \
    for ((pos) = -1;                              \
         (pos) = bitmap_find_next_cleared((bits), (nbits), ((pos) + 1)), (pos) < (nbits);)
