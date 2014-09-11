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
#ifndef STACK_H
#define STACK_H

struct icode_list;
struct function;
struct decl;
struct reg;

#include <stddef.h>

struct stack_block {
	int			is_func_arg;
	/*
	 * 07/26/12: use_frame_pointer determines whether this represents an
	 * fp- or sp-relative address. To address local variables we use the
	 * frame pointer, but it may be desirable to address storage sp-
	 * relatively when e.g. passing function arguments with an ABI that
	 * has a dynamic stack frame size
	 * Note that this is highly backend-specific! x86 and until today
	 * AMD64 didn't even implement sp-relative addressing for stack_blocks
	 * at all
	 */
	int			use_frame_pointer;
	long			offset;
	size_t			nbytes;
	void			*from_reg;
	struct stack_block	*next;
	struct stack_block	*prev;
};

struct stack_block *
stack_malloc(struct function *f, size_t bytes);

void
stack_align(struct function *f, size_t bytes);

void
stack_free(struct function *f, struct stack_block *m);

size_t
add_total_allocated(struct function *f, size_t nbytes);

struct stack_block *
make_stack_block(long offset, size_t nbytes);

void
patch_union_members(struct decl *d);

#endif

