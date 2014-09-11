/*
 * Copyright (c) 2005 - 2009, Nils R. Weller
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
 * Parsing of, and icode generation for, sub-expressions
 */
#include "expr.h"
#include "subexpr.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "token.h"
#include "error.h"
#include "misc.h"
#include "defs.h"

#ifndef PREPROCESSOR
#    include "decl.h"
#    include "scope.h"
#    include "icode.h"
#    include "symlist.h"
#    include "cc1_main.h"
#    include "control.h"
#    include "debug.h"
#    include "zalloc.h"
#    include "functions.h"
#    include "backend.h"
#    include "reg.h"
#    include "x87_nonsense.h"
#    include "builtins.h"
#endif

#include "type.h"
#include "n_libc.h"

struct context {
	struct type		curtype;
	struct decl		*var_lvalue;
	struct vreg		*curitem;
	struct vreg		*load;
	int			is_lvalue;
	int			indir;
};

struct s_expr *
alloc_s_expr(void) {
	struct s_expr	*ret;
#if USE_ZONE_ALLOCATOR
	ret = zalloc_buf(Z_S_EXPR);
#else
	static struct s_expr nullop;
	ret = n_xmalloc(sizeof *ret);
	*ret = nullop;
#endif
	/*
	 * 03/10/09: Set operators pointer to small operators buffer
	 * initially. This means the caller can use 10 operators for
	 * this sub expression before the operators pointer must be
	 * set to a larger buffer. get_sub_expr() should be the only
	 * user who may be affected by this limitation
	 */
	ret->operators = ret->operators_buf;
	return ret;
}


static int
ambig_to_unary(struct token *t, int is_prefix) {
	int	op = *(int *)t->data;

	switch (op) {
	case TOK_OP_AMB_PLUS:
		if (is_prefix) op = TOK_OP_UPLUS;
		break;
	case TOK_OP_AMB_MINUS:
		if (is_prefix) op = TOK_OP_UMINUS;
		break;
	case TOK_OP_AMB_MULTI:
		if (is_prefix) op = TOK_OP_DEREF;
		break;
	case TOK_OP_AMB_BAND:
		if (is_prefix) op = TOK_OP_ADDR;
		break;
	case TOK_OP_AMB_INCR:
		if (is_prefix) op = TOK_OP_INCPRE;
		else op = TOK_OP_INCPOST;
		break;
	case TOK_OP_AMB_DECR:
		if (is_prefix) op = TOK_OP_DECPRE;
		else op = TOK_OP_DECPOST;
		break;
	case TOK_OP_LAND:
		/*
		 * 07/19/08: &&label takes the address of the named label in
		 * GNU C
		 */
		if (is_prefix) {
			t->type = op = TOK_OP_ADDRLABEL;
			warningfl(t, "ISO C does not allow taking the address of a label");
		}
		break;
	default:
		/* XXX handle labels!  foo:  */
		;
		break;
	}

	if (is_prefix && !IS_UNARY(op)) {
		/*
		 * foo + bar   is ok
		 * / foo       is not
		 */
		errorfl(t, "Invalid use of operator `%s'", t->ascii);
		return -1;
	}
	return op;
}



#ifndef PREPROCESSOR
static int
check_memb_select(struct type *curtype, struct token *op) { 
	char	*p;

	if (curtype->code == 0) {
		errorfl(op,
			"Invalid use of `%s' operator", op->ascii);
		return -1;
	}
	if (curtype->tstruc == NULL) {
		errorfl(op,
	"`%s' operator applied to non-union/structure type",
			op->ascii);
		return -1;
	}
		
	if (op->type == TOK_OP_STRUMEMB) {
		/* . requires plain struct/union type */
		if (curtype->tlist != NULL) {
			p = curtype->code == TY_UNION? "union": "struct";
			if (curtype->tstruc->tag != NULL) {
				errorfl(op,
				"`.' operator applied to something "
				"that is not of type `%s %s'",
					p,
					curtype->tstruc->tag);
			} else {
				errorfl(op,
				"`.' operator applied to something "
				"that is not of type `%s'", p);
			}
			return -1;
		}	
	} else {
		/*
	 	 * -> requires *single* pointer (or
		 * array, since array types decay
		 * into pointers in expressions)
		 * struct/union 
		 */
		if (curtype->tlist == NULL
			|| curtype->tlist->next != NULL
			|| curtype->tlist->tfunc != NULL) {
			p = curtype->code == TY_UNION? "union": "struct";
			if (curtype->tstruc->tag != NULL) {
				errorfl(op,
					"`->' operator applied to "
					"something that is not of "
					"type `%s %s *'",
					p,
					curtype->tstruc->tag);
			} else {
				errorfl(op,
				"`->' operator applied to "
					"something that is not of "
					"type `pointer to struct/union'");
			}
			return -1;
		}
	}
	return 0;
}	
#endif


#ifndef PREPROCESSOR
static void
print_nomemb(struct type *curtype, struct token *tok, const char *member) {
	char	*p;

	p = curtype->code == TY_UNION? "Union": "Structure";
	if (curtype->tstruc->tag != NULL) {
		errorfl(tok,
			"%s type `%s' has no member named `%s'",
			p, curtype->tstruc->tag, member);
	} else {
		errorfl(tok, "%s type has no member named `%s'",
			p, member); 
	}
}	
#endif

#ifndef PREPROCESSOR

/*
 * XXX this stuff ends up handling unions and structs identically, thus
 * deferring all the work to the backend :(
 * I've tried removing the union details at this point and patching up
 * all member stack addressing info in the backend as necessary ... But
 * it just never works. Would be cool to have base addresses and offsets
 * such that the var_backed/from_const/stack_addr/from_ptr mess becomes
 * say one ``struct address'' and then one could do
 * struct address *member = addr_offset(base, nbytes);
 * or something like that. Deferring all of this offset addressing
 * nonsense is terrible to the maximum :(
 */
static int 
do_struct_member(
	struct token *tok,
	struct context *context,
	struct icode_list *il,
	int eval) {

	struct decl		*dp;
	struct decl		*uniondp = NULL;
	struct type		*curtype = &context->curtype;
	struct token		*ident = tok->data;
	struct vreg		*vr = NULL;
	int 			is_ptr;
	int			is_struct;
	int			is_anon_union = 0;

	is_ptr = tok->type == TOK_OP_STRUPMEMB;
	is_struct = curtype->code == TY_STRUCT;

	if (check_memb_select(curtype,
		tok) == -1) {
		return -1;
	}	
	if (curtype->tstruc->incomplete) {
		errorfl(tok,
			"Cannot access members of incomplete structure type");
		return -1;
	}	
	++curtype->tstruc->references;


	dp = access_symbol(curtype->tstruc->scope, ident->data, 0);
	if (dp == NULL) {
		/*
		 * Member not found. This may be an anonymous union member!
		 */	
		struct sym_entry	*se;
		
		for (se = curtype->tstruc->scope->slist;
			se != NULL;
			se = se->next) {
			struct type	*ty = se->dec->dtype;

			if (ty->code == TY_UNION
				&& ty->tlist == NULL
				&& ty->name == NULL) {
				/*
				 * This is an anonymous union! But does it
				 * contain the identifier we are looing for?
				 */
				dp = access_symbol(se->dec->dtype->tstruc->scope,
					ident->data, 0);	
				if (dp != NULL) {
					uniondp = se->dec;
					break;
				}
			}
		}
		if (dp == NULL) {
			print_nomemb(curtype, tok, ident->data);
			return -1;
		}
		is_anon_union = 1;
	}

	copy_type(curtype, dp->dtype, 0);
	curtype->storage = 0;
	if (eval) {
		if (is_ptr) {
			vr = vreg_alloc(NULL, NULL, NULL, dp->dtype);
			/* Avoid indirection chain */
#if 0
			if (context->curitem->from_ptr) {
				vreg_anonymify(&context->curitem,
					NULL, NULL, il);
			}
#endif
			/*
			 * XXX 07/09/07: The UNCONDITIONAL anonymify and
			 * vreg_disconnect are new. Without them, map_pregs
			 * in backend.c trashed a shared vreg register
			 * reference when assigning NULL to pregs[0]!
			 */
			vreg_anonymify(&context->curitem, NULL, NULL, il);
			context->curitem = vreg_disconnect(context->curitem);
			if (!is_anon_union) {
				vr->from_ptr = context->curitem;
			}
		} else {
			vr = vreg_alloc(/*dp*/NULL, NULL, NULL, dp->dtype);
		}
		
		vr->memberdecl = dp;
		if (is_anon_union) {
			vr->parent = vreg_alloc(NULL, NULL, NULL,
				uniondp->dtype);
			vr->parent->memberdecl = uniondp;
			if (is_ptr) {
				vr->parent->from_ptr = context->curitem;
			}	
			vr->parent->parent = context->curitem;
		} else {	
			vr->parent = context->curitem;
		}
	
		context->curitem = vr;
	} else {
		vr = vreg_alloc(NULL, NULL, NULL, NULL);
		vreg_set_new_type(vr, dp->dtype);
		context->curitem = vr;
	}

	if (!is_ptr) {
		context->var_lvalue = dp;
	} else {
		context->var_lvalue = NULL;
	}	

	context->is_lvalue = 1;
	if (context->load == NULL) {
		context->load = vr;
	}
	return 0;
}

