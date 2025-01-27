/*
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2020 Ericsson AB
 */

#include "common_tp.h"
#include "ctl.h"
#include "epoll_reg.h"
#include "log_tp.h"
#include "log_utls.h"
#include "util.h"
#include "xcm.h"
#include "xcm_addr.h"
#include "xcm_addr_limits.h"
#include "xcm_tp.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * UTLS XCM Transport
 *
 * UTLS uses UNIX Domain Sockets in case the client and server reside
 * in the same network namespace, and TLS for all other communication.
 *
 * From a user application perspective, the UTLS socket only exists in
 * the server socket form - the connection sockets objects are of the
 * TLS or UNIX types. Internally, there is a UTLS socket wrapping the
 * UX and TLS socket, even in the case of connection sockets.
 */

struct utls_socket
{
    char laddr[XCM_ADDR_MAX+1];

    struct xcm_socket *ux_socket;
    struct xcm_socket *tls_socket;

    struct xcm_tp_attr *utls_attrs;
    const struct xcm_tp_attr **real_attrs;
    struct xcm_socket **real_sockets;
    size_t attrs_len;
};

#define TOUTLS(s) XCM_TP_GETPRIV(s, struct utls_socket)

static int utls_init(struct xcm_socket *s);
static int utls_connect(struct xcm_socket *s, const char *remote_addr);
static int utls_server(struct xcm_socket *s, const char *local_addr);
static int utls_close(struct xcm_socket *s);
static void utls_cleanup(struct xcm_socket *s);
static int utls_accept(struct xcm_socket *conn_s, struct xcm_socket *server_s);
static int utls_send(struct xcm_socket *s, const void *buf, size_t len);
static int utls_receive(struct xcm_socket *s, void *buf, size_t capacity);
static void utls_update(struct xcm_socket *s);
static int utls_finish(struct xcm_socket *s);
static const char *utls_get_transport(struct xcm_socket *s);
static const char *utls_get_remote_addr(struct xcm_socket *s,
					bool suppress_tracing);
static int utls_set_local_addr(struct xcm_socket *s, const char *local_addr);
static const char *utls_get_local_addr(struct xcm_socket *socket,
				       bool suppress_tracing);
static size_t utls_max_msg(struct xcm_socket *conn_s);
const struct cnt_conn *utls_get_cnt(struct xcm_socket *conn_s);
static void utls_enable_ctl(struct xcm_socket *s);
static void utls_get_attrs(struct xcm_socket* s,
			   const struct xcm_tp_attr **attr_list,
			   size_t *attr_list_len);
static size_t utls_priv_size(enum xcm_socket_type type);

static struct xcm_tp_ops utls_ops = {
    .init = utls_init,
    .connect = utls_connect,
    .server = utls_server,
    .close = utls_close,
    .cleanup = utls_cleanup,
    .accept = utls_accept,
    .send = utls_send,
    .receive = utls_receive,
    .update = utls_update,
    .finish = utls_finish,
    .get_transport = utls_get_transport,
    .get_remote_addr = utls_get_remote_addr,
    .set_local_addr = utls_set_local_addr,
    .get_local_addr = utls_get_local_addr,
    .max_msg = utls_max_msg,
    .get_cnt = utls_get_cnt,
    .enable_ctl = utls_enable_ctl,
    .get_attrs = utls_get_attrs,
    .priv_size = utls_priv_size
};

static void reg(void) __attribute__((constructor));
static void reg(void)
{
    xcm_tp_register(XCM_UTLS_PROTO, &utls_ops);
}

static struct xcm_tp_proto *get_proto(const char *name,
				      struct xcm_tp_proto **cached_proto)
{
    struct xcm_tp_proto *proto =
	__atomic_load_n(cached_proto, __ATOMIC_RELAXED);

    if (proto == NULL) {
	proto = xcm_tp_proto_by_name(name);
	__atomic_store_n(cached_proto, proto, __ATOMIC_RELAXED);
    }

    return proto;
}

static struct xcm_tp_proto *ux_proto(void)
{
    static struct xcm_tp_proto *ux_cached_proto = NULL;
    return get_proto(XCM_UX_PROTO, &ux_cached_proto);
}

