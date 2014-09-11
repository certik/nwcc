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
#ifndef FUNCTIONS_H
#define FUNCTIONS_H

struct decl;
struct function;
struct icode;
struct vreg;


extern struct function	*curfunc;

struct statement {
	int			type;
	int			debug;
#define ST_DECL		1 /* struct decl */
#define ST_CODE		2 /* struct icode */
#define ST_COMP		3 /* struct scope */
#define ST_CTRL		4 /* struct control */
#define ST_LABEL	5 /* struct label */
#define ST_ASM		6 /* struct inline_asm_stmt */
#define ST_EXPRSTMT	7 /* same as ST_COMP */

	void			*data;
	struct statement	*next;
};

#include <stddef.h>

struct stack_block;

struct function {
	struct decl		*proto;
	struct type		*rettype;
	struct ty_func	*fty; /* XXX remove proto?!?! */
	struct statement	*code;
	struct statement	*code_tail;
	struct scope		*scope;
	struct icode_list	*icode;
	struct function		*next;
	struct label		*labels_head; /* function scope */
	struct label		*labels_tail;
	size_t			total_allocated;
	size_t			callee_save_offset;
	int			callee_save_used;
	int			gotframe; /* MIPS */
	int			max_bytes_pushed; /* PowerPC */

	/*
	 * 03/26/08: Checkpoint of last static initialized variable
	 * that was present before the function definition was
	 * encountered. This gives us the info we need to output all
	 * initialized static variables belonging to this function
	 */
	struct decl		*static_init_vars_checkpoint;

	/*
	 * 02/05/08: New variable that tells us whether PIC support
	 * has already been set up (e.g. loading ebx on x86)
	 */
	int			pic_initialized;
	/*
	 * 02/15/08: New variable to hold vreg for PIC register IF
	 * that must be saved across function calls (e.g. on SPARC...
	 * and it's cheaper to save instead of recomputing it
	 */
	struct vreg		*pic_vreg;
	/*
	 * 02/05/09: Name of PIC label to get program counter.
	 * Currently only used for OSX
	 */
	char			*pic_label;
	struct vreg		*hidden_pointer; /* MIPS */
	void			*patchme; /* AMD64 :-( */

	struct stack_block	*alloca_head;
	struct stack_block	*alloca_tail;
	struct stack_block	*alloca_regs;
	
	struct stack_block	*vla_head;
	struct stack_block	*vla_tail;
	struct stack_block	*vla_regs;

	struct stack_block	*regs_head;
	struct stack_block	*regs_tail;
	struct stack_block	*free_list;
};

extern struct function	*funclist;
extern struct function	*funclist_tail;

void				append_statement(
						struct statement **, 
						struct statement **,
						struct statement *);
struct statement	*alloc_statement(void);
struct function		*alloc_function(void);
struct label		*lookup_label(struct function *f, const char *name);

#endif

