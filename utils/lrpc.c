/*
 * lrpc.c - shared memory communication channels
 */

#include <errno.h>
#include <string.h>

#include <utils/lrpc.h>

/**
 * lrpc_init - initializes a shared memory channel
 * @chan: the channel struct to initialize
 * @size: the number of message elements in the buffer
 *
 * returns 0 if successful, or -EINVAL if @size is not a power of two.
//  */
int lrpc_init(struct lrpc_chan *chan, struct lrpc_msg *tbl, unsigned int size)
{
    if (!is_power_of_two(size))
        return -EINVAL;

    memset(chan, 0, sizeof(*chan));
    chan->out.tbl = chan->in.tbl = tbl;
    chan->out.size = chan->in.size = size;
    chan->out.mask = chan->in.mask = size - 1;
    return 0;
}