static int
do_subscript(struct context *context, struct token *tok,
	struct icode_list *il, int eval) {
	struct expr		*ex;
	struct vreg		*exvr;
	struct type		*curtype = &context->curtype;

	if (curtype->code == 0) {
		errorfl(tok, "Parse error at `['");
		return -1;
	}	
	ex = tok->data;

	exvr = expr_to_icode(ex, NULL, il, 0, 0, eval);
	if (exvr == NULL) {
		return -1;
	}	

	if (exvr->type->tlist == NULL
		&& curtype->tlist == NULL) {
		errorfl(tok,
			"Array subscript operator applied to non-pointer type");
		return -1;
	} else if (exvr->type->tlist != NULL
		&& curtype->tlist != NULL) {
		errorfl(tok,
			"Index with bad type for array subscript operator");
		return -1;
	} else if (curtype->code == TY_VOID) {
		if (curtype->tlist != NULL
			&& curtype->tlist->next == NULL) {
			errorfl(tok, "Cannot subscript void pointers");
			return -1;
		}
	} else if ((exvr->type->tlist == NULL
		&& !is_integral_type(exvr->type))
		|| (curtype->tlist == NULL
			&& !is_integral_type(curtype))) {
		errorfl(tok, "Subscript not of integral type");
		return -1;
	}
	if (curtype->tlist == NULL) {
		/*
		 * 02/25/08: This is something silly like 0[array]
		 * instead of the usual array[0] - Reorder it
		 */
		struct type	*ptrtype = exvr->type;
		struct vreg	*tempvr = context->curitem;

		context->curitem = exvr;
		copy_type(&context->curtype, ptrtype, 0);
		exvr = tempvr;
	}

	if (eval) {
#if 0
		vreg_faultin(NULL, NULL, exvr, il, 0);
#endif
		/* XXX doesn't work for num[ptr] */
		if (promote(&exvr, NULL, 0, NULL, il, 1) == NULL) {
			return -1;
		}

		vreg_faultin_protected(exvr, NULL, NULL,
				context->curitem, il, 0);

		if (context->curitem->type->tlist->type == TN_ARRAY_OF) {
			context->curitem = vreg_disconnect(context->curitem);
			context->curitem->type = n_xmemdup(context->curitem->type,
				sizeof(struct type));
			copy_tlist(&context->curitem->type->tlist,
				context->curitem->type->tlist);
			context->curitem->type->tlist->type = TN_POINTER_TO;
			context->curitem->size =
				backend->get_sizeof_type(context->curitem->type, NULL);
		}
		do_add_sub(&context->curitem, &exvr, TOK_OP_PLUS, NULL, il, eval);
		free_pregs_vreg(exvr, il, 0, 0);
	}
	copy_type(&context->curtype, context->curitem->type, 0);
	context->curtype.storage = 0;
	copy_tlist(&context->curtype.tlist, context->curtype.tlist->next);
	context->is_lvalue = 1;
	context->var_lvalue = NULL;

#if 0
	if (context->curitem->from_ptr && eval) {
#endif
		/* Avoid chain of indirection */
#if 0
		vreg_anonymify(&context->curitem, NULL, NULL, il);
#endif
		context->curitem = vreg_disconnect(context->curitem);
#if 0
		if (context->curitem->type->tlist
			&& context->curitem->type->tlist->type == TN_ARRAY_OF) {
			context->curitem->type = addrofify_type(context->curitem->type); 
		}
#endif
#if 0
	}
#endif
	context->curitem
			= vreg_alloc(NULL, NULL, context->curitem, NULL);
	return 0;
}

#endif /* #ifndef PREPROCESSOR */


struct fcall_data *
alloc_fcall_data(void) {
#if USE_ZONE_ALLOCATOR
	return zalloc_buf(Z_FCALL_DATA);
#else
	struct fcall_data	*ret = n_xmalloc(sizeof *ret);
	static struct fcall_data	nullfdat;
	*ret = nullfdat;
	return ret;
#endif
}		

#ifndef PREPROCESSOR

static struct fcall_data *
do_func_call(struct token *meat, struct token **tok, int eval) {
	struct expr		*ex;
	struct expr		*args = NULL;
	struct expr		*argstail = NULL;
	struct token		*t = *tok;
	struct fcall_data	*fcall = NULL;
	int			nargs = 0;

	if (next_token(&t) != 0) {
		return NULL;
	}

	if (meat && meat->type == TOK_IDENTIFIER) {
		size_t	len  = sizeof "__builtin_" - 1;

		if (strncmp(meat->data, "__builtin_", len) == 0) {
			*tok = t;
			return get_builtin(tok, meat); 
		} else if (*(char *)meat->data == 'a'
			&& strcmp(meat->data, "alloca") == 0) {
			/*
			 * alloca() support SUCKS! Linux/BSD libraries
			 * assume __builtin_alloca() if __GNUC__ is
			 * defined. However, if we don't define it, it
			 * declares alloca() without prodiving a
			 * definition! So we have to catch that too
			 * because we occasionally have to undefine
			 * __GNUC__ (gcc itself cannot build programs
			 * using alloca() if we explicitly pass
			 * -U__GNUC__.)
			 */
			*tok = t;
			return get_builtin(tok, meat);
		 } 
	}
	
	for (;;) {
		if (t->type == TOK_PAREN_CLOSE) {
			/* Function call ends here */
#ifdef DEBUG3
			printf("Parsed function call with %d arguments\n",
				nargs);
#endif
			if (eval) {
				fcall = alloc_fcall_data();
				fcall->args = args;
				fcall->nargs = nargs;
			}	
			break;
		}

		ex = parse_expr(&t, TOK_OP_COMMA, TOK_PAREN_CLOSE, 0, 1);

		if (ex != NULL) {
			++nargs;
			append_expr(&args, &argstail, ex);
			if (t->type != TOK_PAREN_CLOSE) {
				/* Must be comma */
				if (next_token(&t) != 0) {
					return NULL;
				}
			}
		} else {
			return NULL;
		}
	}

	*tok = t;
	return fcall;
}

/*
 * Parses sizeof or alignof operator because they act exactly the same,
 * including the type of the result
 */
static int
do_sizeof(
	struct token *t, 
	struct token **endp,
	struct token **is_sizeof,
	int extype,
	int delim, int delim2) {

	struct token	*newconst = NULL;
	struct type	*restype;
	int		is_alignof = t->type == TOK_KEY_ALIGNOF;
	int		is_vla = 0;
	int		is_subex_lvalue = 0;
	struct s_expr	*subex = NULL;

	*is_sizeof = t;
	if (next_token(&t) != 0) {
		return -1;
	}
	if (t->type == TOK_PAREN_OPEN
		&& t->next
		&& IS_TYPE(t->next)) {
		struct decl	**d;
		
		/*
		 * sizeof(type-name)
		 */
		(void) next_token(&t);
			
		d = parse_decl(&t, DECL_CAST);
		if (d == NULL) {
			return -1;
		}	
		restype = d[0]->dtype;
		if (IS_VLA(restype->flags)) {
			is_vla = FLAGS_VLA;
			(*is_sizeof)->type = TOK_SIZEOF_VLA_EXPR;
			(*is_sizeof)->data = d[0]->dtype;
		}
		free(d);

		/* Skip ``)'' */
		if (next_token(&t) != 0) {
			return -1;
		}
	} else {
		/* sizeof sub-expr */
		subex = get_sub_expr(&t, delim, delim2, 0);
		if (subex == NULL) {
			return -1;
		}

		/* Don't evaluate! */
		if (s_expr_to_icode(subex, NULL, NULL, 0, 0) == NULL) {
			return -1;
		}	
		if (subex->type == NULL) {
			return -1;
		}	
		is_subex_lvalue = subex->is_lvalue;
		restype = subex->type;
		if (IS_VLA(restype->flags)) {
			is_vla = FLAGS_VLA;
			(*is_sizeof)->type = TOK_SIZEOF_VLA_EXPR;
			(*is_sizeof)->data = subex;
		}
	}
	*endp = t;

	if (restype->tlist
		&& restype->tlist->type == TN_FUNCTION) {	
		errorfl(*is_sizeof,
			"Cannot take size of function");
		return -1;
	} else if (restype->tbit != NULL) {
		/*
		 * 09/24/08: Only disallow sizeof for lvalues! So things like
		 *    sizeof s.member
		 *
		 * are illegal but
		 *
		 *    sizeof (s.member = 0)
		 *
		 * is legal. Likewise with comma operator
		 */
		if (is_subex_lvalue) {
			errorfl(*is_sizeof,
				"Cannot take size of bitfield");
			return -1;
		}
	}
	if (restype->incomplete
		|| (restype->tstruc != NULL	
		&& restype->tstruc->incomplete
		&& is_basic_agg_type(restype))) {
		errorfl(*is_sizeof,
			"Cannot take size of incomplete type");
		return -1;
	}

	if (!is_vla) {
		newconst = const_from_type(restype, is_alignof, extype, *is_sizeof);
		(*is_sizeof)->data = vreg_alloc(NULL, newconst, NULL, NULL);
	}
	return 0;
}


static int
do_addr_label(struct context *context,
	struct token *meat,
	int extype,
	struct icode_list *il,
	int eval) {

	struct label	*l;
	struct vreg	*vr;

	(void) extype;

	/*
	 * Now that the whole function has been read (and icode translation is
	 * taking place), we can check whether the label actually exists
	 */

	l = lookup_label(curfunc, meat->data);
	if (l == NULL) {
		errorfl(meat, "Label `%s' does not exist", (char *)meat->data);
		return -1;
	}

	vr = vreg_alloc(NULL,NULL,NULL,make_void_ptr_type());
	if (eval) {
		struct reg	*r;

		r = ALLOC_GPR(curfunc, backend->get_ptr_size(), il, NULL);
		icode_make_load_addrlabel(r, l->instr, il);
		vreg_map_preg(vr, r);

		context->load = vr;
	} else {
		;
	}
	context->curitem = vr;
	copy_type(&context->curtype, vr->type, 0);

	return 0;
}



static struct decl * 
prepare_implicit_fcall(struct token *t) {
	int		is_builtin = 0;
	struct decl	*dec;

	if (strcmp(t->data, "malloc") == 0
		|| strcmp(t->data, "realloc") == 0
		|| strcmp(t->data, "calloc") == 0) {
		warningfl(t,
			"Incorrect call to `%s' without "
			"declaration, please `#include "
			"<stdlib.h>'", t->data);
	} else if (strncmp(t->data, "__builtin_",
		sizeof "__builtin_" - 1) != 0) {	
		warningfl(t,
	"Call to `%s' without declaration (illegal in C99)",
			t->data);
	} else {
		is_builtin = 1;
	}	

	if (!is_builtin) {
		dec = put_implicit(t->data);
	} else {
		return NULL;
	}
	return dec;
}




