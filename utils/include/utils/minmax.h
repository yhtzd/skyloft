#ifndef _UTILS_MINMAX_H
#define _UTILS_MINMAX_H

#include "compiler.h"

#define __cmp_op_min <
#define __cmp_op_max >

#define __cmp(op, x, y) ((x)__cmp_op_##op(y) ? (x) : (y))

#define __cmp_once_unique(op, type, x, y, ux, uy) \
    ({                                            \
        type ux = (x);                            \
        type uy = (y);                            \
        __cmp(op, ux, uy);                        \
    })

#define __cmp_once(op, type, x, y) \
    __cmp_once_unique(op, type, x, y, __UNIQUE_ID(x_), __UNIQUE_ID(y_))

#define __careful_cmp_once(op, x, y, ux, uy)                                                  \
    ({                                                                                        \
        __auto_type ux = (x);                                                                 \
        __auto_type uy = (y);                                                                 \
        BUILD_BUG_ON_MSG(!__types_ok(x, y, ux, uy), #op "(" #x ", " #y ") signedness error"); \
        __cmp(op, ux, uy);                                                                    \
    })

#define __careful_cmp(op, x, y) __careful_cmp_once(op, x, y, __UNIQUE_ID(x_), __UNIQUE_ID(y_))

/**
 * min - return minimum of two values of the same or compatible types
 * @x: first value
 * @y: second value
 */
#define min(x, y) __careful_cmp(min, x, y)

/**
 * max - return maximum of two values of the same or compatible types
 * @x: first value
 * @y: second value
 */
#define max(x, y) __careful_cmp(max, x, y)

/**
 * max_t - return maximum of two values, using the specified type
 * @type: data type to use
 * @x: first value
 * @y: second value
 */
#define max_t(type, x, y) __cmp_once(max, type, x, y)

#define __clamp(val, lo, hi) ((val) >= (hi) ? (hi) : ((val) <= (lo) ? (lo) : (val)))

#define __clamp_once(val, lo, hi, uval, ulo, uhi)                                             \
    ({                                                                                        \
        __auto_type uval = (val);                                                             \
        __auto_type ulo = (lo);                                                               \
        __auto_type uhi = (hi);                                                               \
        static_assert(__builtin_choose_expr(__is_constexpr((lo) > (hi)), (lo) <= (hi), true), \
                      "clamp() low limit " #lo " greater than high limit " #hi);              \
        BUILD_BUG_ON_MSG(!__types_ok3(val, lo, hi, uval, ulo, uhi),                           \
                         "clamp(" #val ", " #lo ", " #hi ") signedness error");               \
        __clamp(uval, ulo, uhi);                                                              \
    })

#define __careful_clamp(val, lo, hi) \
    __clamp_once(val, lo, hi, __UNIQUE_ID(v_), __UNIQUE_ID(l_), __UNIQUE_ID(h_))

/**
 * clamp - return a value clamped to a given range with strict typechecking
 * @val: current value
 * @lo: lowest allowable value
 * @hi: highest allowable value
 *
 * This macro does strict typechecking of @lo/@hi to make sure they are of the
 * same type as @val.  See the unnecessary pointer comparisons.
 */
#define clamp(val, lo, hi) __careful_clamp(val, lo, hi)

#endif // _UTILS_MINMAX_H
