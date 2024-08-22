/*
 * tcp.c - support for Transmission Control Protocol (RFC 793)
 */

#include <errno.h>
#include <string.h>

#include <skyloft/io.h>
#include <skyloft/mm.h>
#include <skyloft/net.h>
#include <skyloft/sched.h>
#include <skyloft/tcp.h>
#include <utils/defs.h>
#include <utils/hash.h>
#include <utils/log.h>

#include "tcp.h"

/* protects @tcp_conns */
static DEFINE_SPINLOCK(tcp_lock);
/* a list of all TCP connections */
static DEFINE_LIST_HEAD(tcp_conns);

static void tcp_retransmit(void *arg);

void tcp_timer_update(tcp_conn_t *c)
{
    uint64_t next_timeout = -1L;
    struct mbuf *m;
    assert_spin_lock_held(&c->lock);

    if (unlikely(c->pcb.state == TCP_STATE_TIME_WAIT))
        next_timeout = c->time_wait_ts + TCP_TIME_WAIT_TIMEOUT;

    if (c->ack_delayed)
        next_timeout = MIN(next_timeout, c->ack_ts + TCP_ACK_TIMEOUT);

    if (!c->tx_exclusive) {
        m = list_top(&c->txq, struct mbuf, link);
        if (m)
            next_timeout = MIN(next_timeout, m->timestamp + TCP_RETRANSMIT_TIMEOUT);
    }

    if (!list_empty(&c->rxq_ooo))
        next_timeout = MIN(next_timeout, now_us() + TCP_OOQ_ACK_TIMEOUT);

    atomic_store_rel(&c->next_timeout, next_timeout);
}

/* check for timeouts in a TCP connection */
static void tcp_handle_timeouts(tcp_conn_t *c, uint64_t now)
{
    bool do_ack = false, do_retransmit = false;

    spin_lock_np(&c->lock);
    if (c->pcb.state == TCP_STATE_CLOSED) {
        spin_unlock_np(&c->lock);
        return;
    }

    if (c->pcb.state == TCP_STATE_TIME_WAIT && now - c->time_wait_ts >= TCP_TIME_WAIT_TIMEOUT) {
        log_debug("tcp: %p time wait timeout", c);
        tcp_conn_set_state(c, TCP_STATE_CLOSED);
        spin_unlock_np(&c->lock);
        tcp_conn_put(c);
        return;
    }
    if (c->ack_delayed && now - c->ack_ts >= TCP_ACK_TIMEOUT) {
        log_debug("tcp: %p delayed ack timeout", c);
        c->ack_delayed = false;
        do_ack = true;
    }
    if (!c->tx_exclusive && !list_empty(&c->txq)) {
        struct mbuf *m = list_top(&c->txq, struct mbuf, link);
        if (now - m->timestamp >= TCP_RETRANSMIT_TIMEOUT) {
            log_debug("tcp: %p retransmission timeout", c);
            /* It is safe to take a reference, since state != closed */
            tcp_conn_get(c);
            do_retransmit = true;
        }
    }

    do_ack |= !list_empty(&c->rxq_ooo);

    tcp_timer_update(c);

    spin_unlock_np(&c->lock);

    if (do_ack)
        tcp_tx_ack(c);
    if (do_retransmit)
        sl_task_spawn(tcp_retransmit, c, 0);
}

/* a periodic background thread that handles timeout events */
static void tcp_worker(void *arg)
{
    tcp_conn_t *c;
    uint64_t now;

    while (true) {
        now = now_us();

        spin_lock_np(&tcp_lock);
        list_for_each(&tcp_conns, c, global_link)
        {
            if (atomic_load_acq(&c->next_timeout) <= now)
                tcp_handle_timeouts(c, now);
        }
        spin_unlock_np(&tcp_lock);

        timer_sleep(10 * USEC_PER_MSEC);
    }
}

/**
 * tcp_conn_ack - removes acknowledged packets from TX queue
 * @c: the TCP connection to update
 * @freeq: a pointer to a list to store acknowledged buffers to later free
 *
 * WARNING: the caller must hold @c->lock.
 * @freeq is provided so that @c->lock can be released before freeing buffers.
 */
