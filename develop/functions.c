/*
 * Copyright (c) 2004 - 2010, Nils R. Weller
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "functions.h"
#include <string.h>
#include "misc.h"
#include "debug.h"
#include "control.h"
#include "zalloc.h"
#include "backend.h"
#include "n_libc.h"

struct function	*funclist;
struct function	*funclist_tail;
struct function	*curfunc;

struct statement *
alloc_statement(void) {
#if USE_ZONE_ALLOCATOR
	return zalloc_buf(Z_STATEMENT);
#else
	struct statement	*ret = n_xmalloc(sizeof *ret);
	static struct statement	nullstmt;
	*ret = nullstmt;
	return ret;
#endif
}

void
append_statement(
	struct statement **head, 
	struct statement **tail, 
	struct statement *s) {

	if (*head == NULL) {
		*head = *tail = s;
	} else {
		(*tail)->next = s;
		*tail = (*tail)->next;
	}
}


struct function *
alloc_function(void) {
	struct function	*ret = n_xmalloc(sizeof *ret);
	static struct function	nullfunc;

	/* XXXXXXXXXXXXXX can we use the zone allocator!? beware of
	 * inline functions*/
	*ret = nullfunc;

	/*
	 * 02/05/08: Set PIC initialization status, e.g. on
	 * x86 we have to load ebx with the GOT address
	 * before accessing static variables
	 */
	if (!backend->need_pic_init) {
		/* Nothing to do for this arch */
		ret->pic_initialized = 1;
	} else {
		ret->pic_initialized = 0;
	}

	return ret;
}

struct label *
lookup_label(struct function *f, const char *name) {
	struct label	*l;


	for (l = f->labels_head; l != NULL; l = l->next) {
		if (l->name == NULL) {
			/*
			 * 10/15/09: Must be (unnamed) switch label
			 */
			continue;
		}
		if (strcmp(l->name, name) == 0) {
			return l;
		}
	}
	return NULL;
}

