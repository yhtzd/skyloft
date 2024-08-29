/*
 * msgq.h: simple message queue with one reader and one writer
 */

#pragma once

#include <string.h>

#include <utils/assert.h>
#include <utils/atomic.h>
#include <utils/defs.h>

#include <utils/log.h>

#define MSGQ_SIZE        4096
#define MSGQ_MASK        (MSGQ_SIZE - 1)
#define MSGQ_DONE_PARITY (1UL << 63)
#define MSGQ_CMD_MASK    (~MSGQ_DONE_PARITY)

struct msg {
    uint64_t cmd;
    uint64_t payload;
};

struct msgq {
    struct msg *data;
    uint32_t head, tail;
};

static inline bool msgq_empty(struct msgq *q)
{
    struct msg *m;
    uint64_t parity;

    m = &q->data[q->head & MSGQ_MASK];
    parity = (q->head & MSGQ_SIZE) ? 0 : MSGQ_DONE_PARITY;
    return (ACCESS_ONCE(m->cmd) & MSGQ_DONE_PARITY) != parity;
}

/**
 * msgq_send - sends a message
 * @msgq: the ingress/egress queue
 * @cmd: the command to send
 * @payload: the data payload
 *
 * Returns true if successful, otherwise the channel is full.
 */
static inline bool msgq_send(struct msgq *q, uint64_t cmd, uint64_t payload)
{
    struct msg *dst;

    if (unlikely(q->tail >= q->head + MSGQ_SIZE))
        return false;

    dst = &q->data[q->tail & MSGQ_MASK];
    cmd |= (q->tail++ & MSGQ_SIZE) ? 0 : MSGQ_DONE_PARITY;
    dst->payload = payload;
    atomic_store_rel(&dst->cmd, cmd);
    return true;
}

/**
 * msgq_recv - receives a message
 * @msgq: the ingress/egress queue
 * @cmd_out: a pointer to store the received command
 * @payload_out: a pointer to store the received payload
 *
 * Returns true if successful, otherwise the queue is empty.
 */
static inline bool msgq_recv(struct msgq *q, uint64_t *cmd_out, uint64_t *payload_out)
{
    struct msg *m;
    uint64_t parity, cmd;

    m = &q->data[q->head & MSGQ_MASK];
    parity = (q->head & MSGQ_SIZE) ? 0 : MSGQ_DONE_PARITY;
    cmd = atomic_load_acq(&m->cmd);
    if ((cmd & MSGQ_DONE_PARITY) != parity)
        return false;
    q->head++;

    *cmd_out = m->cmd & MSGQ_CMD_MASK;
    *payload_out = m->payload;
    return true;
}

/**
 * msgq_init - initializes a message queue
 * @q: the message queue struct to initialize
 * @size: the number of message elements in the buffer
 *
 * returns 0 if successful, or -EINVAL if @size is not a power of two.
 */
static inline int msgq_init(struct msgq *q, void *ptr)
{
    memset(q, 0, sizeof(struct msgq));
    q->data = ptr;
    return 0;
}