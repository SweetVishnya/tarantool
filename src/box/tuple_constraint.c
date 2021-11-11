/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "tuple_constraint.h"

#include "trivia/util.h"

struct tuple_constraint *
tuple_constraint_collocate(const struct tuple_constraint *constraints,
			   size_t count)
{
	if (count == 0)
		return NULL;

	/*
	 * Allocate the space, calculate total size for that by summarizing
	 * array size and all string sizes (including null termination).
	 */
	size_t total_size = sizeof(constraints[0]) * count;
	for (size_t i = 0; i < count; i++) {
		const struct tuple_constraint *arg_c = &constraints[i];
		total_size += arg_c->name_len + arg_c->func_name_len + 2;
	}
	struct tuple_constraint *res = xmalloc(total_size);

	/* Pointer to string space is right after the array. */
	char *str_storage = (char *)(res + count);

	/* Now fill the new array. */
	for (size_t i = 0; i < count; i++) {
		const struct tuple_constraint *arg_c = &constraints[i];
		struct tuple_constraint *res_c = &res[i];

		res_c->func = arg_c->func;
		res_c->func_ctx = arg_c->func_ctx;

		memcpy(str_storage, arg_c->name, arg_c->name_len);
		str_storage[arg_c->name_len] = 0;
		res_c->name = str_storage;
		res_c->name_len = arg_c->name_len;
		str_storage += arg_c->name_len + 1;

		memcpy(str_storage, arg_c->func_name, arg_c->func_name_len);
		str_storage[arg_c->func_name_len] = 0;
		res_c->func_name = str_storage;
		res_c->func_name_len = arg_c->func_name_len;
		str_storage += arg_c->func_name_len + 1;
	}

	/*
	 * If we did everything correctly then the pointer points to the end
	 * of allocated region.
	 */
	assert(str_storage == (char *)res + total_size);
	return res;
}