void tcp_conn_ack(tcp_conn_t *c, struct list_head *freeq)
{
    struct mbuf *m;

    assert_spin_lock_held(&c->lock);

    /* will free these segments later */
    if (c->tx_exclusive)
        return;

    /* dequeue buffers that are fully acknowledged */
    while (true) {
        m = list_top(&c->txq, struct mbuf, link);
        if (!m)
            break;
        if (wraps_gt(m->seg_end, c->pcb.snd_una))
            break;

        list_pop(&c->txq, struct mbuf, link);
        list_add_tail(freeq, &m->link);
    }
}

/**
 * tcp_conn_set_state - changes the TCP PCB state
 * @c: the TCP connection to update
 * @new_state: the new TCP_STATE_* value
 *
 * WARNING: @c->lock must be held by the caller.
 * WARNING: @new_state must be greater than the current state.
 */
void tcp_conn_set_state(tcp_conn_t *c, int new_state)
{
    assert_spin_lock_held(&c->lock);
    assert(c->pcb.state == TCP_STATE_CLOSED || c->pcb.state < new_state);

    /* unblock any threads waiting for the connection to be established */
    if (c->pcb.state < TCP_STATE_ESTABLISHED && new_state >= TCP_STATE_ESTABLISHED) {
        waitq_release(&c->tx_wq);
    }

    tcp_debug_state_change(c, c->pcb.state, new_state);
    c->pcb.state = new_state;
    tcp_timer_update(c);
}

/* handles network errors for TCP sockets */
static void tcp_conn_err(struct trans_entry *e, int err)
{
    tcp_conn_t *c = container_of(e, tcp_conn_t, e);

    spin_lock_np(&c->lock);
    tcp_conn_fail(c, err);
    spin_unlock_np(&c->lock);
}

/* operations for TCP sockets */
const struct trans_ops tcp_conn_ops = {
    .recv = tcp_rx_conn,
    .err = tcp_conn_err,
};

/*
 * Connection initialization
 */

/**
 * tcp_conn_alloc - allocates a TCP connection struct
 *
 * Returns a connection, or NULL if out of memory.
 */
tcp_conn_t *tcp_conn_alloc(void)
{
    tcp_conn_t *c;

    c = smalloc(sizeof(*c));
    if (!c)
        return NULL;

    /* general fields */
    memset(&c->pcb, 0, sizeof(c->pcb));
    spin_lock_init(&c->lock);
    kref_init(&c->ref);
    c->err = 0;

    /* ingress fields */
    c->rx_closed = false;
    c->rx_exclusive = false;
    waitq_init(&c->rx_wq);
    list_head_init(&c->rxq_ooo);
    list_head_init(&c->rxq);

    /* egress fields */
    c->tx_closed = false;
    c->tx_exclusive = false;
    waitq_init(&c->tx_wq);
    c->tx_last_ack = 0;
    c->tx_last_win = 0;
    c->tx_pending = NULL;
    list_head_init(&c->txq);
    c->do_fast_retransmit = false;

    /* timeouts */
    c->next_timeout = -1L;
    c->ack_delayed = false;
    c->rcv_wnd_full = false;
    c->ack_ts = 0;
    c->time_wait_ts = 0;
    c->rep_acks = 0;

    /* initialize egress half of PCB */
    c->pcb.state = TCP_STATE_CLOSED;
    c->pcb.iss = rand_crc32c(0x12345678); /* TODO: not enough */
    c->pcb.snd_nxt = c->pcb.iss;
    c->pcb.snd_una = c->pcb.iss;
    c->pcb.rcv_wnd = TCP_WIN;

    return c;
}

/**
 * tcp_conn_attach - attaches a connection to the transport layer
 * @c: the connection to attach
 * @laddr: the local network address
 * @raddr: the remote network address
 *
 * After calling this function, if successful, ingress packets and errors will
 * be delivered.
 */