static struct icode_instr *
do_identifier(
	struct context *context,
	struct token *t,
	int extype,
	struct decl **is_func_call,
	int *err,
	int eval) {

	struct decl		*dec;
	struct type		*curtype = &context->curtype;
	struct icode_instr	*ret = NULL;
	struct vreg		*vr;
	int			is_implicit = 0;

	/*
	 * 04/09/08: Now the identifier has already been looked up
	 * by get_sub_expr() and stored in the data2 member. Note
	 * that it may still be NULL too!
	 */
#if ! IMMEDIATE_SYMBOL_LOOKUP
	dec = access_symbol(curscope, t->data, 1);
#else
	dec = t->data2;
	/*
	 * 12/24/08: Use lookup_symbol() for unevaluated expressions. E.g.
	 * using ``sizeof var'' does not really constitute ``use'' of var,
	 * it just reads and uses the type. The difference is important for
	 * declared but undefined extern variables on PPC where we'll get
	 * linker errors for references to externs which don't exist
	 */
	if (eval) {
		/* Bump count */
		(void) access_symbol(curscope, t->data, 1);
	}
#endif

	*err = 1;

	if (is_func_call) {
		if ((*is_func_call = dec) == NULL) {
			/*
			 * Call to an undeclared (implicitly
			 * int in C89) or builtin function.
		 	 */
#if 0
			int	is_builtin = 0;

			if (strcmp(t->data, "malloc") == 0
				|| strcmp(t->data, "realloc") == 0
				|| strcmp(t->data, "calloc") == 0) {
				warningfl(t,
					"Incorrect call to `%s' without "
					"declaration, please `#include "
					"<stdlib.h>'", t->data);
			} else if (strncmp(t->data, "__builtin_",
				sizeof "__builtin_" - 1) != 0) {	
				warningfl(t,
			"Call to `%s' without declaration (illegal in C99)",
					t->data);
			} else {
				is_builtin = 1;
			}	

			if (!is_builtin) {
				dec = put_implicit(t->data);
				is_implicit = 1;
			} else {
				*err = 0;
				return NULL;
			}
#endif

#if ! IMMEDIATE_SYMBOL_LOOKUP
			/*
			 * 05/13/09: This has to be done while parsing too, or
			 * else
			 *
			 *     { int (*p)(); puts("hello"); p = puts; }
			 *
			 * won't work if puts() is implicit, because the
			 * implicit declaration will only be introduced during
			 * icode translation, which is too late
			 */
			if ((dec = prepare_implicit_fcall(t)) == NULL) {
				*err = 0;
				return NULL;
			}
			is_implicit = 1;
			*is_func_call = dec;
#else
			*err = 0;
			return NULL;
#endif
		} else if (0 /* is not really a function!~*/) {
			/* XXX handle */
		} else if (dec->dtype->implicit) {
			is_implicit = 1;
		}	
		*err = 0;
	}

	if (dec == NULL) {
		if (/*(dec=lookup_undeclared(curscope,t->data,1))==NULL*/1) {
			errorfl(t, "Undeclared identifier `%s'",
				(char *)t->data);
		}
		return NULL;
	} else if (extype == EXPR_CONST) {
		if (*is_func_call) {
			errorfl(t, "Function call in constant expression");
		} else {
			errorfl(t, "Use of variable in constant expression");
		}
		return NULL;
	} else if (extype == EXPR_CONSTINIT
		|| extype == EXPR_OPTCONSTINIT
		|| extype == EXPR_OPTCONSTARRAYSIZE
		|| extype == EXPR_OPTCONSTSUBEXPR) {

		if (t->prev->type != TOK_OPERATOR
			|| *(int *)t->prev->data != TOK_OP_ADDR) {
#if 0
			if (*is_func_call) {
				/*
				 * This may be a call to a builtin function
				 * which yields a constant result, such as
				 * __builtin_offsetof(). In that case we
				 * want to evaluate the value as a constant
				 * expression
				 */
			}
#endif
			if (extype == EXPR_CONSTINIT) {
				if (*is_func_call) {
					errorfl(t,
					"Function call in constant expression");
				} else {
					errorfl(t, "Use of variable in constant "
					"expression");
				}
				return NULL;
			} else if (extype == EXPR_OPTCONSTINIT) {
				warningfl(t, "Variable array/struct "
				"initializers are not allowed in ISO "
				"C90 (only GNU C and C99)");
			} else if (extype == EXPR_OPTCONSTSUBEXPR) {
				;
			} else {
				/* OPTCONSTARRAYSIZE */
				warningfl(t, "Variable-size arrays "
				"are not allowed in ISO C90 (only GNU "
				"C and C99)");
			}
		}
		if (dec->dtype->storage != TOK_KEY_STATIC
			&& dec->dtype->storage != TOK_KEY_EXTERN) {
			if (extype == EXPR_CONSTINIT) {
				errorfl(t,
			"Address of non-static variable taken in constant initializer");
				return NULL;
			} else {	
				warningfl(t, "Variable array/struct "
				"initializers are not allowed in ISO "
				"C90 (only GNU C and C99)");
			}
		}
	} else {
		/*
		 * in
		 * int foo; foo = 0; ... the second foo identifier can now
		 * be freed. This is not possible in case of a static variable,
		 * because it will have a different name (such as _Static_foo0)
		 */
		if (dec->dtype->storage != TOK_KEY_STATIC
			&& !is_implicit) {
			/* XXX this breaks with errorfl() for some reason!!
			 * see bs.c with valgrind
			 */
#if 0
			free(t->data);
			t->data = dec->dtype->name;
#endif
		}
	}

	context->var_lvalue = dec;
	context->is_lvalue = 1;


	copy_type(curtype, dec->dtype, 0);

	if (eval) {
		*err = 0;
		/*
		 * 03/26/08: Previously if this identifier was an enum
		 * constant, dec->vreg would already be cached with the
		 * correct value. Now that dec->vreg doesn't exist anymore,
		 * we have to distinguish at this point
		 */
		if (dec->dtype->tenum != NULL
			&& dec->tenum_value != NULL) {
			vr = vreg_alloc(NULL, dec->tenum_value, NULL, NULL);
			/*
			 * XXX is this really cross-safe with the token
			 * value and TY_INT?
			 */
			vr->type = n_xmemdup(make_basic_type(TY_INT),
				sizeof *vr->type);
			vr->size = backend->get_sizeof_type(vr->type, NULL);
		} else {
			vr = vreg_alloc(dec, NULL, NULL, NULL);
		}
#if 1   /*USE_ZONE_ALLOCATOR*/
		/*
		 * Do not save the vreg to ``save'' memory! That
		 * will break because the data structures are 
		 * reused, so the vreg member in struct decl must
		 * go
		 *
		 * CANOFWORMS
		 */
		context->load = context->curitem = vr;
#else
		context->load = dec->vreg = context->curitem = vr;
#endif
	} else {
		*err = 0; /* XXX ...?! */
		vr = vreg_alloc(NULL, NULL, NULL, dec->dtype);
		context->curitem = vr;
		return NULL;
	}	
	return ret;
}

static void
do_constant(
	struct context *context,
	struct token *t,
	int eval) {

	struct type		*curtype = &context->curtype;

	struct type		*ctype;
	struct vreg		*vr;

	ctype = make_basic_type(t->type);
	copy_type(curtype, ctype, 0);
	if (eval) {
		vr = vreg_alloc(NULL, t, NULL, NULL);
		context->load = context->curitem = vr;
	} else {
		vr = vreg_alloc(NULL, NULL, NULL, ctype);
		context->curitem = vr;
	}
}


static struct vreg *
do_string(struct context *context, struct token *t, int eval) {
	struct type	*curtype = &context->curtype;
	struct vreg	*vr;

	copy_type(curtype, ((struct ty_string *)t->data)->ty, 0);
	context->is_lvalue = 1;

	if (eval) {
		/*
		 * XXX 10/30/07: Hmmm, t will get the size of the
		 * string, not the pointer size, is this always
		 * desirable? It seems ``decay'' of string constants
		 * are not handled e.g. when loading a string
		 * address into a register to call a function.
		 * Check by aborting on vr->size != pr->size in
		 * vreg_map_preg() and call fputs("hello", stdout)
		 */
		vr = vreg_alloc(NULL, t, NULL, NULL);
		return context->load = context->curitem = vr;
	} else {
		vr = vreg_alloc(NULL, NULL, NULL, NULL);
		vr->size = backend->get_sizeof_type(curtype, NULL);
		vr->type = ((struct ty_string *)t->data)->ty;
		return context->curitem = vr;
	}	
}


/*
 * Increment/decrement current item. If we're dealing with postfix
 * increment/decrement operators, the result will be stored to the
 * item backing the virtual register we're operating on and then the
 * inc/dec step is reversed such that the old value of that vreg
 * will be used in the expression. This is sort of slow and kludgy 
 * but sort of works ok for now
 */
