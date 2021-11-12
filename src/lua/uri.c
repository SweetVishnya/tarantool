/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "lua/uri.h"

#include "lua/utils.h"
#include "uri/uri.h"
#include "diag.h"

/**
 * Add URI query param with @a name to all URIs in @a uri_set from  lua table,
 * which located at the top of the lua stack. All not numeric keys are ignored.
 * Allowed types for URI query params are LUA_TSTRING and LUA_TNUMBER.
 * Expected inpuit format: { "param-name" = string }
 */
static int
uri_set_add_param_from_lua_table(struct uri_set *uri_set, const char *name,
				 struct lua_State *L)
{
	int size = lua_objlen(L, -1);
	for (int i = 0; i < size; i++) {
		lua_rawgeti(L, -1, i + 1);
		int type = lua_type(L, -1);
		if (type == LUA_TSTRING || type == LUA_TNUMBER) {
			const char *val = lua_tostring(L, -1);
			uri_set_add_param(uri_set, name, val);
		} else {
			lua_pop(L, 1);
			return -1;
		}
		lua_pop(L, 1);
	}
	return 0;
}

/**
 * Add URI query param with @a name to all URIs in @a uri_set. Value or
 * several values are taken from the string or lua table appropriately.
 * Expected input format: string|table.
 */
static int
uri_set_add_param_from_lua(struct uri_set *uri_set, const char *name,
			   int type, struct lua_State *L)
{
	if (type == LUA_TSTRING || type == LUA_TNUMBER) {
		const char *val = lua_tostring(L, -1);
		uri_set_add_param(uri_set, name, val);
	} else if (type == LUA_TTABLE) {
		if (uri_set_add_param_from_lua_table(uri_set, name, L) != 0)
			return -1;
	} else {
		return -1;
	}
	return 0;
}

/**
 * Add URI query params to all URIs in @a uri_set from lua table, which located
 * at the top of the lua stack. Keys are interpreted as param names. Expected
 * input format: { ["param-name" = string|number] }
 */
static int
uri_set_add_params_from_lua_table(struct uri_set *uri_set, struct lua_State *L)
{
	int rc = 0;
	lua_pushnil(L);
	while (lua_next(L, -2) != 0 && rc == 0) {
		const char *key = lua_tostring(L, -2);
		int type = lua_type(L, -1);
		if (uri_set_add_param_from_lua(uri_set, key, type, L) != 0)
			rc = -1;
		lua_pop(L, 1);
	}
	return rc;
}

/**
 * Add all URI query params to @a uri_set, from lua table which located at
 * the top of the lua stack and contains both URIs and it's params. Ignore
 * all numeric keys, because they correspond to URIs. Expected input format:
 * { ["params" = string|table], ["param-name" = string|table] }
 */
static int
uri_set_add_params_from_lua(struct uri_set *uri_set, struct lua_State *L)
{
	int rc = 0;
	lua_pushnil(L);
	while (lua_next(L, lua_gettop(L) - 1) != 0 && rc == 0) {
		if (lua_type(L, -2) == LUA_TSTRING) {
			const char *key = lua_tostring(L, -2);
			int type = lua_type(L, -1);
			if (strcmp(key, "params") == 0) {
				/*
				 * One or more params, that can be stored in
				 * a string separated by '&' or in a lua table.
				 */
				if (type == LUA_TSTRING || type == LUA_TNUMBER) {
					const char *params = lua_tostring(L, -1);
					uri_set_add_params(uri_set, params);
				} else if (type == LUA_TTABLE) {
					rc = uri_set_add_params_from_lua_table(uri_set, L);
				} else {
					rc = -1;
				}
			} else if (strcmp(key, "uri") != 0) {
				rc = uri_set_add_param_from_lua(uri_set, key,
								type, L);
			}
		} else if (lua_type(L, -2) != LUA_TNUMBER) {
			rc = -1;
		}
		lua_pop(L, 1);
	}
	return rc;
}

/**
 * Add URIs to @a uri_set form lua table, which located at the to of lua
 * stack. All numeric keys in the table refer to URIs, all string keys to
 * URI params common to all URIs in the table. Expected input format:
 * { "uris", ["params" = string|table], ["param-name" = string|table] }
 */
