/* reference counting, for libreswan
 *
 * Copyright (C) 2015-2019 Andrew Cagney <cagney@gnu.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <https://www.gnu.org/licenses/gpl2.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "refcnt.h"

#define DEBUG_LOG(WHAT)					\
	dbg("%s %s@%p(%u) "PRI_WHERE"",			\
	    WHAT, what, pointer, refcnt->count,		\
	    pri_where(where))

void refcnt_init(const char *what, const void *pointer,
		 refcnt_t *refcnt, where_t where)
{
	/* on main thread */
	if (refcnt->count != 0) {
		log_pexpect(where, "refcnt should be 0");
	}
	refcnt->count = 1;
	DEBUG_LOG("newref");
}

void refcnt_add(const char *what, const void *pointer,
		refcnt_t *refcnt, where_t where)
{
	/* on main thread */
	if (refcnt->count == 0) {
		log_pexpect(where, "refcnt should be non-0");
	}
	refcnt->count++;
	DEBUG_LOG("addref");
}

bool refcnt_delete(const char *what, const void *pointer,
		   refcnt_t *refcnt, where_t where)
{
	/* on main thread */
	if (refcnt->count == 0) {
		log_pexpect(where, "refcnt should be non-0");
	} else {
		refcnt->count--;}
	DEBUG_LOG("delref");
	return refcnt->count == 0;
}
