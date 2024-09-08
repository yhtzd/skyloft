/*
 * stddef.h - standard definitions
 */

#pragma once

#include <bits/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * NOTE: Some code in this file is derived from the public domain CCAN project.
 * http://ccodearchive.net/
 */

#define CACHE_LINE_SIZE 64
#define RSP_ALIGNMENT   16

#define check_type(expr, type)          ((typeof(expr) *)0 != (type *)0)
#define check_types_match(expr1, expr2) ((typeof(expr1) *)0 != (typeof(expr2) *)0)

/**
 * container_of - get pointer to enclosing structure
 * @member_ptr: pointer to the structure member
 * @containing_type: the type this member is within
 * @member: the name of this member within the structure.
 *
 * Given a pointer to a member of a structure, this macro does pointer
 * subtraction to return the pointer to the enclosing type.
 */
#ifndef container_of
#define container_of(member_ptr, containing_type, member)                            \
    ((containing_type *)((char *)(member_ptr) - offsetof(containing_type, member)) + \
     check_types_match(*(member_ptr), ((containing_type *)0)->member))
#endif

/**
 * container_of_var - get pointer to enclosing structure using a variable
 * @member_ptr: pointer to the structure member
 * @container_var: a pointer of same type as this member's container
 * @member: the name of this member within the structure.
 */
#define container_of_var(member_ptr, container_var, member) \
    container_of(member_ptr, typeof(*container_var), member)

/**
 * ARRAY_SIZE - get the number of elements in a visible array
 * @arr: the array whose size you want.
 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/**
 * MAX - picks the maximum of two expressions
 *
 * Arguments @a and @b are evaluated exactly once
 */
#define MAX(a, b)           \
    ({                      \
        typeof(a) _a = (a); \
        typeof(b) _b = (b); \
        _a > _b ? _a : _b;  \
    })

/**
 * MIN - picks the minimum of two expressions
 *
 * Arguments @a and @b are evaluated exactly once
 */
#define MIN(a, b)           \
    ({                      \
        typeof(a) _a = (a); \
        typeof(b) _b = (b); \
        _a < _b ? _a : _b;  \
    })

/**
 * is_power_of_two - determines if an integer is a power of two
 * @x: the value
 *
 * Returns true if the integer is a power of two.
 */
#define is_power_of_two(x) ((x) != 0 && !((x) & ((x) - 1)))

/**
 * align_up - rounds a value up to an alignment
 * @x: the value
 * @align: the alignment (must be power of 2)
 *
 * Returns an aligned value.
 */
#define align_up(x, align)                          \
    ({                                              \
        assert(is_power_of_two(align));             \
        (((x) - 1) | ((typeof(x))(align) - 1)) + 1; \
    })

/**
 * align_down - rounds a value down to an alignment
 * @x: the value
 * @align: the alignment (must be power of 2)
 *
 * Returns an aligned value.
 */
#define align_down(x, align)               \
    ({                                     \
        assert(is_power_of_two(align));    \
        ((x) & ~((typeof(x))(align) - 1)); \
    })

/**
 * is_aligned - determines if a value is aligned
 * @x: the value
 * @align: the alignment (must be power of 2)
 *
 * Returns true if the value is aligned.
 */
#define is_aligned(x, align) (((x) & ((typeof(x))(align) - 1)) == 0)

/**
 * div_up - divides two numbers, rounding up to an integer
 * @x: the dividend
 * @d: the divisor
 *
 * Returns a rounded-up quotient.
 */
#define div_up(x, d) ((((x) + (d) - 1)) / (d))

/**
 * Define an array of per-cpu variables, make the array size a multiple of
 * cacheline.
 */
#define declear_cpu_array(type, ident, num)                                                   \
    extern type ident[((sizeof(type) * num + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1)) / \
                      sizeof(type)]

#define define_cpu_array(type, ident, num)                                             \
    type ident[((sizeof(type) * num + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1)) / \
               sizeof(type)];                                                          \
    _Static_assert(sizeof(ident) % CACHE_LINE_SIZE == 0)

/**
 * __cstr - converts a value to a string
 */
#define __cstr_t(x...) #x
#define __cstr(x...)   __cstr_t(x)

/**
 * BIT - generates a value with one set bit by index
 * @n: the bit index to set
 *
 * Returns a long-sized constant.
 */
#define BIT(n) (1UL << (n))

/* common sizes */
#define KB (1024)
#define MB (1024 * KB)
#define GB (1024 * MB)

