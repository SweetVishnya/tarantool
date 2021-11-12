/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include <sys/uio.h>
#include <fcntl.h>
#include <sys/errno.h>

#include "test_iostream.h"
#include "trivia/util.h"

static ssize_t
test_stream_read(struct iostream *io, void *buf, size_t count)
{
	struct test_stream_ctx *ctx = io->ctx;
	io->fd = ctx->rfd;
	(void)buf;
	ssize_t nrd = 0;
	if (ctx->avail_rd == 0)
		return IOSTREAM_WANT_READ;
	nrd = MIN(count, ctx->avail_rd);
	ctx->avail_rd -= nrd;
	return nrd;
}

static ssize_t
test_stream_write(struct iostream *io, const void *buf, size_t count)
{
	assert(io->fd >= 0);
	struct test_stream_ctx *ctx = io->ctx;
	io->fd = ctx->wfd;
	(void)buf;
	ssize_t nwr = 0;
	if (ctx->avail_wr == 0)
		return IOSTREAM_WANT_WRITE;
	nwr = MIN(count, ctx->avail_wr);
	ctx->avail_wr -= nwr;
	return nwr;
}

static ssize_t
test_stream_writev(struct iostream *io, const struct iovec *iov, int iovcnt)
{
	assert(io->fd >= 0);
	struct test_stream_ctx *ctx = io->ctx;
	io->fd = ctx->wfd;
	if (ctx->avail_wr == 0)
		return IOSTREAM_WANT_WRITE;
	ssize_t start_wr = ctx->avail_wr;
	for (int i = 0; i < iovcnt && ctx->avail_wr > 0; i++) {
		size_t nwr = MIN(iov[i].iov_len, ctx->avail_wr);
		ctx->avail_wr -= nwr;
	}
	return start_wr - ctx->avail_wr;
}

static void
test_stream_delete_ctx(void *ptr)
{
	struct test_stream_ctx *ctx = ptr;
	close(ctx->rfds[0]);
	close(ctx->rfds[1]);
	close(ctx->wfds[0]);
	close(ctx->wfds[1]);
}

static const struct iostream_vtab test_stream_vtab = {
	test_stream_delete_ctx,
	test_stream_read,
	test_stream_write,
	test_stream_writev,
};

static void
fill_pipe(int fd)
{
	char buf[4096];
	int rc = 0;
	rc = fcntl(fd, F_SETFL, O_NONBLOCK);
	assert(rc >= 0);
	while (rc >= 0 || errno == EINTR)
		rc = write(fd, buf, sizeof(buf));
	assert(errno == EAGAIN || errno == EWOULDBLOCK);
}

static void
test_stream_ctx_create(struct test_stream_ctx *ctx, size_t maxrd, size_t maxwr)
{
	ctx->avail_rd = maxrd;
	ctx->avail_wr = maxwr;
	int rc = pipe(ctx->rfds);
	assert(rc == 0);
	rc = pipe(ctx->wfds);
	assert(rc == 0);
	/* rfd is never readable, since we didn't write anything to the pipe. */
	ctx->rfd = ctx->rfds[0];
	ctx->wfd = ctx->wfds[1];
	/* Make wfd never writeable by filling the pipe. */
	fill_pipe(ctx->wfd);
}

void
test_stream_create(struct test_stream *s, size_t maxrd, size_t maxwr)
{
	test_stream_ctx_create(&s->ctx, maxrd, maxwr);
	s->io.ctx = &s->ctx;
	s->io.vtab = &test_stream_vtab;
	s->io.fd = -1; /* Initialized on demand. */
}

void
test_stream_reset(struct test_stream *s, size_t maxrd, size_t maxwr)
{
	struct test_stream_ctx *ctx = s->io.ctx;
	ctx->avail_rd = maxrd;
	ctx->avail_wr = maxwr;
}

void
test_stream_destroy(struct test_stream *s)
{
	iostream_destroy(&s->io);
}