static struct xcm_tp_proto *tls_proto(void)
{
    static struct xcm_tp_proto *tls_cached_proto = NULL;
    return get_proto(XCM_TLS_PROTO, &tls_cached_proto);
}

static struct xcm_socket *create_sub_socket(struct xcm_tp_proto *proto,
					    enum xcm_socket_type type,
					    int epoll_fd)
{
    struct xcm_socket *s =
	xcm_tp_socket_create(proto, type, epoll_fd, false);

    if (!s)
	goto err;

    if (xcm_tp_socket_init(s) < 0)
	goto err_destroy;

    return s;

err_destroy:
    xcm_tp_socket_destroy(s);
err:
    return NULL;
}

static int utls_init(struct xcm_socket *s)
{
    struct utls_socket *us = TOUTLS(s);

    us->ux_socket = create_sub_socket(ux_proto(), s->type, s->epoll_fd);
    us->tls_socket = create_sub_socket(tls_proto(), s->type, s->epoll_fd);

    if (!us->ux_socket || !us->tls_socket) {
	xcm_tp_socket_destroy(us->ux_socket);
	return -1;
    }

    return 0;
}

static void deinit(struct xcm_socket *s)
{
    if (s) {
	struct utls_socket *us = TOUTLS(s);
	xcm_tp_socket_destroy(us->ux_socket);
	xcm_tp_socket_destroy(us->tls_socket);
	ut_free(us->utls_attrs);
	ut_free(us->real_attrs);
	ut_free(us->real_sockets);
    }
}

static size_t utls_priv_size(enum xcm_socket_type type)
{
    return sizeof(struct utls_socket);
}

#define PROTO_SEP_LEN (1)

static void map_tls_to_ux(const char *tls_addr, char *ux_addr, size_t capacity)
{
    int rc = xcm_addr_ux_make(tls_addr+strlen(XCM_TLS_PROTO)+PROTO_SEP_LEN,
			      ux_addr, capacity);
    ut_assert(rc == 0);
}

void remove_sub_socket(struct xcm_socket **s)
{
    xcm_tp_socket_destroy(*s);
    *s = NULL;
}

static int utls_connect(struct xcm_socket *s, const char *remote_addr)
{
    LOG_CONN_REQ(remote_addr);

    struct utls_socket *us = TOUTLS(s);

    struct xcm_addr_host host;
    uint16_t port;
    if (xcm_addr_parse_utls(remote_addr, &host, &port) < 0) {
	LOG_ADDR_PARSE_ERR(remote_addr, errno);
	goto err_close_both;
    }

    char tls_addr[XCM_ADDR_MAX+1];
    int rc = xcm_addr_make_tls(&host, port, tls_addr, XCM_ADDR_MAX);
    ut_assert(rc == 0);

    char ux_addr[XCM_ADDR_MAX+1];
    map_tls_to_ux(tls_addr, ux_addr, sizeof(ux_addr));

    /* unlike TCP sockets, if the UX socket doesn't exists,
       ECONNREFUSED will be returned immediately, even for
       non-blocking connect */

    if (xcm_tp_socket_connect(us->ux_socket, ux_addr) == 0) {
	xcm_tp_socket_close(us->tls_socket);
	remove_sub_socket(&us->tls_socket);
	return 0;
    }

    if (errno != ECONNREFUSED)
	goto err_close_tls;

    LOG_UTLS_FALLBACK;

    if (xcm_tp_socket_connect(us->tls_socket, tls_addr) < 0)
	goto err;

    remove_sub_socket(&us->ux_socket);
    return 0;

err_close_both:
    xcm_tp_socket_close(us->ux_socket);
err_close_tls:
    xcm_tp_socket_close(us->tls_socket);
err:
    deinit(s);
    return -1;
}

static int bind_sub_server(struct xcm_socket **s, const char* local_addr)
{
    int rc = xcm_tp_socket_server(*s, local_addr);

    if (rc < 0) {
	xcm_tp_socket_destroy(*s);
	*s = NULL;
	return -1;
    }

    return 0;
}

