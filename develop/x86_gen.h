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
#ifndef X86_GEN_H
#define X86_GEN_H

struct backend;
struct vreg;
struct reg;
struct icode_list;
struct function;

struct init_with_name;

#if ! REMOVE_FLOATBUF
extern struct vreg	floatbuf;
#endif

extern struct vreg	x87cw_old;
extern struct vreg	x87cw_new;
extern int		allocated_sse_fpr;


void	print_asmitem_x86(FILE *, void *, int, int, int);
struct reg	*get_smaller_reg(struct reg *, size_t);
int	x86_have_immediate_op(struct type *, int op);

struct reg *
alloc_sse_fpr(struct function *f, int size, struct icode_list *il,
struct reg *dontwipe);


#define CSAVE_EBX	1
#define CSAVE_ESI	(1 << 1)
#define CSAVE_EDI	(1 << 2)

#if 0
#define STUPID_X87(reg) \
	(0 && (backend->arch == ARCH_X86 && reg->type == REG_FPR) \
		|| (backend->arch == ARCH_AMD64 \
			&& reg->vreg && reg->vreg->type \
			&& reg->vreg->type->code == TY_LDOUBLE))
#endif

#include "reg.h"

struct filddata;
struct fistdata;

typedef void	(*fxch_func_t)(struct reg *, struct reg *);
typedef void	(*ffree_func_t)(struct reg *);
typedef void	(*fnstcw_func_t)(struct vreg *vr);
typedef void	(*fldcw_func_t)(struct vreg *vr);
typedef void	(*cdq_func_t)(void);
typedef void	(*fild_func_t)(struct filddata *);
typedef void	(*fist_func_t)(struct fistdata *);
typedef void	(*ulong_to_float_func_t)(struct icode_instr *);

struct emitter_x86 {
	fxch_func_t	fxch;
	ffree_func_t	ffree;
	fnstcw_func_t	fnstcw;
	fldcw_func_t	fldcw;
	cdq_func_t	cdq;
	fist_func_t	fist;
	fild_func_t	fild;
	ulong_to_float_func_t	ulong_to_float;
};	

extern struct backend		x86_backend;
extern struct reg		x86_gprs[7];
extern struct reg		x86_fprs[8];
extern struct reg		x86_sse_regs[8];
extern int			sse_csave_map[8];
extern struct stack_block	*saved_ret_addr;
extern struct emitter_x86	*emit_x86;

/* nonportable in ANSI */
#define STUPID_X87(reg) \
	(reg >= x86_fprs && reg <= (x86_fprs + 7))

extern struct init_with_name	*init_list_head;
extern struct init_with_name	*init_list_tail;

#endif