/**
 * wraps_lt - a < b ?
 *
 * This comparison is safe against unsigned wrap around.
 */
static inline bool wraps_lt(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

/**
 * wraps_lte - a <= b ?
 *
 * This comparison is safe against unsigned wrap around.
 */
static inline bool wraps_lte(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) <= 0;
}

/**
 * wraps_gt - a > b ?
 *
 * This comparison is safe against unsigned wrap around.
 */
static inline bool wraps_gt(uint32_t a, uint32_t b)
{
    return (int32_t)(b - a) < 0;
}

/**
 * wraps_gte - a >= b ?
 *
 * This comparison is safe against unsigned wrap around.
 */
static inline bool wraps_gte(uint32_t a, uint32_t b)
{
    return (int32_t)(b - a) <= 0;
}

enum {
    PGSHIFT_4KB = 12,
    PGSHIFT_2MB = 21,
    PGSHIFT_1GB = 30,
};

enum {
    PGSIZE_4KB = (1 << PGSHIFT_4KB), /* 4096 bytes */
    PGSIZE_2MB = (1 << PGSHIFT_2MB), /* 2097152 bytes */
    PGSIZE_1GB = (1 << PGSHIFT_1GB), /* 1073741824 bytes */
};

#define PGMASK_4KB (PGSIZE_4KB - 1)
#define PGMASK_2MB (PGSIZE_2MB - 1)
#define PGMASK_1GB (PGSIZE_1GB - 1)

/* page numbers */
#define PGN_4KB(la) (((uintptr_t)(la)) >> PGSHIFT_4KB)
#define PGN_2MB(la) (((uintptr_t)(la)) >> PGSHIFT_2MB)
#define PGN_1GB(la) (((uintptr_t)(la)) >> PGSHIFT_1GB)

#define PGOFF_4KB(la) (((uintptr_t)(la)) & PGMASK_4KB)
#define PGOFF_2MB(la) (((uintptr_t)(la)) & PGMASK_2MB)
#define PGOFF_1GB(la) (((uintptr_t)(la)) & PGMASK_1GB)

#define PGADDR_4KB(la) (((uintptr_t)(la)) & ~((uintptr_t)PGMASK_4KB))
#define PGADDR_2MB(la) (((uintptr_t)(la)) & ~((uintptr_t)PGMASK_2MB))
#define PGADDR_1GB(la) (((uintptr_t)(la)) & ~((uintptr_t)PGMASK_1GB))

#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif
#define unreachable() __builtin_unreachable()

#define prefetch0(x)   __builtin_prefetch((x), 0, 3)
#define prefetch1(x)   __builtin_prefetch((x), 0, 2)
#define prefetch2(x)   __builtin_prefetch((x), 0, 1)
#define prefetchnta(x) __builtin_prefetch((x), 0, 0)
#define prefetch(x)    prefetch0(x)

/* variable attributes */
#define __packed            __attribute__((packed))
#define __notused           __attribute__((unused))
#define __used              __attribute__((used))
#define __aligned(x)        __attribute__((aligned(x)))
#define __aligned_cacheline __aligned(CACHE_LINE_SIZE)

/* function attributes */
#define __api
#define __noinline          __attribute__((noinline))
#define __noreturn          __attribute__((noreturn))
#define __must_use_return   __attribute__((warn_unused_result))
#define __pure              __attribute__((pure))
#define __weak              __attribute__((weak))
#define __malloc            __attribute__((malloc))
#define __assume_aligned(x) __attribute__((assume_aligned(x)))

#define barrier() asm volatile("" ::: "memory")

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

static __always_inline void __write_once_size(volatile void *p, void *res, int size)
{
    switch (size) {
    case 1:
        *(volatile uint8_t *)p = *(uint8_t *)res;
        break;
    case 2:
        *(volatile uint16_t *)p = *(uint16_t *)res;
        break;
    case 4:
        *(volatile uint32_t *)p = *(uint32_t *)res;
        break;
    case 8:
        *(volatile uint64_t *)p = *(uint64_t *)res;
        break;
    default:
        barrier();
        __builtin_memcpy((void *)p, (const void *)res, size);
        barrier();
    }
}

#define WRITE_ONCE(x, val)                           \
    ({                                               \
        union {                                      \
            typeof(x) __val;                         \
            char __c[1];                             \
        } __u = {.__val = (__force typeof(x))(val)}; \
        __write_once_size(&(x), __u.__c, sizeof(x)); \
        __u.__val;                                   \
    })

/*
 * These attributes are defined only with the sparse checker tool.
 */
