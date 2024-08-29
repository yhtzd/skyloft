/*
 * tcp.h - local header for TCP support
 */

#pragma once

#include <skyloft/net.h>
#include <skyloft/tcp.h>
#include <utils/defs.h>
#include <utils/kref.h>

#include "waitq.h"

/* adjustable constants */
#define TCP_MSS                    (ETH_MTU - sizeof(struct ip_hdr) - sizeof(struct tcp_hdr))
#define TCP_WIN                    ((65535 / TCP_MSS) * TCP_MSS)
#define TCP_ACK_TIMEOUT            (10 * USEC_PER_MSEC)
#define TCP_OOQ_ACK_TIMEOUT        (300 * USEC_PER_MSEC)
#define TCP_TIME_WAIT_TIMEOUT      (1 * USEC_PER_SEC)    /* FIXME: should be 8 minutes */
#define TCP_RETRANSMIT_TIMEOUT     (300 * USEC_PER_MSEC) /* FIXME: should be dynamic */
#define TCP_FAST_RETRANSMIT_THRESH 3
#define TCP_OOO_MAX_SIZE           2048
#define TCP_RETRANSMIT_BATCH       16

/* connecion states (RFC 793 Section 3.2) */
enum {
    TCP_STATE_SYN_SENT = 0,
    TCP_STATE_SYN_RECEIVED,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT1,
    TCP_STATE_FIN_WAIT2,
    TCP_STATE_CLOSE_WAIT,
    TCP_STATE_CLOSING,
    TCP_STATE_LAST_ACK,
    TCP_STATE_TIME_WAIT,
    TCP_STATE_CLOSED,
};

/* TCP protocol control block (PCB) */
struct tcp_pcb {
    int state; /* the connection state */

    /* send sequence variables (RFC 793 Section 3.2) */
    uint32_t snd_una; /* send unacknowledged */
    uint32_t snd_nxt; /* send next */
    uint32_t snd_wnd; /* send window */
    uint32_t snd_up;  /* send urgent pointer */
    uint32_t snd_wl1; /* last window update - seq number */
    uint32_t snd_wl2; /* last window update - ack number */
    uint32_t iss;     /* initial send sequence number */

    /* receive sequence variables (RFC 793 Section 3.2) */
    union {
        struct {
            uint32_t rcv_nxt; /* receive next */
            uint32_t rcv_wnd; /* receive window */
        };
        uint64_t rcv_nxt_wnd;
    };
    uint32_t rcv_up; /* receive urgent pointer */
    uint32_t irs;    /* initial receive sequence number */
};

/* the TCP connection struct */
struct tcp_conn {
    struct trans_entry e;
    struct tcp_pcb pcb;
    struct list_node global_link;
    struct list_node queue_link;
    spinlock_t lock;
    struct kref ref;
    int err; /* error code for read(), write(), etc. */

    /* ingress path */
    unsigned int rx_closed : 1;
    unsigned int rx_exclusive : 1;
    waitq_t rx_wq;
    struct list_head rxq_ooo;
    struct list_head rxq;

    /* egress path */
    unsigned int tx_closed : 1;
    unsigned int tx_exclusive : 1;
    waitq_t tx_wq;
    uint32_t tx_last_ack;
    uint16_t tx_last_win;
    struct mbuf *tx_pending;
    struct list_head txq;
    bool do_fast_retransmit;
    uint32_t fast_retransmit_last_ack;

    /* timeouts */
    uint64_t next_timeout;
    bool ack_delayed;
    bool rcv_wnd_full;
    uint64_t ack_ts;
    uint64_t time_wait_ts;
    int rep_acks;
};
tcp_conn_t *tcp_conn_alloc(void);
int tcp_conn_attach(tcp_conn_t *c, struct netaddr laddr, struct netaddr raddr);
void tcp_conn_ack(tcp_conn_t *c, struct list_head *freeq);
void tcp_conn_set_state(tcp_conn_t *c, int new_state);
void tcp_conn_fail(tcp_conn_t *c, int err);
void tcp_conn_shutdown_rx(tcp_conn_t *c);
void tcp_conn_destroy(tcp_conn_t *c);

void tcp_timer_update(tcp_conn_t *c);

/**
 * tcp_conn_get - increments the connection ref count
 * @c: the connection to increment
 *
 * Returns @c.
 */
static inline tcp_conn_t *tcp_conn_get(tcp_conn_t *c)
{
    kref_get(&c->ref);
    return c;
}

void tcp_conn_release_ref(struct kref *r);

/**
 * tcp_conn_put - decrements the connection ref count
 * @c: the connection to decrement
 */
static inline void tcp_conn_put(tcp_conn_t *c)
{
    kref_put(&c->ref, tcp_conn_release_ref);
}

/*
 * ingress path
 */

void tcp_rx_conn(struct trans_entry *e, struct mbuf *m);
tcp_conn_t *tcp_rx_listener(struct netaddr laddr, struct mbuf *m);

/*
 * egress path
 */

int tcp_tx_raw_rst(struct netaddr laddr, struct netaddr raddr, tcp_seq seq);
int tcp_tx_raw_rst_ack(struct netaddr laddr, struct netaddr raddr, tcp_seq seq, tcp_seq ack);
int tcp_tx_ack(tcp_conn_t *c);
int tcp_tx_ctl(tcp_conn_t *c, uint8_t flags);
ssize_t tcp_tx_send(tcp_conn_t *c, const void *buf, size_t len, bool push);
void tcp_tx_retransmit(tcp_conn_t *c);
struct mbuf *tcp_tx_fast_retransmit_start(tcp_conn_t *c);
void tcp_tx_fast_retransmit_finish(tcp_conn_t *c, struct mbuf *m);

/*
 * utilities
 */

/* free all mbufs in a linked list */
static inline void mbuf_list_free(struct list_head *h)
{
    struct mbuf *m;

    while (true) {
        m = list_pop(h, struct mbuf, link);
        if (!m)
            break;

        mbuf_free(m);
    }
}

/*
 * debugging
 */

// #if DEBUG
// extern void tcp_debug_egress_pkt(tcp_conn_t *c, struct mbuf *m);
// extern void tcp_debug_ingress_pkt(tcp_conn_t *c, struct mbuf *m);
// extern void tcp_debug_state_change(tcp_conn_t *c, int last, int next);
// #else  /* DEBUG */
static inline void tcp_debug_egress_pkt(tcp_conn_t *c, struct mbuf *m) {}
static inline void tcp_debug_ingress_pkt(tcp_conn_t *c, struct mbuf *m) {}
static inline void tcp_debug_state_change(tcp_conn_t *c, int last, int next) {}
// #endif /* DEBUG */