int tcp_conn_attach(tcp_conn_t *c, struct netaddr laddr, struct netaddr raddr)
{
    int ret;

    if (laddr.ip == 0)
        laddr.ip = io->addr;
    else if (laddr.ip != io->addr)
        return -EINVAL;

    trans_init_5tuple(&c->e, IPPROTO_TCP, &tcp_conn_ops, laddr, raddr);
    if (laddr.port == 0)
        ret = trans_table_add_with_ephemeral_port(&c->e);
    else
        ret = trans_table_add(&c->e);
    if (ret)
        return ret;

    static bool worker_running;
    bool start_worker = false;
    spin_lock_np(&tcp_lock);
    if (unlikely(!worker_running))
        worker_running = start_worker = true;
    list_add_tail(&tcp_conns, &c->global_link);
    spin_unlock_np(&tcp_lock);

    if (start_worker)
        BUG_ON(sl_task_spawn(tcp_worker, NULL, 0));

    return 0;
}

static void tcp_conn_release(struct rcu_head *h)
{
    tcp_conn_t *c = container_of(h, tcp_conn_t, e.rcu);

    spin_lock_np(&tcp_lock);
    list_del_from(&tcp_conns, &c->global_link);
    spin_unlock_np(&tcp_lock);

    if (c->tx_pending)
        mbuf_free(c->tx_pending);
    mbuf_list_free(&c->rxq_ooo);
    mbuf_list_free(&c->rxq);
    mbuf_list_free(&c->txq);
    sfree(c);
}

/**
 * tcp_conn_destroy - tears down a frees a TCP connection
 * @c: the connection to destroy
 */
void tcp_conn_destroy(tcp_conn_t *c)
{
    trans_table_remove(&c->e);
    rcu_free(&c->e.rcu, tcp_conn_release);
}

/**
 * tcp_conn_release_ref - a helper to free the conn when ref reaches zero
 * @r: the embedded reference count structure
 */
void tcp_conn_release_ref(struct kref *r)
{
    tcp_conn_t *c = container_of(r, tcp_conn_t, ref);

    BUG_ON(c->pcb.state != TCP_STATE_CLOSED);
    tcp_conn_destroy(c);
}

/*
 * Support for accepting new connections
 */

struct tcp_queue {
    struct trans_entry e;
    spinlock_t l;
    waitq_t wq;
    struct list_head conns;
    int backlog;
    bool shutdown;
};

static void tcp_queue_recv(struct trans_entry *e, struct mbuf *m)
{
    tcp_queue_t *q = container_of(e, tcp_queue_t, e);
    tcp_conn_t *c;
    struct task *t;

    /* make sure the connection queue isn't full */
    spin_lock_np(&q->l);
    if (unlikely(q->backlog == 0 || q->shutdown)) {
        spin_unlock_np(&q->l);
        goto done;
    }
    q->backlog--;
    spin_unlock_np(&q->l);

    /* create a new connection */
    c = tcp_rx_listener(e->laddr, m);
    if (!c) {
        spin_lock_np(&q->l);
        q->backlog++;
        spin_unlock_np(&q->l);
        goto done;
    }

    /* wake a thread to accept the connection */
    spin_lock_np(&q->l);
    list_add_tail(&q->conns, &c->queue_link);
    t = waitq_signal(&q->wq, &q->l);
    spin_unlock_np(&q->l);
    waitq_signal_finish(t);

done:
    mbuf_free(m);
}

/* operations for TCP listen queues */
const struct trans_ops tcp_queue_ops = {
    .recv = tcp_queue_recv,
};

/**
 * tcp_listen - creates a TCP listening queue for a local address
 * @laddr: the local address to listen on
 * @backlog: the maximum number of unaccepted sockets to queue
 * @q_out: a pointer to store the newly created listening queue
 *
 * Returns 0 if successful, otherwise fails.
 */
int tcp_listen(struct netaddr laddr, int backlog, tcp_queue_t **q_out)
{
    tcp_queue_t *q;
    int ret;

    if (backlog < 1)
        return -EINVAL;

    /* only can support one local IP so far */
    if (laddr.ip == 0)
        laddr.ip = io->addr;
    else if (laddr.ip != io->addr)
        return -EINVAL;

    q = smalloc(sizeof(*q));
    if (!q)
        return -ENOMEM;

    trans_init_3tuple(&q->e, IPPROTO_TCP, &tcp_queue_ops, laddr);
    spin_lock_init(&q->l);
    waitq_init(&q->wq);
    list_head_init(&q->conns);
    q->backlog = backlog;
    q->shutdown = false;

    ret = trans_table_add(&q->e);
    if (ret) {
        sfree(q);
        return ret;
    }

    *q_out = q;
    return 0;
}

