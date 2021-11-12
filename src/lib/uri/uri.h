/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdbool.h>
#include <netdb.h> /* NI_MAXHOST, NI_MAXSERV */
#include <limits.h> /* _POSIX_PATH_MAX */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct uri_query_param {
	/** Name of URI query parameter. */
	char *name;
	/** Count of values for this parameter. */
	int value_count;
	/** Array of values for this parameter. */
	char **values;
};

struct uri {
	char *scheme;
	char *login;
	char *password;
	char *host;
	char *service;
	char *path;
	char *query;
	char *fragment;
	int host_hint;
	/** Count of URI query parameters */
	int param_count;
	/** Different URI query parameters */
	struct uri_query_param *params;
};

struct uri_set {
	/** Count of URIs */
	int uri_count;
	/** Array of URIs */
	struct uri *uris;
};

#define URI_HOST_UNIX "unix/"
#define URI_MAXHOST NI_MAXHOST
#define URI_MAXSERVICE _POSIX_PATH_MAX /* _POSIX_PATH_MAX always > NI_MAXSERV */


/**
 * Creates new @a uri structure according to passed @a str.
 * If @a str parsing failed function return -1, and fill
 * @a uri structure with zeros, otherwise return 0 and save
 * URI components in appropriate fields of @a uri. @a uri
 * can be safely destroyed in case this function fails.
 * Expected format of @a src string: "uri?query", where
 * query contains parameters separated by '&'. This function
 * doesn't set diag.
 */
int
uri_create(struct uri *uri, const char *str);

/**
 * Destroy previosly created @a uri. Should be called
 * after each `uri_create` function call. Safe to call
 * if uri_create failed.
 */
void
uri_destroy(struct uri *uri);

/**
 * Work same as `uri_create` function but could parse
 * string which contains several URIs separated by
 * commas. Create @a uri_set from appropriate @a str.
 */
int
uri_set_create(struct uri_set *uri_set, const char *str);

/**
 * Destroy previosly created @a uri_set. Should be called
 * after each `uri_set_create` function call.
 */
void
uri_set_destroy(struct uri_set *uri_set);

/**
 * Add one or several URIs to @a uri_set structure. New URIs are
 * created based on @a str, which contains one or several URIs
 * separated by commas. Return 0 if success, otherwise return -1
 * and leaves @a uri_set structure in the same state it was in
 * before calling this function.
 */
int
uri_set_add_uris(struct uri_set *uri_set, const char *str);

int
uri_format(char *str, int len, const struct uri *uri, bool write_password);

/**
 * Return @a uri query parameter value by given @a idx. If parameter with
 * @a name does not exist or @a idx is greater than or equal to URI parameter
 * value count, return NULL.
 */
const char *
uri_query_param(const struct uri *uri, const char *name, int idx);

/**
 * Return count of values for @a uri query parameter with given @a name.
 * If parameter with such @a name does not exist return 0.
 */
int
uri_query_param_count(const struct uri *uri, const char *name);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
