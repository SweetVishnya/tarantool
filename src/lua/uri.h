/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct lua_State;
struct uri_set;

/**
* Initialize box.uri system
*/
void
tarantool_lua_uri_init(struct lua_State *L);

int
luaL_uri_set_create(struct lua_State *L, struct uri_set *v);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