/**
 * tcp_accept - accepts a TCP connection
 * @q: the listen queue to accept the connection on
 * @c_out: a pointer to store the connection
 *
 * Returns 0 if successful, otherwise -EPIPE if the listen queue was closed.
 */
int tcp_accept(tcp_queue_t *q, tcp_conn_t **c_out)
{
    tcp_conn_t *c;

    spin_lock_np(&q->l);
    while (list_empty(&q->conns) && !q->shutdown) waitq_wait(&q->wq, &q->l);

    /* was the queue drained and shutdown? */
    if (list_empty(&q->conns) && q->shutdown) {
        spin_unlock_np(&q->l);
        return -EPIPE;
    }

    /* otherwise a new connection is available */
    q->backlog++;
    c = list_pop(&q->conns, tcp_conn_t, queue_link);
    assert(c != NULL);
    spin_unlock_np(&q->l);

    *c_out = c;
    return 0;
}

static void __tcp_qshutdown(tcp_queue_t *q)
{
    /* mark the listen queue as shutdown */
    spin_lock_np(&q->l);
    BUG_ON(q->shutdown);
    q->shutdown = true;
    spin_unlock_np(&q->l);

    /* prevent ingress receive and error dispatch (after RCU period) */
    trans_table_remove(&q->e);
}

/**
 * tcp_qshutdown - disables a TCP listener queue
 * @q: the TCP listener queue to disable
 *
 * All blocking requests on the queue will return -EPIPE.
 */
void tcp_qshutdown(tcp_queue_t *q)
{
    /* shutdown the listen queue */
    __tcp_qshutdown(q);

    /* wake up all pending threads */
    waitq_release(&q->wq);
}

static void tcp_queue_release(struct rcu_head *h)
{
    tcp_queue_t *q = container_of(h, tcp_queue_t, e.rcu);
    sfree(q);
}

/**
 * tcp_qclose - frees a TCP listener queue
 * @q: the TCP listener queue to close
 *
 * WARNING: Only the last reference can safely call this method. Call
 * tcp_qshutdown() first if any threads are sleeping on the queue.
 */
void tcp_qclose(tcp_queue_t *q)
{
    tcp_conn_t *c, *nextc;

    if (!q->shutdown)
        __tcp_qshutdown(q);

    BUG_ON(!waitq_empty(&q->wq));

    /* free all pending connections */
    list_for_each_safe(&q->conns, c, nextc, queue_link)
    {
        list_del_from(&q->conns, &c->queue_link);
        tcp_conn_destroy(c);
    }

    rcu_free(&q->e.rcu, tcp_queue_release);
}

/*
 * Support for the TCP socket API
 */

/**
 * tcp_dial - opens a TCP connection, creating a new socket
 * @laddr: the local address
 * @raddr: the remote address
 * @c_out: a pointer to store the new connection
 *
 * Returns 0 if successful, otherwise fail.
 */
int tcp_dial(struct netaddr laddr, struct netaddr raddr, tcp_conn_t **c_out)
{
    tcp_conn_t *c;
    int ret;

    /* create and initialize a connection */
    c = tcp_conn_alloc();
    if (unlikely(!c))
        return -ENOMEM;

    /*
     * Attach the connection to the transport layer. From this point onward
     * ingress packets can be dispatched to the connection.
     */
    ret = tcp_conn_attach(c, laddr, raddr);
    if (unlikely(ret)) {
        sfree(c);
        return ret;
    }

    /* send a SYN to the remote host */
    spin_lock_np(&c->lock);
    ret = tcp_tx_ctl(c, TCP_SYN);
    if (unlikely(ret)) {
        spin_unlock_np(&c->lock);
        tcp_conn_destroy(c);
        return ret;
    }
    tcp_conn_get(c); /* take a ref for the state machine */
    tcp_conn_set_state(c, TCP_STATE_SYN_SENT);

    /* wait until the connection is established or there is a failure */
    while (!c->tx_closed && c->pcb.state < TCP_STATE_ESTABLISHED) waitq_wait(&c->tx_wq, &c->lock);

    /* check if the connection failed */
    if (c->tx_closed) {
        ret = -c->err;
        spin_unlock_np(&c->lock);
        tcp_conn_destroy(c);
        return ret;
    }
    spin_unlock_np(&c->lock);

    *c_out = c;
    return 0;
}

