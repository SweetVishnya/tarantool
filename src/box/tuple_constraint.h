/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

struct tuple_constraint;

/**
 *
 */
typedef int (*tuple_constraint_f)(const struct tuple_constraint *constraint,
				  const char *mp_data, const char *mp_data_end);

/**
 * Generic constraint of a tuple or a tuple field field.
 */
struct tuple_constraint {
	/** The constraint function itself. */
	tuple_constraint_f func;
	/** User-defined context that can be used by function. */
	void *func_ctx;
	/** Name of the constraint (null-terminated). */
	const char *name;
	/** Name of function to check the constraint (null-terminated). */
	const char *func_name;
	/** Length of name of the constraint. */
	uint32_t name_len;
	/** Length of name of function to check the constraint. */
	uint32_t func_name_len;
};

/**
 * Convenient setter of two members at once.
 */
static inline void
tuple_constraint_set_func(struct tuple_constraint *constraint,
			  tuple_constraint_f func, void *func_ctx)
{
	constraint->func = func;
	constraint->func_ctx = func_ctx;
}

/**
 * Allocate a single memory block with an array of constraints.
 * The memory block starts immediately with tuple_constraint array and then
 * is followed by strings block with strings that each constraint refers to.
 * This memory block must be freed by free() call.
 * Never fail (uses xmalloc) and returns NULL if constraint_count == 0.
 *
 * @param constraints - array of given constraints.
 * @param constraint_count - number of give constraints.
 * @return a single memory block with constraints.
 */
struct tuple_constraint *
tuple_constraint_collocate(const struct tuple_constraint *constraints,
			   size_t constraint_count);