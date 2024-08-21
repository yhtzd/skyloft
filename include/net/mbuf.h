/*
 * mbuf.h - buffer management for network packets
 *
 * TODO: Maybe consider adding refcounts to mbuf's. Let's wait until this turns
 * out to be necessary.
 */

#pragma once

#include <limits.h>

#include <utils/assert.h>
#include <utils/atomic.h>
#include <utils/defs.h>
#include <utils/list.h>
#include <utils/types.h>

#define MBUF_DEFAULT_LEN      2048
#define MBUF_DEFAULT_HEADROOM 128

struct mbuf {
    struct mbuf *next;      /* the next mbuf in the mbufq */
    unsigned char *head;    /* start of the buffer */
    unsigned char *data;    /* current position within the buffer */
    unsigned int head_len;  /* length of the entire buffer from @head */
    unsigned int len;       /* length of the data */
    unsigned int csum_type; /* type of checksum */
    unsigned int csum;      /* 16-bit one's complement */

    union {
        unsigned int txflags;  /* TX offload flags */
        unsigned int rss_hash; /* RSS 5-tuple hash from HW */
    };

    unsigned short network_off;      /* the offset of the network header */
    unsigned short transport_off;    /* the offset of the transport header */
    unsigned long release_data;      /* data for the release method */
    void (*release)(struct mbuf *m); /* frees the mbuf */

    /* TCP fields */
    struct list_node link; /* list node for RX and TX queues */
    uint64_t timestamp;    /* the time the packet was last sent */
    uint32_t seg_seq;      /* the first seg number */
    uint32_t seg_end;      /* the last seg number (noninclusive) */
    uint8_t flags;         /* which flags were set? */
    atomic_int ref;        /* a reference count for the mbuf */
};

static inline unsigned char *__mbuf_pull(struct mbuf *m, unsigned int len)
{
    unsigned char *tmp = m->data;
    m->len -= len;
    m->data += len;
    return tmp;
}

/**
 * mbuf_pull - strips data from the beginning of the buffer
 * @m: the packet
 * @len: the length in bytes to strip
 *
 * Returns the previous start of the buffer.
 */
static inline unsigned char *mbuf_pull(struct mbuf *m, unsigned int len)
{
    BUG_ON(len > m->len);
    return __mbuf_pull(m, len);
}

/**
 * mbuf_pull_or_null - strips data from the beginning of the buffer
 * @m: the packet
 * @len: the length in bytes to strip
 *
 * Returns the previous start of the buffer or NULL if the buffer is smaller
 * than @len.
 */
static inline unsigned char *mbuf_pull_or_null(struct mbuf *m, unsigned int len)
{
    return m->len >= len ? __mbuf_pull(m, len) : NULL;
}

/**
 * mbuf_push - prepends data to the beginning of the buffer
 * @m: the packet
 * @len: the length in bytes to prepend
 *
 * Returns the new start of the buffer.
 */
static inline unsigned char *mbuf_push(struct mbuf *m, unsigned int len)
{
    m->data -= len;
    BUG_ON(m->data < m->head);
    m->len += len;
    return m->data;
}

/**
 * mbuf_put - appends data to the end of the buffer
 * @m: the packet
 * @len: the length in bytes to append
 *
 * Returns the previous end of the buffer.
 */
static inline unsigned char *mbuf_put(struct mbuf *m, unsigned int len)
{
    unsigned char *tmp = m->data + m->len;
    m->len += len;
    BUG_ON(m->len > m->head_len);
    return tmp;
}

/**
 * mbuf_trim - strips data off the end of the buffer
 * @m: the packet
 * @len: the length in bytes to strip
 *
 * Returns a pointer to the start of the bytes that were stripped.
 */
static inline unsigned char *mbuf_trim(struct mbuf *m, unsigned int len)
{
    BUG_ON(len > m->len);
    m->len -= len;
    return m->data + m->len;
}

/**
 * mbuf_reset - forces an mbuf to reposition the data pointer to an offset
 * @m: the packet
 * @offset: the new data offset from @m->head
 */
static inline void mbuf_reset(struct mbuf *m, unsigned int offset)
{
    unsigned int total_len = m->data - m->head + m->len;
    BUG_ON(offset > total_len);
    m->data = m->head + offset;
    m->len = total_len - offset;
}

/**
 * mbuf_headroom - returns the space available before the start of the buffer
 * @m: the packet
 */
static inline unsigned int mbuf_headroom(struct mbuf *m)
{
    return m->data - m->head;
}

/**
 * mbuf_tailroom - returns the space available after the end of the buffer
 * @m: the packet
 */
static inline unsigned int mbuf_tailroom(struct mbuf *m)
{
    return m->head + m->head_len - m->data - m->len;
}

/**
 * mbuf_data - returns the current data pointer
 * @m: the packet
 */
static inline unsigned char *mbuf_data(struct mbuf *m)
{
    return m->data;
}

/**
 * mbuf_length - returns the current data length
 * @m: the packet
 */
static inline unsigned int mbuf_length(struct mbuf *m)
{
    return m->len;
}

/*
 * These marcos automatically typecast and determine the size of header structs.
 * In most situations you should use these instead of the raw ops above.
 */