/**
 * tcp_local_addr - gets the local address of a TCP connection
 * @c: the TCP connection
 */
struct netaddr tcp_local_addr(tcp_conn_t *c)
{
    return c->e.laddr;
}

/**
 * tcp_remote_addr - gets the remote address of a TCP connection
 * @c: the TCP connection
 */
struct netaddr tcp_remote_addr(tcp_conn_t *c)
{
    return c->e.raddr;
}

static ssize_t tcp_read_wait(tcp_conn_t *c, size_t len, struct list_head *q, struct mbuf **mout)
{
    struct mbuf *m;
    size_t readlen = 0;

    *mout = NULL;
    spin_lock_np(&c->lock);

    /* block until there is an actionable event */
    while (!c->rx_closed && (c->rx_exclusive || list_empty(&c->rxq)))
        waitq_wait(&c->rx_wq, &c->lock);

    /* is the socket closed? */
    if (c->rx_closed) {
        spin_unlock_np(&c->lock);
        return -c->err;
    }

    /* pop off the mbufs that will be read */
    while (readlen < len) {
        m = list_top(&c->rxq, struct mbuf, link);
        if (!m)
            break;

        if (unlikely((m->flags & TCP_FIN) > 0)) {
            tcp_conn_shutdown_rx(c);
            if (mbuf_length(m) == 0)
                break;
        }

        if (len - readlen < mbuf_length(m)) {
            c->rx_exclusive = true;
            *mout = m;
            readlen = len;
            break;
        }

        list_del_from(&c->rxq, &m->link);
        list_add_tail(q, &m->link);
        readlen += mbuf_length(m);
    }

    c->pcb.rcv_wnd += readlen;
    if (unlikely(c->rcv_wnd_full && c->pcb.rcv_wnd >= TCP_WIN / 4)) {
        tcp_tx_ack(c);
        c->rcv_wnd_full = false;
    }
    spin_unlock_np(&c->lock);

    return readlen;
}

static void tcp_read_finish(tcp_conn_t *c, struct mbuf *m)
{
    struct list_head waiters;

    if (!m)
        return;

    list_head_init(&waiters);
    spin_lock_np(&c->lock);
    c->rx_exclusive = false;
    waitq_release_start(&c->rx_wq, &waiters);
    spin_unlock_np(&c->lock);
    waitq_release_finish(&waiters);
}

/**
 * tcp_read - reads data from a TCP connection
 * @c: the TCP connection
 * @buf: a buffer to store the read data
 * @len: the length of @buf
 *
 * Returns the number of bytes read, 0 if the connection is closed, or < 0
 * if an error occurred.
 */
ssize_t tcp_read(tcp_conn_t *c, void *buf, size_t len)
{
    char *pos = buf;
    struct list_head q;
    struct mbuf *m;
    ssize_t ret;

    list_head_init(&q);

    /* wait for data to become available */
    ret = tcp_read_wait(c, len, &q, &m);

    /* check if connection was closed */
    if (ret <= 0)
        return ret;

    /* copy the data from the buffers */
    while (true) {
        struct mbuf *cur = list_pop(&q, struct mbuf, link);
        if (!cur)
            break;

        memcpy(pos, mbuf_data(cur), mbuf_length(cur));
        pos += mbuf_length(cur);
        mbuf_free(cur);
    }

    /* we may have to consume only part of a buffer */
    if (m) {
        size_t cpylen = len - (uintptr_t)pos + (uintptr_t)buf;
        memcpy(pos, mbuf_pull(m, cpylen), cpylen);
        m->seg_seq += cpylen;
    }

    /* wakeup any pending readers */
    tcp_read_finish(c, m);

    return ret;
}

