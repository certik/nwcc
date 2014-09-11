/*
 * Copyright (c) 2006 - 2010, Nils R. Weller
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
#ifndef AMD64_GEN_H
#define AMD64_GEN_H

struct backend;
struct vreg;
struct icode_instr;

struct init_with_name;

#include "reg.h"

struct amd64_va_patches {
	int			*gp_offset;
	int			*fp_offset;
	struct stack_block	*overflow_arg_area;
	struct stack_block	*reg_save_area;

	/*
	 * 08/07/08: Allow for multiple va_start() per function... DUH!!!!
	 */
	struct amd64_va_patches	*next;
};

typedef void	(*emit_amd64_cvtsi2sd_func_t)(struct icode_instr *);
typedef void	(*emit_amd64_cvtsi2ss_func_t)(struct icode_instr *);
typedef void	(*emit_amd64_cvtsi2sdq_func_t)(struct icode_instr *);
typedef void	(*emit_amd64_cvtsi2ssq_func_t)(struct icode_instr *);
typedef void	(*emit_amd64_cvttsd2si_func_t)(struct icode_instr *);
typedef void	(*emit_amd64_cvttsd2siq_func_t)(struct icode_instr *);
typedef void	(*emit_amd64_cvttss2si_func_t)(struct icode_instr *);
typedef void	(*emit_amd64_cvttss2siq_func_t)(struct icode_instr *);
typedef void	(*emit_amd64_cvtsd2ss_func_t)(struct icode_instr *);
typedef void	(*emit_amd64_cvtss2sd_func_t)(struct icode_instr *);
typedef void	(*emit_amd64_load_negmask_func_t)(struct icode_instr *);
typedef void	(*emit_amd64_xorps_func_t)(struct icode_instr *);
typedef void	(*emit_amd64_xorpd_func_t)(struct icode_instr *);
typedef void	(*emit_amd64_ulong_to_float_func_t)(struct icode_instr *);

struct emitter_amd64 {
	emit_amd64_cvtsi2sd_func_t	cvtsi2sd;
	emit_amd64_cvtsi2ss_func_t	cvtsi2ss;
	emit_amd64_cvtsi2sdq_func_t	cvtsi2sdq;
	emit_amd64_cvtsi2ssq_func_t	cvtsi2ssq;
	emit_amd64_cvttsd2si_func_t	cvttsd2si;
	emit_amd64_cvttsd2siq_func_t	cvttsd2siq;
	emit_amd64_cvttss2si_func_t	cvttss2si;
	emit_amd64_cvttss2siq_func_t	cvttss2siq;
	emit_amd64_cvtsd2ss_func_t	cvtsd2ss;
	emit_amd64_cvtss2sd_func_t	cvtss2sd;
	emit_amd64_load_negmask_func_t	load_negmask;
	emit_amd64_xorps_func_t		xorps;
	emit_amd64_xorpd_func_t		xorpd;
	emit_amd64_ulong_to_float_func_t	ulong_to_float;
};

extern int	amd64_need_negmask; /* XXX this sucks */
extern int	amd64_need_ulong_float_mask; /* 08/04/08: ... this too :-) */

extern struct emitter_amd64	*emit_amd64;

struct reg	*find_top_reg(struct reg *r);
extern struct reg	*amd64_argregs[];

extern struct backend		amd64_backend;
extern struct reg		amd64_x86_gprs[7];
extern struct reg		amd64_gprs[16];
extern struct stack_block	*saved_ret_addr;
extern struct emitter_x86	*emit_x86;

extern struct init_with_name	*init_list_head;
extern struct init_with_name	*init_list_tail;

#endif