static void
do_inc_dec(struct context *context, int op, struct icode_list *il,
	struct token *optok,	
	int is_standalone,
	int eval) {

	struct icode_instr	*ii = NULL;

	(void) eval;
	/* XXXXXXXXXXXXXXXXXXX use eval! */
	if (context->curtype.tlist != NULL) {
		/*int*/ long	factor =
			backend->get_sizeof_elem_type(
				&context->curtype);	
		struct token	*tmp;
		struct type	*tytmp;
		struct vreg	*tmpvr = vreg_alloc(NULL, NULL, NULL, NULL);

		if (context->curtype.code == TY_VOID) {
			/* if (! GNUC) {
			errorfl(optok, "Cannot "
				"perform pointer arithmetic "
				"on void pointers (cast to "
				"`(char *)' instead!)");
			} else { */
				warningfl(optok, "Pointer arithmetic "
					"on void pointers is a GNU C "
					"extension (you should cast "
					"to `(char *)' instead!)");

		}

		tmp = const_from_value(&factor, /*NULL*/make_basic_type(TY_LONG));
		tmpvr->from_const = tmp;
		tytmp = make_basic_type( /*TY_INT*/  TY_LONG);
		tmpvr->type = n_xmemdup(tytmp, sizeof *tytmp);
		tmpvr->size = backend->get_sizeof_type(tytmp, NULL);
		vreg_faultin(NULL, NULL, tmpvr, il, 0);
		vreg_faultin_protected(tmpvr, NULL, NULL,
			context->curitem, il, 0);

		/* XXX this stuff sucks... changing the tmpvr from int to long
		 * is better for 64bit systems..but the real solution is
		 * probbly to use ptrarit()!?
		 */
		if (op == TOK_OP_INCPRE
			|| op == TOK_OP_INCPOST) {
			ii = icode_make_add(context->curitem, tmpvr);
		} else {
			ii = icode_make_sub(context->curitem, tmpvr);
		}
		append_icode_list(il, ii);

		icode_make_store(curfunc, context->curitem,
			context->curitem, il);

		/*
		 * Reverse effect to get previous value in the surrounding
		 * expression.
		 *
		 * 04/12/08: Only do it if this isn't a standalone sub-
		 * expression such as ``i--;''
		 */
		if (!is_standalone || Oflag == -1) {
			if (op == TOK_OP_INCPOST) {
				ii = icode_make_sub(context->curitem, tmpvr);
				append_icode_list(il, ii);
			} else if (op == TOK_OP_DECPOST) {
				ii = icode_make_add(context->curitem, tmpvr);
				append_icode_list(il, ii);
			}
		}
	} else {
		if (context->curitem->type->code == TY_BOOL) {
			/* Ooops */
			struct reg	*r = NULL;
			struct reg	*oldvalue;

			/*
			if (lang == cplusplus) {
				if (IS_DECREMENT) {
					error("cannot decrement booolllllllllol");
				lol
				}
			}
			*/	

			vreg_faultin(NULL, NULL, context->curitem, il, 0);
			oldvalue = context->curitem->pregs[0];
			if (op == TOK_OP_INCPOST
				|| op == TOK_OP_DECPOST) {
				/* New value goes into r */
				r = ALLOC_GPR(curfunc, 1, il, NULL);
				icode_make_copyreg(r, oldvalue,
					context->curitem->type,
					context->curitem->type, il);
				/* XXX copyreg shouldn't set used */
				vreg_map_preg(context->curitem, oldvalue);
			}	

			if (op == TOK_OP_INCPRE
				|| op == TOK_OP_INCPOST) {
				/* ++ always sets to 1 */
				ii = icode_make_setreg(r? r: oldvalue, 1);
				append_icode_list(il, ii);
			} else {
				/* decrement - wraps around :( */
				struct icode_instr	*label;
				struct icode_instr	*endlabel;

				label = icode_make_label(NULL);
				endlabel = icode_make_label(NULL);
				ii = icode_make_cmp(context->curitem, NULL);
				append_icode_list(il, ii);
				ii = icode_make_branch(label, INSTR_BR_EQUAL,
					context->curitem);
				append_icode_list(il, ii);
				ii = icode_make_setreg(r? r: oldvalue, 0);
				append_icode_list(il, ii);
				ii = icode_make_jump(endlabel);
				append_icode_list(il, ii);
				append_icode_list(il, label);
				ii = icode_make_setreg(r? r: oldvalue, 1);
				append_icode_list(il, ii);
				append_icode_list(il, endlabel);

			}
			vreg_map_preg(context->curitem, r? r: oldvalue); 
			icode_make_store(curfunc, context->curitem,
				context->curitem, il);
			if (r != NULL) {
				free_preg(r, il, 1, 0);
				icode_make_copyreg(oldvalue, r,
					context->curitem->type,
					context->curitem->type, il);
				vreg_map_preg(context->curitem, r);
			}
		} else {
			/*
			 * 08/22/07: Allow floating point increment/decrement
			 */
			struct vreg	*fp_one = NULL;
			struct token	*otok;
			struct vreg	*orig_curitem = context->curitem;
			int		x87_trash = 0;
			int		is_bitfield = 0;

			if (is_floating_type(context->curitem->type)) {
				otok = fp_const_from_ascii("1.0",
					context->curitem->type->code);	
				fp_one = vreg_alloc(NULL, otok, NULL, NULL);

				if (!is_x87_trash(context->curitem)) {
					vreg_faultin(NULL, NULL, context->curitem,
						il, 0);
					reg_set_unallocatable(
						context->curitem->pregs[0]);
					vreg_faultin(NULL, NULL, fp_one, il, 0);
					reg_set_allocatable(
						context->curitem->pregs[0]);
				} else {
					x87_trash = 1;
				}	
			} else {
				vreg_faultin(NULL, NULL, context->curitem, il, 0);
			}	

			if (op == TOK_OP_INCPRE
				|| op == TOK_OP_INCPOST) {
				if (fp_one) {
					if (x87_trash) {
						context->curitem =
							x87_do_binop(fp_one,
					context->curitem, TOK_OP_PLUS, il);
					} else {
						ii = icode_make_add(context->curitem,
							fp_one);
					}
				} else {
					ii = icode_make_inc(context->curitem);
				}
			} else {
				if (fp_one) {
					if (x87_trash) {
						context->curitem =
							x87_do_binop(fp_one,
					context->curitem, TOK_OP_MINUS, il);
					} else {	
						ii = icode_make_sub(context->curitem,
							fp_one);
					}
				} else {	
					ii = icode_make_dec(context->curitem);
				}	
			}
			if (!x87_trash) {
				append_icode_list(il, ii);
			}

			/*
			 * 08/12/08: Apply bitmask for bitfields!
			 * 
			 *    unsigned foo:1;
			 *
			 * ...   foo = 1; ++foo;
			 *
			 * Should yield
			 *
			 *    (foo + 1) & 0x1
			 */
			if (context->var_lvalue != NULL
				&& context->var_lvalue->dtype->tbit != NULL) {
				is_bitfield = 1;
				mask_source_for_bitfield(context->var_lvalue->dtype,
					context->curitem, il, 0);
			}
	
			if (x87_trash) {
				vreg_faultin_x87(NULL, NULL, context->curitem,
					il, 0);
				vreg_map_preg(orig_curitem,
					context->curitem->pregs[0]);
				icode_make_store(curfunc, orig_curitem,
					orig_curitem, il);
			} else {	
				icode_make_store(curfunc, context->curitem,
					context->curitem, il);
			}	
	
			/*
			 * Reverse effect to get previous value in the
			 * surrounding expression.
		 	 * 04/12/08: Only do it if this isn't a standalone
			 * sub-expression such as ``i--;''
		 	*/
			if (!is_standalone || Oflag == -1) {
				if (op == TOK_OP_INCPOST) {
					if (fp_one) {
						if (x87_trash) {
							context->curitem =
								x87_do_binop(
									fp_one,
								context->curitem,
								TOK_OP_MINUS, il);
						} else {	
							ii = icode_make_sub(
								context->curitem,
								fp_one);
						}
					} else {
						ii = icode_make_dec(
							context->curitem);
					}
					if (!x87_trash) {
						append_icode_list(il, ii);
					}	
				} else if (op == TOK_OP_DECPOST) {
					if (fp_one) {
						if (x87_trash) {
							context->curitem =
								x87_do_binop(
										fp_one,
								context->curitem,
								TOK_OP_PLUS,
								il);
						} else {	
							ii = icode_make_add(
								context->curitem,
								fp_one);
						}
					} else {
						ii = icode_make_inc(context->curitem);
					}

					if (!x87_trash) {
						append_icode_list(il, ii);
					}	
				}
				if (is_bitfield) {
					/*
					 * 08/12/08: We have to mask another
					 * time, since wrap-around is not
					 * handled naturally! E.g. in
					 *
					 *    unsigned val:2;
					 *    val = 0;
					 *    use(val--);
					 *
					 * ... the inc after the dec will yield
					 * value 4, so we have to compensate
					 * for it by ANDing
					 */
					mask_source_for_bitfield(context->
						var_lvalue->dtype,
						context->curitem, il, 0);
				}
			}
		}
	}	
	vreg_anonymify(&context->curitem, NULL, NULL, il);
	context->is_lvalue = 0;
}

static int 
unary_to_icode(struct context *context, struct token *op, int extype,
struct icode_list *il, int is_standalone, int eval) {
	struct type		*curtype = &context->curtype;
	int			opval = *(int *)op->data;
	int			is_x87 = 0;
	int			is_void_expr;
	int			is_fp = 0;
	struct icode_instr	*ii = NULL;
	struct vreg		*vr;
	struct reg		*r = NULL;
	struct operator		*operator;
	struct icode_instr	*label;
	struct type		*tytmp;
	struct type		*oldtype = context->curitem->type;
	struct vreg		*temp_curitem = NULL;

	operator = &operators[LOOKUP_OP2(opval)];

	if (is_floating_type(&context->curtype)) {
		if (opval != TOK_OP_LNEG
			&& opval != TOK_OP_UPLUS
			&& opval != TOK_OP_UMINUS
			&& opval != TOK_OP_ADDR
			&& opval != TOK_OP_INCPOST
			&& opval != TOK_OP_INCPRE
			&& opval != TOK_OP_DECPOST
			&& opval != TOK_OP_DECPRE) {
			errorfl(op, "`%s' operator applied to floating "
				"point type", op->ascii);
			return -1;
		}
		if (is_x87_trash(context->curitem)) {
			is_x87 = 1;
		}
		is_fp = 1;
	}