static size_t iov_len(const struct iovec *iov, int iovcnt)
{
    size_t len = 0;
    int i;

    for (i = 0; i < iovcnt; i++) len += iov[iovcnt].iov_len;

    return len;
}

/**
 * tcp_readv - reads vectored data from a TCP connection
 * @c: the TCP connection
 * @iov: a pointer to the IO vector
 * @iovcnt: the number of vectors in @iov
 *
 * Returns the number of bytes read, 0 if the connection is closed, or < 0
 * if an error occurred.
 */
ssize_t tcp_readv(tcp_conn_t *c, const struct iovec *iov, int iovcnt)
{
    struct list_head q;
    struct mbuf *m;
    ssize_t len = iov_len(iov, iovcnt);
    size_t offset = 0;
    int i = 0;

    list_head_init(&q);

    /* wait for data to become available */
    len = tcp_read_wait(c, len, &q, &m);

    /* check if connection was closed */
    if (len <= 0)
        return len;

    /* copy the data from the buffers */
    while (true) {
        struct mbuf *cur = list_pop(&q, struct mbuf, link);
        if (!cur)
            break;

        do {
            const struct iovec *vp = &iov[i];
            size_t cpylen = MIN(vp->iov_len - offset, mbuf_length(m));

            memcpy((char *)vp->iov_base + offset, mbuf_pull(m, cpylen), cpylen);

            offset += cpylen;
            if (offset == vp->iov_len) {
                offset = 0;
                i++;
            }

            assert(i <= iovcnt);
        } while (mbuf_length(m) > 0);
        mbuf_free(cur);
    }

    /* we may have to consume only part of a buffer */
    if (m) {
        do {
            const struct iovec *vp = &iov[i];
            size_t cpylen = MIN(vp->iov_len - offset, mbuf_length(m));

            memcpy((char *)vp->iov_base + offset, mbuf_pull(m, cpylen), cpylen);
            m->seg_seq += cpylen;
            offset += cpylen;
            if (offset == vp->iov_len) {
                offset = 0;
                i++;
            }

            assert(mbuf_length(m) > 0);
        } while (i < iovcnt);
    }

    /* wakeup any pending readers */
    tcp_read_finish(c, m);

    return len;
}

static int tcp_write_wait(tcp_conn_t *c, size_t *winlen)
{
    spin_lock_np(&c->lock);

    /* block until there is an actionable event */
    while (!c->tx_closed && (c->pcb.state < TCP_STATE_ESTABLISHED || c->tx_exclusive ||
                             wraps_lte(c->pcb.snd_una + c->pcb.snd_wnd, c->pcb.snd_nxt))) {
        waitq_wait(&c->tx_wq, &c->lock);
    }

    /* is the socket closed? */
    if (c->tx_closed) {
        spin_unlock_np(&c->lock);
        return c->err ? -c->err : -EPIPE;
    }

    /* drop the lock to allow concurrent RX processing */
    c->tx_exclusive = true;
    /* TODO: must allow at least one byte to avoid zero window deadlock */
    *winlen = c->pcb.snd_una + c->pcb.snd_wnd - c->pcb.snd_nxt;
    spin_unlock_np(&c->lock);

    return 0;
}

static void tcp_write_finish(tcp_conn_t *c)
{
    struct list_head q;
    struct list_head waiters;
    struct mbuf *retransmit = NULL;

    assert(c->tx_exclusive == true);
    list_head_init(&q);
    list_head_init(&waiters);

    spin_lock_np(&c->lock);
    c->tx_exclusive = false;
    tcp_conn_ack(c, &q);
    if (c->pcb.rcv_nxt == c->tx_last_ack) /* race condition check */
        c->ack_delayed = false;
    else
        c->ack_ts = now_us();
    if (c->pcb.state == TCP_STATE_CLOSED) {
        list_append_list(&q, &c->txq);
        if (c->tx_pending) {
            list_add_tail(&q, &c->tx_pending->link);
            c->tx_pending = NULL;
        }
    } else if (c->do_fast_retransmit) {
        c->do_fast_retransmit = false;
        if (c->fast_retransmit_last_ack == c->pcb.snd_una)
            retransmit = tcp_tx_fast_retransmit_start(c);
    }

    tcp_timer_update(c);
    waitq_release_start(&c->tx_wq, &waiters);
    spin_unlock_np(&c->lock);

    tcp_tx_fast_retransmit_finish(c, retransmit);
    waitq_release_finish(&waiters);
    mbuf_list_free(&q);
}

