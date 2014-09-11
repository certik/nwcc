/*
 * Copyright (c) 2008 - 2010, Nils R. Weller
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
#include "x87_nonsense.h"
#include <assert.h>
#include <stdlib.h>
#include "reg.h"
#include "icode.h"
#include "type.h"
#include "token.h"
#include "functions.h"
#include "backend.h"
#include "x86_gen.h"
#include "n_libc.h"

struct vreg *
x87_anonymify(struct vreg *vr, struct icode_list *il) {
	struct vreg	*ret;

	assert(is_x87_trash(vr));

	if (vr->var_backed == NULL
		&& vr->from_ptr == NULL
		&& vr->parent == NULL
		&& vr->from_const == NULL
		&& vr->stack_addr != NULL) {
		/* Already anonymous (stack backed) */
#if 0
		*ret = *vr;
		return ret;
#endif
		return vr;
	} else {
		ret = vreg_alloc(NULL, NULL, NULL, vr->type);
	}

	vreg_faultin_x87(&x86_fprs[0], NULL, vr, il, 0);

	vreg_map_preg(ret, &x86_fprs[0]);

	/* Store below frees register */
	icode_make_store(curfunc, ret, ret, il);
	
	ret->pregs[0] = NULL;

	/*
	 * XXXXXXXXXXXXXXXXXXXXXXXX
	 * without tht memdup below, casting to (void) breaks because
	 * icode_make_cast  stes the size oif ``ret'' to 0, which
	 * makes the emitter's store function fail
	 */
	return n_xmemdup(ret, sizeof *ret);
}

struct vreg *
x87_do_binop(struct vreg *dest, struct vreg *src, int op,
	struct icode_list *il) {

	struct icode_instr	*ii = NULL;
	struct vreg		*ret = vreg_alloc(NULL,NULL,NULL,dest->type);

	vreg_faultin_x87(NULL, NULL, dest, il, 0);
	vreg_faultin_x87(NULL, NULL, src, il, 0);
	vreg_map_preg(dest, &x86_fprs[1]);
	
	switch (op) {
	case TOK_OP_PLUS:
		ii = icode_make_add(dest, src);
		break;
	case TOK_OP_MINUS:
		ii = icode_make_sub(dest, src);
		break;
	case TOK_OP_DIVIDE:
		ii = icode_make_div(dest, src);
		break;
	case TOK_OP_MULTI:
		ii = icode_make_mul(dest, src);
		break;
	default:
		unimpl();
	}
	append_icode_list(il, ii);

	/*
	 * The operation is always performed with a ``pop''. So the result
	 * is in the TOS, and we can save that to the stack to free the
	 * register
	 */
	vreg_map_preg(ret, &x86_fprs[0]);
	icode_make_store(curfunc, ret, ret, il);
	ret->pregs[0] = NULL;

	return ret;
}

struct vreg *
x87_do_unop(struct vreg *dest, int op,
		struct icode_list *il) {

	struct vreg		*ret = vreg_alloc(NULL,NULL,NULL,dest->type);
	struct icode_instr	*ii;

	vreg_faultin_x87(&x86_fprs[0], NULL, dest, il, 0);
	vreg_map_preg(ret, &x86_fprs[0]);
	if (op == TOK_OP_UPLUS) {
		; /* no operation */
	} else if (op == TOK_OP_UMINUS) {
		ii = icode_make_neg(ret);
		append_icode_list(il, ii);
	}

	/* Store below frees register */
	icode_make_store(curfunc, ret, ret, il);
	ret->pregs[0] = NULL;
	return ret;
}