static int utls_server(struct xcm_socket *s, const char *local_addr)
{
    struct utls_socket *us = TOUTLS(s);

    LOG_SERVER_REQ(local_addr);

    struct xcm_addr_host host;
    uint16_t port;
    if (xcm_addr_parse_utls(local_addr, &host, &port) < 0) {
	LOG_ADDR_PARSE_ERR(local_addr, errno);
	goto err_close_both;
    }

    /* XXX: how to handle "wildcard" 0.0.0.0 correctly? So the client
       can connect with 127.0.0.1, or any local IP, but end up on UX socket */

    char tls_addr[XCM_ADDR_MAX+1];
    int rc = xcm_addr_make_tls(&host, port, tls_addr, sizeof(tls_addr));
    ut_assert(rc == 0);

    /* XXX: here's a race condition with performance implications: a
       client may connect to the TLS port before the UX port is
       opened, in which case they will stay with TLS, even though UX
       will exists. The reason for the socket being created in the
       order TLS and then UX is that we want to allow for
       kernel-allocated TCP ports. You could first allocated the port,
       without accepting connections on that socket, but then you
       would need some special hacks, and the not regular TCP
       transport API */

    if (bind_sub_server(&us->tls_socket, tls_addr) < 0)
	goto err_close_ux;

    const char *actual_addr;
    if (port == 0) {
	/* application asked for automatic dynamic TCP port allocation
	   - find out what the port actually is */
	actual_addr = xcm_local_addr(us->tls_socket);
	ut_assert(actual_addr);
	int rc = xcm_addr_parse_tls(actual_addr, &host, &port);
	ut_assert(rc == 0 && port > 0);
	LOG_UTLS_TCP_PORT(port);
    } else
	actual_addr = tls_addr;

    char ux_addr[XCM_ADDR_MAX+1];
    map_tls_to_ux(actual_addr, ux_addr, sizeof(ux_addr));

    if (bind_sub_server(&us->ux_socket, ux_addr) <  0)
	goto err;

    LOG_SERVER_CREATED(s);

    return 0;

err_close_both:
    xcm_tp_socket_close(us->tls_socket);
err_close_ux:
    xcm_tp_socket_close(us->ux_socket);
err:
    deinit(s);
    return -1;
}

static int utls_close(struct xcm_socket *s)
{
    LOG_CLOSING(s);

    int rc = 0;

    if (s) {
	struct utls_socket *us = TOUTLS(s);

	if (xcm_tp_socket_close(us->ux_socket) < 0)
	    rc = -1;
	if (xcm_tp_socket_close(us->tls_socket) < 0)
	    rc = -1;

	deinit(s);
    }
    return rc;
}

static void utls_cleanup(struct xcm_socket *s)
{
    LOG_CLEANING_UP(s);

    if (s)  {
	struct utls_socket *us = TOUTLS(s);

	xcm_tp_socket_cleanup(us->ux_socket);
	xcm_tp_socket_cleanup(us->tls_socket);

	deinit(s);
    }
}

static int utls_accept(struct xcm_socket *conn_s, struct xcm_socket *server_s)
{
    struct utls_socket *server_us = TOUTLS(server_s);
    struct utls_socket *conn_us = TOUTLS(conn_s);

    LOG_ACCEPT_REQ(server_s);

    if (xcm_tp_socket_accept(conn_us->ux_socket, server_us->ux_socket) == 0) {
	xcm_tp_socket_close(conn_us->tls_socket);
	remove_sub_socket(&conn_us->tls_socket);
	return 0;
    }

    if (xcm_tp_socket_accept(conn_us->tls_socket, server_us->tls_socket) == 0) {
	remove_sub_socket(&conn_us->ux_socket);
	return 0;
    }

    deinit(conn_s);

    return -1;
}

static struct xcm_socket *active_sub_conn(struct xcm_socket *s)
{
    struct utls_socket *us = TOUTLS(s);

    return us->ux_socket ? us->ux_socket : us->tls_socket;
}