/**
 * tcp_write - writes data to a TCP connection
 * @c: the TCP connection
 * @buf: a buffer from which to copy the data
 * @len: the length of the data
 *
 * Returns the number of bytes written (could be less than @len), or < 0
 * if there was a failure.
 */
ssize_t tcp_write(tcp_conn_t *c, const void *buf, size_t len)
{
    size_t winlen;
    ssize_t ret;

    /* block until the data can be sent */
    ret = tcp_write_wait(c, &winlen);
    if (ret)
        return ret;

    /* actually send the data */
    ret = tcp_tx_send(c, buf, MIN(len, winlen), len <= winlen);

    /* catch up on any pending work */
    tcp_write_finish(c);

    return ret;
}

/**
 * tcp_writev - writes vectored data to a TCP connection
 * @c: the TCP connection
 * @iov: a pointer to the IO vector
 * @iovcnt: the number of vectors in @iov
 *
 * Returns the number of bytes written (could be less than requested), or < 0
 * if there was a failure.
 */
ssize_t tcp_writev(tcp_conn_t *c, const struct iovec *iov, int iovcnt)
{
    size_t winlen;
    ssize_t sent = 0, ret;
    int i;

    /* block until the data can be sent */
    ret = tcp_write_wait(c, &winlen);
    if (ret)
        return ret;

    /* actually send the data */
    for (i = 0; i < iovcnt; i++, iov++) {
        if (winlen <= 0)
            break;
        ret = tcp_tx_send(c, iov->iov_base, MIN(iov->iov_len, winlen),
                          i == iovcnt - 1 && iov->iov_len <= winlen);
        if (ret <= 0)
            break;
        winlen -= ret;
        sent += ret;
    }

    /* catch up on any pending work */
    tcp_write_finish(c);

    return sent > 0 ? sent : ret;
}

/* resend any pending egress packets that timed out */
static void tcp_retransmit(void *arg)
{
    tcp_conn_t *c = (tcp_conn_t *)arg;

    spin_lock_np(&c->lock);

    while (c->tx_exclusive && c->pcb.state != TCP_STATE_CLOSED) waitq_wait(&c->tx_wq, &c->lock);

    if (c->pcb.state != TCP_STATE_CLOSED) {
        c->tx_exclusive = true;
        spin_unlock_np(&c->lock);
        tcp_tx_retransmit(c);
        tcp_write_finish(c);
    } else {
        spin_unlock_np(&c->lock);
    }

    tcp_conn_put(c);
}

/**
 * tcp_conn_fail - closes a TCP both sides of a connection with an error
 * @c: the TCP connection to shutdown
 * @err: the error code (failure reason for the close)
 *
 * The caller must hold @c's lock.
 */
void tcp_conn_fail(tcp_conn_t *c, int err)
{
    assert_spin_lock_held(&c->lock);

    c->err = err;
    tcp_conn_set_state(c, TCP_STATE_CLOSED);

    if (!c->rx_closed) {
        c->rx_closed = true;
        waitq_release(&c->rx_wq);
    }

    if (!c->tx_closed) {
        c->tx_closed = true;
        waitq_release(&c->tx_wq);
    }

    /* will be freed by the writer if one is busy */
    if (!c->tx_exclusive) {
        mbuf_list_free(&c->txq);
        if (c->tx_pending) {
            mbuf_free(c->tx_pending);
            c->tx_pending = NULL;
        }
    }
    if (!c->rx_exclusive)
        mbuf_list_free(&c->rxq);
    mbuf_list_free(&c->rxq_ooo);

    /* state machine is disabled, drop ref */
    tcp_conn_put(c);
}

