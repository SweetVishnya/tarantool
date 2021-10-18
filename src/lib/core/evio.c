/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "evio.h"
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <trivia/util.h>
#include "exception.h"

struct evio_service_impl
{
	/** Service name. E.g. 'primary', 'secondary', etc. */
	char name[SERVICE_NAME_MAXLEN];
	/** Bind host:service, useful for logging */
	char host[URI_MAXHOST];
	char serv[URI_MAXSERVICE];

	/** Interface/port to bind to */
	union {
		struct sockaddr addr;
		struct sockaddr_storage addrstorage;
	};
	socklen_t addr_len;

	/**
	 * A callback invoked on every accepted client socket.
	 * If a callback returned != 0, the accepted socket is
	 * closed and the error is logged.
	 */
	evio_accept_f on_accept;
	void *on_accept_param;

	/** libev io object for the acceptor socket. */
	struct ev_io ev;
	ev_loop *loop;
};

static inline bool
evio_service_impl_is_active(struct evio_service_impl *service)
{
	return service->ev.fd >= 0;
}

static inline int
evio_service_impl_bound_address(const struct evio_service_impl *service, char *buf)
{
	const struct sockaddr *sockaddr = (struct sockaddr *)
		&service->addrstorage;
	return sio_addr_snprintf(buf, SERVICE_NAME_MAXLEN, sockaddr,
				 service->addr_len);
}

void *
evio_service_impl_accept_param(struct evio_service_impl *evio_service_impl)
{
	return evio_service_impl->on_accept_param;
}

const char *
evio_service_impl_name(struct evio_service_impl *evio_service_impl)
{
	return evio_service_impl->name;
}

/** Note: this function does not throw. */
void
evio_close(ev_loop *loop, struct ev_io *evio)
{
	/* Stop I/O events. Safe to do even if not started. */
	ev_io_stop(loop, evio);
	/* Close the socket. */
	close(evio->fd);
	/* Make sure evio_has_fd() returns a proper value. */
	evio->fd = -1;
}

/**
 * Create an endpoint for communication.
 * Set socket as non-block and apply protocol specific options.
 */
int
evio_socket(struct ev_io *coio, int domain, int type, int protocol)
{
	assert(coio->fd == -1);
	/* Don't leak fd if setsockopt fails. */
	coio->fd = sio_socket(domain, type, protocol);
	if (coio->fd < 0)
		return -1;
	return evio_setsockopt_client(coio->fd, domain, type);
}

static int
evio_setsockopt_keepalive(int fd)
{
	int on = 1;
	/*
	 * SO_KEEPALIVE to ensure connections don't hang
	 * around for too long when a link goes away.
	 */
	if (sio_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
		       &on, sizeof(on)))
		return -1;
#ifdef __linux__
	/*
	 * On Linux, we are able to fine-tune keepalive
	 * intervals. Set smaller defaults, since the system-wide
	 * defaults are in days.
	 */
	int keepcnt = 5;
	if (sio_setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt,
		       sizeof(int)))
		return -1;
	int keepidle = 30;

	if (sio_setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle,
		       sizeof(int)))
		return -1;

	int keepintvl = 60;
	if (sio_setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl,
		       sizeof(int)))
		return -1;
#endif
	return 0;
}

/** Set common client socket options. */
int
evio_setsockopt_client(int fd, int family, int type)
{
	int on = 1;
	/* In case this throws, the socket is not leaked. */
	if (sio_setfl(fd, O_NONBLOCK, on))
		return -1;
	if (type == SOCK_STREAM && family != AF_UNIX) {
		/*
		 * SO_KEEPALIVE to ensure connections don't hang
		 * around for too long when a link goes away.
		 */
		if (evio_setsockopt_keepalive(fd) != 0)
			return -1;
		/*
		 * Lower latency is more important than higher
		 * bandwidth, and we usually write entire
		 * request/response in a single syscall.
		 */
		if (sio_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
				   &on, sizeof(on)))
			return -1;
	}
	return 0;
}

