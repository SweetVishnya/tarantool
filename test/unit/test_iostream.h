/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include "core/iostream.h"
/**
 * A testing stream implementation. Doesn't write anything anywhere, but is
 * useful for testing coio behaviour when waiting for stream to become readable
 * or writeable.
 */

#if defined (__cplusplus)
extern "C" {
#endif

struct test_stream_ctx {
	/** How many bytes can be written before the stream would block. */
	size_t avail_wr;
	/** How many bytes can be read before the stream would block. */
	size_t avail_rd;
	/**
	 * A file descriptor which is never ready for reads. Points to rfds[0],
	 * the reading end of a pipe which is never written to.
	 */
	int rfd;
	/** A pipe used to simulate a fd never ready for reads. */
	int rfds[2];
	/**
	 * A file descriptor which is never ready for writes. Points to
	 * wfds[1], the writing end of a filled up pipe.
	 */
	int wfd;
	/** A pipe to simulate a fd never ready for writes. */
	int wfds[2];
};

struct test_stream {
	struct iostream io;
	struct test_stream_ctx ctx;
};

/**
 * Create an instance of a testing iostream.
 * The stream will allow reading up to \a maxrd and writing up to \a maxwr
 * bytes before blocking.
 */
void
test_stream_create(struct test_stream *s, size_t maxrd, size_t maxwr);

/**
 * Reset the stream. Allow additional \a maxrd and \a maxwr bytes of input /
 * output respectively.
 */
void
test_stream_reset(struct test_stream *s, size_t maxrd, size_t maxwr);

/**
 * Destroy the stream.
 */
void
test_stream_destroy(struct test_stream *s);

#if defined (__cplusplus)
} /* extern "C" */
#endif