/**
 * tcp_conn_shutdown_rx - closes ingress for a TCP connection
 * @c: the TCP connection to shutdown
 *
 * The caller must hold @c's lock.
 */
void tcp_conn_shutdown_rx(tcp_conn_t *c)
{
    assert_spin_lock_held(&c->lock);

    if (c->rx_closed)
        return;

    c->rx_closed = true;
    waitq_release(&c->rx_wq);
}

static int tcp_conn_shutdown_tx(tcp_conn_t *c)
{
    int ret;

    assert_spin_lock_held(&c->lock);

    if (c->tx_closed)
        return 0;

    assert(c->pcb.state >= TCP_STATE_ESTABLISHED);
    while (c->tx_exclusive) waitq_wait(&c->tx_wq, &c->lock);
    ret = tcp_tx_ctl(c, TCP_FIN | TCP_ACK);
    if (unlikely(ret))
        return ret;
    if (c->pcb.state == TCP_STATE_ESTABLISHED)
        tcp_conn_set_state(c, TCP_STATE_FIN_WAIT1);
    else if (c->pcb.state == TCP_STATE_CLOSE_WAIT)
        tcp_conn_set_state(c, TCP_STATE_LAST_ACK);
    else
        WARN();

    c->tx_closed = true;
    waitq_release(&c->tx_wq);

    return 0;
}

/**
 * tcp_shutdown - shuts a TCP connection down
 * @c: the TCP connection to shutdown
 * @how: the directions to shutdown (SHUT_RD, SHUT_WR, or SHUT_RDWR)
 *
 * Returns 0 if successful, otherwise < 0 for failure.
 */
int tcp_shutdown(tcp_conn_t *c, int how)
{
    bool tx, rx;
    int ret;

    if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR)
        return -EINVAL;

    tx = how == SHUT_WR || how == SHUT_RDWR;
    rx = how == SHUT_RD || how == SHUT_RDWR;

    spin_lock_np(&c->lock);
    if (tx) {
        ret = tcp_conn_shutdown_tx(c);
        if (ret) {
            spin_unlock_np(&c->lock);
            return ret;
        }
    }
    if (rx)
        tcp_conn_shutdown_rx(c);
    spin_unlock_np(&c->lock);

    return 0;
}

/**
 * tcp_abort - force an immediate (ungraceful) close of the connection
 * @c: the TCP connection to abort
 */
void tcp_abort(tcp_conn_t *c)
{
    int i;
    uint32_t snd_nxt;
    struct netaddr l, r;

    spin_lock_np(&c->lock);
    if (c->pcb.state == TCP_STATE_CLOSED) {
        spin_unlock_np(&c->lock);
        return;
    }

    l = c->e.laddr;
    r = c->e.raddr;
    tcp_conn_fail(c, ECONNABORTED);

    while (c->tx_exclusive) waitq_wait(&c->tx_wq, &c->lock);

    snd_nxt = c->pcb.snd_nxt;
    spin_unlock_np(&c->lock);

    for (i = 0; i < 10; i++) {
        if (tcp_tx_raw_rst(l, r, snd_nxt) == 0)
            return;
        timer_sleep(10);
    }

    log_warn("tcp: failed to transmit TCP_RST");
}

/**
 * tcp_close - frees a TCP connection
 * @c: the TCP connection to free
 *
 * WARNING: Only the last reference can safely call this method. Call
 * tcp_shutdown() or tcp_abort() first if any threads are sleeping on the
 * socket.
 */
void tcp_close(tcp_conn_t *c)
{
    int ret;

    spin_lock_np(&c->lock);
    BUG_ON(!waitq_empty(&c->rx_wq));
    ret = tcp_conn_shutdown_tx(c);
    if (ret)
        tcp_conn_fail(c, -ret);
    tcp_conn_shutdown_rx(c);
    spin_unlock_np(&c->lock);

    tcp_conn_put(c);
}

/**
 * tcp_init_late - starts the TCP worker thread
 *
 * Returns 0 if successful.
 */
int tcp_init_late(void)
{
    return 0;
}