int
evio_setsockopt_server(int fd, int family, int type)
{
	int on = 1;
	/* In case this throws, the socket is not leaked. */
	if (sio_setfl(fd, O_NONBLOCK, on))
		return -1;
	/* Allow reuse local adresses. */
	if (sio_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
		       &on, sizeof(on)))
		return -1;

#ifndef TARANTOOL_WSL1_WORKAROUND_ENABLED
	/* Send all buffered messages on socket before take
	 * control out from close(2) or shutdown(2). */
	struct linger linger = { 0, 0 };

	if (sio_setsockopt(fd, SOL_SOCKET, SO_LINGER,
		       &linger, sizeof(linger)))
		return -1;
#endif
	if (type == SOCK_STREAM && family != AF_UNIX &&
	    evio_setsockopt_keepalive(fd) != 0)
		return -1;
	return 0;
}

/**
 * A callback invoked by libev when acceptor socket is ready.
 * Accept the socket, initialize it and pass to the on_accept
 * callback.
 */
static void
evio_service_impl_accept_cb(ev_loop *loop, ev_io *watcher, int events)
{
	(void) loop;
	(void) events;
	struct evio_service_impl *service =
		(struct evio_service_impl *) watcher->data;
	int fd;
	while (1) {
		/*
		 * Accept all pending connections from backlog during event
		 * loop iteration. Significally speed up acceptor with enabled
		 * io_collect_interval.
		 */
		struct sockaddr_storage addr;
		socklen_t addrlen = sizeof(addr);
		fd = sio_accept(service->ev.fd, (struct sockaddr *)&addr,
				&addrlen);

		if (fd < 0) {
			if (! sio_wouldblock(errno))
				break;
			return;
		}
		if (evio_setsockopt_client(fd, service->addr.sa_family,
					   SOCK_STREAM) != 0)
			break;
		if (service->on_accept(service, fd, (struct sockaddr *)&addr,
				       addrlen) != 0)
			break;
	}
	if (fd >= 0)
		close(fd);
	diag_log();
}

/*
 * Check if the UNIX socket exists and no one is
 * listening on it. Unlink the file if it's the case.
 */
static int
evio_service_impl_reuse_addr(const char *uri)
{
	struct uri u;
	if (uri_parse(&u, uri) || u.service == NULL) {
		diag_set(IllegalParams, "invalid uri for bind: %s", uri);
		return -1;
	}
	if (strncmp(u.host, URI_HOST_UNIX, u.host_len) != 0)
		return 0;

	struct sockaddr_un un;
	snprintf(un.sun_path, sizeof(un.sun_path), "%.*s",
		 (int) u.service_len, u.service);
	un.sun_family = AF_UNIX;

	int cl_fd = sio_socket(un.sun_family, SOCK_STREAM, 0);
	if (cl_fd < 0)
		return -1;

	if (connect(cl_fd, (struct sockaddr *)&un, sizeof(un)) == 0)
		goto err;

	if (errno == ECONNREFUSED  && unlink(un.sun_path) != 0)
		goto err;

	close(cl_fd);
	return 0;
err:
	errno = EADDRINUSE;
	diag_set(SocketError, sio_socketname(cl_fd), "unlink");
	close(cl_fd);
	return -1;
}

/**
 * Try to bind on the configured port.
 *
 * Throws an exception if error.
 */
static int
evio_service_impl_bind_addr(struct evio_service_impl *service)
{
	say_debug("%s: binding to %s...", evio_service_impl_name(service),
		  sio_strfaddr(&service->addr, service->addr_len));
	/* Create a socket. */
	int fd = sio_socket(service->addr.sa_family,
			    SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0)
		return -1;

	if (evio_setsockopt_server(fd, service->addr.sa_family,
				   SOCK_STREAM) != 0)
		goto error;

	if (sio_bind(fd, &service->addr, service->addr_len))
		goto error;

	/*
	 * After binding a result address may be different. For
	 * example, if a port was 0.
	 */
	if (sio_getsockname(fd, &service->addr, &service->addr_len) != 0)
		goto error;

	say_info("%s: bound to %s", evio_service_impl_name(service),
		 sio_strfaddr(&service->addr, service->addr_len));

	/* Register the socket in the event loop. */
	ev_io_set(&service->ev, fd, EV_READ);
	return 0;
error:
	close(fd);
	return -1;
}