static int utls_send(struct xcm_socket *s, const void *buf, size_t len)
{
    return xcm_tp_socket_send(active_sub_conn(s), buf, len);
}

static int utls_receive(struct xcm_socket *s, void *buf, size_t capacity)
{
    return xcm_tp_socket_receive(active_sub_conn(s), buf, capacity);
}

static void sync_update(struct xcm_socket *s, struct xcm_socket *sub_socket)
{
    sub_socket->condition = s->condition;
    xcm_tp_socket_update(sub_socket);
}

static void utls_update(struct xcm_socket *s)
{
    LOG_UPDATE_REQ(s, s->epoll_fd);

    if (s->type == xcm_socket_type_conn)
	sync_update(s, active_sub_conn(s));
    else {
	struct utls_socket *us = TOUTLS(s);
	sync_update(s, us->ux_socket);
	sync_update(s, us->tls_socket);
    }
}

static int utls_finish(struct xcm_socket *s)
{
    if (s->type == xcm_socket_type_conn)
	return xcm_tp_socket_finish(active_sub_conn(s));
    else {
	struct utls_socket *us = TOUTLS(s);
	if (xcm_tp_socket_finish(us->ux_socket) < 0)
	    return -1;
	if (xcm_tp_socket_finish(us->tls_socket) < 0)
	    return -1;
	return 0;
    }
}

static const char *utls_get_transport(struct xcm_socket *s)
{
    if (s->type == xcm_socket_type_conn)
	/* masquerade as the underlying transport */
	return xcm_tp_socket_get_transport(active_sub_conn(s));
    else
	return XCM_UTLS_PROTO;
}

static const char *utls_get_remote_addr(struct xcm_socket *s,
					bool suppress_tracing)
{
    return xcm_tp_socket_get_remote_addr(active_sub_conn(s),
					 suppress_tracing);
}

static int utls_set_local_addr(struct xcm_socket *s, const char *local_addr)
{
    struct utls_socket *us = TOUTLS(s);

    if (!us->tls_socket) {
	errno = EACCES;
	return -1;
    }

    struct xcm_addr_host host;
    uint16_t port;
    if (xcm_addr_parse_utls(local_addr, &host, &port) < 0) {
	LOG_ADDR_PARSE_ERR(local_addr, errno);
	errno = EINVAL;
	return -1;
    }

    char tls_local_addr[XCM_ADDR_MAX+1];
    int rc = xcm_addr_make_tls(&host, port, tls_local_addr, XCM_ADDR_MAX);
    ut_assert(rc == 0);

    return xcm_tp_socket_set_local_addr(us->tls_socket, tls_local_addr);
}

static const char *get_conn_local_addr(struct xcm_socket *s,
				       bool suppress_tracing)
{
    struct xcm_socket *active = active_sub_conn(s);

    if (!active)
	return NULL;

    return xcm_tp_socket_get_local_addr(active, suppress_tracing);
}

static const char *get_server_local_addr(struct xcm_socket *s,
					 bool suppress_tracing)
{
    struct utls_socket *us = TOUTLS(s);

    if (us->tls_socket == NULL)
	return NULL;

    const char *tls_addr =
	xcm_tp_socket_get_local_addr(us->tls_socket, suppress_tracing);

    if (!tls_addr)
	return NULL;

    struct xcm_addr_ip ip;
    uint16_t port;

    int rc = xcm_addr_tls6_parse(tls_addr, &ip, &port);
    ut_assert(rc == 0);

    rc = xcm_addr_utls6_make(&ip, port, us->laddr, sizeof(us->laddr));
    ut_assert(rc == 0);

    return us->laddr;
}

static const char *utls_get_local_addr(struct xcm_socket *s,
				       bool suppress_tracing)
{
    switch (s->type) {
    case xcm_socket_type_conn:
	return get_conn_local_addr(s, suppress_tracing);
    case xcm_socket_type_server:
	return get_server_local_addr(s, suppress_tracing);
    default:
	ut_assert(0);
    }
}

static size_t utls_max_msg(struct xcm_socket *conn_s)
{
    return xcm_tp_socket_max_msg(active_sub_conn(conn_s));
}

