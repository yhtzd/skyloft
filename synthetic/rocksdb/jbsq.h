#ifndef _SKYLOFT_EXPERIMENT_JBSQ_H_
#define _SKYLOFT_EXPERIMENT_JBSQ_H_

/* Join-Bounded-Shortest-Queue Policy */

#define JBSQ_K    2
#define JBSQ_MASK (JBSQ_K - 1)

#define JBSQ(type, name)    \
    struct {                \
        type _data[JBSQ_K]; \
        int _head, _tail;   \
    } name
#define JBSQ_INIT(q) ((q)->_head = (q)->_tail = 0)
#define JBSQ_DATA(q) ((q)->_data)

#define JBSQ_LEN(q)     ((q)->_tail - (q)->_head)
#define JBSQ_EMPTY(q)   ((q)->_tail == (q)->_head)
#define JBSQ_FULL(q)    ((q)->_tail == ((q)->_head + JBSQ_K))
#define JBSQ_PUSH(q, e) ((q)->_data[((q)->_tail++) & JBSQ_MASK] = (e))
#define JBSQ_POP(q)     ((q)->_data[((q)->_head++) & JBSQ_MASK])

#endif