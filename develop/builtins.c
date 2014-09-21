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
 *
 * Parsing of, and icode generation for, builtin functions.
 * This stuff is mostly kinda ad-hoc and ugly :-(
 *
 * Maybe parsing and code generation should be separated as this
 * file keeps growing.
 *
 * Implemented builtins:
 *
 *      __builtin_va_start
 *      __builtin_va_stdarg_start
 *      __builtin_va_arg
 *      __builtin_va_next_arg
 *      __builtin_va_end
 *      __builtin_memcpy
 *      __builtin_memset
 *      __builtin_constant_p
 *      __builtin_va_expect
 *            NOTE: This one is just ignored
 *      __builtin_va_alloca
 *            NOTE: This one uses malloc()/free() instead of the stack. Also,
 *                  it does not honor block scope, so every allocated block
 *                  is kept until the current function is left
 *      __builtin_va_copy
 *            NOTE: No typechecking
 */
#include "builtins.h"
#include <string.h>
#include <stdlib.h>
#include "icode.h"
#include "token.h"
#include "error.h"
#include "subexpr.h"
#include "icode.h"
#include "functions.h"
#include "backend.h"
#include "expr.h"
#include "type.h"
#include "symlist.h"
#include "scope.h"
#include "typemap.h"
#include "amd64_gen.h"
#include "token.h"
#include "backend.h"
#include "cc1_main.h"
#include "decl.h"
#include "reg.h"
#include "x87_nonsense.h"
#include "n_libc.h"



struct type	*builtin_va_list_type;

/*
 * 08/22/07: This stuff was nonsense for va_arg()s first argument, but
 * seems to still make some sense for va_start()
 */
static char *
get_ident(struct expr *ex) {
	struct token	*idtok;

	if (ex->op != 0) {
		return NULL;
	}
	if (ex->data->meat != NULL) {
		idtok = ex->data->meat;
	} else if (ex->data->is_expr) {
		if (ex->data->is_expr->op != 0) {
			return NULL;
		} else if (ex->data->is_expr->data->meat != NULL) {
			idtok = ex->data->is_expr->data->meat;
		} else {
			return NULL;
		}	
	} else {
		return NULL;
	}	
	return idtok->ascii;
}	


static int
builtin_parse_va_start(struct token **tok, struct fcall_data *fdat) {
	struct token		*t = *tok;
	struct decl		*dec;
	struct expr		*ex;
	struct sym_entry	*se;
	char			*ident;
	int			i;

	if ((ex = parse_expr(&t, TOK_OP_COMMA, 0, 0, 1)) == NULL) {
		return -1;
	}	
	fdat->builtin->args[0] = ex;
	fdat->builtin->args[1] = NULL;
	if (next_token(&t) != 0) {
		return -1;
	}	

	if ((ex = parse_expr(&t, TOK_PAREN_CLOSE, 0, 0, 1)) == NULL) {
		return -1;
	}	
#if 0
	if (ex->op != 0 || ex->data->meat->type != TOK_IDENTIFIER) {
		errorfl(t, "Bad second argument to va_start() - "
			"Identifier expected");
		return -1;
	}
#endif
	if ((ident = get_ident(ex)) == NULL) {
		errorfl(t, "Bad second argument to va_start() - "
			"Identifier expected");
		return -1;
	}	

	if ((dec = lookup_symbol(curfunc->scope, ident, 0)) == NULL) {
		errorfl(t, "Undeclared identifier `%s'", ident);
		return -1;
	}	

	for (i = 0, se = curfunc->scope->slist; se != NULL; se = se->next) {
		++i;
		if (i == curfunc->proto->dtype->tlist->tfunc->nargs) {
			break;
		}	
	}	
	if (se == NULL || se->dec != dec) {
		errorfl(t, "Second argument to va_start() is not %s "
			"parameter of `%s'",
			se == NULL? "a": "last",
			curfunc->proto->dtype->name);
		return -1;
	}	

	if (t->type != TOK_PAREN_CLOSE) {
		errorfl(t, "Syntax error at `%s'", t->ascii);
		return -1;
	}	

	*tok = t;
	return 0;
}

static int
builtin_parse_next_arg(struct token **tok, struct fcall_data *fdat) {
	struct expr	*ex;
	struct token	*t = *tok;

	(void) fdat;
	if ((ex = parse_expr(&t, TOK_PAREN_CLOSE, 0, 0, 1)) == NULL) {
		return -1;
	}
	if (ex->op != 0 || ex->data->meat->type != TOK_IDENTIFIER) {
		errorfl(t, "Bad second argument to va_start() - "
			"Identifier expected");
		return -1;
	}
	*tok = t;
	return 0;
}	

static int
builtin_parse_va_end(struct token **tok, struct fcall_data *fdat) {
	struct token	*t = *tok;
	struct expr	*ex;

	(void) fdat;

	if ((ex = parse_expr(&t, TOK_PAREN_CLOSE, 0, 0 ,1)) == NULL) {
		return -1;
	}
	if (ex->op != 0 || ex->data->meat->type != TOK_IDENTIFIER) {
		errorfl(t, "Bad second argument to va_start() - "
			"Identifier expected");
		return -1;
	}
	*tok = t;
	fdat->builtin->args[0] = ex;
	return 0;
}

static int
builtin_parse_va_arg(struct token **tok, struct fcall_data *fdat) {
	struct decl	**typedec;
	struct token	*t = *tok;
	struct expr	*ex;
	struct type	*type;

	if ((ex = parse_expr(&t, TOK_OP_COMMA, 0, 0, 1)) == NULL) {
		return -1;
	}	

	fdat->builtin->args[0] = ex;

	if (next_token(&t) != 0) {	
		return -1;
	}

	if ((typedec = parse_decl(&t, DECL_CAST)) == NULL) {
		return -1;
	}	
	type = typedec[0]->dtype;
	free(typedec);

	fdat->builtin->args[1] = type;
	fdat->builtin->args[2] = NULL;

#if 0
	if (is_basic_agg_type(type)) {
		unimpl();
	}
#endif

	*tok = t;
	return 0;
}

static int
builtin_parse_expect(struct token **tok, struct fcall_data *fdat) {
	struct expr	*ex;
	struct expr	*ex2;
	struct token	*t = *tok;

	if ((ex = parse_expr(&t, TOK_OP_COMMA, 0, 0, 1)) == NULL) {
		return -1;
	}
	/* hmm - gcc warns about first argument of pointer type */
	if (next_token(&t) != 0) {
		return -1;
	}		
	if ((ex2 = parse_expr(&t, TOK_PAREN_CLOSE, 0, EXPR_CONST, 1)) == NULL) {
		return -1;
	}
#if 0
	/* const_value is always null!?!??!?!?!?!?!??? :-( :-( :-( */
	if (!is_integral_type(ex2->const_value->type)) {
		/* hmm - gcc accepts stuff like 0.0f */
		errorfl(second_start,
			"Second argument of __builtin_expect not integer constant");
		return -1;
	}
#endif
	fdat->builtin->args[0] = ex;
	fdat->builtin->args[1] = ex2;
	fdat->builtin->type = BUILTIN_EXPECT;
	*tok = t;
	return 0;
}

static int
generic_builtin_parse_alloca(struct token **tok, struct fcall_data *fdat) {
	struct expr	*ex;

	if ((ex = parse_expr(tok, TOK_PAREN_CLOSE, 0, 0, 1)) == NULL) {
		return -1;
	}
	fdat->builtin->args[0] = ex;
	return 0;
}

static int
generic_builtin_parse_va_copy(struct token **tok, struct fcall_data *fdat) {
	struct expr	*ex;
	struct expr	*ex2;

	if ((ex = parse_expr(tok, TOK_OP_COMMA, 0, 0, 1)) == NULL) {
		return -1;
	}
	if (next_token(tok) != 0) {
		return -1;
	}
	if ((ex2 = parse_expr(tok, TOK_PAREN_CLOSE, 0, 0, 1)) == NULL) {
		return -1;
	}
	fdat->builtin->args[0] = ex;
	fdat->builtin->args[1] = ex2;
	return 0;
}


static int
generic_builtin_parse_constant_p(struct token **tok, struct fcall_data *fdat) {
	struct expr	*ex;

	if ((ex = parse_expr(tok, TOK_PAREN_CLOSE, 0, EXPR_OPTCONSTSUBEXPR, 1)) == NULL) {
		return -1;
	}
	fdat->builtin->args[0] = ex;
	return 0;
}

static int
generic_builtin_parse_memcpy(struct token **tok, struct fcall_data *fdat) {
	struct expr	*ex;
	struct expr	*ex2;
	struct expr	*ex3;

	if ((ex = parse_expr(tok, TOK_OP_COMMA, 0, 0, 1)) == NULL) {
		return -1;
	}
	if (next_token(tok) != 0) {
		return -1;
	}
	if ((ex2 = parse_expr(tok, TOK_OP_COMMA, 0, 0, 1)) == NULL) {
		return -1;
	}
	if (next_token(tok) != 0) {
		return -1;
	}
	if ((ex3 = parse_expr(tok, TOK_PAREN_CLOSE, 0, 0, 1)) == NULL) {
		return -1;
	}
	fdat->builtin->args[0] = ex;
	fdat->builtin->args[1] = ex2;
	fdat->builtin->args[2] = ex3;
	return 0;
}

static int
generic_builtin_parse_offsetof(struct token **tok, struct fcall_data *fdat) {
	struct token		*t = *tok;
	struct token		*starttok = t;
	struct decl		**typedec;
	struct expr		*ex;
	/*static*/ struct token	*tokens;
	int			i;
	static int		dummy;
	int			*ip;
	int			not_constant;

	if ((typedec = parse_decl(&t, DECL_FUNCARG)) == NULL) {
		return -1;
	}
	if ((typedec[0]->dtype->code != TY_STRUCT && typedec[0]->dtype->code != TY_UNION)
		|| typedec[0]->dtype->tlist != NULL) {
		errorfl(starttok, "First operand of __builtin_offsetof() "
			"is not structure type");
		return -1;
	}
	if (t->type != TOK_OPERATOR
		|| *(int *)t->data != TOK_OP_COMMA) {
		errorfl(t, "Syntax error at `%s'", t->ascii);
		return -1;
	}
	if (next_token(&t) != 0) {
		return -1;
	}

	/*
	 * 07/16/08: Do it thoroughly; Don't just allow a single member
	 * identifier, but arbitrary expressions.
	 *
	 * The operand may contain multiple struct member or array
	 * subscript operators, so the easiest way is to construct the
	 * traditional offsetof() implementation as a token list, parse
	 * and evaluate it!
	 */
	tokens = NULL;

	/*
	 * Construct token list. It would be nice to cache these things, but
	 * since the expression parsing routiens alter some tokens (cast,
	 * indirection), it is just safer to start over every time
	 */
	i = TOK_OP_AMB_BAND;
	store_token(&tokens, n_xmemdup(&i, sizeof i), TOK_OPERATOR, 0, NULL);
	store_token(&tokens, &dummy, TOK_PAREN_OPEN, 0, NULL);
	store_token(&tokens, &dummy, TOK_PAREN_OPEN, 0, NULL);

	/*               &      (      ( */

	if (typedec[0]->dtype->code == TY_STRUCT) {
		store_token(&tokens, n_xstrdup("struct"), TOK_KEY_STRUCT, 0, NULL);
	} else {
		store_token(&tokens, n_xstrdup("union"), TOK_KEY_UNION, 0, NULL);
	}

	if (typedec[0]->dtype->tstruc->tag != NULL) {
		/* A well-behaved struct with tag */
		store_token(&tokens, typedec[0]->dtype->tstruc->tag, TOK_IDENTIFIER, 0, NULL);
	} else {
		/* A sinister anonymous struct type - use dummy tag */
		store_token(&tokens, typedec[0]->dtype->tstruc->dummytag, TOK_IDENTIFIER, 0, NULL);
	}

	/*               &      (     (   struct tag */

	i = TOK_OP_AMB_MULTI;
	store_token(&tokens, n_xmemdup(&i, sizeof i), TOK_OPERATOR, 0, NULL);
	store_token(&tokens, &dummy, TOK_PAREN_CLOSE, 0, NULL);
	i = 0;

	ip = n_xmalloc(16); /* XXX */
	memcpy(ip, &i, sizeof i);
	store_token(&tokens, ip, TY_INT, 0, NULL);
	store_token(&tokens, &dummy, TOK_PAREN_CLOSE, 0, NULL);

	i = TOK_OP_STRUPMEMB;
	store_token(&tokens, n_xmemdup(&i, sizeof i), TOK_OPERATOR, 0, NULL);


	/*
	 * Append member selection to token template 
	 *
	 * &     (     (   struct tag    *     )     0     )    -> 
	 */
	tokens->next->next->next->next->next->next->next->next->next->next = t; 

	ex = parse_expr(&tokens, TOK_PAREN_CLOSE, 0, 0, 1);
	if (ex == NULL) {
		return -1;
	}
	if (ex->op != 0) {
		errorfl(ex->tok, "Parse error - Binary operator unexpected");
		return -1;
	}
	if (eval_const_expr(ex, EXPR_OPTCONSTSUBEXPR, &not_constant) != -1) {
		cross_do_conv(ex->const_value, backend->get_size_t()->code, 1);
	}

	/*
	 * Continue where parse_expr() stopped 
	 */
	t = tokens;

	if (t->type != TOK_PAREN_CLOSE) { 
		errorfl(t, "Syntax error at `%s' - expected closing parenthesis",
			t->ascii);
		return -1;
	}	

	*tok = t;
	/*fdat->builtin->args[0] = n_xmemdup(&total_offset, sizeof total_offset);*/
	fdat->builtin->args[0] = ex;
	fdat->builtin->type = BUILTIN_OFFSETOF;
	return 0;
}

static int
generic_builtin_parse_frame_address(struct token **tok, struct fcall_data *fdat) {
	struct expr	*ex;
	struct token	*t = *tok;
	size_t		frameno;

	if ((ex = parse_expr(tok, TOK_PAREN_CLOSE, 0, EXPR_CONST, 1)) == NULL) {
		return -1;
	}
	if (ex->const_value == NULL) {
		errorfl(t, "Argument to __builtin_frame_address() not "
			"constant");
		return -1;
	}
	if (!is_integral_type(ex->const_value->type)) {
		errorfl(t, "Argument to __builtin_frame_address() not "
			"integral");
		return -1;
	}
	cross_do_conv(ex->const_value, TY_INT, 1);
	frameno = cross_to_host_size_t(ex->const_value); /* XXXX CROSS??? */

	if (frameno > 0) {
		errorfl(t, "__builtin_frame_address() currently only "
			"works with the current stack frame (argument 0)!");
		return -1;
	}
	fdat->builtin->args[0] = n_xmemdup(&frameno, sizeof frameno);
	return 0;
}


static struct vreg *
generic_builtin_constant_p_to_icode(struct fcall_data *fdat,
	struct icode_list *il, int eval) {

	struct vreg		*vr = vreg_alloc(NULL, NULL, NULL, NULL);
	struct expr		*ex = fdat->builtin->args[0];
	int			val;
	struct token		*tok;

	(void) il; (void) eval;

	val = ex->const_value != NULL; /*ex->is_const;*/
	tok = const_from_value(&val, NULL);

	vr = vreg_alloc(NULL, tok, NULL, NULL);
	return vr;
}

static struct vreg *
generic_builtin_va_copy_to_icode(struct fcall_data *fdat,
	struct icode_list *il, int eval) {

	struct vreg		*vr = vreg_alloc(NULL, NULL, NULL, NULL);
	struct vreg		*arg0;
	struct vreg		*arg1;
	struct vreg		*tmpvr;
	struct expr		*ex0 = fdat->builtin->args[0];
	struct expr		*ex1 = fdat->builtin->args[1];
	struct token		*tok;
	int			size;
	int			va_list_is_array = 0;

	arg0 = expr_to_icode(ex0, NULL, il, 0, 0, eval);
	if (arg0 == NULL) {
		return NULL;
	}
	arg1 = expr_to_icode(ex1, NULL, il, 0, 0, eval);
	if (arg1 == NULL) {
		return NULL;
	}

	if (!eval) { 
		/* XXX Could not hurt to type-check the operand results?!?!? */
		vreg_set_new_type(vr, make_basic_type(TY_VOID));
		return vr;
	}

	/*
	 * XXX this is just a quick kludge... we will later want to do
	 * full type-checking! Ideally use unary_to_icode() from
	 * subexpr.c for this to determine whether the address can be
	 * taken
	 */
	size = backend->get_sizeof_type(arg0->type, NULL);

	if (backend->arch == ARCH_AMD64) {
		/*
		 * XXX use backend->va_list_is_sysv_like?!?!
		 * This must be done on Linux/PPC too
		 */ 
		va_list_is_array = 1;
		vreg_faultin(NULL, NULL, arg0, il, 0);
		size = 24;  /* XXX because the source really is an array! */
	} else {
		struct reg	*r;
		/*ii =*/ r = icode_make_addrof(NULL, arg0, il);
/*		append_icode_list(il, ii);*/
		arg0 = vreg_disconnect(arg0);
		arg0->type = addrofify_type(arg0->type);
		arg0->size = backend->get_sizeof_type(arg0->type, NULL);
		vreg_map_preg(arg0, r /* ii->dat*/);
	}
	reg_set_unallocatable(arg0->pregs[0]);

	if (!va_list_is_array) {
		struct reg *r;
		/*ii =*/ r = icode_make_addrof(NULL, arg1, il);
		/*append_icode_list(il, ii);*/
		arg1 = vreg_disconnect(arg1);
		arg1->type = addrofify_type(arg1->type);
		arg1->size = backend->get_sizeof_type(arg1->type, NULL);
		vreg_map_preg(arg1, r /*ii->dat*/);
	} else {	
		vreg_faultin(NULL, NULL, arg1, il, 0);
	}	

	reg_set_unallocatable(arg1->pregs[0]);

	tok = const_from_value(&size, make_basic_type(TY_INT));
	tmpvr = vreg_alloc(NULL, tok, NULL, make_basic_type(TY_INT));
	vreg_faultin(NULL, NULL, tmpvr, il, 0);
	
	reg_set_allocatable(arg0->pregs[0]);
	reg_set_allocatable(arg1->pregs[0]);
	icode_make_intrinsic_memcpy_or_memset(BUILTIN_MEMCPY,
		arg0, arg1, tmpvr, 0, il);
	
#if 0
	vr->type = make_basic_type(TY_VOID);
#endif
	/*
	 * 06/20/08: Use vreg_set_new_type() instead of ad-hoc
	 * assignments. Size assignment was missing
	 */
	vreg_set_new_type(vr, make_basic_type(TY_VOID));
	return vr;
}

static struct vreg *
generic_builtin_memcpy_or_memset_to_icode(struct fcall_data *fdat,
	struct icode_list *il, int eval) {

	struct vreg	*resvr = vreg_alloc(NULL, NULL, NULL, NULL);
	struct vreg	*arg0;
	struct vreg	*arg1;
	struct vreg	*arg2;
	struct expr	*ex0 = fdat->builtin->args[0];
	struct expr	*ex1 = fdat->builtin->args[1];
	struct expr	*ex2 = fdat->builtin->args[2];
	char		*funcname = fdat->builtin->type == BUILTIN_MEMCPY?
				"memcpy": "memset";

	resvr->type = n_xmemdup(make_basic_type(TY_VOID), sizeof(struct type));
	append_typelist(resvr->type, TN_POINTER_TO, 0, NULL, NULL);
	resvr->size = backend->get_sizeof_type(resvr->type, NULL);

	arg0 = expr_to_icode(ex0, NULL, il, 0, 0, eval);
	if (arg0 == NULL) {
		return NULL;
	}
	if (arg0->type->tlist == NULL) {
		errorfl(ex0->tok, "First argument to __builtin_%s "
			"must be pointer", funcname);
		return NULL;
	}

	/*
	 * Now save target address to stack (this is the return value
	 * of __builtin_memcpy() and memcpy() generally.)
	 */
	if (eval) {
		vreg_faultin(NULL, NULL, arg0, il, 0);	
		vreg_map_preg(resvr, arg0->pregs[0]);
		free_preg(resvr->pregs[0], il, 0, 1); /* save, but don't invalidate */
		vreg_map_preg(arg0, resvr->pregs[0]);
	}
	
	arg1 = expr_to_icode(ex1, NULL, il, 0, 0, eval);
	if (arg1 == NULL) {
		return NULL;
	}

	/*
	 * 03/26/08: Hmm, the faultin was missing, it only showed up
	 * when dec->vreg was removed. We should recheck the guarantees
	 * that expr_to_icode() gives us, apparently register residence
	 * is not included. This makes sense too!
	 */
	if (eval) {
		vreg_faultin(NULL, NULL, arg1, il, 0);
	}
	if (fdat->builtin->type == BUILTIN_MEMCPY) {
		if (arg1->type->tlist == NULL) {
			errorfl(ex1->tok, "Second argument to __builtin_%s "
				"must be pointer", funcname);
			return NULL;
		}
	} else {
		/* memset */
		if (!is_integral_type(arg1->type)) {
			errorfl(ex1->tok, "Second argument to __builtin_%s ",
				"must be integral", funcname);
		}
		if (eval) {
			arg1 = backend->icode_make_cast(arg1,
				make_basic_type(TY_UCHAR), il);
		}
	}

	arg2 = expr_to_icode(ex2, NULL, il, 0, 0, eval);
	if (arg2 == NULL) {
		return NULL;
	}

	/* 03/26/08: Faultin missing, see above */
	if (eval) {
		vreg_faultin(NULL, NULL, arg2, il, 0);
	}
	if (!is_integral_type(arg2->type)) {
		errorfl(ex2->tok, "Third argument to __builtin_%s "
			"must be integral type", funcname);
		return NULL;
	}

	if (eval) {
		icode_make_intrinsic_memcpy_or_memset(fdat->builtin->type,
			arg0, arg1, arg2, 0, il);
	}
	return resvr;
}


static struct vreg *
generic_builtin_alloca_to_icode(struct fcall_data *fdat,
	struct icode_list *il, int eval) {

	struct vreg		*vr = vreg_alloc(NULL, NULL, NULL, NULL);
	struct vreg		*size_vr;
	struct type		*ty;
	struct stack_block	*sb;
	struct reg		*r;
	struct expr		*ex = fdat->builtin->args[0];
	
	size_vr = expr_to_icode(ex, NULL, il, 0, 0, eval);
	if (size_vr == NULL) {
		return NULL;
	}
	if (!is_integral_type(size_vr->type)) {
		errorfl(ex->tok, "Argument not of integral type");
		return NULL;
	}	

	/* Result type is ``void *'' */
	ty = n_xmemdup(make_basic_type(TY_VOID), sizeof *ty);
	append_typelist(ty, TN_POINTER_TO, 0, NULL, NULL);
	vr->type = ty;
	vr->size = backend->get_sizeof_type(ty, NULL);

	if (!eval) {
		return vr;
	}

	/*
	 * Now allocate a stack block in which to store the returned
	 * pointer, so that we can free it upon return from the function.
	 * As usual the offset is patched in gen_function()
	 */
	sb = make_stack_block(0, vr->size);
	if (curfunc->alloca_head == NULL) {
		curfunc->alloca_head = curfunc->alloca_tail = sb;
	} else {
		curfunc->alloca_tail->next = sb;
		curfunc->alloca_tail = sb;
	}

	/* Get register to store result pointer */
	vreg_set_unallocatable(size_vr);

	/*
	 * 11/12/07: ALLOC_GPR() is wrong here! We have to use the ABI's
	 * malloc() return address register. This stuff is kludgedx XXX
	 * XXXXXXXXXXXXXX
	 */
	r = backend->get_abi_ret_reg(make_void_ptr_type());
	free_preg(r, il, 1, 1);
#if 0
	r = ALLOC_GPR(curfunc, vr->size, il, 0);
#endif

	icode_make_alloca(r, size_vr, sb, il);
	vreg_map_preg(vr, r);

	return vr;
}

static struct vreg *
generic_builtin_offsetof_to_icode(struct fcall_data *fdat,
	struct icode_list *il, int eval) {
	
	struct vreg	*vr;
	long		val; /* XXX because const_from_type() is broken */
	struct token	*tok;
	struct expr	*ex = fdat->builtin->args[0];
	int		not_constant = 0;

	if (eval) {
		if (ex->const_value != NULL) {
			val = cross_to_host_size_t(ex->const_value);
		} else {
			val = 0;
			not_constant = 1;
		}
	} else {
		val = 0;
	}	

	if (not_constant) {
		vr = expr_to_icode(ex, NULL, il, 0, 0, eval); 
		vr = backend->icode_make_cast(vr, backend->get_size_t(), il);
	} else {	
		tok = alloc_token();
		tok->data = n_xmalloc(16);  /* XXX */
		tok->type = backend->get_size_t()->code;
		cross_from_host_long(tok->data, val, tok->type);
		vr = vreg_alloc(NULL, tok, NULL, NULL);
	}
	
	return vr;
}

static struct vreg *
generic_builtin_frame_address_to_icode(struct fcall_data *fdat,
	struct icode_list *il, int eval) {

	struct vreg	*vr = vreg_alloc(NULL,NULL,NULL,NULL);
	struct type	*ty;
	struct reg	*r;
	struct reg	*r2;

	ty = n_xmemdup(make_basic_type(TY_VOID), sizeof *ty);
	append_typelist(ty, TN_POINTER_TO, 0, NULL, NULL);
	vreg_set_new_type(vr, ty);

	if (!eval) {
		return vr;
	}

	/*
	 * Allocate two GPRs to make walking up the stack possible
	 */
	r = ALLOC_GPR(curfunc, vr->size, il, NULL);
	r2 = ALLOC_GPR(curfunc, vr->size, il, NULL);
	icode_make_builtin_frame_address(r, r2, fdat->builtin->args[0], il);
	free_preg(r2, il, 1, 0);
	vreg_map_preg(vr, r);
	return vr;
}

static struct vreg * 
x86_builtin_va_start_to_icode(
	struct fcall_data *fdat,
	struct icode_list *il, 
	int eval) {

	struct type		*ty;
	struct vreg		*vr = vreg_alloc(NULL, NULL, NULL, NULL);
	struct vreg		*valist_vr;
	struct reg 		*r = NULL;
	
	if (eval) {
		/*ii =*/ r = icode_make_addrof(NULL, NULL, il);
/*		append_icode_list(il, ii);*/
	}

	valist_vr = expr_to_icode(fdat->builtin->args[0], NULL, il, 0, 0, eval);
	if (valist_vr == NULL) {
		return NULL;
	}

	if (eval) {
		vreg_map_preg(valist_vr, r /*ii->dat*/);
		icode_make_store(curfunc, valist_vr, valist_vr, il);

		free_preg(valist_vr->pregs[0], il, 0, 0);
	}

	ty = make_basic_type(TY_VOID);
	vr->type = ty;
	vr->size = 0;
	return vr;
}

static struct vreg * 
x86_builtin_next_arg_to_icode(
	struct fcall_data *fdat,
	struct icode_list *il,
	int eval) {

	struct type		*ty;
	struct vreg		*vr = vreg_alloc(NULL, NULL, NULL, NULL);
	
	(void) fdat;

	if (eval) {
		struct reg *r;
		/*ii =*/ r = icode_make_addrof(NULL, NULL, il);
/*		append_icode_list(il, ii);*/
	
		vreg_map_preg(vr, r /*ii->dat*/);
	}

	ty = n_xmemdup(make_basic_type(TY_CHAR), sizeof *ty);
	ty->tlist = alloc_type_node();
	ty->tlist->type = TN_POINTER_TO;
	vr->type = ty;
	vr->size = backend->get_sizeof_type(ty, NULL);
	return vr;
}

static struct vreg * 
x86_builtin_va_end_to_icode(
	struct fcall_data *fdat,
	struct icode_list *il,
	int eval) {

	struct type	*ty;
	struct vreg	*vr = vreg_alloc(NULL, NULL, NULL, NULL);

	/*
	 * 07/21/08: This was not actually evaluating the operand to va_end()!
	 * But that's needed because it may contain side effects
	 */
	if (expr_to_icode(fdat->builtin->args[0], NULL, il, 0, 0, eval) == NULL) {
		return NULL;
	}
	/* XXX type-checking for argument to va_end()!!!!!!!! */

	ty = make_basic_type(TY_VOID);
	vr->type = ty;
	vr->size = 0;
	return vr;
}

static struct vreg *
x86_builtin_va_arg_to_icode(
	struct fcall_data *fdat,
	struct icode_list *il,
	int eval) {

	struct vreg		*valist_vr;
	struct vreg		*vr;
	struct type		*argtype = fdat->builtin->args[1];
	int			size;
	int			intsize;
	int			rightadjust = 0;
	


	/*
	 * 08/22/07: Wow, this stuff used get_ident() and did not
	 * generate icode for the first argument at all! Thus
	 *
	 *   va_list *foo;
	 *   va_arg(*foo, type);
	 *
	 * ... didn't work because *foo was not loaded
	 */
	valist_vr = expr_to_icode(fdat->builtin->args[0], NULL, il, 0, 0, eval);
	if (valist_vr == NULL) {
		return NULL;
	 } /* else {
	      check type...
	 } */     

	if (eval) {
		vreg_faultin(NULL, NULL, /*valist->vreg*/valist_vr, il, 0);
	}


	intsize = backend->get_sizeof_type(
		make_basic_type(backend->arch == ARCH_X86? TY_INT: TY_LONG),
		NULL);




	if ((argtype->code == TY_STRUCT || argtype->code == TY_UNION)
		&& argtype->tlist == NULL) {
		/* Struct/union passed by value */
		vr = vreg_alloc(NULL, NULL, /*valist->vreg*/valist_vr, NULL);
		vr->type = fdat->builtin->args[1];
		vr->size = size = backend->get_sizeof_type(vr->type, NULL);
		if (fdat->lvalue != NULL) {
			/* Result is assigned - copy */

			/*
			 * 05/22/11: Account for empty structs (a GNU
			 * C silliness) being passed
			 */
			if (backend->get_sizeof_type(argtype, NULL) == 0) {
				return vr;
			}
			if (eval) {
				/*
				 * 07/21/08: invalidate_gprs() was missing
				 * since copystruct will probably call memcpy()!
				 */
				backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
				vreg_faultin_ptr(/*valist->vreg*/ valist_vr, il);
				vreg_faultin_ptr(fdat->lvalue, il);
				icode_make_copystruct(fdat->lvalue, vr, il);

				/*
				 * 07/21/08: Invalidate va_list vreg for
				 * offset addition below... Since memcpy
				 * may have trashed it
				 */
				free_preg(valist_vr->pregs[0], il, 1, 0);
			}
			vr->struct_ret = 1;
		}
	} else {
		/* XXX from_ptr?? check for indirection chain!!??! */
		vr = vreg_alloc(NULL, NULL, /*valist->vreg*/ valist_vr, NULL);

		/*
		 * 06/20/08: Use vreg_set_new_type() instead of ad-hoc
		 * assignments (is_multi_reg_obj was not set!)
		 */
		vreg_set_new_type(vr, fdat->builtin->args[1]);
		size = vr->size;

		if (eval) {
			if (size < intsize
				&& backend->arch == ARCH_POWER
				&& backend->abi == ABI_POWER64) {
				/*
				 * Right adjust small argument - Only
				 * for PPC for now
				 */
				rightadjust = calc_slot_rightadjust_bytes(size, intsize);
				if (rightadjust != 0) {
					add_const_to_vreg(valist_vr, rightadjust, il);
				}
			}
			if (is_x87_trash(vr)) {
				vr = x87_anonymify(vr, il);
			} else {
				vreg_faultin(NULL, NULL, vr, il, 0);
				vreg_anonymify(&vr, NULL, NULL, il);
			}
		}
	}

	if (!eval) {
		return vr;
	}


	if (size < intsize) {
		size = intsize;
	} else if (vr->type->code == TY_LDOUBLE) {
		if (backend->arch == ARCH_X86) {
			if (sysflag == OS_OSX) {
#if 0
				size += 6;
#endif
			} else {
#if 0
				size += 2;
#endif
			}
		} else if (backend->arch == ARCH_AMD64) {
			; /* ... */
		} else if (backend->arch == ARCH_POWER) {
#if _AIX
#else
	/*		size += 8;*/
#endif
		}
	}

	/* 07/21/08: Align to multiple of 4
	 * XXXX What to do with SPARC and PPC? 
	 */
	if ((vr->type->code == TY_STRUCT || vr->type->code == TY_UNION)
		&& vr->type->tlist == NULL) {
		if (size & 3) {
			size += 4 - (size & 3);
		}
	}

	/*
	 * 11/25/08: If the item was right-adjusted for a dword slot, we have
	 * to take that into account
	 */
	if (rightadjust != 0) {
		size -= rightadjust;
	}

	add_const_to_vreg(valist_vr, size, il);

	#if 0
	c = const_from_value(&size, NULL);
	tmpvr = vreg_alloc(NULL, c, NULL, NULL);
	vreg_faultin(NULL, NULL, tmpvr, il, 0);
	vreg_faultin_protected(tmpvr, NULL, NULL, /*valist->vreg*/valist_vr, il, 0);
	ii = icode_make_add(  /*valist->vreg*/valist_vr, tmpvr);
	append_icode_list(il, ii);
	icode_make_store(curfunc, /*valist->vreg*/valist_vr,
			/*valist->vreg*/valist_vr, il);
	#endif

	return vr;
}



static struct builtin	builtins[] = { 
	{ "va_start", sizeof "va_start" - 1, BUILTIN_VA_START,
		builtin_parse_va_start, /*x86_builtin_va_start_to_icode*/0 },
	/* va_start and stdarg_start are equivalent */	
	{ "stdarg_start", sizeof "stdarg_start" - 1, BUILTIN_STDARG_START,
		builtin_parse_va_start, x86_builtin_va_start_to_icode },
	{ "next_arg", sizeof "next_arg" - 1, BUILTIN_NEXT_ARG,
		builtin_parse_next_arg, x86_builtin_next_arg_to_icode },
	{ "va_end", sizeof "va_end" - 1, BUILTIN_VA_END,
		builtin_parse_va_end, x86_builtin_va_end_to_icode },
	{ "va_arg", sizeof "va_arg" - 1, BUILTIN_VA_ARG,
		builtin_parse_va_arg, x86_builtin_va_arg_to_icode },
	{ "expect", sizeof "expect" - 1, BUILTIN_EXPECT,
		builtin_parse_expect, NULL },
	{ "alloca", sizeof "alloca" - 1, BUILTIN_ALLOCA,
		generic_builtin_parse_alloca, generic_builtin_alloca_to_icode },
	{ "va_copy", sizeof "va_copy" - 1, BUILTIN_VA_COPY,
		generic_builtin_parse_va_copy, generic_builtin_va_copy_to_icode },
	{ "constant_p", sizeof "constant_p" - 1, BUILTIN_CONSTANT_P,
		generic_builtin_parse_constant_p, generic_builtin_constant_p_to_icode },
	{ "memcpy", sizeof "memcpy" - 1, BUILTIN_MEMCPY,
		generic_builtin_parse_memcpy, generic_builtin_memcpy_or_memset_to_icode },
	{ "memset", sizeof "memset" - 1, BUILTIN_MEMSET,
		generic_builtin_parse_memcpy, generic_builtin_memcpy_or_memset_to_icode },
	{ "frame_address", sizeof "frame_address" - 1, BUILTIN_FRAME_ADDRESS,
		generic_builtin_parse_frame_address,
		generic_builtin_frame_address_to_icode },
	{ "offsetof", sizeof "offsetof" - 1, BUILTIN_OFFSETOF,
		generic_builtin_parse_offsetof,
		generic_builtin_offsetof_to_icode },
	{ "prefetch", sizeof "offsetof" - 1, 0, NULL, NULL },
	{ NULL, 0, 0, NULL, NULL } 
};


static struct vreg * 
mips_builtin_next_arg_to_icode(
	struct fcall_data *fdat,
	struct icode_list *il,
	int eval) {
	(void) fdat; (void) il; (void) eval;
	unimpl();
	return NULL;
}

/*
 * XXXXXXXXXX misses struct/union arguments!
 */
static struct vreg * 
mips_builtin_va_arg_to_icode(
	struct fcall_data *fdat,
	struct icode_list *il,
	int eval) {

	struct vreg		*valist_vr;
	struct vreg		*vr;
	struct vreg		*tmpvr;
	struct token		*c;
	struct type		*argtype = fdat->builtin->args[1];
	struct icode_instr	*ii;
	int			size;
	int			intsize;
	
	/*
	 * 08/22/07: Wow, this stuff used get_ident() and did not
	 * generate icode for the first argument at all! Thus
	 *
	 *   va_list *foo;
	 *   va_arg(*foo, type);
	 *
	 * ... didn't work because *foo was not loaded
	 */
	valist_vr = expr_to_icode(fdat->builtin->args[0], NULL, il, 0, 0, eval);
	if (valist_vr == NULL) {
		return NULL;
	 } /* else {
	      check type...
	 } */    


	if (eval) {
		vreg_faultin(NULL, NULL, /*valist->vreg*/valist_vr, il, 0);

#if 0 /* 07/23/09: Struct-by-value should work now */
		if ((argtype->code == TY_STRUCT || argtype->code == TY_UNION)
			&& argtype->tlist == NULL) {
			unimpl();
		}
#endif
	}
	
	/* XXX from_ptr?? check for indirection chain!!??! */
	vr = vreg_alloc(NULL, NULL, /*valist->vreg*/valist_vr, NULL);
#if 0
	vr->type = fdat->builtin->args[1];
	vr->size = size = backend->get_sizeof_type(vr->type, NULL);
#endif
	/*
	 * 06/20/08: Use vreg_set_new_type() instead of ad-hoc
	 * assignments (is_multi_reg_obj was not set!)
	 */
	vreg_set_new_type(vr, fdat->builtin->args[1]);
	size = vr->size;

	if (!eval) {
		return vr;
	}
	
#if 0
	vreg_faultin(NULL, NULL, vr, il, 0);
#endif

	if ((argtype->code == TY_STRUCT || argtype->code == TY_UNION)
		&& argtype->tlist == NULL) {
		/* Struct/union passed by value */
#if 0
		vr = vreg_alloc(NULL, NULL, /*valist->vreg*/valist_vr, NULL);
		vr->type = fdat->builtin->args[1];
		vr->size = size = backend->get_sizeof_type(vr->type, NULL);
#endif

		/*
		 * 05/22/11: Account for empty structs (a GNU
		 * C silliness) being passed
		 */
		if (backend->get_sizeof_type(argtype, NULL) == 0) {
			return vr;
		}
		if (fdat->lvalue != NULL) {
			/* Result is assigned - copy */
			vreg_faultin_ptr(/*valist->vreg*/valist_vr, il);
			vreg_faultin_ptr(fdat->lvalue, il);

			/*
			 * 07/21/08: invalidate_gprs() was missing
			 * since copystruct will probably call memcpy()!
			 */
			backend->invalidate_gprs(il, 1, INV_FOR_FCALL);

			icode_make_copystruct(fdat->lvalue, vr, il);

			/*
			 * 07/21/08: Invalidate va_list vreg for
			 * offset addition below... Since memcpy
			 * may have trashed it
			 */
			free_preg(valist_vr->pregs[0], il, 1, 0);

			vr->struct_ret = 1;
		}
	} else {
		/*
		 * Small arguments are passed as dwords.
		 * XXX take fp into account!!
		 * ... so let us load the dword, then just treat it as
		 * the destination type
		 */
		tmpvr = n_xmemdup(vr, sizeof *vr);

		if (!is_floating_type(vr->type)) {
			tmpvr->type = make_basic_type(TY_ULLONG);
			tmpvr->size = 8;
			vreg_faultin(NULL, NULL, tmpvr, il, 0);
			vreg_map_preg(vr, tmpvr->pregs[0]);
			vreg_anonymify(&vr, NULL, NULL, il);
		} else if (vr->type->code == TY_LDOUBLE) {
			/*
			 * 07/17/09: long double support. Note that the va_list
			 * has to be 16-byte-aligned because long double is
			 * always passed aligned! E.g.
			 *
			 *    printf("%Lf\n", ld);
			 *
			 * ... will put the format string to r4, SKIP r5 due to
			 * alignment, then put ld to r6 and r7. Since the list
			 * is already aligned on 8-byte boundaries, we just add
			 * 8 if not 16-byte-aligned.
		 	 */
			icode_align_ptr_up_to(valist_vr, 16, 8, il);

			tmpvr->type = make_basic_type(TY_LDOUBLE);
			tmpvr->size = 16;
			vreg_faultin(NULL, NULL, tmpvr, il, 0);
			vreg_map_preg(vr, tmpvr->pregs[0]);
			if (vr->is_multi_reg_obj) {
				/* 07/31/09: May not be multi-reg for e.g. SPARC */
				vreg_map_preg2(vr, tmpvr->pregs[1]);
			}
			vreg_anonymify(&vr, NULL, NULL, il);
		} else {
			tmpvr->type = make_basic_type(TY_DOUBLE);
			tmpvr->size = 8;
			vreg_faultin(NULL, NULL, tmpvr, il, 0);
			vreg_map_preg(vr, tmpvr->pregs[0]);
			vreg_anonymify(&vr, NULL, NULL, il);
		}
	}
	intsize = backend->get_sizeof_type(make_basic_type(TY_INT), NULL);
	if (size < intsize) {
		size = intsize;
	}	

	if (size < 8) {
		/* Even small types are stored as dwords */
		size = 8;
	} else {
		/* 07/21/08: XXX handle struct alignment! */
	}

	c = const_from_value(&size, NULL);
	tmpvr = vreg_alloc(NULL, c, NULL, NULL);
	vreg_faultin(NULL, NULL, tmpvr, il, 0);
	vreg_faultin_protected(tmpvr, NULL, NULL, /*valist->vreg*/valist_vr, il, 0);
	ii = icode_make_add(/*valist->vreg*/valist_vr, tmpvr);
	append_icode_list(il, ii);
	icode_make_store(curfunc, /*valist->vreg*/valist_vr,
			/*valist->vreg*/valist_vr, il);

	return vr;
}


static void
get_member_vreg(struct decl *d, struct vreg **dest, struct vreg *parent) {
	struct vreg	*vr;

	vr = vreg_alloc(NULL, NULL, NULL, d->dtype);
	vr->parent = parent;
	vr->memberdecl = d;
	vr->from_ptr = parent;
	*dest = vr;
}



static struct type	*voidptr_type;


static int
calc_fp_start_offset(struct function *f) {
	struct sym_entry	*se;
	int			fp_regs_used = 0;

	for (se = f->scope->slist; se != NULL; se = se->next) {
		if (is_floating_type(se->dec->dtype)
			&& se->dec->dtype->code != TY_LDOUBLE) {
			++fp_regs_used;
			if (fp_regs_used == 8) {
				break;
			}
		}
		if (se->dec == f->proto->dtype->tlist->tfunc->lastarg) {
			break;
		}
	}
	return 48 + fp_regs_used * 8;
}


/*
 * typedef struct {
 *    unsigned gp_offset;
 *    unsigned fp_offset;
 *    void *overflow_arg_area;
 *    void *reg_save_area;
 * } va_list[1];
 *
 * -  reg_save_area is recorded by fty->lastarg if some variadic args are
 *    passed in registers
 * -  if all variadic stuff is passed on the stack, lastarg points to the
 *    beginning of the stack arguments
 * -  if registers are used for arguments, fty->lastarg->stack_addr->from_reg
 *    is non-null, and
 *     (struct reg **)fty->lastarg->stack_addr->from_reg - amd64_argregs
 *    is the number of gprs used, which, multiplied by 8, gives the gp_offset.
 * -  XXX fp is UNIMPLEMENTED
 * phew ..
 */
static struct vreg * 
amd64_builtin_va_start_to_icode(
	struct fcall_data *fdat,
	struct icode_list *il,
	int eval) {

	struct type		*ty;
	struct vreg		*vr = vreg_alloc(NULL, NULL, NULL, NULL);
	struct vreg		*valist_vr;
	struct sym_entry	*se;
	struct icode_instr	*ii;
	struct reg		*r;
	struct decl		*srcdec;
	struct vreg		*srcvr;
	struct vreg		*tempvr;
	struct amd64_va_patches	*patches;
	static struct amd64_va_patches	nullp;
	int			fp_start_offset;

	if (eval) {
		patches = n_xmalloc(sizeof *patches);
		*patches = nullp;
/*		curfunc->patchme = patches;*/
		/*
		 * 08/07/08: Allow for multiple va_start() per function
		 * (oops)
		 */
		patches->next = curfunc->patchme;
		curfunc->patchme = patches;
	}

	valist_vr = expr_to_icode(fdat->builtin->args[0], NULL, il, 0, 0, eval);
	if (valist_vr == NULL) {
		return NULL;
	}

	if (eval) {
		vreg_faultin(NULL, NULL, valist_vr, il, 0);
	}

	if (valist_vr->type->tlist == NULL
		|| valist_vr->type->tlist->type != TN_POINTER_TO
		|| valist_vr->type->tlist->next != NULL
		|| valist_vr->type->code != TY_STRUCT
		|| valist_vr->type->tstruc != builtin_va_list_type->tstruc) {
		errorfl(NULL /* XXX */, "Argument to va_start() has wrong "
			"type");
		return NULL;
	} 

	if (!eval) {
		ty = make_basic_type(TY_VOID);
		vreg_set_new_type(vr, ty);
		return vr;
	}

	r = ALLOC_GPR(curfunc, 0, il, 0);

	se = valist_vr->type->tstruc->scope->slist;

	/* se = gp_offset */
	get_member_vreg(se->dec, &tempvr, valist_vr);
	ii = icode_make_setreg(r->composed_of[0], /*n*/0);
	append_icode_list(il, ii);
	patches->gp_offset = ii->dat; 

	vreg_map_preg(tempvr, r->composed_of[0]);
	icode_make_store(curfunc, tempvr, tempvr, il);

	se = se->next;

	/* se = fp_offset */
	get_member_vreg(se->dec, &tempvr, valist_vr);

	/*
	 * 07/31/08: Don't always begin at 48 (%xmm0), but also take non-
	 * elliptic parameters into account, e.g. for
	 *
	 *     void foo(double d, ...)
	 *
	 * ... set t the start offset to 48 + 1*8
	 */
	fp_start_offset = calc_fp_start_offset(curfunc);
	ii = icode_make_setreg(r->composed_of[0], /*48*/ fp_start_offset); /* begins after gprs */

	append_icode_list(il, ii);

	patches->fp_offset = ii->dat;
	vreg_map_preg(tempvr, r->composed_of[0]);
	icode_make_store(curfunc, tempvr, tempvr, il);

	se = se->next;
	/* se = overflow_arg_area; */
	get_member_vreg(se->dec, &tempvr, valist_vr);

	srcdec = alloc_decl();

	/* XXX typing stuff is totally bogus?! untyped addres suffices?? */
	srcdec->dtype = voidptr_type;
	srcdec->stack_addr = make_stack_block(0, 8); 
	patches->overflow_arg_area = srcdec->stack_addr;
	srcvr = vreg_alloc(srcdec, NULL, NULL, NULL); 

	{
		struct reg *r;
		/*ii =*/ r = icode_make_addrof(NULL, srcvr, il);
	/*	append_icode_list(il, ii);*/
		vreg_map_preg(srcvr, r /*ii->dat*/);
		icode_make_store(curfunc, srcvr, tempvr, il);
		free_preg(r /*ii->dat*/, il, 0, 0);
	}

	se = se->next;
	/* se = reg_save_area */
	get_member_vreg(se->dec, &tempvr, valist_vr);
	srcdec = alloc_decl();
	srcdec->dtype = voidptr_type;
	srcdec->stack_addr = make_stack_block(0, 8); 
	patches->reg_save_area = srcdec->stack_addr;
	srcvr = vreg_alloc(srcdec, NULL, NULL, NULL); 
	{
		struct reg *r;
		/*ii =*/ r = icode_make_addrof(NULL, srcvr, il);
/*		append_icode_list(il, ii);*/
		vreg_map_preg(srcvr, r /*ii->dat*/);
		icode_make_store(curfunc, srcvr, tempvr, il);
		free_preg(r /*ii->dat*/, il, 0, 0);
	}
#if 0
	se->dec->stack_addr = make_stack_block(0, 8);
	patches->reg_save_area = se->dec->stack_addr;
#endif

	ty = make_basic_type(TY_VOID);
#if 0
	vr->type = ty;
	vr->size = 0;
#endif
	/* 06/20/08: Use vreg_set_new_type() instead of ad-hoc assignments */
	vreg_set_new_type(vr, ty);
	free_preg(r, il, 0, 0);
	return vr;
}

static struct vreg * 
amd64_builtin_next_arg_to_icode(
	struct fcall_data *fdat,
	struct icode_list *il,
	int eval) {

	struct type		*ty;
	struct vreg		*vr = vreg_alloc(NULL, NULL, NULL, NULL);
	
	(void) fdat;

	if (eval) {
		struct reg *r;
		/*ii =*/ r = icode_make_addrof(NULL, NULL, il);
/*		append_icode_list(il, ii);*/
	
		vreg_map_preg(vr, r /*ii->dat*/);
	}

	ty = n_xmemdup(make_basic_type(TY_CHAR), sizeof *ty);
	ty->tlist = alloc_type_node();
	ty->tlist->type = TN_POINTER_TO;
#if 0
	vr->type = ty;
	vr->size = backend->get_sizeof_type(ty, NULL);
#endif
	/*
	 * 06/20/08: Use vreg_set_new_type() instead of ad-hoc
	 * assignments
	 */
	vreg_set_new_type(vr, ty);
	return vr;
}


static struct vreg *
amd64_builtin_va_arg_to_icode(
	struct fcall_data *fdat,
	struct icode_list *il,
	int eval) {

	struct vreg		*valist_vr;
	struct vreg		*vr;
	struct vreg		*tmpvr;
	struct vreg		*tempmembvr;
	struct vreg		*pointer;
	struct vreg		*limit_vreg;
	struct vreg		*offset_vreg = NULL;
	struct token		*c;
	struct icode_instr	*ii;
	struct type		*argtype = fdat->builtin->args[1];
	struct sym_entry	*se;
	struct icode_instr	*label;
	struct icode_instr	*label2;
	struct token		*fe_const;
	struct reg		*r;
	long			long_size;
	int			size;
	struct vreg		*gp_offset_vreg;
	struct vreg		*fp_offset_vreg;
	struct vreg		*overflow_arg_area_vreg;
	struct vreg		*reg_save_area_vreg;

#define gp_offset se->dec
#define fp_offset se->next->dec
#define overflow_arg_area se->next->next->dec
#define reg_save_area se->next->next->next->dec
	/*
	 * 08/22/07: Wow, this stuff used get_ident() and did not
	 * generate icode for the first argument at all! Thus
	 *
	 *   va_list *foo;
	 *   va_arg(*foo, type);
	 *
	 * ... didn't work because *foo was not loaded
	 */
	valist_vr = expr_to_icode(fdat->builtin->args[0], NULL, il, 0, 0, eval);
	if (valist_vr == NULL) {
		return NULL;
	 } /* else {
	      check type...
	 } */     

	pointer = vreg_alloc(NULL, NULL, NULL, voidptr_type);
	if (!eval) { 
		vr = vreg_alloc(NULL, NULL, /*pointer,*/NULL, NULL);
		vr->from_ptr = pointer;
		/*
		 * 06/20/08: Use vreg_set_new_type() instead of ad-hoc
		 * assignments
		 */
		vreg_set_new_type(vr, fdat->builtin->args[1]);
		return vr;
	}

	vreg_faultin(NULL, NULL, /*valist->vreg*/valist_vr, il, 0);

#if 0
	/* Load va_list pointer into register */
	vreg_faultin(NULL, NULL, valist->vreg, il, 0);
	reg_set_unallocatable(valist->vreg->pregs[0]);
#endif	
	se = valist_vr->type->tstruc->scope->slist;

	/* All va_list members come from the va_list pointer */
	get_member_vreg(gp_offset, &tempmembvr, valist_vr);
	gp_offset_vreg = tempmembvr;
	get_member_vreg(fp_offset, &tempmembvr, valist_vr);
	fp_offset_vreg = tempmembvr;
	get_member_vreg(overflow_arg_area, &tempmembvr, valist_vr);
	overflow_arg_area_vreg = tempmembvr;
	get_member_vreg(reg_save_area, &tempmembvr, valist_vr);
	reg_save_area_vreg = tempmembvr;

	if ((argtype->code == TY_STRUCT
		|| argtype->code == TY_UNION
		|| argtype->code == TY_LDOUBLE)
		&& argtype->tlist == NULL) {
		/* Struct/union/long double passed by value */
		struct vreg	*oarea;

		vreg_faultin(NULL, NULL, overflow_arg_area_vreg, il, 0);
		oarea = vreg_disconnect(overflow_arg_area_vreg);
		oarea->type = n_xmemdup(argtype, sizeof *argtype);
		append_typelist(oarea->type, TN_POINTER_TO, 0, NULL, NULL);

		vr = vreg_alloc(NULL, NULL, NULL, NULL);
		vr->from_ptr = oarea;
#if 0
		vr->type = argtype;
		vr->size = size = backend->get_sizeof_type(vr->type, NULL);
#endif
		/*
		 * 06/20/08: Use vreg_set_new_type() instead of ad-hoc
		 * assignments
		 */
		vreg_set_new_type(vr, argtype);
		size = vr->size;



		if (argtype->code == TY_LDOUBLE) {
			/*
			 * 07/26/12: Ensure 16-byte alignment
			 */
			icode_align_ptr_up_to(vr->from_ptr, 16, 8, il);

			/*
			 * 07/29/08: This used to unconditionally
			 * anonymify, then fault-in the vreg. This is
			 * wrong because it will cause two x87 FPR
			 * loads (not quite sure why), which fills 
			 * up the register stack
			 */
			vreg_anonymify(&vr, NULL, NULL, il);

			if (vr->pregs[0] == NULL
				|| vr->pregs[0]->vreg != vr) {
				vreg_faultin_x87(NULL, NULL, vr, il, 0);
			}
		} else if (fdat->lvalue != NULL) {
			/* Result is assigned - copy */


			/*
			 * 05/22/11: Account for empty structs (a GNU
			 * C silliness) being passed
			 */
			if (backend->get_sizeof_type(argtype, NULL) == 0) {
				return vr;
			}

			vreg_faultin_ptr(/*valist->vreg*/valist_vr, il);
			vreg_faultin_ptr(fdat->lvalue, il);
			/* Struct/union result */ 

			/*
			 * 07/21/08: invalidate_gprs() was missing
			 * since copystruct will probably call memcpy()!
			 */
			backend->invalidate_gprs(il, 1, INV_FOR_FCALL);

			icode_make_copystruct(fdat->lvalue, vr, il);

			/*
			 * 07/21/08: Invalidate va_list vreg for
			 * offset addition below... Since memcpy
			 * may have trashed it
			 */
			free_preg(valist_vr->pregs[0], il, 1, 0);
			vr->struct_ret = 1;
		}

		if (size % 8) {
			size += 8 - size % 8;
		}
		long_size = size;
		fe_const = const_from_value(&long_size, make_basic_type(TY_LONG));
		limit_vreg = vreg_alloc(NULL, fe_const, NULL, NULL);
		vreg_faultin_protected(oarea,
			 NULL, NULL, limit_vreg, il, 0);
		ii = icode_make_add(oarea, limit_vreg);
		append_icode_list(il, ii);
		vreg_map_preg(overflow_arg_area_vreg, oarea->pregs[0]);
		icode_make_store(curfunc, overflow_arg_area_vreg,
			overflow_arg_area_vreg, il);

		return vr; /* !!!!!!!!!!!!!!!!! NOTE "EARLY RETURN" !!!!!!!!!!!!!!!!!!!!!!! */

	} else if (argtype->tlist == NULL && IS_FLOATING(argtype->code)) {
		if (argtype->code == TY_LDOUBLE) {
			/* Passed on stack */
		} else {
			offset_vreg = fp_offset_vreg;
			long_size = 112; /* 48 gpr- + 64 fpr-bytes */
		}
	} else {
		offset_vreg = gp_offset_vreg;
		long_size = 48; /* 48 gpr bytes */
	}

	vreg_faultin(NULL, NULL, /*gp_offset->vreg*/offset_vreg, il, 0);
	fe_const = const_from_value(&long_size, make_basic_type(TY_LONG));
	limit_vreg = vreg_alloc(NULL, fe_const, NULL, NULL);
	vreg_faultin(NULL, NULL, limit_vreg, il, 0);

	ii = icode_make_cmp(/*gp_offset->vreg*/offset_vreg, limit_vreg);
	append_icode_list(il, ii);

	r = limit_vreg->pregs[0]; /* reuse register */

	label = icode_make_label(NULL);
	label2 = icode_make_label(NULL);
	ii = icode_make_branch(label, INSTR_BR_EQUAL, offset_vreg);
	append_icode_list(il, ii);

	/* offset is below 48 - use register save area */ 
	vreg_faultin(r, NULL, reg_save_area_vreg, il, 0);
	ii = icode_make_add(reg_save_area_vreg, offset_vreg);
	append_icode_list(il, ii);
	append_icode_list(il, icode_make_jump(label2));

	append_icode_list(il, label);

	/* offset is 48 - use overflow area */

	vreg_faultin(r, NULL, overflow_arg_area_vreg, il, 0);
	append_icode_list(il, label2);
	vreg_map_preg(pointer, r);
	reg_set_unallocatable(r);

	/* XXX from_ptr?? check for indirection chain!!??! */
	vr = vreg_alloc(NULL, NULL, /*pointer,*/NULL, NULL);
	vr->from_ptr = pointer;
#if 0
	vr->type = fdat->builtin->args[1];
	vr->size = size = backend->get_sizeof_type(vr->type, NULL);
#endif
	/*
	 * 06/20/08: Use vreg_set_new_type() instead of ad-hoc
	 * assignments
	 */
	vreg_set_new_type(vr, fdat->builtin->args[1]);
	size = vr->size;

	vreg_faultin(NULL, NULL, vr, il, 0);
	vreg_anonymify(&vr, NULL, NULL, il);

	if (size < 8) {
		size = 8;
	}	
	c = const_from_value(&size, NULL);
	tmpvr = vreg_alloc(NULL, c, NULL, NULL);
	vreg_faultin(NULL, NULL, tmpvr, il, 0);

	vreg_faultin(NULL, NULL, offset_vreg, il, 0);
	limit_vreg = vreg_alloc(NULL, fe_const, NULL, NULL);
	vreg_faultin(NULL, NULL, limit_vreg, il, 0);
	ii = icode_make_cmp(offset_vreg, limit_vreg);
	append_icode_list(il, ii);
	label = icode_make_label(NULL);
	label2 = icode_make_label(NULL);
	ii = icode_make_branch(label, INSTR_BR_EQUAL, offset_vreg);
	append_icode_list(il, ii);

	/* offset is below 48/112 - use register save area */ 
	vreg_faultin_protected(offset_vreg, NULL, NULL, tmpvr, il, 0);
	ii = icode_make_add(offset_vreg, tmpvr);
	append_icode_list(il, ii);
	icode_make_store(curfunc, offset_vreg, offset_vreg, il);
	append_icode_list(il, icode_make_jump(label2));
	append_icode_list(il, label);

	/* offset is 48/112 - use overflow area */
	vreg_faultin(NULL, NULL, overflow_arg_area_vreg, il, 0);
	vreg_faultin(NULL, NULL, tmpvr, il, 0);
	ii = icode_make_add(overflow_arg_area_vreg, tmpvr);
	append_icode_list(il, ii);
	icode_make_store(curfunc, overflow_arg_area_vreg,
		overflow_arg_area_vreg, il);
	append_icode_list(il, label2);

	free_preg(/*valist->vreg*/ valist_vr->pregs[0], il, 0, 0);
	free_preg(offset_vreg->pregs[0], il, 0, 0);
	free_preg(overflow_arg_area_vreg->pregs[0], il, 0, 0);
	free_preg(reg_save_area_vreg->pregs[0], il, 0, 0);
	free_preg(r, il, 0, 0);
#undef gp_offset
#undef fp_offset
#undef overflow_arg_area
#undef reg_save_area
	return vr;
}

static builtin_to_icode_func_t	x86_icode_funcs[] = {
	x86_builtin_va_start_to_icode,
	x86_builtin_va_start_to_icode,
	x86_builtin_next_arg_to_icode,
	x86_builtin_va_end_to_icode,
	x86_builtin_va_arg_to_icode,
	NULL, /* expect */
	generic_builtin_alloca_to_icode,
	generic_builtin_va_copy_to_icode,
	generic_builtin_constant_p_to_icode,
	generic_builtin_memcpy_or_memset_to_icode,
	generic_builtin_memcpy_or_memset_to_icode,
	generic_builtin_frame_address_to_icode,
	generic_builtin_offsetof_to_icode
};

static builtin_to_icode_func_t	mips_icode_funcs[] = {
	x86_builtin_va_start_to_icode,
	x86_builtin_va_start_to_icode,
	mips_builtin_next_arg_to_icode,
	x86_builtin_va_end_to_icode,
	mips_builtin_va_arg_to_icode,
	NULL, /* expect */
	generic_builtin_alloca_to_icode,	
	generic_builtin_va_copy_to_icode,
	generic_builtin_constant_p_to_icode,
	generic_builtin_memcpy_or_memset_to_icode,
	generic_builtin_memcpy_or_memset_to_icode,
	generic_builtin_frame_address_to_icode,
	generic_builtin_offsetof_to_icode
};

static builtin_to_icode_func_t	sparc_icode_funcs[] = {
	x86_builtin_va_start_to_icode,
	x86_builtin_va_start_to_icode,
	mips_builtin_next_arg_to_icode,
	x86_builtin_va_end_to_icode,
	mips_builtin_va_arg_to_icode,
	NULL, /* expect */
	generic_builtin_alloca_to_icode,
	generic_builtin_va_copy_to_icode,
	generic_builtin_constant_p_to_icode,
	generic_builtin_memcpy_or_memset_to_icode,
	generic_builtin_memcpy_or_memset_to_icode,
	generic_builtin_frame_address_to_icode,
	generic_builtin_offsetof_to_icode
};

static builtin_to_icode_func_t	power_icode_funcs[] = {
	x86_builtin_va_start_to_icode,
	x86_builtin_va_start_to_icode,
	/*mips*/ x86_builtin_next_arg_to_icode,
	x86_builtin_va_end_to_icode,
	/*mips*/ x86_builtin_va_arg_to_icode,
	NULL, /* expect */
	generic_builtin_alloca_to_icode,	
	generic_builtin_va_copy_to_icode,
	generic_builtin_constant_p_to_icode,
	generic_builtin_memcpy_or_memset_to_icode,
	generic_builtin_memcpy_or_memset_to_icode,
	generic_builtin_frame_address_to_icode,
	generic_builtin_offsetof_to_icode
};
	
static builtin_to_icode_func_t	amd64_icode_funcs[] = {
	amd64_builtin_va_start_to_icode,
	amd64_builtin_va_start_to_icode,
	amd64_builtin_next_arg_to_icode,
	x86_builtin_va_end_to_icode,
	amd64_builtin_va_arg_to_icode,
	NULL, /* expect */
	generic_builtin_alloca_to_icode,	
	generic_builtin_va_copy_to_icode,
	generic_builtin_constant_p_to_icode,
	generic_builtin_memcpy_or_memset_to_icode,
	generic_builtin_memcpy_or_memset_to_icode,
	generic_builtin_frame_address_to_icode,
	generic_builtin_offsetof_to_icode
};

/*
 * XXX the initialization below means that the ``builtins'' and
 * ``*_icode_funcs'' tables have to have the same ordering
 */
static void
init_builtin_functions(void) {
	builtin_to_icode_func_t	*p = NULL;
	unsigned int		i;

	if (backend->arch == ARCH_X86) {
		p = x86_icode_funcs;
	} else if (backend->arch == ARCH_AMD64) {
		p = amd64_icode_funcs;
	} else if (backend->arch == ARCH_MIPS) {
		p = mips_icode_funcs;
	} else if (backend->arch == ARCH_POWER) {
		p = power_icode_funcs;
	} else if (backend->arch == ARCH_SPARC) {
		p = sparc_icode_funcs;
	} else {
		unimpl();
	}
	for (i = 0; i < sizeof x86_icode_funcs /
		sizeof x86_icode_funcs[0]; ++i) {
		builtins[i].toicode = p[i]; 
	}
}

static struct builtin *
lookup_builtin(const char *name) {
	size_t	namelen = strlen(name);
	int	i;

	/*
	 * XXX test below is for alloca()
	 */
	if (name[0] == '_') {
		name += sizeof "__builtin_" - 1;
		namelen -= sizeof "__builtin_" - 1;
	}	

	for (i = 0; builtins[i].name != NULL; ++i) {
		if (builtins[i].namelen != namelen) {
			continue;
		}
		if (strcmp(builtins[i].name, name) == 0) {
			return &builtins[i];
		}
	}
	return NULL;
}	


struct fcall_data *
get_builtin(struct token **tok, struct token *nametok) {
	struct builtin		*b;
	struct fcall_data	*ret;
	static int		inited;

	if (!inited) {
		/* XXX ... */
		voidptr_type = n_xmemdup(make_basic_type(TY_VOID),
			sizeof *voidptr_type);
		voidptr_type->tlist = alloc_type_node();
		voidptr_type->tlist->type = TN_POINTER_TO;
		init_builtin_functions();
		inited = 1;
	}

	if ((b = lookup_builtin(nametok->data)) == NULL) {
		errorfl(nametok, "Unknown builtin `%s'", nametok->ascii);
		return NULL;
	}

	if (b->parse == NULL) {
		/*
		 * 03/09/09: This builtin is completely ignored! Currently
		 * we want this for __builtin_prefetch() used in MySQL.
		 * No typechecking at all is done
		 */
		recover(tok, TOK_PAREN_CLOSE, 0);
		return NULL;
	}

	if (b->parse == builtin_parse_va_start) {
		/* Seems nonsense because of va_list arguments */
		if (!curfunc->proto->dtype->tlist->tfunc->variadic) {
			errorfl(nametok,
				"`%s' used in non-variadic function `%s'",
				nametok->ascii, curfunc->proto->dtype->name);
			return NULL;
		}	
	}

	ret = alloc_fcall_data();
	ret->builtin = n_xmalloc(sizeof *ret->builtin);
	memset(ret->builtin, 0, sizeof *ret->builtin);
	ret->builtin->type = b->type;
	ret->builtin->builtin = b;
	if (b->parse(tok, ret) != 0) {
		/* 07/16/08: Used free() unconditionally! */
#if ! USE_ZONE_ALLOCATOR
		free(ret);
#endif
		return NULL;
	}
	return ret;
}


static const struct {
	char	*name;
} renamable_builtins[] = {
	{ "strlen" },
	{ "strcpy" },
	{ "strcat" },
	{ "strncat" },
	{ "strncpy" },
/*	{ "memcpy" },*/
	{ "_exit" },
	{ "alloca" },
	{ "bcmp" },
	{ "bzero" },
	{ "dcgettext" },
	{ "dgettext" },
	{ "dremf" },
	{ "dreml" },
	{ "drem" },
	{ "exp10f" },
	{ "exp10l" },
	{ "exp10" },
	{ "ffsll" },
	{ "ffsl" },
	{ "ffs" },
	{ "fprintf_unlocked" },
	{ "fputs_unlocked" },
	{ "gammaf" },
	{ "gammal" },
	{ "gamma" },
	{ "gammaf_r" },
	{ "gammal_r" },
	{ "gamma_r" },
	{ "gettext" },
	{ "index" },
	{ "isascii" },
	{ "j0f" },
	{ "j0l" },
	{ "j0" },
	{ "j1f" },
	{ "j1l" },
	{ "j1" },
	{ "jnf" },
	{ "jnl" },
	{ "jn" },
	{ "lgammaf_r" },
	{ "lgammal_r" },
	{ "lgamma_r" },
	{ "mempcpy" },
	{ "pow10f" },
	{ "pow10l" },
	{ "pow10" },
	{ "printf_unlocked" },
	{ "rindex" },
	{ "scalbf" },
	{ "scalbl" },
	{ "scalb" },
	{ "signbit" },
	{ "signbitf" },
	{ "signbitl" },
	{ "signbitd32" },
	{ "signbitd64" },
	{ "signbitd128" },
	{ "significandf" },
	{ "significandl" },
	{ "significand" },
	{ "sincosf" },
	{ "sincosl" },
	{ "sincos" },
	{ "stpcpy" },
	{ "stpncpy" },
	{ "strcasecmp" },
	{ "strdup" },
	{ "strfmon" },
	{ "strncasecmp" },
	{ "strndup" },
	{ "toascii" },
	{ "y0f" },
	{ "y0l" },
	{ "y0" },
	{ "y1f" },
	{ "y1l" },
	{ "acosf" },
	{ "acosl" },
	{ "asinf" },
	{ "asinl" },
	{ "atan2f" },
	{ "atan2l" },
	{ "atanf" },
	{ "atanl" },
	{ "ceilf" },
	{ "ceill" },
	{ "cosf" },
	{ "coshf" },
	{ "coshl" },
	{ "cosl" },
	{ "expf" },
	{ "expl" },
	{ "fabsf" },
	{ "fabsl" },
	{ "floorf" },
	{ "floorl" },
	{ "fmodf" },
	{ "fmodl" },
	{ "frexpf" },
	{ "frexpl" },
	{ "ldexpf" },
	{ "ldexpl" },
	{ "log10f" },
	{ "log10l" },
	{ "logf" },
	{ "logl" },
	{ "modfl" },
	{ "modf" },
	{ "powf" },
	{ "powl" },
	{ "sinf" },
	{ "sinhf" },
	{ "sinhl" },
	{ "sinl" },
	{ "sqrtf" },
	{ "sqrtl" },
	{ "tanf" },
	{ "tanhf" },
	{ "tanhl" },
	{ "tan" },
	{ "abort" },
	{ "abs" },
	{ "acos" },
	{ "asin" },
	{ "atan2" },
	{ "atan" },
	{ "calloc" },
	{ "ceil" },
	{ "cosh" },
	{ "cos" },
	{ "exit" },
	{ "exp" },
	{ "fabs" },
	{ "floor" },
	{ "fmod" },
	{ "fprintf" },
	{ "fputs" },
	{ "frexp" },
	{ "fscanf" },
	{ "isalnum" },
	{ "isalpha" },
	{ "iscntrl" },
	{ "isdigit" },
	{ "isgraph" },
	{ "islower" },
	{ "isprint" },
	{ "ispunct" },
	{ "isspace" },
	{ "isupper" },
	{ "isxdigit" },
	{ "tolower" },
	{ "toupper" },
	{ "labs" },
	{ "ldexp" },
	{ "log10" },
	{ "log" },
	{ "malloc" },
	{ "memchr" },
	{ "memcmp" },
	/*
	 * 07/26/09: We already have an implementation for __builtin_memcpy()
	 * It is better to use that one because it has the correct type.
	 * puts(__builtin_memcpy(buf, buf2));
	 * breaks on AMD64 because the return value of renamed builtins is
	 * implicitly int. As a workaround we could try to make it ``long''
	 * instead, which would likely make more cases work. But it would be
	 * best to have a function catalog with correct type signatures
	 *
	 * 07/30/09: Ok, now we have such a function catalog! (Except it
	 * currently only contains C89 functions)
	 */
/*	{ "memcpy" },*/
	{ "memset" },
	{ "memmove" },
	{ "modf" },
	{ "pow" },
	{ "printf" },
	{ "putchar" },
	{ "puts" },
	{ "scanf" },
	{ "sinh" },
	{ "sin" },
	{ "snprintf" },
	{ "sprintf" },
	{ "sqrt" },
	{ "sscanf" },
	{ "strcat" },
	{ "strchr" },
	{ "strcmp" },
	{ "strcpy" },
	{ "strcspn" },
	{ "strlen" },
	{ "strncat" },
	{ "strncmp" },
	{ "strncpy" },
	{ "strpbrk" },
	{ "strrchr" },
	{ "strspn" },
	{ "strstr" },
	{ "tanh" },
	{ "tan" },
	{ "vfprintf" },
	{ "vprintf" },
	{ "vsprintf" },
	{ NULL }
}; 


static struct renamed_entry {
	char			*name;
	struct renamed_entry	*next;
} *renamed_head[128],
  *renamed_tail[128];

static unsigned int
hash_builtin(const char *name) {
	unsigned key = 0;

	for (; *name != 0; ++name) {
		key = key * 33 + *name;
	}
	return key & 127;
}

static void
init_builtins_to_be_renamed(void) {
	int				i;
	static struct renamed_entry	buffers[sizeof renamable_builtins / sizeof renamable_builtins[0]];

	for (i = 0; renamable_builtins[i].name != NULL; ++i) {
		struct renamed_entry	*ent = &buffers[i];
		unsigned 		key;

		ent->name = renamable_builtins[i].name;
		ent->next = NULL;
		key = hash_builtin(ent->name);
		if (renamed_head[key] != NULL) {
			renamed_tail[key]->next = ent;
			renamed_tail[key] = ent;
		} else {
			renamed_head[key] = renamed_tail[key] = ent;
		}
	}
}

int
builtin_to_be_renamed(const char *name) {
	int			i;
	static int		initialized;
	struct renamed_entry	*ent;

	if (!initialized) {
		init_builtins_to_be_renamed();
		initialized = 1;
	}

	name += strlen("__builtin_");

	i = hash_builtin(name);
	for (ent = renamed_head[i]; ent != NULL; ent = ent->next) {
		if (strcmp(ent->name, name) == 0) {
			return 1;
		}
	}
	return 0;
}