const struct cnt_conn *utls_get_cnt(struct xcm_socket *conn_s)
{
    return xcm_tp_socket_get_cnt(active_sub_conn(conn_s));
}

static void utls_enable_ctl(struct xcm_socket *s)
{
#ifdef XCM_CTL
    if (s->type == xcm_socket_type_conn) {
	struct xcm_socket *active = active_sub_conn(s);
	active->ctl = ctl_create(active);
    } else {
	struct utls_socket *us = TOUTLS(s);
	/* the reason all three sockets are exposed in the case of
	   the UTLS server socket are mostly historical */
	us->ux_socket->ctl = ctl_create(us->ux_socket);
	us->tls_socket->ctl = ctl_create(us->tls_socket);
	s->ctl = ctl_create(s);
    }
#endif
}

static int set_attr_proxy(struct xcm_socket *s,
			  const struct xcm_tp_attr *attr,
			  const void *value, size_t len)
{
    struct utls_socket *us = TOUTLS(s);

    size_t idx = attr - us->utls_attrs;

    const struct xcm_tp_attr *real_attr = us->real_attrs[idx];
    struct xcm_socket *real_socket = us->real_sockets[idx];

    return real_attr->set_fun(real_socket, real_attr, value, len);
}

static int get_attr_proxy(struct xcm_socket *s,
			  const struct xcm_tp_attr *attr,
			  void *value, size_t capacity)
{
    struct utls_socket *us = TOUTLS(s);

    size_t idx = attr - us->utls_attrs;

    const struct xcm_tp_attr *real_attr = us->real_attrs[idx];
    struct xcm_socket *real_socket = us->real_sockets[idx];

    return real_attr->get_fun(real_socket, real_attr, value, capacity);
}

static void add_attr(struct utls_socket *us,
		     const struct xcm_tp_attr *real_attr,
		     struct xcm_socket *real_socket)
{
    size_t idx = us->attrs_len;

    us->utls_attrs[idx] = *real_attr;
    if (real_attr->get_fun)
	us->utls_attrs[idx].get_fun = get_attr_proxy;
    if (real_attr->set_fun)
	us->utls_attrs[idx].set_fun = set_attr_proxy;

    us->real_attrs[idx] = real_attr;

    us->real_sockets[idx] = real_socket;

    us->attrs_len++;
}

static void update_attrs(struct xcm_socket *s)
{
    struct utls_socket *us = TOUTLS(s);

    const struct xcm_tp_attr *ux_attrs;
    size_t ux_attrs_len = 0;
    if (us->ux_socket)
	xcm_tp_socket_get_attrs(us->ux_socket, &ux_attrs, &ux_attrs_len);

    const struct xcm_tp_attr *tls_attrs;
    size_t tls_attrs_len = 0;
    if (us->tls_socket)
	xcm_tp_socket_get_attrs(us->tls_socket, &tls_attrs, &tls_attrs_len);

    size_t attrs_len = ux_attrs_len + tls_attrs_len;

    if (attrs_len == 0)
	return;

    us->utls_attrs =
	ut_realloc(us->utls_attrs, sizeof(struct xcm_tp_attr) * attrs_len);
    us->real_attrs =
	ut_realloc(us->real_attrs, sizeof(struct xcm_tp_attr *) * attrs_len);
    us->real_sockets =
	ut_realloc(us->real_sockets, sizeof(struct xcm_socket *) * attrs_len);
    us->attrs_len = 0;

    size_t i;
    for (i = 0; i < ux_attrs_len; i++)
	add_attr(us, &ux_attrs[i], us->ux_socket);

    for (i = 0; i < tls_attrs_len; i++)
	add_attr(us, &tls_attrs[i], us->tls_socket);
}

static void utls_get_attrs(struct xcm_socket *s,
			   const struct xcm_tp_attr **attr_list,
			   size_t *attr_list_len)
{
    update_attrs(s);

    struct utls_socket *us = TOUTLS(s);

    *attr_list = us->utls_attrs;
    *attr_list_len = us->attrs_len;
}
