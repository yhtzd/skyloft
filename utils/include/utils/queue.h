/*
 * queue.h: lock free queue implementation.
 *   - queue_t: simple FIFO queue with no thread safety
 *   - spsc_queue_t: Single-Producer-Single-Consumer queue
 */

#ifndef _UTILS_QUEUE_H_
#define _UTILS_QUEUE_H_

#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <utils/atomic.h>

#define QUEUE_CAP      (1 << 20)
#define QUEUE_CAP_MASK (QUEUE_CAP - 1)

#define queue_init(queue)                  \
    do {                                   \
        (queue)->head = (queue)->tail = 0; \
    } while (0)

/////////////////// naive queue ///////////////////

typedef struct {
    unsigned int head;
    unsigned int tail;
    void *buf[QUEUE_CAP];
} queue_t;

static inline int queue_len(queue_t *queue)
{
    return queue->tail - queue->head;
}

static inline bool queue_is_empty(queue_t *queue)
{
    return queue_len(queue) == 0;
}

static inline bool queue_is_full(queue_t *queue)
{
    return queue_len(queue) >= QUEUE_CAP;
}

static inline void *queue_head(queue_t *queue)
{
    return queue->buf[queue->head & QUEUE_CAP_MASK];
}

static inline int queue_push(queue_t *queue, void *item)
{
    if (queue_is_full(queue)) {
        return -EOVERFLOW;
    }
    queue->buf[queue->tail & QUEUE_CAP_MASK] = item;
    queue->tail++;
    return 0;
}

static inline void *queue_pop(queue_t *queue)
{
    if (queue_is_empty(queue)) {
        return NULL;
    }
    void *item = queue_head(queue);
    queue->head++;
    return item;
}

//////////////// SPSC queue ////////////////

struct spsc_queue_t {
    volatile unsigned int head;
    volatile unsigned int tail;
    void *buf[QUEUE_CAP];
};

static inline int spsc_queue_len(struct spsc_queue_t *queue)
{
    return atomic_load(&(queue)->tail) - atomic_load(&(queue)->head);
}

static inline bool spsc_queue_is_empty(struct spsc_queue_t *queue)
{
    return spsc_queue_len(queue) == 0;
}

static inline bool spsc_queue_is_full(struct spsc_queue_t *queue)
{
    return spsc_queue_len(queue) >= QUEUE_CAP;
}

static inline void *spsc_queue_head(struct spsc_queue_t *queue)
{
    return queue->buf[atomic_load_relax(&queue->head) & QUEUE_CAP_MASK];
}

static inline int spsc_queue_push(struct spsc_queue_t *queue, void *item)
{
    unsigned int tail = atomic_load_relax(&queue->tail);
    if (tail == atomic_load_acq(&queue->head) + QUEUE_CAP) {
        return -EOVERFLOW;
    }
    queue->buf[tail & QUEUE_CAP_MASK] = item;
    atomic_store_rel(&queue->tail, tail + 1);
    return 0;
}

static inline void *spsc_queue_pop(struct spsc_queue_t *queue)
{
    unsigned int head = atomic_load_relax(&queue->head);
    if (head == atomic_load_acq(&queue->tail)) {
        return NULL;
    }
    void *item = queue->buf[head & QUEUE_CAP_MASK];
    atomic_store_rel(&queue->head, head + 1);
    return item;
}

#endif // _UTILS_QUEUE_H_