#define mbuf_pull_hdr(mbuf, hdr) (typeof(hdr) *)mbuf_pull(mbuf, sizeof(hdr))

#define mbuf_pull_hdr_or_null(mbuf, hdr) (typeof(hdr) *)mbuf_pull_or_null(mbuf, sizeof(hdr))

#define mbuf_push_hdr(mbuf, hdr) (typeof(hdr) *)mbuf_push(mbuf, sizeof(hdr))

#define mbuf_put_hdr(mbuf, hdr) (typeof(hdr) *)mbuf_put(mbuf, sizeof(hdr))

#define mbuf_trim_hdr(mbuf, hdr) (typeof(hdr) *)mbuf_trim(mbuf, sizeof(hdr))

/**
 * mbuf_mark_network_offset - sets the network offset to the data pointer
 * @m: the mbuf in which to set the network offset
 */
static inline void mbuf_mark_network_offset(struct mbuf *m)
{
    ptrdiff_t off = m->data - m->head;
    assert(off <= USHRT_MAX);
    m->network_off = off;
}

/**
 * mbuf_mark_transport_offset - sets the transport offset to the data pointer
 * @m: the mbuf in which to set the transport offset
 */
static inline void mbuf_mark_transport_offset(struct mbuf *m)
{
    ptrdiff_t off = m->data - m->head;
    assert(off <= USHRT_MAX);
    m->transport_off = off;
}

/**
 * mbuf_network_offset - returns a pointer to the network header
 * @m: the mbuf containing the network offset
 */
static inline unsigned char *mbuf_network_offset(struct mbuf *m)
{
    return m->head + m->network_off;
}

/**
 * mbuf_network_offset - returns a pointer to the transport header
 * @m: the mbuf containing the transport offset
 */
static inline unsigned char *mbuf_transport_offset(struct mbuf *m)
{
    return m->head + m->transport_off;
}

#define mbuf_network_hdr(mbuf, hdr) (typeof(hdr) *)mbuf_network_offset(mbuf)

#define mbuf_transport_hdr(mbuf, hdr) (typeof(hdr) *)mbuf_transport_offset(mbuf)

/**
 * mbuf_init - initializes an mbuf
 * @m: the packet to initialize
 * @head: the start of the backing buffer
 * @head_len: the length of backing buffer
 * @reserve_len: the number of bytes to reserve at the start of @head
 */
static inline void mbuf_init(struct mbuf *m, unsigned char *head, unsigned int head_len,
                             unsigned int reserve_len)
{
    assert(reserve_len < head_len);
    m->head = head;
    m->head_len = head_len;
    m->data = m->head + reserve_len;
    m->len = 0;
}

/**
 * mbuf_free - frees an mbuf back to an allocator
 * @m: the mbuf to free
 */
static inline void mbuf_free(struct mbuf *m)
{
    m->release(m);
}

struct mbuf *mbuf_clone(struct mbuf *dst, struct mbuf *src);

struct mbufq {
	struct mbuf *head, *tail;
};

/**
 * mbufq_push_tail - push an mbuf to the tail of the queue
 * @q: the mbuf queue
 * @m: the mbuf to push
 */
static inline void mbufq_push_tail(struct mbufq *q, struct mbuf *m)
{
	m->next = NULL;
	if (!q->head) {
		q->head = q->tail = m;
		return;
	}
	q->tail->next = m;
	q->tail = m;
}

/**
 * mbufq_pop_head - pop an mbuf from the head of the queue
 * @q: the mbuf queue
 *
 * Returns an mbuf or NULL if the queue is empty.
 */
static inline struct mbuf *mbufq_pop_head(struct mbufq *q)
{
	struct mbuf *head = q->head;
	if (!head)
		return NULL;
	q->head = head->next;
	return head;
}

/**
 * mbufq_peak_head - reads the head of the queue without popping
 * @q: the mbuf queue
 *
 * Returns an mbuf or NULL if the queue is empty.
 */
static inline struct mbuf *mbufq_peak_head(struct mbufq *q)
{
	return q->head;
}

/**
 * mbufq_merge_to_tail - merges a queue to the end of another queue
 * @dst: the destination queue (will contain all the mbufs)
 * @src: the source queue (will become empty)
 */
static inline void mbufq_merge_to_tail(struct mbufq *dst, struct mbufq *src)
{
	if (!src->head)
		return;
	if (!dst->head)
		dst->head = src->head;
	else 
		dst->tail->next = src->head;
	dst->tail = src->tail;
	src->head = NULL;
}

/**
 * mbufq_empty - returns true if the queue is empty
 */
static inline bool mbufq_empty(struct mbufq *q)
{
	return q->head == NULL;
}

/**
 * mbufq_release - frees all the mbufs in the queue
 * @q: the queue to release
 */
static inline void mbufq_release(struct mbufq *q)
{
	struct mbuf *m;
	while (true) {
		m = mbufq_pop_head(q);
		if (!m)
			break;
		mbuf_free(m);
	}
}

/**
 * mbufq_init - initializes a queue
 * @q: the mbuf queue to initialize
 */
static inline void mbufq_init(struct mbufq *q)
{
	q->head = NULL;
}
