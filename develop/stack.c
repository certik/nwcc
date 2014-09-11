/*
 * Copyright (c) 2005 - 2010, Nils R. Weller
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
#include "stack.h"
#include "backend.h"
#include "limits.h"
#include "icode.h"
#include "decl.h"
#include "type.h"
#include "scope.h"
#include "zalloc.h"
#include "symlist.h"
#include "functions.h"
#include "n_libc.h"

static struct stack_block *
alloc_stack_block(void) {
/*	return debug_malloc_pages(sizeof(struct stack_block));*/
#if USE_ZONE_ALLOCATOR
	struct stack_block	*ret;
	ret = zalloc_buf(Z_STACK_BLOCK);
	ret->use_frame_pointer = 1; /* usually the case */
	return ret;
#else
	struct stack_block	*ret = n_xmalloc(sizeof *ret);
	static struct stack_block	nullsm;
	*ret = nullsm;
	ret->use_frame_pointer = 1; /* usually the case */
	return ret;
#endif
}

static struct stack_block *
alloc_from_free_list(struct function *f, size_t nbytes) {
	struct stack_block	*sp;
	size_t				least_bytes = (size_t)-1;
	struct stack_block	*bptr = NULL;

	for (sp = f->free_list; sp != NULL; sp = sp->next) {
		if (sp->nbytes >= nbytes) {
			if (sp->nbytes < least_bytes) {
				least_bytes = sp->nbytes;
				bptr = sp;
			}
		}
	}
	if (bptr != NULL) {
		if (bptr == f->free_list) {
			f->free_list = f->free_list->next;
		}
		if (bptr->prev) bptr->prev->next = bptr->next;
		if (bptr->next) bptr->next->prev = bptr->prev;
	}
	return bptr;
}


/*
 * Never returns ``NULL'' ;-)
 */
static struct stack_block *
alloc_bytes(struct function *f, size_t nbytes) {
	struct stack_block	*ret = alloc_stack_block();

	f->total_allocated += nbytes;
	ret->nbytes = nbytes; 
	ret->offset = f->total_allocated;
	return ret;
}

size_t
add_total_allocated(struct function *f, size_t nbytes) {
	return (f->total_allocated += nbytes); 
}

struct stack_block *
stack_malloc(struct function *f, size_t bytes) {
	struct stack_block	*ret;

	if ((ret = alloc_from_free_list(f, bytes)) == NULL) {
		ret = alloc_bytes(f, bytes);
	}
	return ret;
}

void
stack_align(struct function *f, size_t bytes) {
	while (f->total_allocated % bytes) {
		++f->total_allocated;
	}	
}


struct stack_block *
make_stack_block(long offset, size_t nbytes) {
	struct stack_block	*ret = alloc_stack_block();

	ret->nbytes = nbytes;
	ret->offset = offset;
	return ret;
}

void
stack_free(struct function *f, struct stack_block *m) {
	m->next = f->free_list;
	if (f->free_list) f->free_list->prev = m;
	m->prev = NULL;
	f->free_list = m;
}