	if (IS_INCDEC_OP(opval)) {
		if (extype == EXPR_CONST || extype == EXPR_CONSTINIT) {
			errorfl(op, "Increment/decrement "
				"operators are not allowed in "
				"constant %ss",
			extype == EXPR_CONST? "expression"
				: "initializer");
			return -1;
		} else if (extype == EXPR_OPTCONSTINIT) {
			warningfl(op, "Variable array/struct "
				"initializers are not allowed in ISO "
				"C90 (only GNU C and C99)");
		} else if (extype == EXPR_OPTCONSTARRAYSIZE) {
			warningfl(op, "Variable-size arrays "
				"are not allowed in ISO C90 (only GNU "
				"C and C99)");
		} else if (extype == EXPR_OPTCONSTSUBEXPR) {
			;
		}
		if (is_basic_agg_type(curtype)) {
			errorfl(op, "Operator cannot be applied to basic "
				"aggregate types");
			return -1;
		} else if (!is_modifyable(curtype)) {
			errorfl(op, "Operator cannot be applied to const-"
				"qualified operands");
			return -1;
		}
	}


	/*
	 * 08/09/08: Do promotions for all operators that require this here!
	 * That was already correctly done for logical negation and unary
	 * plus, but not for bitwise negation and unary minus
	 */
	if (opval == TOK_OP_LNEG || opval == TOK_OP_BNEG
		|| opval == TOK_OP_UPLUS || opval == TOK_OP_UMINUS) {
		tytmp = promote(&context->curitem, NULL, opval,
			op,
			eval? (void *)il: (void *)NULL, eval);
		if (tytmp == NULL) {
			return -1;
		}
		if (tytmp != oldtype) { 
			copy_type(&context->curtype, tytmp, 0); 
		}	
	}


	/*
	 * 11/24/08: Handle emulated long double! If we're not using the address-of
	 * or bitwise negation operator (invalid for fp anyway), we may wish to
	 * convert a long double operand to a double before performing the operation
	 */
	if (opval != TOK_OP_ADDR
		&& opval != TOK_OP_DEREF
		&& opval != TOK_OP_BNEG) {

		struct vreg	*temp_curitem;
		int		changed_vr;

		/*
		 * XXX This doesn't work with long double increment/decrement yet,
		 * because that one requires us to convert back and save the changes!
		 * Setting is_lvalue below will make this yield an lvalue error if
		 * we do encounter it for a backend that requires long double
		 * emulation
		 */
		changed_vr =  emul_conv_ldouble_to_double(&temp_curitem, NULL,
			context->curitem, NULL, il, eval);

		if (changed_vr) {
			context->curitem = temp_curitem;
			copy_type(&context->curtype, temp_curitem->type, 0);
			context->is_lvalue = 0;
		}
	}

	switch (opval) {
	case TOK_OP_DECPRE:
	case TOK_OP_INCPRE:
		if (!context->is_lvalue) {
			errorfl(op, "Pre-%s operator applied "
				"to something that is not an lvalue",
				opval == TOK_OP_INCPRE?
					"increment": "decrement");
			return -1;
		}
		if (eval) {
			do_inc_dec(context, opval, il, op, is_standalone, eval);
		}
		context->is_lvalue = 0;
		break;
	case TOK_OP_INCPOST:
	case TOK_OP_DECPOST:
		if (!context->is_lvalue) {
			errorfl(op, "Post-%s operator applied "
				"to something that is not an lvalue",
				opval == TOK_OP_INCPOST?
					"increment": "decrement");
			return -1;
		}
		if (eval) {
			do_inc_dec(context, opval, il, op, is_standalone, eval);
		}	
		context->is_lvalue = 0;
		break;
	case TOK_OP_LNEG:
		/* 
		 * XXX as with the short circuit and relation operators,
		 * use conditional set instructions instead!
		 */
		if (eval) {
			r = ALLOC_GPR(curfunc, backend->get_sizeof_type(
				make_basic_type(TY_INT), NULL), il, NULL);
			reg_set_unallocatable(r);

			/* XXX hmm x87 stuff not encapsulated */
			vreg_faultin_x87(NULL, NULL, context->curitem, il, 0);
			vreg_anonymify(&context->curitem, NULL, NULL, il);
			reg_set_allocatable(r);

			ii = icode_make_setreg(r, 0);
			append_icode_list(il, ii);
#if 0
			ii = icode_make_cmp(context->curitem, NULL);
#endif
			ii = compare_vreg_with_zero(context->curitem, il);
			append_icode_list(il, ii);

			if (!context->curitem->is_multi_reg_obj) {
				free_pregs_vreg(context->curitem, il, 0, 0);
			}	

			/* If result zero, set to 1 */
			label = icode_make_label(NULL);
			ii = icode_make_branch(label, INSTR_BR_NEQUAL,
				context->curitem);
			append_icode_list(il, ii);

			if (context->curitem->is_multi_reg_obj) {
				ii = icode_make_cmp(context->curitem, NULL);
				append_icode_list(il, ii);
				ii = icode_make_branch(label, INSTR_BR_NEQUAL,
					context->curitem);
				append_icode_list(il, ii);
			}

			ii = icode_make_setreg(r, 1);
			append_icode_list(il, ii);
			append_icode_list(il, label);
		}

		context->is_lvalue = 0;
		context->curitem = vreg_alloc(NULL, NULL, NULL, NULL);

		if (eval) {
			vreg_map_preg(context->curitem, r);
		}	
		context->curitem->type = make_basic_type(TY_INT);
		copy_type(&context->curtype, context->curitem->type, 0);
		context->curitem->size =
			backend->get_sizeof_type(&context->curtype, NULL);
		ii = NULL;
		
		break;
	case TOK_OP_UPLUS:
	case TOK_OP_UMINUS:
		/*
		 * Operand must have arithmetic type (ie. integral or
		 * floating point) 
		 */
		if (!is_arithmetic_type(curtype)) {	
			errorfl(op, "Operand of `%s' operator must have "
				"arithmetic type", operator->name);
			return -1;
		}	
		if (opval == TOK_OP_UPLUS) {
			/* 
			 * This operator does not do anything except
			 * for causing integral promotion - it only
			 * exists for symmetry with unary ``-''
			 */

			if (is_x87 && eval) {
				context->curitem = x87_do_unop(context->curitem,
					TOK_OP_UPLUS, il);
				tytmp = oldtype;
			} else {
				vreg_anonymify(&context->curitem,NULL,NULL,il);
			}
#if 0
			if (tytmp != oldtype) { 
				copy_type(&context->curtype, tytmp, 0); 
			}	
#endif
		} else {
			/* Minus */
			if (is_x87 && eval) {
				context->curitem = x87_do_unop(context->curitem,
					TOK_OP_UMINUS, il);
			} else {
				if (eval) {
					vreg_faultin(NULL, NULL, context->curitem,il,0);
				}	
				vreg_anonymify(&context->curitem,NULL,NULL,il);
				pro_mote(&context->curitem, il, eval);
				if (eval) {
					if ((backend->arch == ARCH_AMD64
						|| sysflag == OS_OSX)
						&& is_floating_type(context->
							curitem->type)
						&& !is_x87) {
						struct reg	*temp;
						
						temp = backend->alloc_fpr(
							curfunc,
							context->curitem->size,
							il,
							NULL);
						icode_make_amd64_load_negmask(
							temp,
							context->curitem->type
							->code == TY_DOUBLE,
							il);
	if (context->curitem->type->code == TY_DOUBLE) {
						icode_make_amd64_xorpd(
							context->curitem,
							temp, il);
	} else {	
						icode_make_amd64_xorps(
							context->curitem,
							temp, il);
	}				
						free_preg(temp, il, 1, 0);
					} else {
						ii = icode_make_neg(
							context->curitem);
					}
				}
			}
		}	
		context->is_lvalue = 0;
		break;
	case TOK_OP_BNEG:
		if (!is_integral_type(curtype)) {
			errorfl(op, "Operand of `%s' operator must have "
				"arithmetic type", operator->name);
			return -1;
		}
		if (eval) {
			vreg_faultin(NULL, NULL, context->curitem, il, 0);
		}	
		vreg_anonymify(&context->curitem, NULL, NULL, il);
		pro_mote(&context->curitem, il, eval);
		if (eval) {
			ii = icode_make_not(context->curitem);
			context->curitem = n_xmemdup(context->curitem,
				sizeof *context->curitem);
			vreg_map_preg(context->curitem, context->curitem->pregs[0]);
			if (context->curitem->is_multi_reg_obj) {
				vreg_map_preg2(context->curitem,
					context->curitem->pregs[1]);
			}	
		}
		context->is_lvalue = 0;
		break;
	case TOK_OP_ADDR:
		if (!context->is_lvalue) {
			errorfl(op, "Address-of operator applied "
				"to something that is not an lvalue");
			return -1;
		} else if (context->curtype.storage == TOK_KEY_REGISTER) {
			warningfl(op, "Address-of operator applied to "
				"`register' variable");	
		} else if (context->curtype.tbit != NULL) {
			errorfl(op, "Cannot take address of bitfield");
			return -1;
		}
		context->is_lvalue = 0;
		context->var_lvalue = NULL;
		if (eval) {
			struct vreg		*vr2 = NULL;
			
			if (context->curitem->parent) {
				vr2 = get_parent_struct(context->
						curitem);
				if (vr2->from_ptr) {
					vreg_faultin(NULL, NULL,
						vr2->from_ptr, il, 0);
					
				}
			}	
			
			ii = icode_make_addrof(NULL, context->curitem,
				il);
			append_icode_list(il, ii);
			if (vr2 && vr2->from_ptr) {
				free_preg(vr2->from_ptr->pregs[0],
					il, 0, 0);
			}	
		}	

		vr = vreg_alloc(NULL, NULL, NULL, NULL);
		if (eval) {
			vreg_map_preg(vr, ii->dat);
		}
		vr->type = addrofify_type(&context->curtype);

		/* 02/02/08: Kill thread flag.. any others? */
		vr->type->flags &= ~FLAGS_THREAD;
		vr->size = backend->
			get_sizeof_type(vr->type, NULL);
		context->curtype.tlist = /*tn*/vr->type->tlist;
		
		ii = NULL;
		context->curitem = vr;
		break;
	case TOK_OP_DEREF:
		if (curtype->tlist == NULL) {
			errorfl(op,
			"Attempt to dereference something that is not "
			"a pointer");
			return -1;
		}
		if (curtype->tlist->type == TN_FUNCTION
			|| (curtype->tlist->next
			&& curtype->tlist->next->type == TN_FUNCTION)) {
			/* (*f)(); is the same as f(); */
			break;
		}	
		if (curtype->code == TY_VOID
			&& curtype->tlist->next == NULL) {
			/* Dereferencing void pointer - Useless but harmless */
			is_void_expr = 1;
		} else {
			is_void_expr = 0;
		}

		if (curtype->incomplete
			|| (curtype->tstruc
			&& curtype->tstruc->incomplete
			&& curtype->tlist->next == NULL)) {
			errorfl(op,
			"Cannot dereference pointer to incomplete type");
			return -1;
		}

		/*
		 * XXX we need to pay more attention to storage class
		 * stuff when modifying types... This was just made
		 * aparent by an error message about taking the address
		 * of     
		 *          register char *p;   &p[i];
		 * ... here p[i] shouldn't be register-specified!
		 */
		context->curtype.storage = 0;
		copy_tlist(&context->curtype.tlist,
			context->curtype.tlist->next);	
		if (is_void_expr) {
			context->is_lvalue = 0;
		} else {	
			context->is_lvalue = 1;
		}
		context->var_lvalue = NULL;
		if (eval && !is_void_expr) {
			struct reg	*r;
			/* Fault in pointer */

			r = vreg_faultin(NULL, NULL, context->curitem, il, 0);
			ii = icode_make_indir(r);
			append_icode_list(il, ii);
			context->indir = 1;
			ii = NULL;
		}

		/*
		 * If the parent pointer also came from a pointer, it must
		 * be anonymified to avoid long chains of indirection
		 */
		if (context->curitem->from_ptr && eval && !is_void_expr) {
			vreg_anonymify(&context->curitem, NULL, NULL, il);
		}
		context->curitem
			= vreg_alloc(NULL, NULL, context->curitem, NULL);
		break;
	default:
		puts("WHAT??????????????");
		printf("%d\n", opval);
		abort();
	}
	
