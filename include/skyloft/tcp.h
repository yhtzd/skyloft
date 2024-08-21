/*
 * tcp.h - TCP sockets
 */

#pragma once

#include <sys/socket.h>
#include <sys/uio.h>

#include <skyloft/net.h>

struct tcp_queue;
typedef struct tcp_queue tcp_queue_t;
struct tcp_conn;
typedef struct tcp_conn tcp_conn_t;

int tcp_dial(struct netaddr laddr, struct netaddr raddr, tcp_conn_t **c_out);
int tcp_listen(struct netaddr laddr, int backlog, tcp_queue_t **q_out);
int tcp_accept(tcp_queue_t *q, tcp_conn_t **c_out);
void tcp_qshutdown(tcp_queue_t *q);
void tcp_qclose(tcp_queue_t *q);
struct netaddr tcp_local_addr(tcp_conn_t *c);
struct netaddr tcp_remote_addr(tcp_conn_t *c);
ssize_t tcp_read(tcp_conn_t *c, void *buf, size_t len);
ssize_t tcp_write(tcp_conn_t *c, const void *buf, size_t len);
ssize_t tcp_readv(tcp_conn_t *c, const struct iovec *iov, int iovcnt);
ssize_t tcp_writev(tcp_conn_t *c, const struct iovec *iov, int iovcnt);
int tcp_shutdown(tcp_conn_t *c, int how);
void tcp_abort(tcp_conn_t *c);
void tcp_close(tcp_conn_t *c);