/**
 * Listen on bounded port.
 *
 * @retval 0 for success
 */
static int
evio_service_impl_listen(struct evio_service_impl *service)
{
	say_debug("%s: listening on %s...", evio_service_impl_name(service),
		  sio_strfaddr(&service->addr, service->addr_len));

	int fd = service->ev.fd;
	if (sio_listen(fd))
		return -1;
	ev_io_start(service->loop, &service->ev);
	return 0;
}

static void
evio_service_impl_init(ev_loop *loop, struct evio_service_impl *service,
		       const char *name, evio_accept_f on_accept,
		       void *on_accept_param)
{
	memset(service, 0, sizeof(struct evio_service_impl));
	snprintf(service->name, sizeof(service->name), "%s", name);

	service->loop = loop;

	service->on_accept = on_accept;
	service->on_accept_param = on_accept_param;
	/*
	 * Initialize libev objects to be able to detect if they
	 * are active or not in evio_service_impl_stop().
	 */
	ev_init(&service->ev, evio_service_impl_accept_cb);
	ev_io_set(&service->ev, -1, 0);
	service->ev.data = service;
}

/**
 * Try to bind.
 */
static int
evio_service_impl_bind(struct evio_service_impl *service, const char *uri)
{
	struct uri u;
	if (uri_parse(&u, uri) || u.service == NULL) {
		diag_set(IllegalParams, "invalid uri for bind: %s", uri);
		return -1;
	}

	snprintf(service->serv, sizeof(service->serv), "%.*s",
		 (int) u.service_len, u.service);
	if (u.host != NULL && strncmp(u.host, "*", u.host_len) != 0) {
		snprintf(service->host, sizeof(service->host), "%.*s",
			(int) u.host_len, u.host);
	} /* else { service->host[0] = '\0'; } */

	assert(! ev_is_active(&service->ev));

	if (strcmp(service->host, URI_HOST_UNIX) == 0) {
		/* UNIX domain socket */
		struct sockaddr_un *un = (struct sockaddr_un *) &service->addr;
		service->addr_len = sizeof(*un);
		snprintf(un->sun_path, sizeof(un->sun_path), "%s",
			 service->serv);
		un->sun_family = AF_UNIX;
		return evio_service_impl_bind_addr(service);
	}

	/* IP socket */
	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE|AI_ADDRCONFIG;

	/* make no difference between empty string and NULL for host */
	if (getaddrinfo(*service->host ? service->host : NULL, service->serv,
			&hints, &res) != 0 || res == NULL) {
		diag_set(SocketError, sio_socketname(-1),
			 "can't resolve uri for bind");
		return -1;
	}
	for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
		memcpy(&service->addr, ai->ai_addr, ai->ai_addrlen);
		service->addr_len = ai->ai_addrlen;
		if (evio_service_impl_bind_addr(service) == 0) {
			freeaddrinfo(res);
			return 0;
		}
		say_error("%s: failed to bind on %s: %s",
			  evio_service_impl_name(service),
			  sio_strfaddr(ai->ai_addr, ai->ai_addrlen),
			  diag_last_error(diag_get())->errmsg);
	}
	freeaddrinfo(res);
	diag_set(SocketError, sio_socketname(-1), "%s: failed to bind",
		 evio_service_impl_name(service));
	return -1;
}

static void
evio_service_impl_detach(struct evio_service_impl *service)
{
	if (ev_is_active(&service->ev)) {
		ev_io_stop(service->loop, &service->ev);
		service->addr_len = 0;
	}
	ev_io_set(&service->ev, -1, 0);
}

