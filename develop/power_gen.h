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

#ifndef POWER_GEN
#define POWER_GEN

struct backend;
struct icode_instr;

extern struct backend	power_backend;
extern struct reg	power_gprs[32];
extern struct reg	power_fprs[32];

/* XXX possible namespace pollution!!!!!!!!!!!!! */
typedef void (*srawi_func_t)(struct icode_instr *);
typedef void (*rldicl_func_t)(struct icode_instr *);
typedef void (*fcfid_func_t)(struct icode_instr *);
typedef void (*frsp_func_t)(struct icode_instr *);
typedef void (*rlwinm_func_t)(struct icode_instr *);
typedef void (*slwi_func_t)(struct icode_instr *);
typedef void (*extsb_func_t)(struct icode_instr *);
typedef void (*extsh_func_t)(struct icode_instr *);
typedef void (*extsw_func_t)(struct icode_instr *);
typedef void (*xoris_func_t)(struct icode_instr *);
typedef void (*lis_func_t)(struct icode_instr *);
typedef void (*loadup4_func_t)(struct icode_instr *);
typedef void (*fctiwz_func_t)(struct icode_instr *);

struct emitter_power {
	srawi_func_t	srawi;
	rldicl_func_t	rldicl;
	fcfid_func_t	fcfid;
	frsp_func_t	frsp;
	rlwinm_func_t	rlwinm;
	slwi_func_t	slwi;
	extsb_func_t	extsb;
	extsh_func_t	extsh;
	extsw_func_t	extsw;
	xoris_func_t	xoris;
	lis_func_t	lis;
	loadup4_func_t	loadup4;
	fctiwz_func_t	fctiwz;
};

extern struct emitter	power_emit_as;
extern struct emitter_power	power_emit_power_as;
extern struct emitter_power	*emit_power;

#endif