	if (ii != NULL && eval) {
		append_icode_list(il, ii);
	}

	return 0; 
}

static int 
do_cast(struct context *context, struct token *cast, 
	struct icode_list *il, int eval) {

	struct decl		*d = cast->data;
	struct type		*ty = d->dtype;
	struct type		*curtype = &context->curtype;
	int			is_ptr_cast = 0;

	if (curtype->tlist != NULL) {
		/* Possibly dangerous cast */
	
		if (ty->tlist == NULL && ty->code != TY_VOID) {
			if (ty->code == TY_FLOAT
				|| ty->code == TY_DOUBLE
				|| ty->code == TY_LDOUBLE) {
				errorfl(cast, "Invalid cast of pointer "
						"to floating point type");
				return 1;
			} else if (ty->code != TY_ULONG
				&& ty->code != TY_ULLONG
				&& pedanticflag) {
				warningfl(cast, 
					"Dangerous cast of pointer to"
					" %s type, use "
					"`unsigned long' to improve "
					"portability",
					ty->code == TY_LONG? "signed long"
						: "small integral");
			}
		} else if (ty->tlist != NULL) {
			/* IMPORTANT: Depends on type order in token.h! */
			is_ptr_cast = 1;
			if (ty->code > curtype->code
				&& ty->code != TY_VOID
				&& curtype->code != TY_VOID
				&& (IS_CHAR(ty->code) /* XXX kludge ... */
					!= IS_CHAR(curtype->code)
				|| IS_SHORT(ty->code)
					!= IS_SHORT(curtype->code)
				|| IS_INT(ty->code)
					!= IS_INT(curtype->code)
				|| IS_LONG(ty->code)
					!= IS_LONG(curtype->code)
				|| IS_LLONG(ty->code)
					!= IS_LLONG(curtype->code)
				|| IS_FLOATING(ty->code))) {
				if (pedanticflag) {
					warningfl(cast,
					"Cast to pointer type with possibly "
					"higher alignment constraints");
				}	
			}	
		}
	} else if (curtype->tbit != NULL) {
		/*
		 * 09/21/08: Promote bitfield! Otherwise something like
		 *
		 *    (unsigned)s.bf
		 *
		 * ... will go from bitfield storage unit to unsigned int
		 * without decoding the value by shifting and masking
		 *
		 * XXX This is ugly, but e.g. putting decode/promote_
		 * bitield() into vreg_faultin() has other problems (like
		 * when loading a bitfield storage unit in order to OR it
		 * with a bitfield value and write it back to memory, we
		 * don't want to decode it automatically)
		 */
		if (eval) {
			/*
			 * 05/25/09: This does not really seem to change the
			 * type, it just loads and decodes the bitfield.
			 * That's not possible if we're not evaluating of
			 * course
			 */
			context->curitem = promote_bitfield(context->curitem, il);
		}
		copy_type(curtype, context->curitem->type, 0);
	}

	if (eval) {
		context->curitem =
			backend->icode_make_cast(context->curitem, ty, il);
	} else {
		context->curitem = n_xmemdup(context->curitem,
			sizeof *context->curitem);
		context->curitem->type = ty;
		context->curitem->size =
			backend->get_sizeof_type(ty, NULL);
	}	

	/*
	 * 08/22/08: Wow, this was setting is_lvalue to 1 instead of 0! Can
	 * only have been unintentional since support for the ancient GNU C
	 * cast-as-lvalue was never seen as desirable 
	 */
	context->is_lvalue = 0;
	context->var_lvalue = NULL;
	copy_type(curtype, ty, 0);
	return 0;
}

static void
do_comp_literal(struct context *context, struct token *item,
	struct icode_list *il, int eval) {

	struct comp_literal	*comp_lit = item->data;
	struct initializer	*init = comp_lit->init;
	struct type		*ty = comp_lit->struct_type;
	struct vreg		*vr;

	if (curscope == &global_scope) {
		/* ?@?@?@? WHAT ,this is never executed?!?!?! XXXXX */
		vr = vreg_alloc(NULL,NULL,NULL,NULL);
		vreg_set_new_type(vr, ty);
		unimpl();
	} else {
		if (eval) {
			vr = vreg_stack_alloc(ty, il, 1, init);
		} else {
			vr = vreg_alloc(NULL,NULL,NULL,NULL);
			vreg_set_new_type(vr, ty);
		}
	}

	copy_type(&context->curtype, ty, 0);
	
	/*
	 * Yes, you heard right, compound literals are lvalues! That is to
	 * make it possible to take their address (as a side effect, one 
	 * can also assign to them)
	 */
	context->is_lvalue = 1;
	context->curitem = vr;
	context->load = vr;
}


struct token *
make_cast_token(struct type *ty) {
	struct token	*ret;
	struct decl	*d;

	ret = alloc_token();
	ret->type = TOK_OP_CAST;
	d = alloc_decl();
	d->dtype = ty;
	ret->data = d;
	return ret;
}

#endif /* #ifndef PREPROCESSOR */

/*
 * Function that reads what I like to refer to as ``sub-expression''.
 * A sub-expression is just an argument to the binary and ternary
 * operators, and it includes all possible unary/prefix/postfix
 * operators (bitwise/logical negation, pre/post-inc/dec-rement,
 * function call, indirection, etc.)
 * This is typically an identifier or a constant, possibly including
 * operators, but it may also be a parenthesized expression, which
 * in turn consists of one or more sub-expressions.
 *
 * In other words, a ``sub-expression'' is what the standard refers to
 * as ``unary expression''
 *
 * Examples:
 * +foo + *bar--
 * ^^^^   ^^^^^^
 * *(x + 5) - foo? bar: baz
 *   ^   ^
 * ^^^^^^^^   ^^^  ^^^  ^^^
 */