/** It's safe to stop a service which is not started yet. */
static void
evio_service_impl_stop(struct evio_service_impl *service)
{
	say_info("%s: stopped", evio_service_impl_name(service));

	int service_fd = service->ev.fd;
	evio_service_impl_detach(service);
	if (service_fd < 0)
		return;

	if (close(service_fd) < 0)
		say_error("Failed to close socket: %s", strerror(errno));

	if (service->addr.sa_family != AF_UNIX)
		return;

	if (unlink(((struct sockaddr_un *)&service->addr)->sun_path) < 0) {
		say_error("Failed to unlink unix "
			  "socket path: %s", strerror(errno));
	}
}

static void
evio_service_impl_attach(struct evio_service_impl *dst,
			 const struct evio_service_impl *src)
{
	strcpy(dst->host, src->host);
	strcpy(dst->serv, src->serv);
	dst->addrstorage = src->addrstorage;
	dst->addr_len = src->addr_len;
	ev_io_set(&dst->ev, src->ev.fd, EV_READ);
}

static inline int
evio_service_reuse_addr(const char **uris, int size)
{
	for (int i = 0; i < size; i++)
		if (evio_service_impl_reuse_addr(uris[i]) != 0)
			return -1;
	return 0;
}

char **
evio_service_bound_address(const struct evio_service *service, int *size)
{
	*size = service->size;
	if (service->size == 0)
		return NULL;
	char **bound_address_array = (char **)
		xcalloc(service->size, sizeof(char *) + SERVICE_NAME_MAXLEN);
	char *dstbuf = (char *)bound_address_array +
		service->size * sizeof(char *);
	for (int i = 0; i < service->size; i++) {
		bound_address_array[i] =
			dstbuf + i * SERVICE_NAME_MAXLEN;
		evio_service_impl_bound_address(&service->services[i],
						bound_address_array[i]);
	}
	return bound_address_array;
}

void
evio_service_bound_address_free(char **bound_address_array)
{
	free(bound_address_array);
}

void
evio_service_init(struct ev_loop *loop, struct evio_service *service, int size,
		  const char *name, evio_accept_f on_accept,
		  void *on_accept_param)
{
	service->services =
		(size != 0 ? xcalloc(size, sizeof(struct evio_service_impl)) : NULL);
	service->size = size;
	for (int i = 0; i < service->size; i++) {
		evio_service_impl_init(loop, &service->services[i], name,
				       on_accept, on_accept_param);
	}
}

void
evio_service_attach(struct evio_service *dst, const struct evio_service *src)
{
	for (int i = 0; i < src->size; i++)
		evio_service_impl_attach(&dst->services[i], &src->services[i]);
	dst->size = src->size;
}

void
evio_service_detach(struct evio_service *service)
{
	for (int i = 0; i < service->size; i++)
		evio_service_impl_detach(&service->services[i]);
	free(service->services);
	service->size = 0;
	service->services = NULL;
}

bool
evio_service_is_active(struct evio_service *service)
{
	for (int i = 0; i < service->size; i++)
		if (evio_service_impl_is_active(&service->services[i]))
			return true;
	return false;
}

int
evio_service_listen(struct evio_service *service)
{
	for (int i = 0; i < service->size; i++)
		if (evio_service_impl_listen(&service->services[i]) != 0)
			return -1;
	return 0;
}

void
evio_service_stop(struct evio_service *service)
{
	for (int i = 0; i < service->size; i++)
		evio_service_impl_stop(&service->services[i]);
	free(service->services);
	service->size = 0;
	service->services = NULL;
}

int
evio_service_bind(struct evio_service *service, const char **uris)
{
	if (evio_service_reuse_addr(uris, service->size) != 0)
		return -1;
	for (int i = 0; i < service->size; i++) {
		if (evio_service_impl_bind(&service->services[i], uris[i]) != 0)
			return -1;
	}
	return 0;
}
