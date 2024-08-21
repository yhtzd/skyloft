/*
 * udp.h - UDP sockets
 */

#pragma once

#include <sys/socket.h>
#include <sys/uio.h>

#include <skyloft/net.h>

/* the maximum size of a UDP payload */
#define UDP_MAX_PAYLOAD 1472

/*
 * UDP Socket API
 */

struct udp_conn;
typedef struct udp_conn udp_conn_t;

int udp_dial(struct netaddr laddr, struct netaddr raddr, udp_conn_t **c_out);
int udp_listen(struct netaddr laddr, udp_conn_t **c_out);
struct netaddr udp_local_addr(udp_conn_t *c);
struct netaddr udp_remote_addr(udp_conn_t *c);
int udp_set_buffers(udp_conn_t *c, int read_mbufs, int write_mbufs);
ssize_t udp_read_from(udp_conn_t *c, void *buf, size_t len, struct netaddr *raddr);
ssize_t udp_write_to(udp_conn_t *c, const void *buf, size_t len, const struct netaddr *raddr);
ssize_t udp_read(udp_conn_t *c, void *buf, size_t len);
ssize_t udp_write(udp_conn_t *c, const void *buf, size_t len);
void udp_shutdown(udp_conn_t *c);
void udp_close(udp_conn_t *c);

/*
 * UDP Parallel API
 */

struct udp_spawner;
typedef struct udp_spawner udp_spawner_t;
typedef struct udp_spawn_data udp_spawn_data_t;

struct udp_spawn_data {
    const void *buf;
    size_t len;
    struct netaddr laddr;
    struct netaddr raddr;
    void *release_data;
};

typedef void (*udpspawn_fn_t)(struct udp_spawn_data *d);

int udp_create_spawner(struct netaddr laddr, udpspawn_fn_t fn, udp_spawner_t **s_out);
void udp_destroy_spawner(udp_spawner_t *s);
ssize_t udp_send(const void *buf, size_t len, struct netaddr laddr, struct netaddr raddr);
ssize_t udp_sendv(const struct iovec *iov, int iovcnt, struct netaddr laddr, struct netaddr raddr);
void udp_spawn_data_release(void *release_data);

/**
 * udp_respond - sends a response datagram to a spawner datagram
 * @buf: a buffer containing the datagram
 * @len: the length of the datagram
 * @d: the UDP spawner data
 *
 * Returns @len if successful, otherwise fail.
 */
static inline ssize_t udp_respond(const void *buf, size_t len, struct udp_spawn_data *d)
{
    return udp_send(buf, len, d->laddr, d->raddr);
}

static inline ssize_t udp_respondv(const struct iovec *iov, int iovcnt, struct udp_spawn_data *d)
{
    return udp_sendv(iov, iovcnt, d->laddr, d->raddr);
}