struct s_expr *
get_sub_expr(struct token **tok, int delim, int delim2, int extype) {
	struct token		*t;
	struct token		*meat = NULL;
	struct token		*left;
	struct token		*right;
	struct token		*is_sizeof = NULL;
	struct token		*continue_at = NULL;
	struct token		*endp = NULL;
	struct expr		*ex = NULL;
	struct expr		*is_expr = NULL;
	int			is_func_call = 0;
	struct token		*operators[128]; /* XXX */
	struct token		*operators_f[128];
	void			*res = NULL;
	struct s_expr		*ret = NULL;
	int			op;
	int			i;
	int			j = 0;


	t = *tok;

#ifndef PREPROCESSOR
	il = alloc_icode_list();
	il->head = il->tail = NULL;
#endif

	/*
	 * Read prefix or unary operators, if any, and the
	 * identifier/constant/parenthesized expression
	 * constituting this sub-expression
	 */
	for (t = *tok, i = 0; t != NULL; t = t->next) {
		if (expr_ends(t, delim, delim2)) {
			if (t == *tok) {
				errorfl(t, "Parse error at `%s'", t->ascii);
				return NULL;
			}	
			endp = t;
			goto out;
		} else if (t->type == TOK_PAREN_OPEN) {
			/* Must be cast or parenthesized expression */
			if (t->next == NULL) {
				errorfl(t, "Unexpected end of file");
				return NULL;
			}

			/*
			 * 10/01/07: This was missing the check for an
			 * identifier. Thus
			 *
			 *   typepdef int foo;
			 *   {
			 *       int foo;
			 *       (foo);
			 *
			 * ... the last line was interpreted as a cast
			 * expression. Not sure if this is fully correct
			 * now
			 */
#ifndef PREPROCESSOR
			if (IS_TYPE(t->next)
				&& (t->next->type != TOK_IDENTIFIER
				|| lookup_symbol(curscope, t->next->data, 1)
				== NULL)) {
				/* Is cast */
				struct token	*casttok = t;
				struct type	*ty;

				operators[i++] = t;
				t = t->next;
				decv = parse_decl(&t, DECL_CAST);
				if (decv == NULL) {
					return NULL;
				}
				ty = decv[0]->dtype;
				casttok->data = decv[0];
				free(decv);
				if (t->next
					&& t->next->type == TOK_COMP_OPEN
					&& is_basic_agg_type(ty)) {
					struct initializer	*init;
					int			init_type;
					struct comp_literal	*comp_lit;

					if (curscope == &global_scope) {
						init_type = EXPR_CONSTINIT;
					} else {
						init_type = EXPR_OPTCONSTINIT;
					}

					/*
					 * Must be compound literal
					 */
					(void) next_token(&t);
					meat = casttok;
					init = get_init_expr(&t, NULL, init_type,
						ty, 1, 1);	
					if (init == NULL) {
						return NULL;
					}
					comp_lit = n_xmalloc(sizeof *comp_lit);
					comp_lit->init = init;
					comp_lit->struct_type = ty;
					meat->type = TOK_COMP_LITERAL;
					meat->data = comp_lit;
					operators[--i] = NULL;
					if (ty->tlist != NULL
						&& ty->tlist->type == TN_ARRAY_OF
#if REMOVE_ARRARG
						&& !ty->tlist->have_array_size) {
#else
						&& ty->tlist->arrarg->const_value
						== NULL) {
#endif
						/*
						 * No size specified - update
						 * using initializer info!
						 */
						init_to_array_size(ty, init);
					}
					break;
				} else {
					casttok->type = TOK_OP_CAST;
				}
			} else
#endif /* #ifndef PREPROCESSOR */
			{
				/* Is parenthesized expression */
				t = t->next;
				ex = parse_expr(&t, TOK_PAREN_CLOSE, 0, extype, 0);
				if (ex == NULL) {
					return NULL;
				}

				is_expr = ex;
				continue_at = t;
				break;
			}
		} else if (t->type == TOK_OPERATOR) {
			/* Must be unary prefix operator */
			if ((op = ambig_to_unary(t, 1)) == -1) { 
				return NULL;
			}
			*(int *)t->data = op;
			operators[i++] = t;
#ifndef PREPROCESSOR
		} else if (t->type == TOK_KEY_SIZEOF
			|| t->type == TOK_KEY_ALIGNOF) {
			if (do_sizeof(t, &endp, &is_sizeof,
				extype, delim, delim2) == -1) {
				return NULL;
			}
			operators[i++] = t;
			break;
#endif
		} else if (t->type == TOK_IDENTIFIER) {
#ifdef PREPROCESSOR
			int     is_defined = 0;

			if (strcmp(t->data, "defined") == 0) {
				is_defined = 1;
				t->type = TOK_DEFINED;
			}

			if (t->next && t->next->type == TOK_PAREN_OPEN) {
				if (is_defined) {
					if (t->next->next == NULL
						|| t->next->next->next
							== NULL) {
						lexerror("Incomplete `defined()' "
							"expression");
						return NULL;
					} else if (t->next->next->type
						!= TOK_IDENTIFIER) {
						lexerror("Invalid `defined()' "
							"expression - no "
							"identifier argument");
						return NULL;
					} else if (t->next->next->next->type
						!= TOK_PAREN_CLOSE) {
						lexerror("Invalid `defined()' "
							"expression - no "
							"closing `)'");
						return NULL;
					}
					t->data = t->next->next;
					continue_at = t->next->next->next->next;
				} else {
					/* Is function call */
					is_func_call = 1;
				}
			} else if (is_defined) {
				if (t->next
					&& t->next->type == TOK_IDENTIFIER) {
					t->data = t->next;
					continue_at = t->next->next;
				} else {
					lexerror("Invalid `defined' expression - "
						"not followed by identifier");
					return NULL;
				}
			}
			meat = t;
#else /* Is not PREPROCESSOR */
			struct decl	*d;

			if (t->next && t->next->type == TOK_PAREN_OPEN) {
				/* Is function call */
				is_func_call = 1; 
			}
			meat = t;
#if IMMEDIATE_SYMBOL_LOOKUP
			/*
			 * 04/09/08: Perform symbol lookup here (during
			 * parsing already!
			 *
			 * This fixes a long-standing scope bug, where
			 *
			 *    printf("%d\n", foo);
			 *    int foo = 123;
			 *
			 * ... was accepted because the symbol lookup
			 * was done during code translation, i.e. when
			 * there was no location information about the
			 * declaration anymore.
			 *
			 * Notes:
			 *
			 *     - We still let do_identifier() handle the
			 * case that the symbol is not declared, because
			 * that requires some additional logic for
			 * implicit and builtin function calls
			 *
			 *     - We set the data2 member because the data
			 * field (the name) is probably used in too many
			 * other places (at least one that yields a
			 * crash), so don't trash it for now
			 *
			 * XXX Deleting t->data would be good, but then
			 * make it work with the zone allocator!
			 *
			 * 12/24/08: Use access_symbol() later only if it
			 * turns out to be an evaluated expression.
			 */
			d = lookup_symbol(curscope, t->data, 1);
			if (d == NULL && is_func_call) {
				/*
				 * 05/13/09: Whoops, we have to put implicit
				 * declaration into this scope before icode
				 * translation too!
				 */
				d = prepare_implicit_fcall(t);
			}
			t->data2 = d;
#endif
#endif /* #ifdef PREPROCESSOR */
			break;
		} else if (t->type == TOK_STRING_LITERAL
				|| IS_CONSTANT(t->type)) {
			meat = t;
			break;
		} else {
			errorfl(t, "Parse error at `%s'(#1)", t->ascii);
			return NULL;
		}
	}

	if (is_expr) {
		right = continue_at;
		if (next_token(&right) != 0) {
			return NULL;
		}
#ifdef PREPROCESSOR
	} else if (meat->type == TOK_DEFINED) {
		right = continue_at;
#endif
	} else if (is_func_call) {
		right = meat->next;
	} else if (!is_sizeof) {
		if (meat != *tok) {
			left = meat->prev;
		} else {
			left = NULL;
		}

		if (meat->type == TOK_COMP_LITERAL) {
			right = t;
		} else {	
			right = meat;

			if (next_token(&right) != 0) {
				return NULL;
			}
		}	
	} else {
		right = NULL;
	}

	/*
	 * Compare prefix with postfix operators and
	 * bind them accordingly
	 */
	if (i > 0) {
		left = operators[--i];
	} else {
		left = NULL;
	}

	j = 0;

	for (;;) {
		if (right != NULL) {
			endp = right;
			if (expr_ends(right, delim, delim2)) {
				right = NULL;
			}
		}

		/*
		 * Postfix operators always have higher precedence
		 * than prefix and unary operators
		 */
		res = NULL;
		if (right != NULL) {
			if (right->type == TOK_PAREN_OPEN) {
				/*
				 * 05/22/09: In the preprocessor case, it is
				 * not a function call but a function-like
				 * macro invocation. This is handled elsewhere
				 */
#ifndef PREPROCESSOR
				struct token		*old_right = right;
				struct fcall_data	*fres;

				/*
				 * Must be function call. This isn't allowed
				 * in constant expressions if we have a real
				 * function, but there are some builtins,
				 * such as __builtin_offsetof(), which yield
				 * a constant value and should be computable
				 * in constant expressions. Thus we check if
				 * that's the case here
				 */
				res = do_func_call(meat, &right, 1); 
				if (res == NULL) {
					return NULL;
				}
				fres = res;

				if (fres->builtin != NULL
					&& fres->builtin->type
					== BUILTIN_OFFSETOF) {
					/*
					 * 07/16/08: __builtin_offsetof() need
					 * not be constant!
					 */
					struct expr	*ex;

					ex = fres->builtin->args[0];
					if (ex->const_value != NULL) {
						goto const_ok;
					}
					/*
					 * XXX Warnings below are misleading
					 * for non-constant builtins
					 */
				}
				if (extype == EXPR_CONST
					|| extype == EXPR_CONSTINIT) {
					errorfl(right,
						"Function calls are not "
						"allowed in constant %ss",
						extype == EXPR_CONST?
							"expression":
							"initializer");
					return NULL;
				} else if (extype == EXPR_OPTCONSTINIT) {
					warningfl(right, "Variable array/struct "
					"initializers are not allowed in ISO "
					"C90 (only GNU C and C99)");
				} else if (extype == EXPR_OPTCONSTARRAYSIZE) {
					warningfl(right, "Variable-size arrays "
					"are not allowed in ISO C90 (only GNU "
					"C and C99)");
				}
const_ok:		
				if (fres->builtin
					&& fres->builtin->type == BUILTIN_EXPECT) {
					if (is_expr) {
						errorfl(right, "Parse error at `%s'",
							right->ascii);
						return NULL;
					}
					is_expr = fres->builtin->args[0];
				} else {
					operators_f[j] = old_right;
					operators_f[j++]->data = res;
				}
#endif /* #ifndef PREPROCESSOR */

#ifndef PREPROCESSOR
			} else if (right->type == TOK_OPERATOR) {
				/* 
				 * Must be postfix operator or end of
				 * sub-expression
				 */
				op = ambig_to_unary(right, 0);
				if (IS_UNARY(op)) {
					/* XXX handle op */
					operators_f[j++] = right;
					*(int *)right->data = op;
				} else if (op == TOK_OP_STRUMEMB
						|| op == TOK_OP_STRUPMEMB) {
					operators_f[j] = right;
					right->type = op;
					if (next_token(&right) != 0) {
						return NULL;
					}
					if (right->type != TOK_IDENTIFIER) {
						errorfl(right,
							"Structure indirection"
							" operator not "
							"followed by structure"
							" member name");
						return NULL;
					}
					operators_f[j++]->data = right;
				} else {
					right = NULL;
				}
#endif
			} else if (right->type == TOK_ARRAY_OPEN) {
				operators_f[j] = right;
				if (next_token(&right) != 0) {
					return NULL;
				}
				res = parse_expr(&right, TOK_ARRAY_CLOSE, 0, 0, 1);
				if (res == NULL) {
					return NULL;
				}
				operators_f[j++]->data = res;
			} else {
				right = NULL;
			}
			if (right) right = right->next;
		} else if (left != NULL) {
			operators_f[j++] = left;
			if (i > 0) {
				left = operators[--i];
			} else {
				left = NULL;
			}
		} else {
			break;
		}
	}
	operators_f[j++] = NULL;

out:
	*tok = endp;


	if (meat == NULL && is_expr == NULL && is_sizeof == NULL) {
		errorfl(endp, "Parse error at `%s'", endp->ascii);
	}	

	ret = alloc_s_expr();

#if 0
	memcpy(ret->operators, operators_f, j * sizeof *operators_f);
#endif
	if (j < (int)(sizeof ret->operators_buf / sizeof ret->operators_buf[0])) {
		ret->operators = ret->operators_buf;
	} else {
		/*
		 * 03/10/09: XXXXXXXXXXXXx This is a memory leak! But the
		 * case where we need more than 10 unary/pre/post operators
		 * should be VERY rare. However we may still wish to save
		 * this dynamically allocated buffer in some sort of list
		 * to make it reusable when the s_expr zones are freed
		 */
		ret->operators = n_xmalloc(j * sizeof *operators_f);
	}
	memcpy(ret->operators, operators_f, j * sizeof *operators_f);


	ret->is_expr = is_expr;
	ret->meat = meat;
	ret->is_sizeof = is_sizeof;

	return ret;
}