static int
uri_set_add_uris_from_lua_table(struct uri_set *uri_set, struct lua_State *L)
{
	int rc = 0;
	struct uri_set u = { 0 };
	int size = lua_objlen(L, -1);
	for (int i = 0; i < size && rc == 0; i++) {
		lua_rawgeti(L, -1, i + 1);
		int type = lua_type(L, -1);
		if (type == LUA_TSTRING || type == LUA_TNUMBER) {
			const char *str = lua_tostring(L, -1);
			rc = uri_set_add_uris(&u, str);
		} else {
			rc = -1;
		}
		lua_pop(L, 1);
	}
	if (rc != 0 || size == 0)
		return rc;
	rc = uri_set_add_params_from_lua(&u, L);
	if (rc != 0)
		return rc;
	uri_set_merge(uri_set, &u);
	return 0;
}

/**
 * Create @a uri_set from the table, which located at the top of lua stack.
 * Supports many different formats.
 */
static int
uri_set_create_from_lua(struct uri_set *uri_set, struct lua_State *L, int *depth)
{
	/*
	 * Used to save all URIs from currently parsed lua table. We cannot
	 * directly add URIs and their options in the main @a uri_set because
	 * some options, should be added only for URIs which was added from
	 * this table and all nested tables.
	 */
	struct uri_set u = { 0 };
	*depth = *depth + 1;
	int rc = 0;
	/*
	 * First we go through all the values in the table, with numeric keys.
	 * If key is number than value can be string, number or table. If it
	 * is string or number, we interpret it as URI. If this is a table,
	 * then there are two possible options:
	 * 1. if *depth == 1, than this table can contain one or several URIs
	 * and we recursively parse it.
	 * 2. if *depth > 1, it's error, since input like this `{ { { uri } } }`
	 * is banned.
	 */
	int size = lua_objlen(L, -1);
	for (int i = 0; i < size && rc == 0; i++) {
		lua_rawgeti(L, -1, i + 1);
		int type = lua_type(L, -1);
		if (type == LUA_TSTRING || type == LUA_TNUMBER) {
			const char *str = lua_tostring(L, -1);
			rc = uri_set_add_uris(&u, str);
		} else if (type == LUA_TTABLE && *depth == 1) {
			rc = uri_set_create_from_lua(&u, L, depth);
		} else {
			rc = -1;
		}
		lua_pop(L, 1);
	}
	if (rc != 0)
		goto fail;
	/*
	 * Let's add the URIs, that the "uri" key corresponds to in the table.
	 */
	lua_pushstring(L, "uri");
	lua_rawget(L, -2);
	int type = lua_type(L, -1);
	if (type == LUA_TSTRING || type == LUA_TNUMBER) {
		const char *str = lua_tostring(L, -1);
		rc = uri_set_add_uris(&u, str);
	} else if (type == LUA_TTABLE) {
		rc = uri_set_add_uris_from_lua_table(&u, L);
	} else if (type != LUA_TNIL) {
		rc = -1;
	}
	lua_pop(L, 1);
	if (rc != 0)
		goto fail;

	if ((rc = uri_set_add_params_from_lua(&u, L) != 0) != 0)
		goto fail;

	*depth = *depth - 1;
	uri_set_merge(uri_set, &u);
	return rc;
fail:
	*depth = *depth + 1;
	uri_set_destroy(&u);
	return rc;
}


int
luaL_uri_set_create(struct lua_State *L, struct uri_set *uri_set)
{
	int rc = 0;
	memset(uri_set, 0, sizeof(struct uri_set));
	if (lua_isstring(L, -1)) {
		rc = uri_set_create(uri_set, lua_tostring(L, -1));
	} else if (lua_istable(L, -1)) {
		int depth = 0;
		rc = uri_set_create_from_lua(uri_set, L, &depth);
		assert(depth == 0);
	} else if (!lua_isnil(L, -1)) {
	       rc = -1;
	}
	if (rc == -1)
		uri_set_destroy(uri_set);
	return rc;
}

static int
luaT_uri_set_create(lua_State *L)
{
	int rc = 0;
	struct uri_set *uri_set = (struct uri_set *)lua_topointer(L, 1);
	if (uri_set == NULL || luaL_uri_set_create(L, uri_set) != 0)
		rc = -1;
	lua_pushnumber(L, rc);
	return 1;
}

void
tarantool_lua_uri_init(struct lua_State *L)
{
	static const struct luaL_Reg lua_uri_set_methods[] = {
		{"create", luaT_uri_set_create},
		{NULL, NULL}
	};
	luaL_register_module(L, "uri", lua_uri_set_methods);
	lua_pop(L, 1);
};
