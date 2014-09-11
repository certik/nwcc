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

#ifndef MIPS_GEN
#define MIPS_GEN

struct backend;
struct icode_instr;

extern struct backend	mips_backend;
extern struct reg		mips_gprs[32];

typedef void	(*mfc1_func_t)(struct icode_instr *ii);
typedef void	(*mtc1_func_t)(struct icode_instr *ii);
typedef void	(*cvt_func_t)(struct icode_instr *ii);
typedef void	(*trunc_func_t)(struct icode_instr *ii);
typedef void	(*make_32bit_mask_func_t)(struct icode_instr *ii);

struct emitter_mips {
	mfc1_func_t		mfc1;
	mtc1_func_t		mtc1;
	cvt_func_t		cvt;
	trunc_func_t	trunc;
	make_32bit_mask_func_t	make_32bit_mask;
};

extern struct emitter_mips	*emit_mips;

#endif