#ifndef PREPROCESSOR

/*
 * 04/12/08: Extended with ``standalone'' info; This permits us to treat
 *
 *   foo--;  
 *
 * like 
 *
 *   --foo
 *
 * and warn about statements without effect, and so on.
 */
struct vreg *
s_expr_to_icode(
	struct s_expr *s,
	struct vreg *lvalue,
	struct icode_list *il,
	int is_standalone,
	int eval) {

	struct context		context;
	struct icode_instr	*ii;
	struct fcall_data	*fcall;
	struct type_node	*funcnode = NULL;
	struct decl		*is_func_call = NULL;
	struct type		*fty;
	int			i;

	context.curtype.code = 0;
	context.var_lvalue = NULL;
	context.is_lvalue = 0;
	context.curitem = NULL;
	context.indir = 0;
	context.load = NULL;

	s->only_load = 1;
	if (s->is_expr) {
		/*
		 * 04/12/08: XXX We could set the resval_not_used flag here,
		 * but expr_to_icode()'s static nesting variable ``level''
		 * prevents this from being useful. We should remove that
		 * one and pass the level as argument
		 */
		context.curitem = expr_to_icode(s->is_expr, lvalue,
			il, TOK_PAREN_OPEN, 0, eval);
		if (context.curitem == NULL) {
			return NULL;
		}	
		copy_type(&context.curtype, context.curitem->type, 0);

		/*
		 * The property of being an lvalue is kept across parenthses;
		 * int x; &(x);
		 * and
		 * int *p; ... ++(*p);
		 * both have to work
		 */
		if (s->is_expr->op == 0) {
			/* data may be NULL if this is a statement-as-expr */
			if (s->is_expr->data != NULL) {
				context.is_lvalue =
					s->is_expr->data->is_lvalue;
				context.var_lvalue =
					s->is_expr->data->var_lvalue;
			}
		}	
	} else if (s->meat) {
		if (s->meat->type == TOK_IDENTIFIER) {
			struct decl	**is_func_call_p = NULL;	
			int		err = 0;
			int		is_addr_label = 0;

			if (s->operators[0] != NULL) {
				if (s->operators[0]->type == TOK_PAREN_OPEN) {
					is_func_call_p = &is_func_call;
				} else if (s->operators[0]->type == TOK_OP_ADDRLABEL) {
					is_addr_label = 1;
				}
			}

			ii = NULL;
			if (is_addr_label) {
				/* 07/19/08: Computed gotos */
				if (do_addr_label(&context, s->meat, s->extype, il, eval) != 0) {
					return NULL;
				}
			} else {
				ii = do_identifier(&context, s->meat,
					s->extype, is_func_call_p, &err, eval);
			}
			if (ii == NULL) {
				if (err) {
					return NULL;
				}
			} else {
				append_icode_list(il, ii);
			}
		} else if (s->meat->type == TOK_STRING_LITERAL) {
			(void) do_string(&context, s->meat, eval);
		} else if (IS_CONSTANT(s->meat->type)) {
			do_constant(&context, s->meat, eval);
		} else if (s->meat->type == TOK_COMP_LITERAL) {
			do_comp_literal(&context, s->meat, il, eval);
		} else {
			puts("BUG: bad item in sub-expression");
			exit(EXIT_FAILURE);
		}
	} else if (s->is_sizeof) {	
		;
	} else {
		puts("BUG: sub-expression has no body??");
			exit(EXIT_FAILURE);
	}
	s->load = context.load;
	if (s->extype == EXPR_CONST
		|| s->extype == EXPR_CONSTINIT
		|| s->extype == EXPR_OPTCONSTINIT
		|| s->extype == EXPR_OPTCONSTARRAYSIZE
		|| s->extype == EXPR_OPTCONSTSUBEXPR) {
		return s->res = context.curitem;
	}

	for (i = 0; s->operators[i] != NULL; ++i) {
		s->only_load = 0;
		
		switch (s->operators[i]->type) {
		case TOK_OP_STRUPMEMB:
		case TOK_OP_STRUMEMB:

			if (do_struct_member(
				s->operators[i],
				&context,
				il,
				eval) != 0) {
				return NULL;
			}
			context.curitem->struct_ret = 0;
			break;
		case TOK_OPERATOR:
			if (unary_to_icode(&context,
				s->operators[i], s->extype, il,
				is_standalone,
				eval) != 0) {
				return NULL;
			}
			break;
		case TOK_KEY_SIZEOF:
		case TOK_KEY_ALIGNOF:	
		case TOK_SIZEOF_VLA_TYPE:	
		case TOK_SIZEOF_VLA_EXPR:	
			if (s->operators[i]->type == TOK_SIZEOF_VLA_TYPE) {
				struct type	*ty;

				unimpl();
				ty = s->operators[i]->data;
				vla_decl_to_icode(ty, il);
			} else if (s->operators[i]->type == TOK_SIZEOF_VLA_EXPR) {
				struct s_expr	*s_ex;
				struct vreg	*vr;

				s_ex = s->operators[i]->data;
				vr = s_expr_to_icode(s_ex, NULL, il, 0, 1);
				if (vr == NULL) {
					return NULL;
				}
				vr = backend->get_sizeof_vla_type(vr->type, il);
				context.curitem = vr;
			} else {
				context.curitem = s->operators[i]->data;		
			}
			context.curitem->type = backend->get_size_t();
			copy_type(&context.curtype, context.curitem->type, 0);
			context.curitem->size = backend->
				get_sizeof_type(context.curitem->type, NULL);
			break;
		case TOK_PAREN_OPEN:
			/* Function call */
			fcall = s->operators[i]->data;
			fcall->lvalue = lvalue;

			if (context.is_lvalue
				&& (fcall->callto = context.var_lvalue) == NULL
				&& fcall->builtin == NULL) {
				fcall->calltovr = context.curitem;
				if (fcall->calltovr == NULL) {
					/* XXX error message?! */
					return NULL;
				}
			} else if (fcall->builtin == NULL) {
				fcall->calltovr = context.curitem;
			}

			fty = NULL;
			if (fcall->callto != NULL) {
				fty = fcall->callto->dtype;
			} else if (fcall->calltovr != NULL) {
				fty = fcall->calltovr->type;
			}
			if (fty && fty->tlist == NULL) {
				errorfl(s->operators[i],
					"Call to something that is not "
					"a function");
				return NULL;
			}	
					
			if (fty != NULL) { 
				fcall->functype = NULL;
				if (fty->tlist->type == TN_FUNCTION) {
					fcall->functype = fty->tlist->tfunc;
					funcnode = fty->tlist;
				} else if (fty->tlist->next != NULL
					&& fty->tlist->next->type
					== TN_FUNCTION) {
					fcall->functype =
						fty->tlist->next->tfunc;
					funcnode = fty->tlist->next;
				}
				if (fcall->functype == NULL) {
					errorfl(s->operators[i],
						"Call to something that is not "
						"a function");
					return NULL;
				}
			}

			if (fcall->builtin != NULL) {
				/*
				 * 07/02/08: This always called toicode
				 * normally, which yields a crash if
				 * we don't want to evaluate. Pass NULL
				 * to toicode to tell it to just give
				 * us the reutrn type
				 */
				if (eval) {
					context.curitem =
						fcall->builtin->
							builtin->toicode(fcall, il, 1);
				} else {
					context.curitem =
						fcall->builtin->
							builtin->toicode(fcall, NULL, 0);
				}
			} else {
				/*
				 * 08/16/07: Check whether we have to create
				 * an anonymous stack struct to hold the
				 * result of the function call. This is
				 * necessary if the result is not directly
				 * assigned to a variable, but e.g. passed
				 * to another function, or some operator is
				 * applied to it;
				 *
				 * struct foo func();
				 * ...
				 * int x = func().x;
				 * ...
				 * void func2(struct foo f);
				 * ...
				 * func2(func());
				 */
				int	needanon = 0;

				if (s->operators[i + 1] != NULL
					&& (fty->code == TY_STRUCT
					|| fty->code == TY_UNION)
					&& funcnode->next == NULL) {
					/* Need anonymous struct */
					needanon = 1;
				}
				fcall->need_anon = needanon;
				context.curitem = fcall_to_icode(fcall, il,
						s->meat, eval);
			}
			if (context.curitem == NULL) {
				return NULL;
			}	
			copy_type(&context.curtype, context.curitem->type, 0);
			context.is_lvalue = 0; /* 05/16/09: TERRIBLE: was missing! */
			break;
		case TOK_OP_CAST:
			if (do_cast(&context, s->operators[i], il, eval)) {
				return NULL;
			}
			break;
		case TOK_ARRAY_OPEN:
			if (do_subscript(&context, s->operators[i],
					il, eval) != 0) {
				return NULL; 
			}	
			break;
		}
	}
	s->var_lvalue = context.var_lvalue;
	s->is_lvalue = context.is_lvalue;
	s->type = n_xmalloc(sizeof(struct type));
	copy_type(s->type, &context.curtype, 1);

	return s->res = context.curitem;
}

#endif /* #ifndef PREPROCESSOR */