#ifdef __CHECKER__
#define __rcu    __attribute__((noderef, address_space(1)))
#define __percpu __attribute__((noderef, address_space(2)))
#define __force  __attribute__((force))
#undef __assume_aligned
#define __assume_aligned(x)
#else /* __CHECKER__ */
#define __rcu
#define __percpu
#define __force
#endif /* __CHECKER__ */

/*
 * Endianness
 */

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321

#define __BYTE_ORDER __LITTLE_ENDIAN

/*
 * Word Size
 */

#define __32BIT_WORDS 32
#define __64BIT_WORDS 64

#define __WORD_SIZE __64BIT_WORDS

#define CACHE_LINE_SIZE 64

/* multiply uint64_t with uint32_t  */
static inline uint64_t mul_u64_u32_shr(uint64_t a, uint32_t mul, unsigned int shift)
{
    uint32_t ah, al;
    uint64_t ret;

    al = a;
    ah = a >> 32;

    ret = ((uint64_t)al * mul) >> shift;
    if (ah)
        ret += ((uint64_t)ah * mul) << (32 - shift);

    return ret;
}

#define div64_long(x, y) div64_s64((x), (y))
#define div64_ul(x, y)   div64_u64((x), (y))

/**
 * div_u64_rem - unsigned 64bit divide with 32bit divisor with remainder
 * @dividend: unsigned 64bit dividend
 * @divisor: unsigned 32bit divisor
 * @remainder: pointer to unsigned 32bit remainder
 *
 * Return: sets ``*remainder``, then returns dividend / divisor
 *
 * This is commonly provided by 32bit archs to provide an optimized 64bit
 * divide.
 */
static inline uint64_t div_u64_rem(uint64_t dividend, uint32_t divisor, uint32_t *remainder)
{
    *remainder = dividend % divisor;
    return dividend / divisor;
}

/**
 * div_s64_rem - signed 64bit divide with 32bit divisor with remainder
 * @dividend: signed 64bit dividend
 * @divisor: signed 32bit divisor
 * @remainder: pointer to signed 32bit remainder
 *
 * Return: sets ``*remainder``, then returns dividend / divisor
 */
static inline int64_t div_s64_rem(int64_t dividend, int32_t divisor, int32_t *remainder)
{
    *remainder = dividend % divisor;
    return dividend / divisor;
}

/**
 * div64_u64_rem - unsigned 64bit divide with 64bit divisor and remainder
 * @dividend: unsigned 64bit dividend
 * @divisor: unsigned 64bit divisor
 * @remainder: pointer to unsigned 64bit remainder
 *
 * Return: sets ``*remainder``, then returns dividend / divisor
 */
static inline uint64_t div64_u64_rem(uint64_t dividend, uint64_t divisor, uint64_t *remainder)
{
    *remainder = dividend % divisor;
    return dividend / divisor;
}

/**
 * div64_uint64_t - unsigned 64bit divide with 64bit divisor
 * @dividend: unsigned 64bit dividend
 * @divisor: unsigned 64bit divisor
 *
 * Return: dividend / divisor
 */
static inline uint64_t div64_u64(uint64_t dividend, uint64_t divisor)
{
    return dividend / divisor;
}

/**
 * div64_int64_t - signed 64bit divide with 64bit divisor
 * @dividend: signed 64bit dividend
 * @divisor: signed 64bit divisor
 *
 * Return: dividend / divisor
 */
static inline int64_t div64_s64(int64_t dividend, int64_t divisor)
{
    return dividend / divisor;
}

/**
 * div_uint64_t - unsigned 64bit divide with 32bit divisor
 * @dividend: unsigned 64bit dividend
 * @divisor: unsigned 32bit divisor
 *
 * This is the most common 64bit divide and should be used if possible,
 * as many 32bit archs can optimize this variant better than a full 64bit
 * divide.
 *
 * Return: dividend / divisor
 */
#ifndef div_u64
static inline uint64_t div_u64(uint64_t dividend, uint32_t divisor)
{
    uint32_t remainder;
    return div_u64_rem(dividend, divisor, &remainder);
}
#endif

/**
 * div_int64_t - signed 64bit divide with 32bit divisor
 * @dividend: signed 64bit dividend
 * @divisor: signed 32bit divisor
 *
 * Return: dividend / divisor
 */
#ifndef div_s64
static inline int64_t div_s64(int64_t dividend, int32_t divisor)
{
    int32_t remainder;
    return div_s64_rem(dividend, divisor, &remainder);
}
#endif