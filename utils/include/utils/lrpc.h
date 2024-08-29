/*
 * lrpc.h - shared memory communication channels
 *
 * This design is inspired by Barrelfish, which in turn was based on Brian
 * Bershad's earlier LRPC work. The goal here is to minimize cache misses to
 * the maximum extent possible.
 */

#pragma once

#include <utils/assert.h>
#include <utils/atomic.h>
#include <utils/defs.h>

struct lrpc_msg {
    uint64_t cmd;
    uint64_t payload;
};

#define LRPC_DONE_PARITY (1UL << 63)
#define LRPC_CMD_MASK    (~LRPC_DONE_PARITY)

/*
 * Ingress/Egress Channel Support
 */

struct lrpc_chan_out {
    struct lrpc_msg *tbl;
    uint32_t size;
    uint32_t mask;
    uint32_t head;
    uint32_t tail;
};

struct lrpc_chan_in {
    struct lrpc_msg *tbl;
    uint32_t size;
    uint32_t mask;
    uint32_t head;
    volatile uint32_t head_wb;
};

struct lrpc_chan {
    struct lrpc_chan_out out;
    uint64_t pad0[5];
    struct lrpc_chan_in in;
    uint64_t pad1[5];
};

BUILD_ASSERT(offsetof(struct lrpc_chan, in) == 64);

/**
 * lrpc_send - sends a message on the channel
 * @chan: the ingress/egress channel
 * @cmd: the command to send
 * @payload: the data payload
 *
 * Returns true if successful, otherwise the channel is full.
 */
static inline bool lrpc_send(struct lrpc_chan *chan, uint64_t cmd, uint64_t payload)
{
    struct lrpc_chan_out *out = &chan->out;
    struct lrpc_msg *dst;

    assert(!(cmd & LRPC_DONE_PARITY));

    if (unlikely(out->tail - out->head >= out->size)) {
        out->head = atomic_load_acq(&chan->in.head_wb);
        if (out->tail - out->head == out->size) {
            return false;
        }
    }

    dst = &out->tbl[out->tail & out->mask];
    cmd |= (out->tail++ & out->size) ? 0 : LRPC_DONE_PARITY;
    dst->payload = payload;
    atomic_store_rel(&dst->cmd, cmd);
    return true;
}

/**
 * lrpc_recv - receives a message on the channel
 * @chan: the ingress/egress channel
 * @cmd_out: a pointer to store the received command
 * @payload_out: a pointer to store the received payload
 *
 * Returns true if successful, otherwise the channel is empty.
 */
static inline bool lrpc_recv(struct lrpc_chan *chan, uint64_t *cmd_out, uint64_t *payload_out)
{
    struct lrpc_chan_in *in = &chan->in;
    struct lrpc_msg *m = &in->tbl[in->head & in->mask];
    uint64_t parity = (in->head & in->size) ? 0 : LRPC_DONE_PARITY;
    uint64_t cmd;

    cmd = atomic_load_acq(&m->cmd);
    if ((cmd & LRPC_DONE_PARITY) != parity)
        return false;
    in->head++;

    *cmd_out = cmd & LRPC_CMD_MASK;
    *payload_out = m->payload;
    atomic_store_rel(&in->head_wb, in->head);
    return true;
}

/**
 * lrpc_get_cached_length - retrieves the number of queued messages
 * @chan: the ingress/egress channel
 *
 * Returns the number of messages queued in the channel.
 */
static inline uint32_t lrpc_size(struct lrpc_chan *chan)
{
    return chan->out.tail - chan->out.head;
}

/**
 * lrpc_empty - returns true if the channel has no available messages
 * @chan: the ingress channel
 */
static inline bool lrpc_empty(struct lrpc_chan *chan)
{
    struct lrpc_chan_in *in = &chan->in;
    struct lrpc_msg *m = &in->tbl[in->head & in->mask];
    uint64_t parity = (in->head & in->size) ? 0 : LRPC_DONE_PARITY;
    return (ACCESS_ONCE(m->cmd) & LRPC_DONE_PARITY) != parity;
}

int lrpc_init(struct lrpc_chan *chan, struct lrpc_msg *tbl, unsigned int size);
