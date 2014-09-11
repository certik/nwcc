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
#include "icode.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include "misc.h"
#include "token.h"
#include "control.h"
#include "decl.h"
#include "type.h"
#include "debug.h"
#include "defs.h"
#include "attribute.h"
#include "expr.h"
#include "error.h"
#include "subexpr.h"
#include "symlist.h"
#include "zalloc.h"
#include "reg.h"
#include "fcatalog.h"
#include "cc1_main.h"
#include "typemap.h"
#include "functions.h"
#include "backend.h" /* for get_sizeof() */
#include "scope.h"
#include "x87_nonsense.h"
#include "inlineasm.h"
#include "n_libc.h"

int	optimizing;
static int	doing_stmtexpr;

#if 0
	sparc
	mov arg1, %o0
	mov arg2, %o1
	...
	page 297
#endif


struct vreg *
fcall_to_icode(
	struct fcall_data *fcall,
	struct icode_list *il,
	struct token *t,
	int eval) {

	struct expr		*ex;
	struct vreg		*args[128];
	struct token		*from_consts[128];
	struct vreg		**argp = NULL;
	struct vreg		*ret = NULL;
	struct type		*fty;
	struct ty_func		*fdecl;
	struct sym_entry	*se;
	int			i = 0;
	int			j;
	int			nstack = sizeof args / sizeof args[0];
	int			alloc = 0;
	

	if (picflag) {
		if (!curfunc->pic_initialized && backend->need_pic_init && eval) {
			backend->icode_initialize_pic(curfunc, il);
			curfunc->pic_initialized = 1;
		}
	}

	/* XXX bad */
	if (fcall->callto != NULL) {
		fty = fcall->callto->dtype;
	} else {
		fty = fcall->calltovr->type;
	}
	fdecl = fcall->functype;


	for (ex = fcall->args; ex != NULL; ex = ex->next) {
		if (i == nstack - 1) {
			alloc = nstack * 2;
			argp = n_xmalloc(alloc * sizeof *argp);
			memcpy(argp, args, nstack * sizeof *argp);
			memset(from_consts, 0, sizeof from_consts);
		} else if (i >= nstack) {
			if (i == alloc - 1) {
				alloc *= 2;
				argp = n_xrealloc(argp, alloc * sizeof *argp);
			}
		} else {
			if (ex->op == 0
				&& ex->data->meat
				&& ex->data->meat->type == TOK_STRING_LITERAL) {
				from_consts[i] = ex->data->meat;
			} else {
				from_consts[i] = NULL;
			}
		}

		if (ex->op == 0
			&& !ex->data->is_expr
			&& ex->data->only_load) {
			/*
			 * Don't need to generate anything
			 */
			if (argp != NULL) {
				argp[i++] = ex->data->res;
			} else {
				args[i++] = ex->data->res;
			}

			if (!eval) {
				/*
				 * 07/15/08: For constant expressions, reset
				 * the vreg and load flags. Otherwise a
				 * subsequent non-constant evaluation will
				 * get an unbacked vreg because it relied
				 * on the constant evaluation, which does
				 * not load registers with values, etc
				 * XXX Not sure this is fully correct
				 */
				ex->data->res = NULL;
				ex->data->only_load = 0;
				ex->res = NULL;
			}
			continue;
		}

		if ((ex->res = expr_to_icode(ex, NULL, il, 0, 0, eval)) == NULL) {
			/* XXX free stuff */
			if (i > nstack) free(argp);
			return NULL;
		}

		if (argp != NULL) {
			argp[i++] = ex->res;
		} else {
			args[i++] = ex->res;
		}

		if (!eval) {
			/*
			 * 07/15/08: For constant expressions, reset
			 * the vreg and load flags. Otherwise a
			 * subsequent non-constant evaluation will
			 * get an unbacked vreg because it relied
			 * on the constant evaluation, which does
			 * not load registers with values, etc
			 * XXX Not sure this is fully correct
			 */
			if (ex->data) {
				ex->data->res = NULL;
				ex->data->only_load = 0;
			}
			ex->res = NULL;
		}
	}
	if (argp != NULL) argp[i] = NULL;
	else args[i] = NULL;

	/* Check correctness of arguments */
	if (fdecl->nargs != -1 && fdecl->nargs != fcall->nargs) {
		if (!fdecl->variadic || fdecl->nargs - 1 > fcall->nargs) {
			if (fdecl->was_just_declared || fdecl->type == FDTYPE_KR) {
				warningfl(t,
				"Call to function `%s' with wrong number of aguments",
				fty->name? fty->name: "");
			} else {
				errorfl(t,
				"Call to function `%s' with wrong number of aguments",
				fty->name? fty->name: "");
				return NULL;
			}	
		}
	} else if (fdecl->nargs == -1 && fcall->nargs > 0 && fty->is_def) {
		/*
		 * 02/21/09: Warn about calls with arguments to functions which 
		 * are DEFINED without a prototype declaration (void foo() {}
		 */
		warningfl(t,
			"Call to function `%s' with wrong number of aguments",
			fty->name? fty->name: "");
	}

	if (fdecl->scope != NULL) {
		se = fdecl->scope->slist;
	} else {
		se = NULL;
	}

	/*if (fcall->calltovr == NULL)*/ /* XXX */
	for (j = 0; j < i && fdecl->nargs != -1; ++j) {
		struct vreg	*arg = argp? argp[j]: args[j];

		/*
		 * 02/15/09: XXXXXXXXXXXXXXXXXXX This does not do typechecking
		 * for arguments outside of the ellpisis!!! But if we just remove
		 * this part, we're getting bogus warnings, so fix it properly
		 * later
		 * 02/21/09: OK, added. j >= fdecl->nargs is true if we're inside
		 * of the ellipsis
		 */
		if (fdecl->variadic && j >= fdecl->nargs) {
#if 0
			if (se != NULL && se->dec == fdecl->lastarg) {
				se = NULL;
			}
#endif
			se = NULL;
		} else if (se != NULL) {
			struct type	*param_type = NULL;

			if (is_transparent_union(se->dec->dtype)) {
				param_type = get_transparent_union_type(t, se->dec->dtype, arg);
				if (param_type == NULL) {
					return NULL;
				}
			}

			if (param_type == NULL) {
				/*
				 * Not transparent union - check type
				 */
				param_type = se->dec->dtype;
				if (fdecl->type == FDTYPE_KR) {
					if (check_types_assign(t, se->dec->dtype,
						arg, 1, 1) != 0) {
						/*
						 * There is a mismatch here, but
						 * we only warn instead of 
						 * erroring because most other
						 * compilers probably accept this
						 * code without warning because
						 * it is a K&R function (e.g. Ruby
						 * declares a ``void *'' parameter
						 * but passes a ``long''
						 */
						warningfl(t, "Incompatible "
							"argument type. This "
							"may be an undetected "
							"bug due to a K&R "
							"declaration instead "
							"of an ANSI prototype.");
					}
				} else {
					if (check_types_assign(t, se->dec->dtype,
						arg, 1, 0) != 0) {
						return NULL;
					}
				}
			}	
			if (is_arithmetic_type(param_type)
				&& eval) {
				/*
				 * Make parameter agree with argument
				 */
				arg = backend->
					icode_make_cast(arg,param_type,il);

				if (argp) {
					argp[j] = arg;
				} else {
					args[j] = arg;
				}	
			}

			se = se->next;
		} else {
			/* Must be implicitly declared */
			;
		}
	}
	
	if (fdecl->nargs == -1) {
		/* XXX finish this .................... */
	}

	if (fty->fastattr & CATTR_FORMAT) {
		check_format_string(t, fty, fdecl, args, from_consts, i);
	}

	if (eval) {
		ret = backend->icode_make_fcall(fcall, argp? argp: args, i, il);
	} else {
		ret = vreg_alloc(NULL,NULL,NULL,NULL);
		ret->type = n_xmemdup(fcall->calltovr->type, sizeof(struct type));
		functype_to_rettype(ret->type);
		if (ret->type->code != TY_VOID || ret->type->tlist != NULL) {
			ret->size = backend->get_sizeof_type(ret->type,NULL);
		} else {
			ret->size = 0;
		}
	}

	if (i > nstack) free(argp);
	return ret;
}


/*
 * Promote vr if necessary
 */
int
pro_mote(struct vreg **vr, struct icode_list *il, int eval) {
	struct type	*ty = (*vr)->type;
	int		is_constant = 0;

	if (il != NULL && eval) {
		if (is_x87_trash(*vr)) {
			*vr = x87_anonymify(*vr, il);
		} else {	
			/*
			 * 04/13/08: Don't anonymify constants!
			 */
			if ((*vr)->from_const != NULL
				&& (*vr)->from_const->type != TOK_STRING_LITERAL
				&& !IS_FLOATING((*vr)->from_const->type)
				&& Oflag != -1) {
				*vr = dup_vreg(*vr);
				is_constant = 1; /* 07/13/08: Whoops, forgot this! */
			} else {

				vreg_anonymify(vr, NULL, NULL, il);
			}
		}
	}
	if (ty->tlist != NULL) {
		return 0;
	}

	/*
	 * 08/09/08: Handle bitfields
	 */
	if (ty->tbit != NULL) {
		if (eval) {
			*vr = promote_bitfield(*vr, il);
			*vr = backend->icode_make_cast(*vr, 
				cross_get_bitfield_promoted_type((*vr)->type),
				il);
		} else {
			(*vr)->type = cross_get_bitfield_promoted_type((*vr)->type); /*make_basic_type(TY_INT);*/
			(*vr)->size = backend->get_sizeof_type((*vr)->type, NULL);
		}
	} else if (IS_CHAR(ty->code)
		|| IS_SHORT(ty->code)) {
		/* Need promotion! */
		ty = make_basic_type(TY_INT);
		if (il != NULL && eval) {
			if (is_constant) {
				/*
				 * 04/12/08: Don't anonymify constants
				 */
				struct token	*t;

				t = cross_convert_const_token((*vr)->from_const,
					TY_INT);
				(*vr)->from_const = t;
			} else {	
				*vr = backend->icode_make_cast(*vr, ty, il);
			}
		}
		(*vr)->type = ty;
		(*vr)->size = backend->get_sizeof_type(ty, NULL);
		return 1;
	}
	return 0;
}


/*
 * Perform usual arithmetic conversions
 */
static int
convert_operands(
	struct vreg **left,
	struct vreg **right,
	struct icode_list *left_il,
	struct icode_list *right_il,
	int op0,
	struct token *optok,
	int eval) {

	struct type		*lt = (*left)->type;
	struct type		*rt = (*right)->type;
	struct operator		*op;
	int			larit;
	int			rarit;
	int			is_cond_op;


	/*
	 * 08/07/09: There were STILL bugs with the conditional
	 * operator! Usual arithmetic conversion between condop
	 * operands operates in a very restricted environment,
	 * because registers allocated and temporarily saved
	 * items on the one side are not valid on the other. So
	 * we have to invalidate GPRs properly after using them
	 *
	 * Now we add LOTS of invalidations just to make sure
	 * that no stale data can be used. This is costly
	 */
	is_cond_op = left_il != right_il;

	larit = is_arithmetic_type(lt);
	rarit = is_arithmetic_type(rt);
	op = &operators[LOOKUP_OP2(op0)];

	if (larit && rarit) {
		/*
		 * Both are arithmetic types and may need usual
		 * arithmetic conversions 
		 */
		pro_mote(left, left_il, eval);
		if (is_cond_op) {
			backend->invalidate_gprs(left_il, 1, 0);
		}

		pro_mote(right, right_il, eval);
		if (is_cond_op) {
			backend->invalidate_gprs(right_il, 1, 0);
		}

		lt = (*left)->type;
		rt = (*right)->type;
		if ((op0 != TOK_OP_BSHL && op0 != TOK_OP_BSHR)
			/*|| backend->arch == ARCH_X868 */  /* :-( */) {
			if (rt->code > lt->code) {
				/*
				 * 06/01/08: In long vs unsigned int, the
				 * result type is only long if long can
				 * represent all unsigned int values;
				 * Otherwise both are converted to unsigned
				 * long. This was not handled correctly
				 */
				if (eval) {
					/*
					 * XXX 07/13/08: We should convert
					 * constants at compile time!
					 */
					if (!(rt->code == TY_LONG
						&& lt->code == TY_UINT)
	|| cross_get_target_arch_properties()->long_can_store_uint) {
						*left = backend->
							icode_make_cast(*left,
								rt,
								left_il);
					} else {
						/* long vs uint */
						*left = backend->
							icode_make_cast(*left,
							make_basic_type(TY_ULONG),
							left_il);
						*right = backend->
							icode_make_cast(*right,
							make_basic_type(TY_ULONG),
							right_il);
					}
				} else {
					(*left)->type = rt;
				}	
			} else if (lt->code > rt->code) {
				if (eval) {
					/*
					 * XXX 07/13/08: We should convert
					 * constants at compile time!
					 */
					if (!(lt->code == TY_LONG
						&& rt->code == TY_UINT)
	|| cross_get_target_arch_properties()->long_can_store_uint) {
						*right = backend->
							icode_make_cast(*right,
								lt,
								right_il);
					} else {
						/* long vs uint */
						*right = backend->
							icode_make_cast(*right,
							make_basic_type(TY_ULONG),
							right_il);
						*left = backend->
							icode_make_cast(*left,
							make_basic_type(TY_ULONG),
							left_il);
					}
#if 0
					*right = backend->
						icode_make_cast(*right, lt,
							right_il);
#endif
				} else {		
					(*right)->type = lt;
				}	
			}
		}
		return 0;
	} else if (lt->tlist == NULL &&
		(lt->code == TY_STRUCT || lt->code == TY_UNION)) {
		errorfl(optok, "Cannot use %s type with `%s' operator",
			lt->code == TY_STRUCT? "structure": "union",
			op->name);
		return 1;
	} else if (rt->tlist == NULL &&
		(rt->code == TY_STRUCT || rt->code == TY_UNION)) {
		errorfl(optok, "Cannot use %s type with `%s' operator",
			rt->code == TY_STRUCT? "structure": "union",
			op->name);
		return 1;
	} else {
		/* At least one side is a pointer or array */
		if (lt->tlist == NULL || rt->tlist == NULL) {
		} else {
		}
	}
	return 1;
}

/*
 * XXX There is some duplicated stuff in promote() and expr_to_icode(),
 * which should be cleaned up. And this is improprely named because it
 * doesn't only do promotions but also usual arithmetic conversion. And
 * in some cases it does it wrongly
 *
 * Should be fully replaced with convert_operands() and pro_mote()
 */
struct type * 
promote(struct vreg **left, struct vreg **right, int op0, struct token *optok,
struct icode_list *il, int eval) {
	struct type		*lt = (*left)->type;
	struct type		*rt = right?
			(void *)(*right)->type: (void *)NULL;
	struct type		*ret;
	struct type		*towhat = NULL;
	struct type_node	*tnl;
	struct type_node	*tnr;
	struct operator		*op;
	int			is_void = 0;

	if (il != NULL && eval) {
		if (is_x87_trash(*left)) {
			*left = x87_anonymify(*left, il);
		} else {
			/*
			 * 04/13/08: Don't anonymify constants!
			 */
			if ((*left)->from_const != NULL
				&& (*left)->from_const->type != TOK_STRING_LITERAL
				&& !IS_FLOATING((*left)->from_const->type)
				&& Oflag != -1) {
				*left = dup_vreg(*left);
			} else {	
				vreg_anonymify(left, NULL, NULL, il);
			}
		}	
	}


	if ((*left)->type->tbit != NULL) {
		if (eval) {
			if (right) {
				vreg_faultin_protected(*right, NULL, NULL, *left, il, 0);
			} else {
				vreg_faultin(NULL, NULL, *left, il, 0);
			}
		}
		(void) pro_mote(left, il, eval);
	}
	if (right != NULL && (*right)->type->tbit != NULL) {
		if (eval) {
			vreg_faultin_protected(*left, NULL, NULL, *right, il, 0);
		}
		(void) pro_mote(right, il, eval);
	}

	if (op0 == 0) {
		op = NULL; /* XXX */
	} else {
		op = &operators[LOOKUP_OP2(op0)];
	}
	if (right == NULL) {
		/*
		 * Promoting argument to unary operator or result of
		 * conditional operator
		 */
		towhat  = (*left)->type;
		if (lt->tlist != NULL) {
			if (op0 == TOK_OP_LNEG) {
			}
		} else if (IS_CHAR(lt->code)
			|| IS_SHORT(lt->code)) {
			towhat = make_basic_type(TY_INT);
			if (il != NULL) {
				if (eval) {
					*left = backend->icode_make_cast(*left,
						towhat, il);	
				} else {
					(*left)->type = towhat;
				}	
			}
		}
		return towhat;
	}	

	if (lt->tlist == NULL || rt->tlist == NULL) {
		if (lt->tlist != rt->tlist) {
			/* 
		 	 * Basic type used with pointer type -
		 	 * only valid for some operations
		 	 */
			struct type	*pointer;
			struct vreg	*basic_vreg;
			struct type	*basic;
			
			if (lt->tlist) {
				pointer = lt;
				basic = rt;
				basic_vreg = *right;
			} else {
				pointer = rt;
				basic = lt;
				basic_vreg = *left;
			}	
			if (basic->code == TY_STRUCT
				|| basic->code == TY_UNION) {
				errorfl(optok, "Cannot use "
					"%s type with `%s' "
					"operator", basic->code ==
					TY_STRUCT? "structure":
					"union", op? op->name: "<unknown>");
				return NULL;
			} else if (IS_FLOATING(basic->code)) {
				errorfl(optok, "Cannot use "
					"pointer types with floating "
					"point types");
				return NULL;
			} else if (op0 == TOK_OP_PLUS
				|| op0 == TOK_OP_MINUS) {
				/* Probably OK - pointer arithmetic */
				if (pointer->code == TY_VOID
					&& pointer->tlist->next == NULL) {
					/* if (std != GNU) {
					errorfl(optok, "Cannot "
						"perform pointer arithmetic "
						"on void pointers (cast to "
						"`(char *)' instead!)");
						} else { */
					warningfl(optok, "Pointer arithmetic "
						"on void pointers is a GNU C "
						"extension (you should cast "
						"to `(char *)' instead!)");
#if 0
					return NULL;
#endif
				}	
			} else if ((op0 == TOK_OP_LEQU
				|| op0 == TOK_OP_LNEQU)
				&& basic_vreg->is_nullptr_const) {
				;
			} else if (op0 == TOK_OP_COMMA) {
				;
			} else { 
				errorfl(optok, "Cannot use "
					"pointer type with `%s' operator "
					"and basic type",
					op? op->name: "<unknown>");
				return NULL;
			}	

			if (pointer->tlist->type == TN_ARRAY_OF
				|| pointer->tlist->type == TN_VARARRAY_OF) {
				pointer = n_xmemdup(pointer, sizeof *pointer);
				copy_tlist(&pointer->tlist, pointer->tlist);
				pointer->tlist->type = TN_POINTER_TO;

				/* XXX 12/07/24: Is this needed? */
				(void) backend->get_sizeof_type(pointer, NULL);
			}	
			return pointer;
		} else {
			/* Both are basic types */
			if (convert_operands(left, right, il, il, op0,
				optok, eval) != 0) {
				return NULL;
			} else {
				return (*left)->type;
			}	
		}	
	}

	tnl = lt->tlist;
	tnr = rt->tlist;

	if (tnl->type == TN_FUNCTION
		&& tnr->type == TN_POINTER_TO) {
		tnr = tnr->next;
	} else if (tnr->type == TN_FUNCTION
		&& tnl->type == TN_POINTER_TO) {
		tnl = tnl->next;
	}

	/* Dealing with two pointer types */
	if ((lt->code == TY_VOID && tnl->next == NULL)
		|| (rt->code == TY_VOID && tnr->next == NULL)) {
		/*
		 * 08/03/07: Avoid error for
		 *
		 *   char *p;
		 *   if (&p == (void *)bla) {
		 *
		 * May not be 100% correct
		 */
		is_void = 1;
	} else {	
		for (;
			tnl != NULL && tnr != NULL;
			tnl = tnl->next, tnr = tnr->next) {
			if (tnl->type != tnr->type
				|| (tnl->type == TN_ARRAY_OF
					&& tnl->arrarg_const
					!= tnr->arrarg_const)) {
				if (tnl != lt->tlist
					|| (tnl->type != TN_ARRAY_OF
					&& tnr->type != TN_ARRAY_OF
					&& tnl->type != TN_VARARRAY_OF
					&& tnr->type != TN_VARARRAY_OF)) {
					errorfl(optok,
						"Incompatible pointer types in "
						"expression");
					return NULL;
				}
			}
		}
	}

	if (!is_void) {
		if (tnl != tnr
			&& !(*right)->is_nullptr_const
			&& !(*left)->is_nullptr_const) {
			/*
			 * XXX this fails for function vs void pointers!!!!
			 * !!!!!!!!!!!!!!!!!!!!!
			 */
			errorfl(optok,
				"Incompatible types in expression");
			return NULL;
		}
	}	

	/* XXX this is complete nonsense */
	if (op0 == TOK_OP_LEQU
		|| op0 == TOK_OP_LNEQU
		|| op0 == TOK_OP_GREAT
		|| op0 == TOK_OP_SMALL
		|| op0 == TOK_OP_GREATEQ
		|| op0 == TOK_OP_SMALLEQ) {
		/* 
		 * The resulting type of relational operators applied to
		 * two pointer values is of type ``int''
		 */
		ret = make_basic_type(TY_INT);
		ret = n_xmemdup(ret, sizeof *ret);
		return ret;
	} else if (op0 == TOK_OP_MINUS) {
		ret = make_basic_type(TY_INT);
		ret = n_xmemdup(ret, sizeof *ret);
		return ret;
	}	
		
	return lt;
}


/*
 * Perform pointer arithmetic on lres or rres (result is returned.) For add
 * this means:  mul n, elemsize; add ptr, n
 * For sub this needs to do one of two separate things: Either subtract a
 * pointer from another pointer and calculate the number of elements between
 * them (sub ptr1, ptr2;  div result, elemsize), or just subtract an element
 * count (mul n, elemsize; sub ptr, n)
 *
 * As a simple optimization, the scaling is done using shift left/shift right
 * rather than mul/div instructions if the element size is a power of two
 */
static void
ptrarit(
	struct vreg **lres0,
	struct vreg **rres0,
	struct icode_list *ilp,
	int op,
	int eval) {

	struct vreg		*toscale;
	struct vreg		*addto;
	struct vreg		*lres = *lres0;
	struct vreg		*rres = *rres0;
	struct type		*ty;
	struct icode_instr	*ii;
	int			factor;
	int			both_ptr;
	int			is_vla;
	struct vreg		*tmpvr;

	both_ptr = lres->type->tlist != NULL && rres->type->tlist != NULL;

	/*
	 * 04/13/08: Additional faultins needed if either side is a now
	 * non-anonymified constant! ptr + 123
	 */
	vreg_faultin_protected(rres, NULL, NULL, lres, ilp, 0);
	vreg_faultin_protected(lres, NULL, NULL, rres, ilp, 0);
	reg_set_unallocatable(rres->pregs[0]);
	reg_set_unallocatable(lres->pregs[0]);
	if (lres->type->tlist) {
		toscale = rres;
		addto = lres;
		if (eval) {
			vreg_anonymify(&addto, NULL, NULL, ilp);
		}
		*lres0 = lres = addto;
	} else {
		toscale = lres;
		addto = rres;
		if (eval) {
			vreg_anonymify(&addto, NULL, NULL, ilp);
		}
		*rres0 = rres = addto;
	}
	reg_set_allocatable(rres->pregs[0]);
	reg_set_allocatable(lres->pregs[0]);

	ty = addto->type;

	/*
	 * 05/22/11: Handle VLA elements: If the pointer we're working
	 * with points to a VLA, then we need to compute its size at
	 * runtime
	 */
	if (IS_VLA(ty->flags)) {
		is_vla = 1;
		factor = 0;
	} else {
		is_vla = 0;
		factor = backend->
			get_sizeof_elem_type(ty);
	}

	if (factor > 1 || is_vla) {
		/*
		 * Scaling something that may be from a variable -
		 * register is not cached value anymore afterwards
		 */
		tmpvr = vreg_alloc(NULL, NULL, NULL, make_basic_type(TY_INT));

		if (toscale == lres) {
			vreg_anonymify(&toscale, NULL, NULL, ilp);
			pro_mote(&toscale, ilp, eval);
			*lres0 = lres = toscale;
		} else {
			vreg_anonymify(&toscale, NULL, NULL, ilp);
			pro_mote(&toscale, ilp, eval);
			*rres0 = rres = toscale;
		}

		/*
		 * 12/23/08: Convert index to size_t. This is necessary on 64bit
		 * architectures like PPC64. For example, if we have
		 *
		 *    p[-1]            (where p is ``int *'')
		 *
		 * ... then the shift or multiplication to scale -1 for an int
		 * must be performed using 64bit instructions to ensure that it
		 * remains negative (i.e. sign-extended to the upper word)
		 *
		 * size_t is a GPR on all supported architectures so it is a
		 * suitable type
		 */
		toscale = backend->icode_make_cast(toscale, backend->get_size_t(), ilp);

		if (!is_vla && (factor & (factor - 1)) == 0) {
			/* 
			 * Scaling by (constant - not VLA) power of two - we can shift!
			 */
			int			shift_by = 0;

			while (factor /= 2) {
				++shift_by;
			}	

			tmpvr->from_const = const_from_value(&shift_by, NULL);

			if (op == TOK_OP_PLUS) {
				ii = icode_make_shl(toscale, tmpvr);
				append_icode_list(ilp, ii);
			}	
			if (op == TOK_OP_PLUS) {
				ii = icode_make_add(addto, toscale);
				append_icode_list(ilp, ii);
			} else {
				if (!both_ptr) {
					/* Second sub operand is scaled */
					ii = icode_make_shl(toscale, tmpvr);
					append_icode_list(ilp, ii);
				}	
				/*
				 * 06/14/09: This was missing - the
				 * icode_prepare_op() may trash our target
				 * register
				 */
				vreg_faultin(NULL, NULL, addto, ilp, 0);
				ii = icode_make_sub(addto, toscale);
				append_icode_list(ilp, ii);
				if (both_ptr) {
					/* Result of p1 - p is scaled */
					ii = icode_make_shr(addto, tmpvr);
					append_icode_list(ilp, ii);
				}
			}
		} else {
			if (is_vla) {
				/*
				 * 05/22/11: VLA - size must be determined at
				 * runtime
				 */
				tmpvr = get_sizeof_elem_vla_type(ty, ilp);
			} else {
				tmpvr->from_const = const_from_value(&factor, NULL);
			}

			if (op == TOK_OP_PLUS) {
				backend->
					icode_prepare_op(&toscale, &tmpvr, 
					TOK_OP_MULTI, ilp);
				ii = icode_make_mul(toscale, tmpvr);
				append_icode_list(ilp, ii);
			}	
			if (op == TOK_OP_PLUS) {
				vreg_faultin_protected(toscale, NULL, NULL,
					addto,
					ilp, 0);
				ii = icode_make_add(addto, toscale);
				append_icode_list(ilp, ii);
			} else {
				if (!both_ptr) {
					backend->icode_prepare_op(&toscale,
						&tmpvr,
						TOK_OP_MULTI, ilp);
					ii = icode_make_mul(toscale, tmpvr);
					append_icode_list(ilp, ii);
				}	

				/*
				 * 06/14/09: This was missing - the
				 * icode_prepare_op() may trash our target
				 * register
				 */
				vreg_faultin(NULL, NULL, addto, ilp, 0);
				ii = icode_make_sub(addto, toscale);
				append_icode_list(ilp, ii);
				if (both_ptr) {
					backend->icode_prepare_op
					(&addto, &tmpvr, TOK_OP_DIVIDE, ilp);
					ii = icode_make_div(addto, tmpvr); 
					append_icode_list(ilp, ii);
				}	
			}	
		}
	} else {
		pro_mote(&toscale, ilp, eval);
		if (op == TOK_OP_PLUS) {
			ii = icode_make_add(addto, toscale);
		} else {
			ii = icode_make_sub(addto, toscale);
		}	
		append_icode_list(ilp, ii);
	}

	if (op == TOK_OP_PLUS && toscale == lres) {
		/*
		 * This is an uncommon operand ordering, such as ``123 + buf''.
		 * The result is stored in buf's register, but 123's register
		 * is returned!
		 */
		 icode_make_copyreg(toscale->pregs[0], addto->pregs[0],
			addto->type, addto->type, ilp); /* XXX long long?? */
	}

	free_pregs_vreg(toscale, ilp, 0, 0);
}


void
do_add_sub(struct vreg **lres, struct vreg **rres,
		int op, struct token *optok, struct icode_list *il,
		int eval) {
	struct icode_instr	*ii = NULL;

	if ((*lres)->type->tlist != NULL
		|| (*rres)->type->tlist != NULL) {
		/* Need to scale first */
		struct type	*newty = NULL;
	
		if (is_floating_type((*lres)->type)
			|| is_floating_type((*rres)->type)) {
			errorfl(optok,
				"Cannot do pointer arithmetic with floating "
				"point values");
			return;
		}
		if ((*lres)->type->tlist != NULL
			&& (*rres)->type->tlist != NULL) {
			/* ptr - ptr2 */
			if (op == TOK_OP_PLUS) {
				errorfl(optok,
					"Cannot add pointers to pointers");
				return;
			} else {
				/* Result of p - p2 is of type ptrdiff_t */
				newty = make_basic_type(TY_LONG); /* XXX */
			}
		} else if ((*lres)->type->tlist != NULL) {
			/* ptr +/- integer */
			if ((*lres)->type->tlist->type == TN_ARRAY_OF
				|| (*lres)->type->tlist->type == TN_VARARRAY_OF) {
				/* Becomes pointer */
				newty = n_xmemdup((*lres)->type,
						sizeof *(*lres)->type);
				copy_tlist(&newty->tlist, newty->tlist);
				newty->tlist->type = TN_POINTER_TO;
			} 
		} else {  /* if ((*rres)->type->tlist != NULL) { */
			/* integer +/- ptr */
			if ((*rres)->type->tlist->type == TN_ARRAY_OF
				|| (*rres)->type->tlist->type == TN_VARARRAY_OF) {
				/* Becomes pointer */
				newty = n_xmemdup((*rres)->type,
						sizeof *(*rres)->type);
				copy_tlist(&newty->tlist, newty->tlist);
				newty->tlist->type = TN_POINTER_TO;
			} else {
				/* Is pointer */
				newty = n_xmemdup((*rres)->type,
						sizeof(struct type));
			}
		}

		if (eval) {
			ptrarit(lres, rres, il, op, eval);
		}	
		if (newty != NULL) {
			/*
			 * The result still has type ``pointer'' and
			 * may be stored in a 64bit register (e.g. on
			 * AMD64) that is bigger than int. Hence,
			 * we need to change type and register
			 */
			if (eval) {
				*lres = backend->
					icode_make_cast(*lres, newty, il);
			} else {
				vreg_set_new_type(*lres, newty);
			}	
		}	
		return;
	} else {
		if (eval) {
			if (is_x87_trash(*lres)) {
				*lres = x87_do_binop(*lres, *rres, op, il);
			} else {
				vreg_anonymify(lres, NULL, NULL, il);

				if (op == TOK_OP_PLUS) {
					ii = icode_make_add(*lres, *rres);
				} else {	
					ii = icode_make_sub(*lres, *rres);
				}
			}
		}	
	}
	if (eval && ii != NULL) {	
		append_icode_list(il, ii);
	}	
}


static int
can_transform_to_bitwise(struct vreg **left, struct vreg **right,
	int *operator, struct icode_list *il) {

	struct vreg	**dest = NULL;
	struct vreg	**src = NULL;
	struct token	*t;
	int		op = *operator;
	int		transformed_op = 0;
	int		reorder_operands = 0;


	if (op != TOK_OP_MOD
		&& op != TOK_OP_DIVIDE
		&& op != TOK_OP_MULTI
		&& op != TOK_OP_COMOD
		&& op != TOK_OP_CODIVIDE
		&& op != TOK_OP_COMULTI) {
		return 0;
	}

	if (Oflag == -1
		|| !is_integral_type((*left)->type)
		|| !is_integral_type((*right)->type)) {
		return 0;
	}

	if ((*left)->from_const) {
		/*
		 * Left is constant, so right isn't. This is only good
		 * for multiplication because division is not commutative
		 */
		if (op != TOK_OP_MULTI && op != TOK_OP_COMULTI) {
			return 0;
		}
		
		/*
		 *   16 * foo
		 *
		 * Original left operand becomes right operand
		 */
		dest = right;
		src = left;

		/*
		 * 07/14/08: This was missing - const * nonconst is changed to
		 * nonconst << const, so we have to reverse the order of
		 * operands
		 */
		reorder_operands = 1;
	} else if ((*right)->from_const) {
		/*
		 *   foo <op> 16 
		 */
		dest = left;
		src = right;
		if (op == TOK_OP_DIVIDE) {
			/*
			 * 07/14/08: XXX: Turned shift transformation off for
			 * signed values! Because negative values are not
			 * handled correctly, the result is off by one
			 */
			if ((*left)->type->sign != TOK_KEY_UNSIGNED) {
				return 0;
			}
		}
	} else {
		return 0;
	}

	switch (op) {
	case TOK_OP_MOD:	
	case TOK_OP_COMOD:	
		/*
		 * % pow2 can be transformed to % (pow2 - 1)
		 *
		 * 01/28/10: Wow, this was wrong for signed values!
		 * (Tcl bug)
		 */
		if ((*dest)->type->sign != TOK_KEY_UNSIGNED) {
			return 0;
		}
		t = cross_get_pow2_minus_1((*src)->from_const);
		if (t == NULL) {
			return 0;
		}
		transformed_op = op == TOK_OP_MOD? TOK_OP_BAND: TOK_OP_COBAND;
		break;
	case TOK_OP_MULTI:
	case TOK_OP_DIVIDE:
	case TOK_OP_COMULTI:
	case TOK_OP_CODIVIDE:
		/* * pow2 and / pow2 can be transformed to <</>> bits */
		t = cross_get_pow2_shiftbits((*src)->from_const);
		if (t == NULL) {
			return 0;
		}
		if (op == TOK_OP_MULTI || op == TOK_OP_COMULTI) {
			transformed_op = op == TOK_OP_MULTI? 
				TOK_OP_BSHL: TOK_OP_COBSHL;
		} else {
			transformed_op = op == TOK_OP_DIVIDE?
				TOK_OP_BSHR: TOK_OP_COBSHR;
		}
		break;
	default:
		unimpl();
		break;
	}
	*src = dup_vreg(*src);
	(*src)->from_const = t;

	if (!backend->have_immediate_op((*dest)->type, transformed_op)) {
		vreg_set_unallocatable(*dest);
/*		vreg_faultin_protected(*src, NULL, NULL, *dest, il, 0);*/
		vreg_anonymify(src, NULL, NULL, il);
		vreg_set_allocatable(*dest);
	}

	*operator = transformed_op;

	if (reorder_operands) {
		/* 07/14/08: Reorder operands */
		struct vreg	*temp = *left;
		*left = *right;
		*right = temp;
	}
	return 1;
}

static int
do_bitwise(struct vreg **lres0, struct vreg *rres,
	struct operator *operator, struct token *optok, struct icode_list *il,
	int eval);

static int 
do_mul(struct vreg **lres0, struct vreg *rres,
	struct operator *operator, struct token *op, struct icode_list *il,
	int eval) {

	struct icode_instr	*ii;
	struct vreg		*lres;
	struct reg		*lres_preg1 = NULL;
	struct reg		*lres_preg2 = NULL;

	/*
	 * 04/12/08: Moved type-checking up
	 */
	lres = *lres0;
	if (operator->value == TOK_OP_MOD) {
		if (!is_integral_type(lres->type)
			|| !is_integral_type(rres->type)) {
			errorfl(op /* XXX */,
				"Operands of `%%' operator must have integral"
				" type");
			return -1;
		}
	} else if (!is_arithmetic_type(lres->type)
		|| !is_arithmetic_type(rres->type)) {
		errorfl(op /* XXX */,
			"Operands of `%s' operator must have arithmetic "
			"(integral or floating point) type",
			operator->name);	
		return -1;
	}	

	if (!eval) {
		/* We're already done */
		return 0;
	}

	if (eval) {
		/*
		 * 04/12/08: Optimize divisions and multiplications by
		 * power-of-two values by using bitwise opertors
		 */
#if 0 
		if (Oflag != -1) {
			struct vreg	*left_tmp = *lres0;
			struct vreg	*right_tmp = rres;

			if (can_transform_to_bitwise(&left_tmp,
					&right_tmp, &operator, il)) {
				int	rc;
				
				rc = do_bitwise(&left_tmp, right_tmp,operator,
					op, il, eval); 
				*lres0 = left_tmp;
				return rc;
			}
		}
#endif

		if (!is_x87_trash(*lres0)) {
			vreg_anonymify(lres0, NULL, NULL, il);
		} else {
			*lres0 = x87_anonymify(*lres0, il);
		}
	}
	lres = *lres0;


	if (lres->is_multi_reg_obj) {
		/*
		 * long long ... so this operation is carried out using
		 * a function call and we need to invalidate GPRs
		 * XXX hm this belongs into icode_prepare_op ?!??!
		 */
		lres->pregs[0]->used = lres->pregs[1]->used = 0;
		rres->pregs[0]->used = rres->pregs[1]->used = 0;
		lres_preg1 = lres->pregs[0];
		lres_preg2 = lres->pregs[1];
		backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
	}

	if (is_x87_trash(lres)) {
		*lres0 = x87_do_binop(*lres0, rres, operator->value, il);
	} else {
		if (operator->value == TOK_OP_MULTI) {
			ii = icode_make_mul(lres, rres);
		} else if (operator->value == TOK_OP_DIVIDE) {
			ii = icode_make_div(lres, rres);
		} else {
			/* MOD */
			ii = icode_make_mod(lres, rres);
		}
		append_icode_list(il, ii);
	}


	if (lres->is_multi_reg_obj) {
		if (backend->arch == ARCH_X86) {
			/* long long results are returned in eax:edx */
			vreg_map_preg(lres, &x86_gprs[0]);
			vreg_map_preg2(lres, &x86_gprs[3]);
		} else if (backend->arch == ARCH_POWER) {
			/* Results are returned in dest pregs */
			vreg_map_preg(lres, lres_preg1);
			vreg_map_preg2(lres, lres_preg2);
		} else {
			unimpl();
		}
	}
	return 0;
}

static int
do_bitwise(struct vreg **lres0, struct vreg *rres,
	struct operator *operator, struct token *optok, struct icode_list *il,
	int eval) {

	struct icode_instr	*ii = NULL;
	struct vreg		*lres;
	int			op = operator->value;

	if (eval) {
		vreg_anonymify(lres0, NULL, NULL, il);
	}	
	lres = *lres0;

	if (!is_integral_type(lres->type)
		|| !is_integral_type(rres->type)) {
		errorfl(optok,
			"Both operands of the `%s' operator have to be "
			"of integral type", operator->name);
		return -1;
	}
	if (!eval) {
		/* We're already done */
		return 0;
	}	

	/* XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX ... anonymify broken */
#if 0
	lres = n_xmemdup(lres, sizeof *lres);
#endif
	lres = copy_vreg(lres);

	if (op == TOK_OP_BSHL) {
		ii = icode_make_shl(lres, rres);
	} else if (op == TOK_OP_BSHR) {
		ii = icode_make_shr(lres, rres);
	} else if (op == TOK_OP_BAND) {
		ii = icode_make_and(lres, rres);
	} else if (op == TOK_OP_BOR) {
		ii = icode_make_or(lres, rres);
	} else if (op == TOK_OP_BXOR) {
		ii = icode_make_xor(lres, rres);
	} else {
		unimpl();
	}

	append_icode_list(il, ii);
	return 0;
}

void
boolify_result(struct vreg *vr, struct icode_list *il) {
	struct icode_instr	*ii;
	struct icode_instr	*label = icode_make_label(NULL);

	ii = icode_make_cmp(vr, NULL);
	append_icode_list(il, ii);
	ii = icode_make_branch(label, INSTR_BR_EQUAL, vr);
	append_icode_list(il, ii);

	/* At this point, the value is nonzero so it always becomes 1 */
	ii = icode_make_setreg(vr->pregs[0], 1);
	append_icode_list(il, ii);
	append_icode_list(il, label);
}



void
mask_source_for_bitfield(struct type *ltype, struct vreg *rres,
	struct icode_list *il, int for_reading) {

	struct vreg		*maskvr;
	struct token		*tok;
	struct icode_instr	*ii;

	(void) for_reading;
/*	tok = make_bitfield_mask(ltype, for_reading, &shift_bits);*/
	tok = ltype->tbit->bitmask_tok;

	maskvr = vreg_alloc(NULL,tok,NULL,NULL);

	vreg_set_unallocatable(rres);
	vreg_faultin_protected(rres, NULL, NULL, maskvr, il, 0);

	/*
	 * 10/12/08: Hmm, ltype doesn't correspond to the input type
	 * storage unit type, so cast to make them match
	 */
	maskvr = backend->icode_make_cast(maskvr, /*ltype*/rres->type, il);
	vreg_set_allocatable(rres);

	ii = icode_make_and(rres, maskvr);
	append_icode_list(il, ii);
}

static void
shift_bitfield(struct type *ty, struct vreg *vr, int encode, struct icode_list *il) {
	struct token		*shiftbits;
	struct icode_instr	*ii = NULL;
	struct vreg		*shiftvr = NULL;

	shiftbits = ty->tbit->shifttok; 

	vreg_faultin(NULL, NULL, vr, il, 0);


	if (shiftbits != NULL) {
		/* Only shift if shift count not 0 */
		shiftvr = vreg_alloc(NULL,shiftbits,NULL,NULL);
		vreg_faultin_protected(vr, NULL, NULL, shiftvr, il, 0);
	}

	if (encode) {
		if (shiftvr != NULL) {
			backend->icode_prepare_op(&vr, &shiftvr, TOK_OP_BSHL, il);
			ii = icode_make_shl(vr, shiftvr);
		}
	} else {
		/*
		 * Decode. First decide whether we need to sign-extend
		 */
		struct vreg	*andvr;

		/*
		 * Mask off all unrelated bits. This must be done at the
		 * beginning because if we do it after sign-extension, then
		 * we will lose sign bits
		 */
		andvr = vreg_alloc(NULL,ty->tbit->bitmask_tok_with_shiftbits,NULL,NULL);
		vreg_faultin_protected(vr, NULL, NULL, andvr, il, 0);
		vreg_faultin_protected(andvr, NULL, NULL, vr, il, 0);
		backend->icode_prepare_op(&vr, &andvr, TOK_OP_BAND, il);
		ii = icode_make_and(vr, andvr);
		append_icode_list(il, ii);
		ii = NULL;

		if (ty->sign != TOK_KEY_UNSIGNED) {
			/* Sign-extend */
			struct vreg	*sign_ext_left;
			struct vreg	*sign_ext_right;

			if (/*vr->type*/ty->tbit->shifttok_signext_left != NULL) {
				sign_ext_left = vreg_alloc(NULL,
					/*vr->type*/ty->tbit->shifttok_signext_left,NULL,NULL);
				vreg_faultin_protected(vr, NULL, NULL, sign_ext_left, il, 0);
				backend->icode_prepare_op(&vr, &sign_ext_left, TOK_OP_BSHL, il);
				ii = icode_make_shl(vr, sign_ext_left);
				append_icode_list(il, ii);
			}

			if (/*vr->type*/ty->tbit->shifttok_signext_right != NULL) {
				sign_ext_right = vreg_alloc(NULL,
					/*vr->type*/ty->tbit->shifttok_signext_right,NULL,NULL);
				vreg_faultin_protected(vr, NULL, NULL, sign_ext_right, il, 0);
				backend->icode_prepare_op(&vr, &sign_ext_right, TOK_OP_BSHR, il);
				ii = icode_make_shr(vr, sign_ext_right);
			}
		} else {
			if (shiftvr != NULL) {
				/* shift right to get to the value */
				backend->icode_prepare_op(&vr, &shiftvr, TOK_OP_BSHR, il);
				ii = icode_make_shr(vr, shiftvr);
			}
		}
		
	}
	if (ii != NULL) {
		append_icode_list(il, ii);
	}
}



static void
encode_bitfield(struct type *ty, struct vreg *vr, struct icode_list *il) {
	shift_bitfield(ty, vr, 1, il);
}

void
decode_bitfield(struct type *ty, struct vreg *vr, struct icode_list *il) {
	shift_bitfield(ty, vr, 0, il);
}

void
load_and_decode_bitfield(struct vreg **lres, struct icode_list *il) {
	*lres = promote_bitfield(*lres, il);
	*lres = backend->icode_make_cast(*lres, 
			cross_get_bitfield_promoted_type((*lres)->type), il);
}


#if 0 /* OBSOLETE!!! */
void
write_back_bitfield_with_or(struct vreg *destvr, struct vreg *lres,
		struct type *desttype,
		struct icode_list *il) {

	struct icode_instr	*ii;
	struct vreg		*temp;

	/*
	 * Limit source value to bitfield range and
	 * encode it
	 */
	lres = backend->icode_make_cast(lres, desttype, il);
	mask_source_for_bitfield(desttype, lres, il, 0);
	encode_bitfield(desttype, lres, il);

	vreg_faultin_protected(lres, NULL, NULL, destvr, il, 0);

	backend->icode_prepare_op(&destvr, &lres, TOK_OP_BOR, il);
	ii = icode_make_or(destvr, lres);
	append_icode_list(il, ii);

	icode_make_store(/*NULL*/curfunc, destvr, destvr, il);
}
#endif


void
write_back_bitfield_by_assignment(struct vreg *lres, struct vreg *rres,
	struct icode_list *ilp) {

	struct vreg		*and_vr;
	struct icode_instr	*ii;
	struct type		*orig_rres_type = rres->type;
	struct vreg		*orig_rres = rres;


	/*
	 * 07/20/08: Bitfield assignment!
	 */
	mask_source_for_bitfield(lres->type, rres, ilp, 0);
	encode_bitfield(lres->type, rres, ilp);

	and_vr = vreg_alloc(NULL,lres->type->tbit->bitmask_inv_tok,NULL,NULL);
	vreg_faultin_protected(lres, NULL, NULL, /*rres*/and_vr, ilp, 0);
	vreg_faultin_protected(and_vr, NULL, NULL, lres, ilp, 0);

	rres = backend->icode_make_cast(rres, lres->type, ilp);
	/*
	 * Mask off old bitfield value by ANDing with the inverted
	 * bitmask
	 */
	backend->icode_prepare_op(&lres, &and_vr, TOK_OP_BAND, ilp);
	ii = icode_make_and(lres, and_vr);
	append_icode_list(ilp, ii);

	vreg_faultin_protected(rres, NULL, NULL, lres, ilp, 0);
	vreg_faultin_protected(lres, NULL, NULL, rres, ilp, 0);
	backend->icode_prepare_op(&lres, &rres, TOK_OP_BOR, ilp);
	ii = icode_make_or(lres, rres);
	append_icode_list(ilp, ii);


	/*
	 * 03/04/09: Added vreg_faultin_ptr() (and vreg_set_unallocatable()
	 * to ensure that the actual bitfield value will not be trashed by
	 * it).
	 *
	 * This is needed because the ORing and ANDing above may trash a
	 * pointer preg if lres comes from a pointer, so it must be
	 * reloaded
	 */
	vreg_set_unallocatable(lres);
	vreg_faultin_ptr(lres, ilp);

	icode_make_store(NULL, lres, lres, ilp);

	vreg_set_allocatable(lres);

	/*
	 * 10/12/08: Seems that rres is used after writing it
	 * back! XXX Find out where and why!
	 * This means that the icode_make_cast() above will trash
	 * the caller's vreg because it will break the register
	 * mapping. So convert the value back, and map the result
	 * register to the caller's vr
	 */
	rres = backend->icode_make_cast(rres, orig_rres_type, ilp);
	vreg_map_preg(orig_rres, rres->pregs[0]);
}

int
emul_conv_ldouble_to_double(struct vreg **temp_lres,
	struct vreg **temp_rres,
	struct vreg *lres,
	struct vreg *rres,
	struct icode_list *ilp,
	int eval) {

	int	changed_vrs = 0;

	if (lres->type->code == TY_LDOUBLE
		&& (rres == NULL || rres->type->code == TY_LDOUBLE)
		&& backend->emulate_long_double
		&& eval) {
		/*
		 * 11/20/08: For now we emulate 128
		 * bit long double by converting it
		 * to double and back whenever we
		 * need to carry out arithmetic
		 * operations
		 *
		 * XXX Perhaps we should use icode_
		 * prepare_op() here...
		 */
		if (rres != NULL) {
			vreg_set_unallocatable(rres);
		}
		*temp_lres = backend->icode_make_cast(
			lres,
			make_basic_type(TY_DOUBLE),
			ilp);
		vreg_set_unallocatable(lres);
		if (rres != NULL) {
			*temp_rres = backend->icode_make_cast(
				rres,
				make_basic_type(TY_DOUBLE),
				ilp);
		}
		changed_vrs = 1;
		vreg_faultin(NULL, NULL, *temp_lres, ilp, 0);
		if (rres != NULL) {
			vreg_faultin_protected(*temp_lres, NULL, NULL, *temp_rres, ilp, 0);
		}
		vreg_set_allocatable(lres);
		if (rres != NULL) {
			vreg_set_allocatable(rres);
		}
	} else {
		changed_vrs = 0;
		*temp_lres = lres;
		if (rres != NULL) {
			*temp_rres = rres;
		}
	}
	return changed_vrs;
}

/*
 * XXX not ``eval-clean'' (sizeof)
 */
static struct vreg * 
do_comp_assign(struct vreg *lres, struct vreg *rres,
	int op, struct token *optok, struct icode_list *il, int eval) {
	struct operator		*operator;
	struct vreg		*destvr = copy_vreg(lres);
	struct type		*desttype = lres->type;
	int			op2 = op;
	int			needprep = 0;
	struct vreg		*temp_lres;
	struct vreg		*temp_rres;

	if (op == TOK_OP_CODIVIDE
		|| op == TOK_OP_COMULTI
		|| op == TOK_OP_COMOD
		|| op == TOK_OP_COBSHL
		|| op == TOK_OP_COBSHR
		|| op == TOK_OP_COBAND
		|| op == TOK_OP_COBXOR
		|| op == TOK_OP_COBOR) {
		if (op == TOK_OP_CODIVIDE) op2 = TOK_OP_DIVIDE;
		else if (op == TOK_OP_COMULTI) op2 = TOK_OP_MULTI;
		else if (op == TOK_OP_COMOD) op2 = TOK_OP_MOD;
		else if (op == TOK_OP_COBSHL) op2 = TOK_OP_BSHL;
		else if (op == TOK_OP_COBSHR) op2 = TOK_OP_BSHR;
		else if (op == TOK_OP_COBAND) op2 = TOK_OP_BAND;
		else if (op == TOK_OP_COBXOR) op2 = TOK_OP_BXOR;
		else if (op == TOK_OP_COBOR) op2 = TOK_OP_BOR;
		needprep = 1;
	} else if (op == TOK_OP_COPLUS) {
		op2 = TOK_OP_PLUS;
	} else if (op == TOK_OP_COMINUS) {
		op2 = TOK_OP_MINUS;
	}


	if (eval) {
		if (lres->type->tbit != NULL) {
			/*
			 * 07/20/08: Bitfield assignment! Load and decode target value
			 * so that we can perform the requested operation and write it
			 * back
			 */
			load_and_decode_bitfield(&lres, il);
			(void) promote(&lres, &rres, op2, optok, il, eval);
		} else {
			(void) promote(&lres, &rres, op2, optok, il, eval);
			/* XXX .... as promote may move stuff :( */
			if (is_x87_trash(lres)) {
				/*
				 * The target may not have been loaded yet
				 */
#if 0
			vreg_faultin_x87(NULL, NULL, lres, il, 0);
			vreg_map_preg(lres, &x86_fprs[1]);
			vreg_faultin_x87(NULL, NULL, rres, il, 0);
#endif
			} else {	
				vreg_faultin(NULL, NULL, lres, il, 0); 
				vreg_faultin_protected(lres, NULL, NULL,
					rres, il, 0); 
			}	
		}
		if (needprep) {
			backend->icode_prepare_op(&lres, &rres, op2, il);
		}
	}

	if (lres->type->code == TY_LDOUBLE && rres->type->code == TY_LDOUBLE) {
		emul_conv_ldouble_to_double(&temp_lres,
			&temp_rres, lres, rres, il, eval);
	} else {
		temp_rres = rres;
		temp_lres = lres;
	}

	switch (op) {
	case TOK_OP_COPLUS:
		do_add_sub(&temp_lres, &temp_rres, TOK_OP_PLUS, optok, il, eval); /* XXX */
		break;
	case TOK_OP_COMINUS:
		do_add_sub(&temp_lres, &temp_rres, TOK_OP_MINUS, optok, il, eval); /* XXX */
		break;
	case TOK_OP_CODIVIDE:
		operator = &operators[LOOKUP_OP2(TOK_OP_DIVIDE)];
		do_mul(&temp_lres, temp_rres, operator, optok, il, eval);
		break;
	case TOK_OP_COMULTI:
		operator = &operators[LOOKUP_OP2(TOK_OP_MULTI)];
		do_mul(&temp_lres, temp_rres, operator, optok, il, eval);
		break;
	case TOK_OP_COMOD:
		operator = &operators[LOOKUP_OP2(TOK_OP_MOD)];
		do_mul(&temp_lres, temp_rres, operator, optok, il, eval);
		break;
	case TOK_OP_COBAND:
		operator = &operators[LOOKUP_OP2(TOK_OP_BAND)];
		do_bitwise(&temp_lres, temp_rres, operator, optok, il, eval);
		break;
	case TOK_OP_COBOR:
		operator = &operators[LOOKUP_OP2(TOK_OP_BOR)];
		do_bitwise(&lres, rres, operator, optok, il, eval);
		break;
	case TOK_OP_COBXOR:
		operator = &operators[LOOKUP_OP2(TOK_OP_BXOR)];
		do_bitwise(&temp_lres, temp_rres, operator, optok, il, eval);
		break;
	case TOK_OP_COBSHL:
		operator = &operators[LOOKUP_OP2(TOK_OP_BSHL)];
		do_bitwise(&temp_lres, temp_rres, operator, optok, il, eval);
		break;
	case TOK_OP_COBSHR:
		operator = &operators[LOOKUP_OP2(TOK_OP_BSHR)];
		do_bitwise(&temp_lres, temp_rres, operator, optok, il, eval);
		break;
	default:
		printf("%d is not compound assignment operator\n", op);
		abort();
	}

	lres = temp_lres;
	rres = temp_rres;

	/*
	 * Given ``char *p;'', ``*p += 4'' has type ``char'', so
	 * promotions to int need to be reverted here
	 */
	if (eval) {
		if (desttype->tbit != NULL) {
			write_back_bitfield_by_assignment(destvr, lres, il);
		} else {
			lres = backend->
				icode_make_cast(lres, desttype, il);
			/*
			 * With x87 the item may not be in a register even
			 * after icode_make_cast
			 */
			vreg_faultin_x87(NULL, NULL, lres, il, 0);
			destvr->pregs[0] = lres->pregs[0];
			if (lres->is_multi_reg_obj) {
				destvr->pregs[1] = lres->pregs[1];
			}

			/*
			 * 070802: This code only did:
			 *
			 *    if (destvr->from_ptr) { faultin(destvr->from_ptr);
			 *
			 * That ignored parent structs which come from pointers.
			 * Multi-register assignmenst were also ignored
			 */
			reg_set_unallocatable(lres->pregs[0]);
			if (lres->is_multi_reg_obj) {
				reg_set_unallocatable(lres->pregs[1]);
			}
			vreg_faultin_ptr(destvr, il);
			reg_set_allocatable(lres->pregs[0]);
			if (lres->is_multi_reg_obj) {
				reg_set_allocatable(lres->pregs[1]);
			}

			icode_make_store(NULL, lres, destvr, il);
		}
	} else {
		vreg_set_new_type(lres, desttype);
	}

	return destvr;
}

/*
 * XXX not ``eval-clean'' (sizeof)  
 * XXX I think we need to be more careful not to trash the right/left operand..
 * Have to use reg_set_unallocatable() before faulting in the left operand
 * pointer if we are assigning through a pointer more carefully?!?!?
 */

static struct vreg *
do_assign(
	struct vreg *lres,
	struct vreg *rres,
	struct expr *ex,
	struct icode_list *ilp,
	int level,
	int purpose,
	int eval) {
	
	int			is_struct = 0;
	int			is_x87 = 0;
	struct icode_instr	*ii = NULL;
	struct decl		*d;
	struct vreg		*vr2;

	/* Need to do typechecking */
	if (ex->op == TOK_OP_ASSIGN
		&& check_types_assign(ex->tok, lres->type, rres, 0, 0) != 0) {
		return NULL;
	}

	if ((rres->type->code == TY_STRUCT
		|| rres->type->code == TY_UNION)
		&& rres->type->tlist == NULL) {
		is_struct = 1;
		if ((lres->type->code != TY_STRUCT
			&& lres->type->code != TY_UNION)
			|| lres->type->tlist != NULL) {
			errorfl(ex->tok,
			"Incompatible types in assignment");
			return NULL;
		}
	} else {
		if (eval) {
			if (is_x87_trash(rres)
				|| is_x87_trash(lres)) {
				is_x87 = 1;
			} else {
				vreg_faultin(NULL, NULL, rres, ilp, 0);
			}
		}	
	}

	if (ex->op != TOK_OP_ASSIGN) {
		/* Compound assignment operator */
		if (eval && !is_x87) {
			vreg_faultin(NULL, NULL, rres, ilp, 0);
			vreg_faultin_protected(rres, NULL, NULL, lres, ilp, 0);
		}
		if (!eval)  {  /*rres->type->tbit != NULL || is_bitfield) {*/
			if (rres->type->tbit != NULL) {
				vreg_set_new_type(rres, cross_get_bitfield_promoted_type(
					rres->type));
			}
		}
		return do_comp_assign(lres, rres,
			ex->op, ex->tok, ilp, eval);
	} else {
		if (level == 1
			&& (purpose == TOK_KEY_IF
			|| purpose == TOK_KEY_DO
			|| purpose == TOK_KEY_WHILE
			|| purpose == TOK_KEY_SWITCH)) {
			warningfl(ex->tok,
				"`=' operator used at top-"
				"level in conditional "
				"expression - perhaps you "
				"meant `=='?");
		}
	}

	if (!eval) {
		/* Not evaluated - just set new type */
		vreg_set_new_type(rres, lres->type);
		return rres;
	}

	if (lres->parent) {
		vr2 = get_parent_struct(lres);
	} else {
		vr2 = NULL;
	}	


	if (ex->left->data->var_lvalue != NULL) {
		/* Store to variable */
		
		d = ex->left->data->var_lvalue;
		if (is_struct) {
			struct vreg	*vr3;

			if (rres->parent) {
				vr3 = get_parent_struct(rres);
			} else {
				vr3 = NULL;
			}	

			if (rres->from_ptr) {
				free_preg(rres->from_ptr->pregs[0],
					ilp, 0, 0);
			}	
			if (vr3 && vr3->from_ptr) {
				free_preg(vr3->from_ptr->pregs[0],
					ilp, 0, 0);
			}

			backend->invalidate_gprs(ilp, /*level==*/1, INV_FOR_FCALL);

			if (rres->from_ptr) {
#if 0
				rres->from_ptr->pregs[0]->used = 1;
#endif
				vreg_map_preg(rres->from_ptr, rres->from_ptr->pregs[0]);
			}	
			if (vr3 && vr3->from_ptr) {
#if 0
				vr3->from_ptr->pregs[0]->used = 1;
#endif
				vreg_map_preg(vr3->from_ptr, vr3->from_ptr->pregs[0]);
			}	

			if (lres->from_ptr) {
				vreg_faultin_protected(rres, NULL, NULL,
					lres->from_ptr, ilp, 0);
			} else if (vr2 && vr2->from_ptr) {
				vreg_faultin_protected(rres, NULL, NULL,
					vr2->from_ptr, ilp, 0);
			}	

			icode_make_copystruct(lres, rres, ilp);
			if (rres->from_ptr) {
				free_preg(rres->from_ptr->pregs[0],
					ilp, 0, 0);	
			}	
			if (vr3 && vr3->from_ptr) {
				free_preg(vr3->from_ptr->pregs[0],
					ilp, 0, 0);
			}	
			if (lres->from_ptr) {
				free_preg(lres->from_ptr->pregs[0],
					ilp, 0, 0);	
			}	
			if (vr2 && vr2->from_ptr) {
				free_preg(vr2->from_ptr->pregs[0],
					ilp, 0, 0);
			}	
		} else {
			struct reg	*r = NULL;

			rres = backend->
				icode_make_cast(rres, lres->type, ilp);



			/*
			 * When casting to x87 fp, the result is not register
			 * resident anymore!
			 */
			vreg_faultin_x87(NULL, NULL, rres, ilp, 0);

			if (eval && lres->type->tbit == NULL) { 
				/*
				 * 070802: This was apparently done the wrong way
				 * around; Possible left-handed pointers were loaded,
				 * then the right side was cast to the left type,
				 * then it was assigned.
				 *
				 * This caused an x86 sign-extension from int to
				 * long long to trash eax (this must be done with eax
				 * and edx), so the pointer was also trashed
				 */
				vreg_set_unallocatable(rres);
				r = vreg_faultin_ptr(lres, ilp);
				vreg_set_allocatable(rres);
			}

			if (!eval) {
				/* 09/09/08: Don't perform icode operations! */
				;
			} else if (lres->type->tbit != NULL) {
				write_back_bitfield_by_assignment(lres, rres, ilp);
				/*
				 * Reset ii pointer because it is appended below if
				 * non-null
				 */
				ii = NULL;
			} else if (lres->parent) {
				if (ex->op == TOK_OP_ASSIGN) {
					lres->pregs[0] = rres->pregs[0];
					if (rres->is_multi_reg_obj) {
						lres->pregs[1] = rres->pregs[1];
					}	
				}

				icode_make_store(NULL,
					rres, lres, ilp);
				ii = NULL;
				if (is_x87_trash(rres)) {
					lres->pregs[0] = NULL;
				}
			} else {
				struct vreg	*vr;

#if 0
				/* CANOFWORMS :( */
				vr = n_xmemdup(d->vreg, sizeof *d->vreg);
#endif
				vr = vreg_alloc(d, NULL, NULL, NULL);
				vreg_set_new_type(vr, d->dtype);

				if (ex->op == TOK_OP_ASSIGN) {
					vr->pregs[0] =
						rres->pregs[0];
					if (rres->is_multi_reg_obj) {
						vr->is_multi_reg_obj = 2;
						vr->pregs[1] =
							rres->pregs[1];
					}	
				} else {
					vr->pregs[0] =
						lres->pregs[0];
					if (lres->is_multi_reg_obj) {
						vr->is_multi_reg_obj = 2;
						vr->pregs[1] =
							lres->pregs[1];
					}	
				}
				icode_make_store(NULL,
					rres, /*d->vreg*/vr, ilp);
				ii = NULL;
				if (is_x87_trash(rres)) {
					vr->pregs[0] = NULL;
				}
			}
			if (r != NULL) {
				free_preg(r, ilp, 1, 0);
			}	
		}	
	} else {
		struct reg	*r = NULL;

		if (!is_struct) {
			rres = backend->
				icode_make_cast(rres, lres->type, ilp);
			/*
			 * When casting to x87 fp, the result is not register
			 * resident anymore!
			 */
			vreg_faultin_x87(NULL, NULL, rres, ilp, 0);
		}

		if (lres->from_ptr || (vr2 && vr2->from_ptr)) {
			r = vreg_faultin_protected(rres,
				NULL, NULL,
				lres->from_ptr? lres->from_ptr:
				vr2->from_ptr,
				ilp, 0);
		}	

		if (is_struct) {
			/* XXXXXXXXXXXXXXXXXXXXXXXX need to faultin
			 * rres if from ptr?!?! */  
			struct reg	*r2;

			if (r) reg_set_unallocatable(r);
			r2 = vreg_faultin_ptr(rres, ilp);
			if (r) reg_set_allocatable(r);

			backend->invalidate_except(ilp,
				/*level==1*/1, INV_FOR_FCALL, r, r2,
				(struct reg *)NULL);
			if (r != NULL) {
				free_preg(r, ilp, 0, 1);
			}
			if (r2 != NULL) {
				free_preg(r2, ilp, 0, 1);
			}
			icode_make_copystruct(lres, rres, ilp);

			/*
			 * 08/02/07: That invlidate_except() is only used
			 * above because the registers shall not needlessly
			 * be saved! However, the free_preg()s below were
			 * missing the invalidate flag, which is still
			 * necessary because copystruct may call memcpy()
			 *
			 * 08/03/07: Hmm ... without SAVING them too, some
			 * pointers become unbacked. E.g. in
			 *
			 *    foo->bar = foo->baz = foo->bam;
			 *
			 * ... where the members are all structures. Here
			 * one of the frees below made a pointer invalid.
			 * Presumably because the return value of the
			 * assignment is the left vreg, not a new one!
			 * XXX fix this!
			 *
			 * 08/03/07: Phew...Now the test structassbug.c
			 * works. Saving pointers below is bogus because
			 * they are already invalidated by the copystruct.
			 * So now we have the FIRST CASE EVER where we can
			 * save but not invalidate with free_preg()
			 * legally... That is done above before the
			 * copstruct. Maybe the invalidate_except() should
			 * be changed instead. Not sure whether what we
			 * have now is really correct
			 */
			if (r != NULL) {
				free_preg(r, ilp, 1, 0);
			}
			if (r2 != NULL) {
				free_preg(r2, ilp, 1, 0);
			}
		} else {
			if (lres->type->tbit != NULL) {
				/*
				 * 05/25/09: Unbelievable, this wasn't handling
				 * bitfields!
				 */
				struct reg	*r2;

				if (r) reg_set_unallocatable(r);
				r2 = vreg_faultin_ptr(rres, ilp);
				if (r) reg_set_allocatable(r);

				write_back_bitfield_by_assignment(lres, rres, ilp);
				if (r != NULL) {
					free_preg(r, ilp, 1, 0);
				}
				if (r2 != NULL) {
					free_preg(r2, ilp, 1, 0);
				}
				ii = NULL;
			} else {

				lres->pregs[0] = rres->pregs[0];
				lres->pregs[1] = rres->pregs[1];
#if 0
			ii = icode_make_store_indir(rres, lres);
#endif
				icode_make_store(curfunc, rres, lres, ilp);
				if (r != NULL) {
					free_preg(r, ilp, 0, 0);
				}
				if (is_x87_trash(rres)) {
					lres->pregs[0] = NULL;
				}
				backend->invalidate_except(ilp,
					/*level==1*/1, INV_FOR_FCALL, rres->pregs[0],
					(struct reg *)NULL);
			}
		}	
	}	
	if (ii) append_icode_list(ilp, ii);

	if (lres->pregs[0] && lres->pregs[0] != rres->pregs[0]) {
		free_pregs_vreg(lres, ilp, 0, 0);
	}

	return rres;
}	


struct icode_instr *
compare_vreg_with_zero(struct vreg *vr, struct icode_list *ilp) {
	struct token		*ztok;
	struct vreg		*zvr;
	struct icode_instr	*ii;
		
	if (IS_FLOATING(vr->type->code) && vr->type->tlist == NULL) {
		int	is_x87 = 0;
		/*
		 * On some or many or most architectures, we
		 * cannot compare fp values with an immediate
		 * zero. Therefore we explicitly construct an
		 * fp zero token, load it into a register, and
		 * compare with that
		 */
		/* if (!backend->has_zero_cmp) { */
			ztok = fp_const_from_ascii("0.0", vr->type->code);
			zvr = vreg_alloc(NULL, ztok, NULL, NULL);
			vreg_faultin_x87(NULL, NULL, vr, ilp, 0); 

			/*
			 * 07/08/03: Was missing a faultin for vr. This
			 * broke when x87 support was rewritten I guess
			 */
			reg_set_unallocatable(vr->pregs[0]);
			vreg_faultin_x87(NULL, NULL, zvr, ilp, 0);
			reg_set_unallocatable(vr->pregs[0]);
			if (is_x87_trash(vr)) {
				vreg_map_preg(vr, &x86_fprs[1]);
				is_x87 = 1;
			}
			ii = icode_make_cmp(vr, zvr);
			if (!is_x87) {
				free_pregs_vreg(zvr, ilp, 0, 0);
			}
		/* } */
		return ii;	
	}
	return icode_make_cmp(vr, NULL);
}		

static struct icode_instr * 
branch_if_zero(
	struct vreg *vr,
	int branch_type,
	struct icode_instr *label0,
	struct icode_list *ilp) {

	struct icode_instr	*ii;
	struct icode_instr	*label;
	struct icode_instr	*not_equal_zero;

	if (vr->is_multi_reg_obj) {
		not_equal_zero = icode_make_label(NULL);
	} else {
		not_equal_zero = NULL;
	}

	if (vr->is_multi_reg_obj && IS_FLOATING(vr->type->code)) {
		/* SPARC long double ?!?!??! */
		unimpl();
	}	

	ii = compare_vreg_with_zero(vr, ilp);

	append_icode_list(ilp, ii);
	free_pregs_vreg(vr, ilp, 0, 0);

	if (label0 != NULL) {
		label = label0;
	} else {	
		label = icode_make_label(NULL);
	}	

	if (vr->is_multi_reg_obj) {
		ii = icode_make_branch(not_equal_zero,
			INSTR_BR_NEQUAL, vr);
	} else {	
		ii = icode_make_branch(label, /*INSTR_BR_EQUAL*/branch_type, vr);
	}	
	append_icode_list(ilp, ii);


	if (vr->is_multi_reg_obj) {
		ii = icode_make_cmp(vr, NULL);
		append_icode_list(ilp, ii);
		ii = icode_make_branch(label, /*INSTR_BR_EQUAL*/branch_type, vr);
		append_icode_list(ilp, ii);
		append_icode_list(ilp, not_equal_zero);
	}
	return label;
}

/*
 * 04/12/08: XXXXXXXX We could use resval_not_used here to optimize the
 * result handling away if this is a top-level conditional operator
 */
static struct vreg * 
do_cond_op(struct expr *ex, struct type **restype,
	struct vreg *lvalue, /* for structs */
	struct icode_list *ilp, int eval) {

	struct vreg		*ret = NULL;
	struct vreg		*lres;
	struct vreg		*rres;
	struct reg		*r = NULL;
	struct type		*lt;
	struct type		*rt;
	struct icode_instr	*label = NULL;
	struct icode_instr	*left_end_jump = NULL;
	struct icode_instr	*end_label = NULL;
	struct icode_list	*left_list = NULL;
	struct icode_list	*right_list = NULL;
	int			is_void;
	int			is_struct = 0;
	int			is_void_botched = 0;


	if (ex->right->op != TOK_OP_COND2) {
		errorfl(ex->right->tok,
	"Parse error - expected second part of conditional operator");	
		return NULL;
	}	

	lres = expr_to_icode(ex->left, NULL, ilp, 0, 0, eval);
	
	if (lres == NULL) {
		return NULL;
	}

	if (!is_scalar_type(lres->type)) {
		errorfl(ex->left->tok,
			"First operand of conditional operator"
			" does not have scalar type");
		return NULL;
	}

	if (eval) {
		left_list = alloc_icode_list();
		right_list = alloc_icode_list();

		backend->invalidate_gprs(ilp, 1, 0);
		if (!is_x87_trash(lres)) {
			vreg_faultin(NULL, NULL, lres, ilp, 0);
		}
		label = branch_if_zero(lres, INSTR_BR_EQUAL, NULL, ilp);
	}

	/*
	 * Now comes the part for (cond) != 0 ...
	 *
	 * 08/18/07: As per GNU C, this part may be empty, in which case
	 * it is replaced with the condition itself
	 */
	if (ex->right->left != NULL) {
		lres = expr_to_icode(ex->right->left, NULL,
			left_list, 0, 0, eval);
	} else {
		;  /* lres already is value of condition */
	}	
	if (lres == NULL) {
		return NULL;
	}

	if (lres->type->code == TY_VOID
		&& lres->type->tlist == NULL) {
		is_void = 1;
	} else {
		is_void = 0;
		if ((lres->type->code == TY_STRUCT
			|| lres->type->code == TY_UNION)
			&& lres->type->tlist == NULL) {
			is_struct = 1;
			if (eval) {
				if (lvalue != NULL) {
					/* Result is being assigned */
					icode_make_copystruct(lvalue, lres,
						left_list);
				} else {
					/*
					 * Result is anonymous struct, e.g
					 * in
					 *    (foo? bar: baz).xyz
					 *
					 * or
					 *
					 *    func(foo? bar: baz)
					 *
					 * ... there is no lvalue to assign
					 * to
					 */
					ret = vreg_stack_alloc(lres->type, ilp, 1,
						NULL);
					
					icode_make_copystruct(ret, lres,
						left_list);	
				}
			}
		}
	}

	if (!is_void && !is_struct) {
		pro_mote(&lres, left_list, eval);
	}	

	if (ret == NULL) {
		/* Not anonymous struct - vreg must still be allocated */
		ret = vreg_alloc(NULL, NULL, NULL, lres->type);
	}	

	if (eval) {
		end_label = icode_make_label(NULL);
		left_end_jump = icode_make_jump(end_label);

		backend->invalidate_gprs(left_list, 1, 0); /* saves lres too */
	}

	/* Now comes the part for (cond) == 0 ... */
	rres = expr_to_icode(ex->right->right, NULL, 
		right_list, 0, 0, eval);

	if (rres == NULL) {
		return NULL;
	}


	if (!is_void && !is_struct) {
		pro_mote(&rres, right_list, eval);
	} else if (is_struct) {
		if (eval) {
			if (lvalue != NULL) {
				/* Result is assigned */
				icode_make_copystruct(lvalue, rres, right_list);
			} else {
				/* Anonymous struct */
				icode_make_copystruct(ret, rres, right_list);
			}
		}
	}

	/*
	 * 07/10/09: We have to invalidate all registers used by the right
	 * expression as well, because the left expression may be converted
	 * (as per usual arithmetic conversion) in a convert_operands() call
	 * below. That can mix up the two ``worlds'' of left and right side,
	 * and may trash registers (i.e. we may end up freeing registers
	 * that were only used in the right expression in the left icode
	 * list)
	 */
	if (eval) {
		backend->invalidate_gprs(right_list, 1, 0);
	}

	/* Now it is FINALLY possible to determine the result type! */
	lt = lres->type;
	rt = rres->type;
	if (lt->tlist == NULL && rt->tlist == NULL) {
		if (lt->code == TY_STRUCT
			|| lt->code == TY_UNION
			|| rt->code == TY_STRUCT
			|| rt->code == TY_UNION) {
			if (rt->code != lt->code
				|| rt->tstruc != lt->tstruc) {
				errorfl(ex->tok,
					"Result of conditional operator has "
					"variable type");
				return NULL;
			}
		} else if  (rt->code != lt->code) {
			if (rt->code == TY_VOID || lt->code == TY_VOID) {
				/*
				 * 02/28/09: Allow void on one side because gcc does so too,
				 * and some programs rely on it (PostgreSQL). gcc only warns
				 * (but doesn't error) about it with -ansi
				 */
				warningfl(ex->tok, "ISO C does not allow one "
					"conditional operator operand being "
					"of type `void' when the other one isn't");
				is_void = is_void_botched = 1;
				if (rt->code != TY_VOID) {
					rt = make_basic_type(TY_VOID);
				} else {
					lt = make_basic_type(TY_VOID);
				}
			} else if (!is_arithmetic_type(rt)
				|| !is_arithmetic_type(lt)) {
				errorfl(ex->tok,
					"Result of conditional operator has "
					"variable type");
				return NULL;
			} else {
				if (convert_operands(&lres, &rres,
					left_list, right_list, TOK_OP_COND,
					ex->tok, eval) != 0) {
					return NULL;
				}	
			}
		}
	} else {
		int	bad = 0;

		if (lt->tlist == NULL || rt->tlist == NULL) {
			/* Either one must be a null pointer constant */
			if (lt->tlist == NULL
				&& !lres->is_nullptr_const) {
				bad = 1;
			} else if (rt->tlist == NULL
				&& !rres->is_nullptr_const) {
				bad = 1;
			} else {
				if (lt->tlist == NULL) {
					if (eval) {
						lres = backend->
							icode_make_cast(lres,rt,
								left_list);
					} else {
						vreg_set_new_type(lres, rt);
					}	
					lt = lres->type;
				} else {
					if (eval) {
						rres = backend->
							icode_make_cast(rres,lt,
							right_list);
					} else {	
						vreg_set_new_type(rres, lt);
					}
					rt = rres->type;
				}
			}
		} else {
			if (rres->is_nullptr_const) {		
				if (eval) {
					rres = backend->
						icode_make_cast(rres, lt,
						right_list);
				} else {
					vreg_set_new_type(rres, lt);
				}
				rt = rres->type;
			} else if (lres->is_nullptr_const) {
				if (eval) {
					lres = backend->
						icode_make_cast(lres, rt,
						left_list);
				} else {
					vreg_set_new_type(lres, rt);
				}
				lt = lres->type;
			} else {
				/* Both are pointers */
				if (compare_types(lres->type, rres->type,
					CMPTY_ALL|
					CMPTY_ARRAYPTR) != 0) {
					bad = 1;
				}
			}
		}
		if (bad) {
			errorfl(ex->tok,
				"Result of conditional operator has "
				"variable type");
			return NULL;
		}
	}
	*restype = lres->type;		

	if (!is_struct && !is_void) {
		ret->type = lres->type;
		ret->size = lres->size;
		ret->is_multi_reg_obj = lres->is_multi_reg_obj;

		if (eval) {
			vreg_faultin_x87(NULL, NULL, lres, left_list, 0); /* !!! */
			r = lres->pregs[0];
			vreg_map_preg(ret, r);
			if (lres->is_multi_reg_obj) {
				struct reg	*r2;
	
				r2 = lres->pregs[1];
				vreg_map_preg2(ret, r2);
			}
		}
	} else {
		r = NULL;
		ret->pregs[0] = NULL;
	}

	if (eval) {
		append_icode_list(left_list, left_end_jump); /* XXX */
		append_icode_list(left_list, label);

		/*
	 	 * As the type is known now, the code lists can be merged and
		 * the unified ilp is used for finishing the processing
	 	 */
		merge_icode_lists(left_list, right_list);
		merge_icode_lists(ilp, left_list);		
	}

	if (!is_void_botched) {
		if (compare_types(lres->type, rres->type, CMPTY_ALL|
				CMPTY_ARRAYPTR) != 0) {
			errorfl(ex->tok,
			"Result of conditional operator has variable type");
			return NULL;
		}
	}
	if (!is_void && !is_struct && eval) {
		if (!is_x87_trash(rres)) {
			vreg_faultin(NULL, NULL, rres, ilp, 0);
			if (rres->pregs[0] != ret->pregs[0]) {
				icode_make_copyreg(ret->pregs[0], rres->pregs[0],
					lres->type, lres->type, ilp);
				free_pregs_vreg(rres, ilp, 0, 0);
			}
		} else {
			vreg_faultin_x87(ret->pregs[0], NULL, rres, ilp, 0);
		}
		if (rres->is_multi_reg_obj) {
			if (rres->pregs[1] != ret->pregs[1]) {
				icode_make_copyreg(ret->pregs[1], rres->pregs[1],
					lres->type, lres->type, ilp);
			}
		}
	}

	if (eval) {
		append_icode_list(ilp, end_label);
		backend->invalidate_except(ilp, 1,
			0, r, (struct reg *)NULL);
	
		if (r != NULL) {
			vreg_map_preg(ret, r);
		}
	}

	if (is_struct) {
		ret->struct_ret = 1;
	} else if (is_x87_trash(ret)) {
		/*
		 * Don't keep stuff in x87 registers, ever!!!
		 */
		free_preg(ret->pregs[0], ilp, 1, 1);
	}
	return ret;
}	



static int 
do_cond(
	struct expr *cond,
	struct icode_list *il,
	struct control *ctrl,
	struct vreg *have_cmp);

struct vreg *
expr_to_icode(
	struct expr *ex,
	struct vreg *lvalue, 
	struct icode_list *ilp,
	int purpose,
	int resval_not_used,
	int eval) {

	struct icode_instr	*ii = NULL;
	struct icode_instr	*label;
	struct icode_instr	*label2;
	struct vreg		*lres = NULL;
	struct vreg		*rres = NULL;
	struct vreg		*ret = NULL;
	struct type		*restype = NULL;
	struct type		*ltold;
	struct type		*rtold;
	static int		level;
	struct reg		*r;
	int			tmpop;
	struct vreg		*temp_rres = NULL;
	struct vreg		*temp_lres = NULL;
	int			changed_vrs = 0;

	if (level++ == 0 && eval) {
		/* Initialize allocator */
		/*
		 * XXX July 2007: This SUCKS! I wasn't aware it's still
		 * here, but it broke inline asm because registers were
		 * not saved but just marked unused. Saving them also
		 * caused problems with multi-gpr long longs converted
		 * to floating point variables... Maybe we should keep
		 * this for some time to debug ``register leak''
		 * problems (i.e. assume something is wrong if the call
		 * below ever does save anything to the stack), and
		 * then get rid of it
		 */
#if FEAT_DEBUG_DUMP_BOGUS_STORES 
		backend_warn_inv = 1;
#endif
		backend->invalidate_gprs(ilp, /*0*/ 1, 0);
#if FEAT_DEBUG_DUMP_BOGUS_STORES 
		backend_warn_inv = 0;
#endif
	}

	if (ex->op != 0) {
		struct operator	*operator;

		operator = &operators[LOOKUP_OP2(ex->op)];

		if (ex->op != TOK_OP_LAND
			&& ex->op != TOK_OP_LOR
			&& !IS_ASSIGN_OP(ex->op)
			&& ex->op != TOK_OP_COND) {

			/* 06/14/09: Don't pass through ``purpose'' */
			lres = expr_to_icode(ex->left, NULL, ilp,
				/*purpose*/0, 0, eval);
#if 0
			hmm this is ABSOLUE nonsense!?!?
			if (is_x87_trash(lres)) {
				free_pregs_vreg(lres, ilp, 1, 1);
			}
#endif

			/* 06/14/09: Don't pass through ``purpose'' */
			rres = expr_to_icode(ex->right, NULL,
				ilp, /*purpose*/0, 0, eval);
			if (is_x87_trash(rres)) {
				free_pregs_vreg(rres, ilp, 1, 1);
			}
			if (lres == NULL || rres == NULL) {
				if (ilp) {
					/* XXX free */
				}
				--level;
				return NULL;
			}

			/* A promotion may be in order */
			if (ex->op != TOK_OP_COMMA) {
				/*
				 * Check whether we have a struct or union -
				 * those cannot be promoted, or decay into
				 * pointers
				 */
				ltold = lres->type;
				rtold = rres->type;

				if (((ltold->code == TY_STRUCT
					|| ltold->code == TY_UNION)	
					&& ltold->tlist == NULL)
					||
					((rtold->code == TY_STRUCT
					|| rtold->code == TY_UNION)
					&& rtold->tlist == NULL)) {
					errorfl(ex->tok,
						"Operator `%s' does not work "
						"with union/struct types!",
						ex->tok->ascii);
					return NULL;
				}
				
				if ((restype = promote(&lres, &rres,
					ex->op, ex->tok, ilp, eval)) == NULL) {
					--level;
					return NULL;
				}
							
				debug_print_conv(ltold, rtold, ex->op,restype);
			} else {
				restype = rres->type;
			}
		}	

		switch (ex->op) {
		case TOK_OP_COMMA:
			/* Comma operator */

			if (lres->pregs[0]) {
				free_pregs_vreg(lres, ilp, 0, 0);
			}
			ret = rres; /* XXX */
			break;
		case TOK_OP_ASSIGN:
		case TOK_OP_COPLUS:
		case TOK_OP_COMINUS:
		case TOK_OP_CODIVIDE:
		case TOK_OP_COMULTI:
		case TOK_OP_COMOD:
		case TOK_OP_COBAND:
		case TOK_OP_COBOR:
		case TOK_OP_COBXOR:
		case TOK_OP_COBSHL:
		case TOK_OP_COBSHR:
			/* Assignment & compound assignment operators */
			
			if (ex->left->op != 0) {
				errorfl(ex->left->tok,
					"Bad lvalue in assignment");
				--level;
				return NULL;
			}

			/*
			 * 08/11/08: Pass intent to assign, so that bitfield
			 * lvalues are not promoted
			 */
			if ((lres = expr_to_icode(ex->left, NULL, ilp, TOK_OP_ASSIGN, 0, eval))
				== NULL) {
				--level;
				return NULL;
			}
			if (!ex->left->data->is_lvalue) {
				errorfl(ex->tok,
			"Left operand in assignment is not an lvalue");
				--level;
				return NULL;
			}
			lres = ex->left->data->res;

			/* 06/14/09: Don't pass through ``purpose'' */
			rres = expr_to_icode(ex->right, lres, ilp,
				/*purpose*/0, 0, eval);
			if (rres == NULL) {
				--level;
				return NULL;
			}


			if (rres->struct_ret) {
				/*
				 * Was returned by function call or
				 * conditional operator - has already
				 * been assigned
				 */
				rres->struct_ret = 0;

				/*
				 * 08/02/07: This wronly returned rres
				 * instead of lres as result!!! Thus when
				 * the left side indirects through a pointer,
				 * any vreg_faultins() working on the right
				 * result will not load the pointer, and
				 * a stale pointer may be used!!!
				 *
				 *     foo = bar[0] = baz;
				 *
				 * ... if we wrongly do foo = baz, &bar[0]
				 * may not be loaded correctly. Bombed in 
				 * GNU tar code
				 */
				ret = lres;
			} else {
				int	savedop = ex->op;

				tmpop = ex->op;

				if (!can_transform_to_bitwise(&lres, &rres,
					&tmpop, ilp)) {
					/* 07/03/08: Eval */
					if (rres->from_const && eval) {
						vreg_anonymify(&rres, NULL,
							NULL, ilp);
					}
				}
				ex->op = tmpop;

				if ((ret = do_assign(lres, rres, ex, ilp,
					level, purpose, eval)) == NULL) {
					--level;
					return NULL;
				}	
				ex->op = savedop;
			}
			restype = ret->type;
			break;
		case TOK_OP_LAND:
		case TOK_OP_LOR:
			/* Short circuit operators */

			/*
			 * foo && bar
			 * generates instruction lists for foo and bar,
			 * creates a label at the end of the bar list
			 * and connects both lists through a conditional
			 * jump to that label
			 *
			 * 06/14/09: Don't pass through the ``purpose''!
			 * Otherwise e.g.
			 *
			 *    (s->bitfield && s->bitfield2)
			 *
			 * breaks because with purpose beging TOK_PAREN_OPEN
			 * we assume this is a parenthesized sub-expression
			 * which cannot be decoded yet
			 */
			lres = expr_to_icode(ex->left, NULL, ilp,
				/*purpose*/0, 0, eval);
			if (lres == NULL) {
				--level;
				return NULL;
			}	

			if (eval) {
				/*
				 * 03/03/09: Invalidate items before executing
				 * the second part! This is needed in cases like
				 * this:
				 *    
				 *    - Assume an expression like
				 *       printf("%d\n", var && func());
				 *
				 *    - This will first load the format string
				 *    - Then it will evaluate var
				 *    - Then it may or may not evaluate func()
				 *      since && short-circuits if the first
				 *      operand is 0
				 *
				 * What may happen, then, is that the second
				 * part of the operator - the part that may or
				 * may not be executed - causes a register
				 * invalidation. This would then save the format
				 * string which was loaded prior to evaluating
				 * the && expression. That causes the format
				 * string vreg to be associated with a stack
				 * save location ___WHICH MAY NOT ACTUALLY HOLD
				 * THE VALUE___ because the code that saves it
				 * is conditional.
				 *
				 * SO we perform a general GPR invalidation
				 * prior to doing the conditional jump to ensure
				 * that external items (to this expression) can
				 * only be associated with real save locations
				 *
				 * XXX Don't invalidate lres
				 */
				backend->invalidate_gprs(ilp, 1, 0);

				label2 = icode_make_label(NULL);
				if (!is_x87_trash(lres) && eval) {
					vreg_faultin(NULL, NULL, lres, ilp, 0);
				}
				if (ex->op == TOK_OP_LAND) {
					label = branch_if_zero(lres, INSTR_BR_EQUAL,
						NULL, ilp);
				} else {	
					label = branch_if_zero(lres, INSTR_BR_NEQUAL,
						NULL, ilp);
				}	
				free_pregs_vreg(lres, ilp, 0, 0);
			}

			/*
			 * 06/14/09: Don't pass through the ``purpose''!
			 * Otherwise e.g.
			 *
			 *    (s->bitfield && s->bitfield2)
			 *
			 * breaks because with purpose beging TOK_PAREN_OPEN
			 * we assume this is a parenthesized sub-expression
			 * which cannot be decoded yet
			 */
			rres = expr_to_icode(ex->right, NULL, ilp,
				/*purpose*/ 0, 0, eval);
			if (rres == NULL) {
				--level;
				return NULL;
			}	
			

			/* The result of these operators has type int */
			ret = vreg_alloc(NULL, NULL, NULL, NULL);
			ret->type = make_basic_type(TY_INT);
			ret->size = backend->get_sizeof_type(ret->type, NULL);
			if (eval) {
				r = ALLOC_GPR(curfunc, ret->size, ilp, NULL);
				vreg_map_preg(ret, r);
				reg_set_unallocatable(r);

				if (!is_x87_trash(rres)) {
					vreg_faultin(NULL, NULL, rres, ilp, 0);
				}

				reg_set_allocatable(r);
				if (ex->op == TOK_OP_LAND) {
					branch_if_zero(rres, INSTR_BR_EQUAL,
						label, ilp);
					ii = icode_make_setreg(r, 1);
				} else {	
					branch_if_zero(rres, INSTR_BR_NEQUAL,
						label, ilp);
					ii = icode_make_setreg(r, 0);
				}	
				append_icode_list(ilp, ii);
				ii = icode_make_jump(label2);
				append_icode_list(ilp, ii);

				append_icode_list(ilp, label);
				if (ex->op == TOK_OP_LAND) {
					ii = icode_make_setreg(r, 0);
				} else {
					ii = icode_make_setreg(r, 1);
				}	
				append_icode_list(ilp, ii);
				append_icode_list(ilp, label2);
				backend->invalidate_except(ilp, /*level == 1*/1, 0, r,
					(struct reg *)NULL);
				vreg_map_preg(ret, r);
			}	
			break;
		case TOK_OP_MINUS:
		case TOK_OP_PLUS:
		case TOK_OP_MULTI:
		case TOK_OP_DIVIDE:
		case TOK_OP_MOD:
		case TOK_OP_BSHL:
		case TOK_OP_BSHR:
		case TOK_OP_BAND:
		case TOK_OP_BXOR:
		case TOK_OP_BOR:
			/* Arithmetic and bitwise operators */

			tmpop = ex->op;
			/* WARNING: Order of faultins below matters */

			/* 07/03/08: Eval */
			if (eval) {
				if (is_x87_trash(lres)) {
					;
				} else if (can_transform_to_bitwise(&lres, &rres,
						&tmpop, ilp)) {
					/*
					 * 04/13/08: Transform operations if possible.
					 * Note that faultins are already handled by
					 * the transformation function
					 */
					operator = &operators[LOOKUP_OP2(tmpop)];
					backend->icode_prepare_op(&lres, &rres,
						tmpop, ilp);
				} else {
					if (rres->from_const) {
						vreg_anonymify(&rres, NULL,
							NULL, ilp);
					} else if (lres->from_const) {
						vreg_anonymify(&lres, NULL,
							NULL, ilp);
					}
	
					vreg_faultin(NULL, NULL, lres, ilp, tmpop);
					vreg_faultin_protected(lres, NULL, NULL,
						rres, ilp, 0);
	
					backend->icode_prepare_op(&lres, &rres,
						tmpop, ilp);
				}
			}

			changed_vrs = emul_conv_ldouble_to_double(&temp_lres, &temp_rres,
				lres, rres, ilp, eval);

			if (tmpop == TOK_OP_PLUS
				|| tmpop == TOK_OP_MINUS) {
				do_add_sub(&temp_lres, &temp_rres, tmpop, ex->tok, ilp,
					eval);
				ii = NULL;
			} else if (tmpop == TOK_OP_MULTI
				|| tmpop == TOK_OP_DIVIDE
				|| tmpop == TOK_OP_MOD) {
				if (do_mul(&temp_lres, temp_rres, operator,
					ex->tok, ilp, eval)) {
					--level;
					return NULL;
				}

				ii = NULL;
			} else if (tmpop == TOK_OP_BSHL
				|| tmpop == TOK_OP_BSHR
				|| tmpop == TOK_OP_BAND
				|| tmpop == TOK_OP_BOR
				|| tmpop == TOK_OP_BXOR) {
				if (do_bitwise(&temp_lres, temp_rres, operator, ex->tok,
					ilp, eval)) {
					--level;
					return NULL;
				}
				ii = NULL;
			}

			/* 07/03/08: Eval */
			if (eval) {
				if (ii != NULL) {
					append_icode_list(ilp, ii);
				}

				if (changed_vrs) {
					/*
					 * 11/24/08: Convert long double value back
					 * to original type (it was emulated using
					 * double0
					 */
					lres = backend->icode_make_cast(temp_lres,
						make_basic_type(TY_LDOUBLE),
						ilp);
					rres = temp_rres;
				} else {
					lres = temp_lres;
					rres = temp_rres;
				}

				if (rres->pregs[0]) {
					/*
					 * XXX free_pregs_vreg() causes unbacked
					 * register problems with cpu_mips.c and
					 * I do not understand why. The problem
					 * is probably elsewhere
					 */
					free_pregs_vreg(rres, ilp, 1, 0);
				}	
			} else {
				lres = temp_lres;
				rres = temp_rres;
			}

			ret = lres;

			/* 07/03/08: Eval */
			if (eval) {
				if (is_x87_trash(ret)) {
					;
				} else {
					vreg_faultin(NULL, NULL, ret, ilp, 0);
					vreg_map_preg(ret, ret->pregs[0]);
					if (lres->pregs[1]) {
						vreg_map_preg2(ret, ret->pregs[1]);
					}
				}	
			}
			restype = ret->type;
			break;
		case TOK_OP_COND:
			/* Conditional operator */
			ret = do_cond_op(ex, &restype, lvalue, ilp, eval);
			if (ret == NULL) {
				--level;
				return NULL;
			}	
			break;
		case TOK_OP_LEQU:
		case TOK_OP_LNEQU:
		case TOK_OP_GREAT:
		case TOK_OP_SMALL:
		case TOK_OP_GREATEQ:
		case TOK_OP_SMALLEQ:
			/* Equality and relational operators */
			r = NULL;
			ret = vreg_alloc(NULL,NULL,NULL,NULL);
			ret->type = make_basic_type(TY_INT);
			ret->size = backend->get_sizeof_type(ret->type, NULL);
			
			/* 07/03/08: Eval */
			if (eval) {
				if (purpose != TOK_KEY_IF || level != 1) {
					/*
					 * Need to allocate gpr so it isn't wiped out
					 * by faultins below
					 */
					r = ALLOC_GPR(curfunc, ret->size, ilp, NULL);
					reg_set_unallocatable(r);
				}
				if (is_x87_trash(lres)) {
					vreg_faultin_x87(NULL, NULL, lres, ilp, 0);
					vreg_map_preg(lres, &x86_fprs[1]);
					vreg_faultin_x87(NULL, NULL, rres, ilp, 0);
				} else {	
					vreg_faultin(NULL, NULL, lres, ilp, 0);
					vreg_faultin_protected(lres, NULL, NULL, rres, ilp, 0);
				}


				/*
				 * 12/29/08: On PPC, where long double is
				 * emulated using double, we want to compare
				 * as double - so convert both operands to
				 * it
				 *
				 * 07/15/09: This applies to MIPS as well now
				 */
				if (backend->emulate_long_double
					&& lres->type->code == TY_LDOUBLE
					&& lres->type->tlist == NULL) {
					lres = backend->icode_make_cast(lres,
						make_basic_type(TY_DOUBLE), ilp);
					rres = backend->icode_make_cast(rres,
						make_basic_type(TY_DOUBLE), ilp);
				}
			}

			
			if (r != NULL && eval) {
				reg_set_allocatable(r);
			}
		
			/* 07/03/08: Eval */
			if (eval) {
				if (purpose == TOK_KEY_IF && level == 1) {
					/*
					 * We want to generate the expected cmp + je
					 * for ``if (stuff == stuff)'' so the caller
					 * has to check for this logical operator and
					 * generate the branch himself
					 */
					ii = icode_make_cmp(lres, rres);
					append_icode_list(ilp, ii);
					free_pregs_vreg(rres, ilp, 0, 0);
				} else {
					static struct control	dummy;
					/*
					 * Generate kludgy
					 * cmp dest, src
					 * mov res, 0
					 * jXX label
					 * mov res, 1 
					 * label:
					 * ... for expressions where the
					 * result of the operator is used by
					 * subsequently applied operators
					 * XXX Again, conditional mov would be
					 * much better ...
					 */
					vreg_map_preg(ret, r);

					label = icode_make_label(NULL);

					/*
					 * 06/29/08: This code path still had manual 
					 * multi-register handling, and wasn't corrected
					 * during all of those long long bug fixes for
					 * the other case. Therefore, we call do_cond()
					 * here now as well
					 */
					dummy.type = TOK_KEY_IF;
					dummy.endlabel = label;

					ii = icode_make_setreg(r, 0);
					append_icode_list(ilp, ii);
					ii = icode_make_cmp(lres, rres);
					append_icode_list(ilp, ii);
					do_cond(ex, ilp, &dummy, ret);
					ii = icode_make_setreg(r, 1);
					append_icode_list(ilp, ii);
					append_icode_list(ilp, label);

					free_pregs_vreg(lres, ilp, 0, 0);
					free_pregs_vreg(rres, ilp, 0, 0);
				}
			}
			restype = NULL;
			break;
		}
	} else if (ex->data != NULL) {
		int	standalone_subexpr;
		
		/*
		 * 04/12/08: Tell s_expr_to_icode() whether this is a
		 * ``standalone'' expression whose value is not used.
		 * That allows us to optimize ``i--;'' to ``--i;''
		 */
		if (resval_not_used && level == 1) {
			standalone_subexpr = 1;
		} else {
			standalone_subexpr = 0;
		}

		ex->data->code = alloc_icode_list();
		ret = s_expr_to_icode(ex->data, lvalue,
			ex->data->code, standalone_subexpr, eval);
		if (eval) {
			if ((ilp->res = ret) == NULL) {
				--level;
				return NULL;
			}
		}
		if (eval) merge_icode_lists(ilp, ex->data->code);
		if (ret != NULL) {
			/*
			 * 07/03/08: This restype assignment was missing, so
			 * the array/function decay below never happened. But
			 * now we sometimes have to do it even for something
			 * that looks like a sub-expression
			 */
			if (ex->data->flags & SEXPR_FROM_CONST_EXPR) {
/*				restype = ret->type;*/
			}
		}
	} else if (ex->stmt_as_expr != NULL) {
		/*
		 * GNU statement-as-expression
		 */
		struct icode_list	*tmp;

		++doing_stmtexpr;
		if (!eval) {
			/*
			 * 07/22/08: Implemented this case
			 */
			struct statement	*s;
			struct scope		*sc;

			/*
			 * stmt_as_expr is a scope containg the compound
			 * statement as body
			 */
			s = ex->stmt_as_expr->code;
			sc = s->data;

			/*
			 * Iterate statement list
			 *
			 * XXX we should check the components for correctness
			 * here!
			 */
			for (s = sc->code; s->next != NULL; s = s->next) {
				;
			}
			if (s->type == ST_CODE) {
				/* Expression statement - return type */
				return expr_to_icode(s->data, NULL, NULL, 0, 0, eval);
			} else {
				/*
				 * The last part of this statement-as-expression
				 * is something other than an expression, e.g. a
				 * declaration
				 */
				return NULL;
			}
		} else {
			/*
			 * 07/25/09: We need to invalidate and save GPRs!
			 * Otherwise things like
			 *
			 *     ptr->m = ({ .... });
			 *
			 * ... may trash the loaded pointer value before
			 * writing through it. It's not quite clear yet
			 * why this happens, but may have something to
			 * do with the statement getting compiled as
			 * ``top level'' statement with no regard for
			 * the surrounding expression
			 */
			backend->invalidate_gprs(ilp, 1, 0);  /* save */
			tmp = xlate_to_icode(ex->stmt_as_expr->code, 0);
			if (tmp == NULL) {
				--level;
				return NULL;
			}	
			merge_icode_lists(ilp, tmp);
			ret = ilp->res;
		}
		if (ret == NULL) {
			/*
			 * Last statement in compound statement wasn't an
			 * expression; treat like void
			 */
			ret = vreg_alloc(NULL, NULL, NULL, NULL);
			ret->type = make_basic_type(TY_VOID);
		}
		--doing_stmtexpr;
	} else {
		puts("BUG: Empty expression passed to expr_to_icode() :(");
		abort();
	}
	if (eval && ret) {
		ilp->res = ret;
	}
	if (--level == 0 && eval) {
		if (ilp->res->pregs[0] != NULL
			&& ilp->res->pregs[0]->vreg == ilp->res) {
			vreg_map_preg(ilp->res, ilp->res->pregs[0]);
		}
	}

#if 0
	} else if (ex->data != NULL) {
#endif
	if (restype != NULL) {
		size_t	newsize;

		/* XXX is check for only ``void'' sufficient!??! */
		if (restype->code != TY_VOID
			|| restype->tlist != NULL) {
			if (IS_VLA(restype->flags) && is_immediate_vla_type(restype)) {
				newsize = 0;
			} else {	
				newsize = backend->get_sizeof_type(restype, NULL);
			}	
		} else {
			newsize = 0;
		}	

		ex->type = restype;		

		/*
		 * 07/03/08: Do this for some sub-expressions as well
		 */
		if (ilp && ilp->res) {
			if (newsize != 0
				&& ((ilp->res->type->code != TY_STRUCT
				&& ilp->res->type->code != TY_UNION)
				|| ilp->res->type->tlist != NULL)) {
				if (is_x87_trash(ilp->res)) {
#if 0
					ilp->res = vreg_disconnect(ilp->res);
#endif
					ilp->res = x87_anonymify(ilp->res, ilp);
				} else {	
					/*
					 * 04/12/08: Only anonymify if we
					 * don't have a constant
					 */
					if (ilp->res->from_const == NULL
						|| ilp->res->from_const->type
						== TOK_STRING_LITERAL
						|| Oflag == -1) {
						vreg_anonymify(&ilp->res, NULL, NULL,
							ilp);
					}
				}	
			}	
			ilp->res->type = restype;
			ilp->res->size = newsize;
		} else if (eval) {
			puts("BUG!!!");
	printf("doing operator %d!\n", ex->op);		
			abort();
		}	
	}


	/*
	 * 01/13/08: Perform array/function decay. Now we don't check ``eval''
	 * anymore, because it is nonsense, since the decay also happens in
	 * unevaluated expressions, such as
	 *
	 *     char buf[128];   foo? buf: buf;
	 *
	 * purpose = TOK_PAREN_OPEN means this call is the top-level call for
	 * a parenthesized sub-expressions, e.g.   sizeof(buf)
	 *
	 * 07/03/08: Extended this for sub-expressions which came from
	 * partially constant larger expressions, currently just to make
	 *
	 *     sizeof (0? buf: buf)      return sizeof(char[128]) and
	 *     sizeof (buf)              return sizeof(char *)
	 *
	 * 05/13/09: We have to do this for the comma operator too!
	 * XXX It's botched in various ways, ``ret = rres;'' seems very
	 * nonsense (and gave us these array/function decay problems if
	 * purpose = TOK_PAREN_OPEN... Not sure why ``(foo, bar)'' was
	 * skipped with TOK_PAREN_OPEN
	 *
	 * 06/14/09: This wasn't decoding parenthesized bitfields, so
	 * added check for tbit != NULL.
	 *
	 *    printf("%d\n", (sp->bf && sp->bf2));  <--- failed
	 */
	if ( (purpose != TOK_PAREN_OPEN
		/*|| ret->type->tbit != NULL*/) 
		|| ex->op == TOK_OP_COMMA
		|| (ex->data != NULL && ex->data->flags & SEXPR_FROM_CONST_EXPR)) {

		if (ret->type->tlist != NULL
			&& (ret->type->tlist->type == TN_ARRAY_OF
				|| ret->type->tlist->type == TN_FUNCTION
				|| ret->type->tlist->type == TN_VARARRAY_OF)) {
			struct type	*ty;
			int		is_array;

			if (ret->type->tlist->type == TN_ARRAY_OF
				|| ret->type->tlist->type == TN_VARARRAY_OF) {
				is_array = 1;
			} else {
				is_array = 0;
			}

			/*
			 * Arrays and functions (01/13/08) decay into
			 * pointers
			 */
			if (eval) {
				/*
				 * 01/13/08: XXX hmmm seems vreg_anonymify
				 * also alters the type already?? This
				 * sucks
				 */
				vreg_anonymify(&ret, NULL, NULL, ilp);
			}
			ty = n_xmemdup(ret->type, sizeof *ret->type);
			copy_tlist(&ty->tlist, ret->type->tlist); 
			if (is_array) {
				ty->tlist->type = TN_POINTER_TO;
			} else {
				static struct type	dummy;

				/* Append new pointer node */
				append_typelist(&dummy, TN_POINTER_TO, 0, NULL, NULL);
				dummy.tlist->next = ty->tlist;
				ty->tlist = dummy.tlist;
			}

			ret->type = ty;
			ret->size = backend->get_sizeof_type(ret->type, NULL);
			if (eval) {
				ilp->res = ret;
			}

			/*
			 * 08/24/08: This was missing; Array decay does not
			 * yield an lvalue, so without resetting the flag, 
			 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX It's not clear
			 * whether altering the s_expr flag here could
			 * yield problems! If the expression is evaluated
			 * twice (for example once to try it as a constant
			 * expression, and a second time if that failed),
			 * then the second evaluation will start out with
			 * a different is_lvalue, which may or may not be
			 * a problem
			 */
			if (ex->data != NULL && ex->data->is_lvalue) {
				ex->data->is_lvalue = 0;
			}
		} else if (ret->type->tbit != NULL) {
			/*
			 * 08/11/08: Handle bitfield promotion
			 * (only if it isn't the lvalue in an assignment)
			 */
			if (purpose != TOK_OP_ASSIGN && !resval_not_used) {
				if (eval) {
					ret = promote_bitfield(ret, ilp);
					ret = backend->icode_make_cast(ret, 
						cross_get_bitfield_promoted_type(ret->type), ilp);
					ilp->res = ret;
				} else {
					vreg_set_new_type(ret, cross_get_bitfield_promoted_type(ret->type));
				}
			}
		}
	}

	return eval? ilp->res: ret;
}

static int 
do_cond(
	struct expr *cond,
	struct icode_list *il,
	struct control *ctrl,
	struct vreg *have_cmp) {

	struct icode_instr	*ii;
	struct icode_instr	*dest_label;
	struct icode_instr	*lastinstr;
	struct icode_instr	*multi_reg_label = NULL;
	struct vreg		*res;
	struct vreg		*lower_vreg = NULL;
	int			positive;
	int			btype = 0;
	int			first_btype = 0;
	int			is_multi_reg_obj;
	int			saved_btype;
	int			have_multi_reg_cmp;
	int			second_is_greater_than = 0;


	if (have_cmp != NULL) {
		res = have_cmp;
	} else {	
		if ((res = expr_to_icode(cond, NULL, il, TOK_KEY_IF, 0, 1)) == NULL) {
			return -1;
		}
	}
	lastinstr = il->tail;


	if (lastinstr != NULL
		&& lastinstr->type == INSTR_CMP
		&& lastinstr->dest_vreg->is_multi_reg_obj) {
		have_multi_reg_cmp = 1;
	} else {
		have_multi_reg_cmp = 0;
	}
	
	if (res->is_multi_reg_obj || have_multi_reg_cmp) {
		/*
		 * This complicates things greatly. If we compare
		 * the registers in the right order (XXX big vs
		 * little endian), relational
		 * operators+branches in a row still work as
		 * expected. However, with equality and inequality,
		 * the first comparison cannot branch to the
		 * target already, because the second register may
		 * still differ.
		 *
		 * Thus instead of
		 * cmp foo, bar
		 * je label
		 * ... we do
		 * cmp foo[0], bar[0]
		 * jne end
		 * cmp foo[1], bar[1]
		 * je label
		 * end:
		 */
		is_multi_reg_obj = 1;
	} else {
		is_multi_reg_obj = 0;
	}

	if (ctrl->type == TOK_KEY_IF
		|| ctrl->type == TOK_KEY_WHILE
		|| ctrl->type == TOK_KEY_FOR) {
		/*
		 * for/while loop or if statement - branch
		 * if condition negative
		 */
		positive = 0;
	} else {
		/* 
		 * do-while loop - branch if condition
		 * positive
		 */
		positive = 1;
	}

	/*
	 * When dealing with relational operators that
	 * occur at the top-level in an expression,
	 * expr_to_icode() already does the branch-
	 * determining cmp such that we can already
	 * branch and get say for ``stuff == stuff'':
	 * ``cmp stuff, stuff; jne end;'' rather than
	 * computing 1 (true) or 0 (false) first and
	 * comparing that against 0 
	 */
	 if (cond->op == 0) {
		goto cmp_zero;
	 } else if (0 /*doing_stmtexpr*/) {
		/*
		 * expr_to_icode() uses a static variable ``level''
		 * to record the recursive call depth. That doesn't
		 * work with GNU C statement-as-expression, where in
		 * ({ if (x == 0) ; })
		 * the if controlling expression is parsed starting
		 * with level = 1.
		 * As a temporary workaround, we explicitly compare
		 * with zero
		 * *
		 * 06/29/08: Removed this part again. I'm not quite sure
		 * what this means, and whether it's still current with
		 * the latest change. The latest change - i.e. to call
		 * do_cond() for nested equality evaluation (creating
		 * 1 or 0 instead of cmp + jump) made this bit wrong,
		 * and hopefully solved the other issues which this
		 * part was supposed to fix before that
		 */
		goto cmp_zero;
	 } else if (cond->op == TOK_OP_LEQU) {
		if (positive) btype = INSTR_BR_EQUAL;
		else btype = INSTR_BR_NEQUAL;
	 } else if (cond->op == TOK_OP_LNEQU) {
		if (positive) btype = INSTR_BR_NEQUAL;
		else btype = INSTR_BR_EQUAL;
	 } else if (cond->op == TOK_OP_GREAT) {
		if (positive) btype = INSTR_BR_GREATER;
		else btype = INSTR_BR_SMALLEREQ;
	 } else if (cond->op == TOK_OP_SMALL) {
		if (positive) btype = INSTR_BR_SMALLER;
		else btype = INSTR_BR_GREATEREQ;
	 } else if (cond->op == TOK_OP_GREATEQ) {
		if (positive) btype = INSTR_BR_GREATEREQ;
		else btype = INSTR_BR_SMALLER;
	 } else if (cond->op == TOK_OP_SMALLEQ) {
		if (positive) btype = INSTR_BR_SMALLEREQ;
		else btype = INSTR_BR_GREATER;
	 } else {
cmp_zero:
		/* 
		 * Branch if condition false (0) and we're
		 * doing an if. Otherwise branch if condition
		 * true in case of do-while(to top of loop.)
		 */
#if 0
		vreg_faultin_protected(res, NULL, NULL, res, il, 0); 
#endif
#if 0 
		vreg_faultin(NULL, NULL, res, il, 0); 
		ii = icode_make_cmp(res, NULL);
		lastinstr = ii;
#endif
		vreg_faultin_x87(NULL, NULL, res, il, 0);
		lastinstr = ii = compare_vreg_with_zero(res, il);
		
		append_icode_list(il, ii);
		if (positive) btype = INSTR_BR_NEQUAL;
		else btype = INSTR_BR_EQUAL;
	}

	if (ctrl->type == TOK_KEY_DO) {
		dest_label = ctrl->startlabel;
	} else {
		dest_label = ctrl->endlabel;
	}


	first_btype = btype;

	/*
	 *  
	 * negative:  
	 * for equality
	 *
	 * if (foo == bar) {
	 *     body;
	 * }
	 *
	 * becomes
	 *
	 * if (foo[0] != bar[0]) {
	 *     goto no;
	 * }
	 * if (foo[1] != bar[1]) {
	 *     goto no;
	 * }
	 *     body
	 * no:
	 *
	 *
	 *
	 * inequality:
	 *
	 * if (foo != bar) {
	 *     body;
	 * }
	 *
	 * becomes
	 *
	 * if (foo[0] != bar[0]) {
	 *    goto yes;
	 * }
	 * if (foo[1] != bar[1]) {
	 *    goto no;
	 * }
	 * yes:
	 *     body;
	 * no:
	 */
	if (is_multi_reg_obj) {
		struct vreg	*res_vr;
		int		need_another_cmp = 0;

		if (lastinstr) {
			res_vr = lastinstr->dest_vreg;
		} else {
			res_vr = res;
		}	
		if (IS_LLONG(res_vr->type->code)) {
			/*
			 * Comparisons of the lower word have to be done
			 * unsigned! Otherwise stuff like
			 *    if (1LL > 0xffffffff) {
			 * goes wrong
			 */

#if 0
			lower_vreg = n_xmemdup(res_vr, sizeof *res_vr);
#endif
			lower_vreg = copy_vreg(res_vr);

			lower_vreg->type = n_xmemdup(res_vr->type,
				sizeof *res_vr->type);
			lower_vreg->type->sign = TOK_KEY_UNSIGNED;
			lower_vreg->type->code = TY_ULLONG;
		}	
		if (btype == INSTR_BR_EQUAL
			|| btype == INSTR_BR_NEQUAL) {
			/*
		 	 * The first true comparison does not allow us to
			 * jump to the target yet; Instead the first one
			 * only determines whether the branch is already
			 * known not to be taken (branch to label after
			 * second cmp), or may be taken (fall through to
			 * next cmp.) Thus the jump condition is reversed
			 * if the ``positive'' flag is set, otherwise not.
			 *
			 * if (llong_value == 123ll) {
			 *     ...
			 * }
			 *
			 * ... here ``positive'' is 0, meaning the branch
		 	 * is NOT taken if the condition is true (the
			 * flow of control ``falls through'' into the
			 * statement body. Here both cmp+branch
			 * instructions of a ``long long'' are allowed to
			 * branch if they do not yield equality
			 *
			 * Yes, this is confusing.
			 */
			if (positive) {
				if (btype == INSTR_BR_EQUAL) {
					/*btype = INSTR_BR_NEQUAL;*/
					btype = INSTR_BR_NEQUAL;
					multi_reg_label = icode_make_label(NULL);
				} else { /* is != */
					/*btype = INSTR_BR_EQUAL;*/
				}
			} else {
				/*
				 * Branch if condition false. Note that the
				 * branch type is already reversed, i.e. if
				 * it's NEQUAL we really had a ``==''
				 */
				if (btype == INSTR_BR_NEQUAL) {
				} else { /* equal, i.e. ``!='' */
					/*btype = INSTR_BR_NEQUAL;*/
					btype = INSTR_BR_NEQUAL;
					multi_reg_label =
						icode_make_label(NULL);
				}	
			}	
		} else if (btype == INSTR_BR_GREATEREQ
			|| btype == INSTR_BR_SMALLEREQ) {
			/*

			 * If we're looking for a greater/smaller-or-equal
			 * relation, the first comparison has to omit the
			 * ``equal'' part, because otherwise we'd get false
			 * hits. Consider comparing the values 123 and 456;
			 * The higher word is 0 in both cases, so the equal
			 * part would trigger
			 */
#if 0
			if (btype == INSTR_BR_GREATEREQ) {
				btype = INSTR_BR_GREATER;
			} else { /* smalleq */
				btype = INSTR_BR_SMALLER;
			}
#endif
			multi_reg_label =
				icode_make_label(NULL);

			/*
			 * 07/20/08: This was missing! At least for < on signed
			 * long long
			 */
			if (IS_LLONG(res_vr->type->code) && res_vr->type->tlist == NULL
				&& (cond->op == TOK_OP_GREAT
				|| cond->op == TOK_OP_SMALL)) {
				need_another_cmp = 1;
			}
		} else if (btype == INSTR_BR_GREATER
			|| btype == INSTR_BR_SMALLER) {
			multi_reg_label =
				icode_make_label(NULL);
			/*
			 * This was MISSING;
			 * 06/02/08: We have to generate for
			 * greater or equal signed:
			 * 
			 *    jump below signed (for upper) <-- was missing!
			 *    jump greater signed (for upper)
			 *    jump below unsigned (for lower) 
			 *
			 * and for smaller or equal signed:
			 *
			 *    jump greater signed (for upper) <-- was missing!
			 *    jump below unsigned (for upper)
			 *    jump above unsigned (for lower)
			 *
			 * 06/29/08: This was wrongly only done for signed
			 * long long, not unsigned!
			 */
			if (IS_LLONG(res_vr->type->code) && res_vr->type->tlist == NULL
				&& (cond->op == TOK_OP_GREATEQ
				|| cond->op == TOK_OP_SMALLEQ)) {
				need_another_cmp = 1;
			}
		}

		if (need_another_cmp) {
			/*ii = copy_icode_instr(lastinstr);*/
			if (lastinstr && lastinstr->type == INSTR_CMP) {
				ii = icode_make_branch(dest_label,
					btype, lastinstr->dest_vreg);
			} else {
				ii = icode_make_branch(dest_label,
					btype, res);
			}

			if (cond->op == TOK_OP_SMALL && ii->type == INSTR_BR_GREATEREQ) {
				/*
				 * 07/20/08: For <, this gives us greater-or-
				 * equal for the first comparison. But we want
				 * greater!
				 */
				ii->type = INSTR_BR_GREATER;
			} else if (cond->op == TOK_OP_GREAT && ii->type == INSTR_BR_SMALLEREQ) {
#if 0
				/*
				 * 07/20/08: For >, this gives us less-or-
				 * equal for the first comparison. But we want
				 * less!
				 */
				ii->type = INSTR_BR_GREATER;
#endif
				ii->type = INSTR_BR_SMALLER;
				second_is_greater_than = 1;
			}

			append_icode_list(il, ii);

			/*
			 * Now create another compare instruction for
			 * the next branch
			 */
			ii = copy_icode_instr(lastinstr);
			append_icode_list(il, ii);

			/*
			 * Make sure that both the original instruction
			 * and the copied one refer to the UPPER dword
			 * of the long long! That's necessary because we
			 * want to jump-if-less, then jump-if-greater
			 * both based on the same dword.
			 *
			 * We have to set a hint so that the backend
			 * does not automatically take the next
			 * compare to mean ``comapare second dword of
			 * long long''
			 */
			lastinstr->hints |=
				HINT_INSTR_NEXT_NOT_SECOND_LLONG_WORD;
		}
	}

	saved_btype = btype;	

	if (is_multi_reg_obj) {
		if (btype == INSTR_BR_GREATER) {
			btype = INSTR_BR_SMALLER /*EQ*/;
		} else if (btype == INSTR_BR_SMALLER) {
			btype = INSTR_BR_GREATER  /*EQ*/;
		} else if (btype == INSTR_BR_SMALLEREQ) {
#if 0
			btype = INSTR_BR_GREATER;
#endif
			btype = INSTR_BR_SMALLER;
			/*
			 * 07/20/08: Setting the multi-reg label below seems
			 * to be wrong. The reason why it was there in the
			 * first place is not clear
			 */
/*			multi_reg_label = NULL;*/
		} else if (btype == INSTR_BR_GREATEREQ) {
			btype = INSTR_BR_SMALLER;
			/* XXX hmm is this right? */
#if 0
			btype = INSTR_BR_GREATER;
			multi_reg_label = NULL;
#endif

		}
	}

	if (cond->op != 0) {
		/*
		 * If the comaparison has already been made, it is an
		 * error to pass the expression's result type to
		 * icode_make_branch. For instance, when comparing two
		 * unsigned integers, the result has type ``signed
		 * int''. This botch, which I'm not sure works in all
		 * cases, fixes it for now. In general it would be
		 * better to always implicitly use the type of the
		 * last comparison, as is already done on e.g. MIPS
		 */
		if (lastinstr && lastinstr->type == INSTR_CMP) {
			ii = icode_make_branch(
				multi_reg_label? multi_reg_label: dest_label,
				btype, lastinstr->dest_vreg);
		} else {
			/*
			 * 10/27/08: This used to just do the branch. This
			 * is wrong - possibly for all cases - if there is
			 * no directly preceding compare instruction.
			 *
			 * In that case we'll probably have something like
			 *
			 *    if ( (x < y) && (y < z) )
			 *
			 * ... so the result vreg contains a value which has
			 * not been compared with 0 yet, so that's what we
			 * have to do here
			 */
			lastinstr = compare_vreg_with_zero(res, il);
			append_icode_list(il, lastinstr);
			ii = icode_make_branch(
				multi_reg_label? multi_reg_label: dest_label,
				/*btype*/INSTR_BR_EQUAL, lastinstr->dest_vreg);
#if 0
			ii = icode_make_branch(
				multi_reg_label? multi_reg_label: dest_label,
				btype, res);
#endif
		}
	} else {
		ii = icode_make_branch(
			multi_reg_label? multi_reg_label: dest_label,
			btype, res);
	}

	if (second_is_greater_than) {
		/*
		 * 07/20/08: For multi-reg ``>''
		 */
		ii->type = INSTR_BR_GREATER;
	}

	btype = saved_btype;
	append_icode_list(il, ii);

	btype = first_btype;

	if (res->is_multi_reg_obj || have_multi_reg_cmp) {
		ii = copy_icode_instr(lastinstr);

		/*
		 * 01/29/08: This less significant word comparison
		 * must be done unsigned. The hint is needed for
		 * PPC32 where the distinction isn't done for the
		 * branch but during comparison already. The vregs
		 * are not unsigned
		 * XXX Does this problem occur elsewhere? Is there
		 * a better way to fix it? (Changing vreg types)
		 */
		ii->hints |= HINT_INSTR_UNSIGNED;

		append_icode_list(il, ii);
		if (multi_reg_label) {
			/* We have to reverse the test again */
#if 0
			if (btype == INSTR_BR_EQUAL) {
				btype = INSTR_BR_NEQUAL;
			} else if (btype == INSTR_BR_NEQUAL) {
				btype = INSTR_BR_EQUAL;
			} else {
				unimpl();
			}	
#endif
		}	
		ii = icode_make_branch(dest_label, btype,
			lower_vreg? lower_vreg: res);
		append_icode_list(il, ii);

		/*
		 * Now append label to which to jump if the first
		 * comparison was false
		 */
		if (multi_reg_label != NULL) {
			append_icode_list(il, multi_reg_label);
		}	
	}

	return 0;
}

static void
do_body_labels(struct control *ctrl, struct icode_list *il) {
	if (ctrl->body_labels != NULL) {
		struct icode_instr	*ii;
		for (ii = ctrl->body_labels->head; ii != NULL;) {
			struct icode_instr	*botch = ii->next;
			ii->next = NULL;
			append_icode_list(il, ii);
			ii = botch;
		}	
	}
}


/*
 * 07/20/09: New function to align a pointer to a multiple of N by adding
 * M if it is not aligned yet.
 */
void
icode_align_ptr_up_to(struct vreg *ptr,
		int target_alignment,
		int addend,
		struct icode_list *il) {

	struct vreg		*andvr;
	struct vreg		*addvr;
	struct vreg		*tempptr;
	struct token		*andtok;
	struct token		*addtok;
	struct icode_instr	*ii;
	struct icode_instr	*label;
	struct reg		*r;

	vreg_faultin(NULL, NULL, ptr, il, 0);

	/*
	 * Reinterpret pointer as an unsigned integer so we can perform
	 * arithmetic on it. We make a copy of the pointer value because
	 * we will change it
	 */
	r = ALLOC_GPR(curfunc, ptr->size, il, NULL);
	icode_make_copyreg(r, ptr->pregs[0], ptr->type, ptr->type, il);

	tempptr = dup_vreg(ptr);
	vreg_set_new_type(tempptr, backend->get_size_t());
	vreg_map_preg(tempptr, r);
	

	/*
	 * AND pointer with desired alignment - 1 (must be power of 2)
	 */
	if (target_alignment & (target_alignment - 1)) {
		(void) fprintf(stderr, "BUG: icode_align_ptr_up_to() with "
			"alignment that is not a power of 2\n");
		abort();
	}
	--target_alignment;

	andtok = const_from_value(&target_alignment, NULL);
	andvr = vreg_alloc(NULL, andtok, NULL, NULL); 
	vreg_faultin_protected(tempptr, NULL, NULL, andvr, il, 0);

	/* 07/26/12: Need to avoid type mismatch on AMD64 */
	andvr = backend->icode_make_cast(andvr, backend->get_size_t(), il);

	backend->icode_prepare_op(&tempptr, &andvr, TOK_OP_BAND, il);
	ii = icode_make_and(tempptr, andvr);
	append_icode_list(il, ii);

	/*
	 * If 0 (alignment correct), skip pointer addition
	 */
	ii = icode_make_cmp(tempptr, NULL);
	append_icode_list(il, ii);
	label = icode_make_label(NULL);
	ii = icode_make_branch(label, INSTR_BR_EQUAL, tempptr);
	append_icode_list(il, ii);

	addtok = const_from_value(&addend, NULL);
	addvr = vreg_alloc(NULL, addtok, NULL, NULL); 
	vreg_faultin(NULL, NULL, ptr, il, 0);
	vreg_faultin_protected(ptr, NULL, NULL, addvr, il, 0);
	ii = icode_make_add(ptr, addvr);
	append_icode_list(il, ii);

	/*
	 * Align pointer by adding specified value (we first restore
	 * the pointer vreg by associating it with
	 */
	append_icode_list(il, label);
}


void
xlate_decl(struct decl *d, struct icode_list *il);

struct icode_list *
ctrl_to_icode(struct control *ctrl) {
	struct icode_list	*il;
	struct icode_list	*il2;
	struct icode_instr	*ii;
	struct icode_instr	*ii2;
	struct label		*label;

			
	il = alloc_icode_list();
	if (ctrl->type == TOK_KEY_DO
		|| ctrl->type == TOK_KEY_WHILE
		|| ctrl->type == TOK_KEY_FOR) {
		backend->invalidate_gprs(il, 1, 0);
	}

	if (ctrl->type == TOK_KEY_IF) {
		/*
		 * Generate
		 * cmp res, 0; je label;
		 * ... where label is returned (but not inserted
		 * into the icode list.)
		 */
		if (do_cond(ctrl->cond, il, ctrl, NULL) != 0) {
			return NULL;
		}
		do_body_labels(ctrl, il);

		il2 = xlate_to_icode(ctrl->stmt, 0);
		if (il2 != NULL) {
			merge_icode_lists(il, il2);
#if ! USE_ZONE_ALLOCATOR
			free(il2);
#endif
		}

		if (ctrl->next != NULL) {
			/*
			 * End of if branch - jump across else
			 * branch, then append else body
			 */
			ii2 = icode_make_jump(ctrl->next->endlabel);
			append_icode_list(il, ii2);
			append_icode_list(il, ctrl->endlabel);

			do_body_labels(ctrl->next, il);

			il2 = xlate_to_icode(ctrl->next->stmt, 0);
			if (il2 != NULL) {
				merge_icode_lists(il, il2);
#if ! USE_ZONE_ALLOCATOR
				free(il2);
#endif
			}	
			/* elsepart: ... code ... end: */
			append_icode_list(il, ctrl->next->endlabel);
		} else {
			append_icode_list(il, ctrl->endlabel);
		}	
	} else if (ctrl->type == TOK_KEY_WHILE) {
		append_icode_list(il, ctrl->startlabel);
		if (do_cond(ctrl->cond, il, ctrl, NULL) == -1) {
			return NULL;
		}	

		do_body_labels(ctrl, il);

		il2 = xlate_to_icode(ctrl->stmt, 0);
		if (il2 != NULL) {
			merge_icode_lists(il, il2);
#if ! USE_ZONE_ALLOCATOR
			free(il2);
#endif
		}
		ii = icode_make_jump(ctrl->startlabel);
		append_icode_list(il, ii);
		append_icode_list(il, ctrl->endlabel);
	} else if (ctrl->type == TOK_KEY_DO) {
		/* do-while loop */
		append_icode_list(il, ctrl->startlabel);
		do_body_labels(ctrl, il);
		il2 = xlate_to_icode(ctrl->stmt, 0);
		if (il2 != NULL) {
			merge_icode_lists(il, il2);
#if ! USE_ZONE_ALLOCATOR
			free(il2);
#endif
		}
		append_icode_list(il, ctrl->do_cond);
		(void) do_cond(ctrl->cond, il, ctrl, NULL);
		append_icode_list(il, ctrl->endlabel);
	} else if (ctrl->type == TOK_KEY_FOR) {
		struct statement	tmpst;
		tmpst.type = ST_CODE;
		tmpst.next = NULL;

		if (ctrl->finit != NULL
			/* crude stuff to rule out empty ex - necessary? */
			&& (ctrl->finit->op || ctrl->finit->data)) {
			tmpst.data = ctrl->finit;
			il2 = xlate_to_icode(&tmpst, 0);
			if (il2 != NULL) {
				merge_icode_lists(il, il2);
#if ! USE_ZONE_ALLOCATOR
				free(il2);
#endif
			}
		} else if (ctrl->dfinit != NULL) {
			int	i;

			for (i = 0; ctrl->dfinit[i] != NULL; ++i) {
				xlate_decl(ctrl->dfinit[i], il);
			}
		}
		append_icode_list(il, ctrl->startlabel);

		if (ctrl->cond != NULL
			&& (ctrl->cond->op || ctrl->cond->data)) {
			if (do_cond(ctrl->cond, il, ctrl, NULL) != 0) {
				return NULL;
			}	
		}

		do_body_labels(ctrl, il);
		il2 = xlate_to_icode(ctrl->stmt, 0);
		if (il2 != NULL) {
			merge_icode_lists(il, il2);
#if ! USE_ZONE_ALLOCATOR
			free(il2);
#endif
		}

		if (ctrl->fcont != NULL
			/* crude stuff to rule out empty ex - necessary? */
			&& (ctrl->fcont->op || ctrl->fcont->data)) {
			append_icode_list(il, ctrl->fcont_label);
			tmpst.data = ctrl->fcont;
			il2 = xlate_to_icode(&tmpst, 0);
			if (il2 != NULL) {
				merge_icode_lists(il, il2);
#if ! USE_ZONE_ALLOCATOR
				free(il2);
#endif
			}
		}
		ii = icode_make_jump(ctrl->startlabel);
		append_icode_list(il, ii);
		if (ctrl->endlabel != NULL) {
			append_icode_list(il, ctrl->endlabel);
		}
	} else if (ctrl->type == TOK_KEY_SWITCH) {
		struct token	*cond;
		struct vreg	*vr_cond;
		struct vreg	*vr_case;
		struct label	*default_case = NULL;

		vr_cond = expr_to_icode(ctrl->cond, NULL, il, 0, 0, 1);

		if (vr_cond == NULL) {
			return NULL;
		}	

		if (!is_integral_type(vr_cond->type)) {
			errorfl(ctrl->cond->tok,
				"Controlling switch expression doesn't have "
				"integral type");
			return NULL;
		}	

		do_body_labels(ctrl, il);
		for (label = ctrl->labels;
			label != NULL;
			label = label->next) {
			if (label->value == NULL) {
				if (label->is_switch_label) {
					default_case = label;
				}
				continue;
			} else if (!label->is_switch_label) {
				continue;
			}

			/*
			 * 08/22/07: This did usual arithmetic conversion
			 * betwen condition and case, instead of converting
			 * case to condition. Also, const_from_value was
			 * called on the original case value, such that
			 *
			 *     case ((char)1):
			 *
			 * would instruct the backend to load an ``immediate
			 * char'', which if bogus. Now the condition is
			 * instead promoted, and then the case is converted
			 * to it.
			 *
			 * Another problem with that:
			 *
			 *    switch (enum_type) {
			 *    case value:
			 *
			 * ... would convert value to an enum type, which is
			 * also not handled by the backends. Thus the TY_INT
			 * workaround below.
			 */
			 
			(void) promote(&vr_cond, NULL, 0, NULL, il, 1);
			cross_do_conv(label->value->const_value,
				vr_cond->type->code, 1);
			label->value->const_value->type->code =
				vr_cond->type->code == TY_ENUM? TY_INT:
				vr_cond->type->code;
			cond = const_from_value(
					label->value->const_value->value,
					label->value->const_value->type);
			vr_case = vreg_alloc(NULL, cond, NULL, NULL); 
			vreg_faultin_protected(vr_cond, NULL, NULL,
				vr_case, il, 0); 
			vreg_faultin_protected(vr_case, NULL, NULL,
				vr_cond, il, 0); 
#if 0
			cond = const_from_value(
					label->value->const_value->value,
					label->value->const_value->type);
			vr_case = vreg_alloc(NULL, cond, NULL, NULL); 
			vreg_faultin(NULL, NULL, vr_case, il, 0); 
			vreg_faultin_protected(vr_case, NULL, NULL,
				vr_cond, il, 0); 
			(void) promote(&vr_cond, &vr_case,
				     TOK_OP_LEQU, NULL, il, 1);

			/* XXX .... as promote may move stuff :( */
			vreg_faultin(NULL, NULL, vr_case, il, 0); 
			vreg_faultin_protected(vr_case, NULL, NULL,
				vr_cond, il, 0); 
#endif

			ii = icode_make_cmp(vr_cond, vr_case);
			append_icode_list(il, ii);
			free_pregs_vreg(vr_case, il, 0, 0);
			ii = icode_make_branch(label->instr, INSTR_BR_EQUAL,
				vr_cond);
			append_icode_list(il, ii);
			if (vr_cond->is_multi_reg_obj) {
				ii = icode_make_cmp(vr_cond, vr_case);
				append_icode_list(il, ii);
				ii = icode_make_branch(label->instr,
					INSTR_BR_EQUAL, vr_cond);
				append_icode_list(il, ii);
			}
		}
		free_pregs_vreg(vr_cond, il, 0, 0);
		if (default_case != NULL) {
			ii = icode_make_jump(default_case->instr);
		} else {
			ii = icode_make_jump(ctrl->endlabel);
		}
		append_icode_list(il, ii);
		
		il2 = xlate_to_icode(ctrl->stmt, 0);
		if (il2 != NULL) {
			merge_icode_lists(il, il2);
#if ! USE_ZONE_ALLOCATOR
			free(il2);
#endif
		}	
		append_icode_list(il, ctrl->endlabel);
	} else if (ctrl->type == TOK_KEY_CASE
		|| ctrl->type == TOK_KEY_DEFAULT) {
		label = ctrl->stmt->data;
		append_icode_list(il, label->instr);
	} else if (ctrl->type == TOK_KEY_RETURN) {
		struct vreg		*vr = NULL;
		struct type		*ret_type = NULL;
		struct type_node	*rettn;

		if (ctrl->cond == NULL) {
			for (rettn = curfunc->proto->dtype->tlist;
				rettn != NULL;
				rettn = rettn->next) {
				if (rettn->type == TN_FUNCTION) {
					rettn = rettn->next;
					break;
				}
			}	

			if (curfunc->proto->dtype->code != TY_VOID
				|| rettn != NULL) { 
				warningfl(ctrl->tok,
"Return statement without a value in function not returning `void'");
			}
		} else {
			if ((vr = expr_to_icode(ctrl->cond, NULL, il,
				TOK_KEY_RETURN, 0, 1)) == NULL) {
				return NULL;
			}
			/* XXX warn if types incompatible */
		}

		if (vr != NULL) {
#if 0
			ret_type = curfunc->proto->dtype;
			rettn = ret_type->tlist;
			ret_type->tlist = ret_type->tlist->next;
#endif
			/* 06/17/08: Stop the tlist kludgery for return type */
			ret_type = curfunc->rettype;
			if (check_types_assign(ctrl->tok, ret_type, vr, 1, 0)
				!= 0) {
				return NULL;
			}	
			if ((ret_type->code != TY_STRUCT
				&& ret_type->code != TY_UNION)
				|| ret_type->tlist != NULL) {	
				vr = backend->icode_make_cast(vr, ret_type, il);
			} else {
				if (vr->type->code != ret_type->code
					|| vr->type->tlist != NULL
					|| vr->type->tstruc
						!= ret_type->tstruc) {
					errorfl(ctrl->tok,
		"Returned expression incompatible with function return type");
					return NULL;
				}
			}
			if (ret_type->code == TY_VOID
				&& ret_type->tlist == NULL) {
				warningfl(ctrl->tok,
					"void expressions as argument to "
					"`return' are only allowed in GNU C "
					"and C++");
			}	
#if 0
			ret_type->tlist = rettn;
#endif
		}

		if (backend->icode_make_return(vr, il) != 0) {
			return NULL;
		}
	} else if (ctrl->type == TOK_KEY_BREAK) {
		ii = icode_make_jump(ctrl->endlabel);
		append_icode_list(il, ii);
	} else if (ctrl->type == TOK_KEY_CONTINUE) {
		ii = icode_make_jump(ctrl->startlabel);
		append_icode_list(il, ii);
	} else if (ctrl->type == TOK_KEY_GOTO) {
		struct label	*l;
		struct token	*dest = (struct token *)ctrl->stmt;

		ii = NULL;
		if (dest != NULL) {
			/* goto label; */
			if ((l = lookup_label(curfunc, dest->data)) != NULL) {
				ii = icode_make_jump(l->instr);
				append_icode_list(il, ii);
			} else {
				errorfl(dest, "Undefined label `%s'", dest->ascii);
			}	
		} else {
			struct vreg	*vr;

			/* goto *expr; */
			if ((vr = expr_to_icode(ctrl->cond, NULL, il,
				TOK_KEY_RETURN, 0, 1)) == NULL) {
				return NULL;
			}

			/*
			 * The type almost doesn't matter a bit! The only thing
			 * to make sure is that the vreg comes from a pointer,
			 * as in ``goto *foo;''
			 */
			if (vr->type->tlist == NULL
				&& (vr->type->code == TY_VOID
				|| vr->type->code == TY_UNION
				|| vr->type->code == TY_STRUCT)) {
				errorfl(ctrl->tok, "Invalid type for computed "
					"goto expression");
				return NULL;
			} else if ((signed)backend->get_sizeof_type(vr->type, NULL)
				!= backend->get_ptr_size()) {
				warningfl(ctrl->tok, "Computed goto expression "
					"value does not have pointer size");
			}
			vreg_faultin(NULL, NULL, vr, il, 0);
			icode_make_comp_goto(vr->pregs[0], il);
		}
	} else {
		printf("UNKNOWN CONTROL STRUCTURE %d\n", ctrl->type);
		abort();
	}	
	return il;
}


/*
 * XXX this should only penalize initializers which really are not
 * constant!!!!!!!!
 */
static void
varinit_to_icode(struct decl *dest,
	struct vreg *destvr,	
	struct initializer *init,
	unsigned long *offset,
	struct icode_list *il) {

	unsigned long	offset0 = 0;

	if (offset == NULL) {
		offset = &offset0;
	}	

	for (; init != NULL; init = init->next) {
		size_t		type_size;
		int		remainder;
		int		type_alignment;

		switch (init->type) {
		case INIT_EXPR:
		case INIT_STRUCTEXPR:
			/* Nothing to do */
			break;
		case INIT_NESTED:
			varinit_to_icode(dest, destvr, init->data, offset, il);
			break;
		case INIT_NULL:
			if (init->varinit != NULL) {
				struct vreg	*res;
				struct vreg	*leftvr;
				struct vreg	*tmpvr;
				struct token	*tok;
				struct icode_instr	*ii;
				struct vreg	*indirvr;
				struct vreg	*addrvr;
				int		i;
				int		is_struct = 0;


				res = expr_to_icode(init->varinit, NULL,
					il, 0, 0, 1);
				if (res == NULL) {
					break;
				}

				if (!is_basic_agg_type(init->left_type)) {
					res = backend->icode_make_cast(res,
						init->left_type, il);
				} else {
					is_struct = 1;
				}

				/*
				 * 08/09/08: Handle bitfield range
				 */
				if (init->left_type->tbit != NULL) {
					/* Mask source value to max range */
					mask_source_for_bitfield(
						init->left_type,
						res, il, 0);
				}

				leftvr = vreg_alloc(NULL, NULL, NULL,
					init->left_type);
				{
					struct reg	*r;
					/*ii =*/ r = icode_make_addrof(NULL, /*dest->vreg*/
							destvr,
							il);	
					/*append_icode_list(il, ii);*/

					addrvr = vreg_alloc(NULL, NULL, NULL,
						addrofify_type(leftvr->type));	
					vreg_map_preg(addrvr, r /*ii->dat*/);	
				}

				/*
				 * Now leftvr is the address of the left
				 * hand struct or array. Now perform some
				 * pointer arithmetic, then indirectly
				 * assign the result. Yes this is kludged,
				 * we need a more general way for offsets
				 * in the long run
				 */
				reg_set_unallocatable(addrvr->pregs[0]);
				if (init->left_type->tbit == NULL) {
					i = (int)*offset;
				} else {
					/*
					 * Get offset from storage unit base
					 * offset plus offset within it. This
					 * is needed because the current offset
					 * passed to this function already
					 * includes a full bitfield initializer
					 * (since we have to create one IN
					 * ADDITION to this null initializer)
					 */
#if 0
					i = init->left_type->tbit->bitfield_storage_unit->
						offset + init->left_type->tbit->
						byte_offset;
#endif
					int	rel_off = init->left_type->tbit->absolute_byte_offset
						- init->left_type->tbit->bitfield_storage_unit->offset;
				i = *offset - backend->get_sizeof_type(
					init->left_type->tbit->bitfield_storage_unit->dtype, NULL)
					+ rel_off;
				}
				tok = const_from_value(&i,
					make_basic_type(TY_INT));
				tmpvr = vreg_alloc(NULL, tok, NULL,
					make_basic_type(TY_INT));
				vreg_faultin(NULL, NULL, tmpvr, il, 0);
				ii = icode_make_add(addrvr, tmpvr);
				append_icode_list(il, ii);
				reg_set_allocatable(addrvr->pregs[0]);

				indirvr = vreg_alloc(NULL, NULL,
					addrvr,
					init->left_type);
				reg_set_unallocatable(indirvr->from_ptr->pregs[0]);
				
				if (is_struct) {
					/*
					 * 04/03/08: Non-constant struct-by-
					 * value initializer... was missing!
					 */
					icode_make_copystruct(indirvr, res, il);
				} else {
					/*
					 * 06/01/08: Use x87 faultin for
					 * floating point. This is save because
					 * the store will pop the reg
					 */
					if (init->left_type->tbit != NULL) {
						/* Bitfield */
						write_back_bitfield_by_assignment(indirvr, res, il);
					} else {
						/* Not bitfield */
						vreg_faultin_x87(NULL, NULL, res, il, 0);
						reg_set_allocatable(indirvr->from_ptr->pregs[0]);
						indirvr->pregs[0] = res->pregs[0];
						icode_make_store(curfunc, res, indirvr, il);
					}
				}
			}
			break;
		default:
			unimpl();
		}
		
		if (init->left_type != NULL) {
			if (init->type == INIT_NESTED) {
				/*
				 * Don't add up sizes for nested initializers -
				 * that has already been done
				 */
				type_size = 0;
			} else if (init->type == INIT_NULL
				&& init->left_type->tbit != NULL) {
				/*
				 * 10/13/08: Don't add sizes for variable bitfield
				 * initializers (struct foo { int x:8; } = { rand() })
				 * either
				 */
				type_size = 0;
			} else if (init->type == INIT_NULL) {
				/*
				 * 02/16/10: We incorrectly used the struct
				 * type size (in the else branch below) rather
				 * than the explicitly set 0 data size field
				 */
				type_size = *(size_t *)init->data;
			} else {
				type_size = backend->get_sizeof_type(init->left_type,
					NULL);
			}
		} else {
			assert(init->type == INIT_NULL);
			type_size = *(size_t *)init->data;
		}
		
		*offset += type_size;
		
		/*
		 * Align for next initializer, unless it is a genuine null
		 * initializer (as opposed to a placeholder for a variable
		 * initializer), in which case alignment is already handled
		 */
		if (init->next != NULL
			&& (init->next->type != INIT_NULL
				|| init->next->left_type != NULL)) {
			struct initializer	*tmp = init->next;

			/*
			 * 10/13/08: Don't align for bitfield types
			 */
			if (tmp->left_type->tbit == NULL) {
				type_alignment = backend->get_align_type(tmp->left_type);
				remainder = *offset % type_alignment;
				if (remainder) {
					*offset += type_alignment - remainder;
				}
			}
		}	
	}
}	

/*
 * Generate initializations for automatic variables
 */
void
init_to_icode(struct decl *d, struct icode_list *il) {
	struct initializer	*init;
	struct vreg		*decvr;

	decvr = vreg_alloc(d, NULL, NULL, NULL);
	vreg_set_new_type(decvr, d->dtype);

	if (is_basic_agg_type(d->dtype)
		&& d->init->type != INIT_STRUCTEXPR) {
		/* Fill remaining elements/members with 0 */
		d->init_name = backend->make_init_name(d->init);
		d->init_name->dec = d;
#if XLATE_IMMEDIATELY
		emit->struct_inits(d->init_name);
#endif
		backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
		icode_make_copyinit(d, il);

		if (d->dtype->storage != TOK_KEY_STATIC
			&& d->dtype->storage != TOK_KEY_EXTERN) {	
			varinit_to_icode(d, decvr, d->init, NULL, il);
#if 0 
			varinit_to_icode(d, d->init, 0, il);
#endif
		}
	} else {
		struct vreg	*vr;
		struct vreg	*left
				= vreg_alloc(NULL, NULL, NULL, d->dtype);

		left->var_backed = d;
		init = d->init;
		if (init->next != NULL
			|| (init->type != INIT_EXPR
			&& init->type != INIT_STRUCTEXPR)) {
			puts("BAD INITIALIZER");
			abort();
		}

		/*
		 * 08/16/07: This didn't pass the variable to be
		 * initialized (left) to expr_to_icode(). Thus
		 * functions returning structures by values didn't
		 * work as initializers
		 */
		if ((vr = expr_to_icode(init->data, left,
			il, 0, 0, 1)) == NULL) { 
			return;
		}
		if (check_types_assign(d->tok, d->dtype,
			vr, 1, 0) == -1) {
			return;
		}

		if (init->type != INIT_STRUCTEXPR) {
			vr = backend->icode_make_cast(vr, d->dtype, il);
			vreg_faultin_x87(NULL, NULL, vr, il, 0);
			vreg_map_preg(/*d->vreg*/ decvr, vr->pregs[0]);
			if (vr->is_multi_reg_obj) {
				vreg_map_preg2(/*d->vreg*/ decvr, vr->pregs[1]);
			}
			icode_make_store(NULL,
				/*d->vreg, d->vreg*/ decvr, decvr, il);
			if (STUPID_X87(vr->pregs[0])) {
#if 0
				backend->free_preg(vr->pregs[0], il);
#endif
				vr->pregs[0]->vreg = NULL;
				vr->pregs[0] = NULL;
			}	
		} else {
			/*
			 * 08/16/07: This generated a bad struct copy if the
			 * initializer was a function returning a structure
			 * by value, in which case the callee does the copy,
			 * not the caller
			 */
			if (!vr->struct_ret) {
				icode_make_copystruct(left, vr, il);
			}
		}
	}
}

/*
 * XXX Some constraints dealt with here are architecture-specific. That stuff
 * should be outsourced to the backend. o and i are missing :-(
 */
static void
asm_to_icode(struct inline_asm_stmt *stmt, struct icode_list *il) {
	struct clobbered_reg	*clob;
	struct inline_asm_io	*io;
	char			*p;
	struct reg		*r;
	int			i;

	for (clob = stmt->clobbered; clob; clob = clob->next) {
		if (clob->reg == NULL) {
			/*
			 * Reg = NULL means "memory" is clobbered -
			 * no values should be cached anymore
			 * XXX really need invalidate_fprs() :(
			 */
			backend->invalidate_gprs(il, 1, 0);
			break;
		} if (clob->reg->type == REG_GPR) {
			free_preg(clob->reg, il, 1, 1);
			/*reg_set_unallocatable(clob->reg);*/
			clob->reg->used = 1;
		} else {
			puts("ERROR: Non-GPRS may not occur "
				"in the asm clobber list yet\n");
			unimpl();
		}
	}

	for (io = stmt->output, i = 1; io != NULL; io = io->next, ++i) {
		io->vreg = expr_to_icode(io->expr, NULL, il, 0, 0, 1);
		io->outreg = NULL;
		if (io->vreg == NULL) {
			return;
		} else if (io->expr->op != 0
			|| !io->expr->data->is_lvalue) {
			errorfl(io->expr->tok,
				"Output operand #%d isn't an lvalue", i);
			return;
		}
		for (p = io->constraints; *p != 0; ++p) {
			if (*p == '=') {
				;
			} else if (*p == '+') {
				;
			} else if (*p == '&') {
				; /* is early clobber */
			} else if (strchr("rqQabcdSD", *p) != NULL) {	
				/* XXX hmm Q is amd64?! */
				r = backend->asmvreg_to_reg(&io->vreg,
					*p, io, il, 0);
				if (r == NULL) {
					return;
				}
				io->outreg = r;
			}
		}
	}

	/* XXX for some reason stuff below uses clobber registers :( */
	for (io = stmt->input; io != NULL; io = io->next) {
		io->vreg = expr_to_icode(io->expr, NULL, il, 0, 0, 1);
		if (io->vreg == NULL) {
			return;
		}
	}

	for (io = stmt->input, i = 1; io != NULL; io = io->next, ++i) {
		for (p = io->constraints; *p != 0; ++p) {
			r = NULL;
			if (strchr("rqabcdSD", *p) != NULL) {
				r = backend->asmvreg_to_reg(&io->vreg, *p, io,
					il, 1);
				if (r == NULL) {
					return;
				}
				reg_set_unallocatable(r);
			} else if (*p == 'm') {
				/*
				 * XXX faultin below assumes it can always get
				 * a register
				 */
				r = vreg_faultin_ptr(io->vreg, il);
				if (r != NULL) {
					/* Output is done through pointer */
					reg_set_unallocatable(r);
				}
			} else if (isdigit((unsigned char)*p)) {
				int			num = *p - '0';
				struct inline_asm_io	*tmp;

				if (num >= stmt->n_outputs) {
					errorfl(io->expr->tok,
					"Output operand %d doesn't exist",
					num+1);
					return;
				}
				for (tmp = stmt->output; num > 0; --num) {
					tmp = tmp->next;
				}

				if (tmp->outreg != NULL) {
					/* Must use same register */
					static char	kludge[2];

					kludge[0] = tmp->outreg->name[1];
					kludge[1] = 0;
					if (strcmp(tmp->outreg->name, "esi")
						== 0 ||
						strcmp(tmp->outreg->name,"edi")
						== 0) {
						kludge[0] = toupper(kludge[0]);
					}	
					p = kludge; /* :-( */
				} else {	
					/* XXX */
					p = tmp->constraints;
				}	
			}

			if (r != NULL){
				/* 03/25/08: Wow, this was missing */
				io->inreg = r;
			}
		}
	}

	/*
	 * At this point, input registers are setup and marked unallocatable,
	 * so now the output regs can be assigned. If the first (write-)
	 * access to an output register comes from a register, the
	 * destination is that same register. That seems sort of bogus, but
	 * then so does the idea of using "r" for output at all
	 */
	for (io = stmt->output; io != NULL; io = io->next) {
		for (p = io->constraints; *p != 0; ++p) {
			if (*p == '=') {
				;
			} else if (*p == '+') {
				/* Used for both input and output */
				if (io->outreg != NULL) {
					vreg_faultin(io->outreg, NULL,
						io->vreg, il, 0);
				}
			} else if (*p == 'm') {	
				/*
				 * XXX faultin below assumes it can always get
				 * a register
				 */
				r = vreg_faultin_ptr(io->vreg, il);
				if (r != NULL) {
					/* Output is done through pointer */
					reg_set_unallocatable(r);
					assert(io->outreg == NULL);
					io->outreg = r;
				}
			}	
		}	
	}

	icode_make_asm(stmt, il);

	/* Write back output registers (memory operands need no writeback) */
	for (io = stmt->output; io != NULL; io = io->next) {
		for (p = io->constraints; *p != 0; ++p) {
			if (*p == '=') {
				continue;
			} else if (strchr("qrabcdSD", *p) != NULL) {	
				vreg_map_preg(io->vreg, io->outreg);
				icode_make_store(curfunc, /* XXX ?!*/
					io->vreg, io->vreg, il);
				free_pregs_vreg(io->vreg, il, 0, 0);
			}
		}
	}
	
	for (io = stmt->input; io != NULL; io = io->next) {
		if (io->vreg->pregs[0] != NULL) {
			/*eg_set_allocatable(io->vreg->preg);*/
			free_pregs_vreg(io->vreg, il, 0, 0);
		}
	}

	for (clob = stmt->clobbered; clob; clob = clob->next) {
		if (clob->reg && clob->reg->type == REG_GPR) {
			reg_set_allocatable(clob->reg);
		}
	}	
}

struct stack_block *
vla_decl_to_icode(struct type *ty, struct icode_list *il) {	
	struct type_node	*tn;
	int			vlas_done = 0;
	int			total_vla_dims = 0;
	struct stack_block	*sb;
	
	for (tn = ty->tlist; tn != NULL; tn = tn->next) {
		++total_vla_dims;
	}

	/*
	 * Create block to store the VLA info in:
	 *
	 *    struct vlainfo {
	 *        void *addr;
	 *        unsigned long total_size;
	 *        unsigned long var_dim_sizes[1];
	 *    };
	 *
	 * XXX for now we assume alignof(unsigned long)
	 * = alignof(void *)
	 */
	sb = make_stack_block(0,
		backend->get_ptr_size()
		+ (1 + total_vla_dims) *
		backend->get_sizeof_type(
			make_basic_type(TY_ULONG), NULL));	
				
	if (curfunc->vla_head == NULL) {
		curfunc->vla_head = curfunc->vla_tail =
			sb;
	} else {
		curfunc->vla_tail->next = sb;
		curfunc->vla_tail = sb;
	}

	ty->vla_addr = sb;

	vlas_done = 0;
	for (tn = ty->tlist; tn != NULL; tn = tn->next) {
		struct vreg	*vexpr;

		if (tn->type != TN_VARARRAY_OF) {
			continue;
		}	
		/*
		 * Variable - execute variable
		 * expression
		 */

		vexpr = expr_to_icode(
#if REMOVE_ARRARG
			tn->variable_arrarg, NULL,
#else
			tn->arrarg, NULL,
#endif
			il, 0, 0, 1);
		if (vexpr == NULL) {
			return NULL;
		}
		vexpr = backend->icode_make_cast(
			vexpr, make_basic_type(TY_ULONG), il);

		/*
		 * Store dimension size in VLA block
		 */
		vreg_faultin(NULL, NULL, vexpr, il, 0);
		icode_make_put_vla_size(vexpr->pregs[0], sb, vlas_done, il);
		tn->vla_block_no = vlas_done;

		++vlas_done;
	}
	return sb;
}

static int
stmt_ends_with_ret(struct statement *stmt) {
	if (stmt->type == ST_CTRL
		&& ((struct control *)stmt->data)->type == TOK_KEY_RETURN) {
		return 1;
	} else if (stmt->type == ST_COMP) {
		struct scope	*s = stmt->data;

		if (s->code_tail != NULL) {
			if (s->code_tail->type == ST_CTRL
				&& ((struct control *)s->code_tail->data)->type	
				== TOK_KEY_RETURN) {
				return 1;
			}
		}
	}	
	return 0;
}


/*
 * 11/26/07: Moved out of analyze() into separate function, added missing
 * return checking
 */
void
xlate_func_to_icode(struct function *func) {
	debug_print_function(func);
	curfunc = func;
	curscope = func->scope;
	

	func->icode = xlate_to_icode(func->scope->code, 1);

	/*
	 * 11/26/07: Check whether last statement is a return;
	 * otherwise warn and generate a return. This used to be
	 * done in the backend
	 */
	if (func->scope->code_tail
		&& func->scope->code_tail->type == ST_CTRL
		&& ((struct control *)func->scope->code_tail->data)->type
		== TOK_KEY_RETURN) {
		; /* OK there is a return */
	} else {
		/* No return */
		int		warn = 1;
		int		needret = 1;

	 	/*
		 * 04/09/08: This assumed that code_tail is non-NULL! That
		 * usually works because we always store a declaration
		 * statement for __func__. But that could change - don't
		 * depend on it
		 */
		if (!func_returns_void(func)
			&& func->scope->code_tail != NULL) {
			struct control	*ctrl = func->scope->code_tail->data;

			if (ctrl->type == TOK_KEY_GOTO) {
				warn = 0; /* goto - don't warn */
			} else if (ctrl->type == TOK_KEY_IF) {
				if (ctrl->next != NULL) {
					/*
					 * Looks like we have an
					 *   if (foo) { } else { }
					 *
					 * as last part. If both statement
					 * bodies end with a return, this
					 * part of the function is never
					 * reached
					 */
					if (stmt_ends_with_ret(ctrl->stmt)
						&& stmt_ends_with_ret(
							ctrl->next->stmt)) {
						warn = 0;
						needret = 0;
					}
				}
			}
			if (warn) {
#if 0
				/* This warning is too verbose for now since
				 * the if-else check above does not cover
				 * stuff like switch statements and exit()/
				 * abort() calls
				 */
				warningfl(func->proto->tok,
					"Falling off non-void function `%s' "
					"without a return",
					func->proto->dtype->name);
#endif
			}
		}
		if (needret) {
			struct icode_instr	*ii;

			if (func->icode) {
				ii = icode_make_ret(NULL);
				append_icode_list(func->icode, ii);
			}	
		}
	}

	/*
	 * 10/31/07: Added this to make sure that all registers are
	 * completely thrown away when a function ends. Anything else
	 * is just nonsense and causes subsequent function definitions
	 * to perform unnecessary saves of these registers
	 */
	backend->invalidate_gprs(NULL, 0, 0);

	/*
	 * 02/05/08: Added backend function to generate function outro
	 * if necessary. This currently just marks PIC gprs (ebx on x86)
	 * allocatable again. It may make sense to combien this with
	 * emit_func_outro() somehow
	 */
	if (backend->icode_complete_func != NULL) {
		backend->icode_complete_func(func, func->icode);
	}
}

void
xlate_decl(struct decl *d, struct icode_list *il) {
#if 0
	d->vreg = vreg_alloc(d, NULL, NULL, NULL);
	vreg_set_new_type(d->vreg, d->dtype);
#endif

	if (d->init != NULL
		&& d->dtype->storage != TOK_KEY_STATIC
		&& d->dtype->storage != TOK_KEY_EXTERN) {
		/*icode_make_dbginfo_line(st, il);*/
		init_to_icode(d, il);
	} else if (IS_VLA(d->dtype->flags)) {
		/*
		 * This is a VLA declaration in some way.
		 * It may e.g. be a one- or multi-
		 * dimensional array, or a pointer to such
		 * a thing
		 */
		struct type_node	*tn;
		struct stack_block	*sb;

		sb = vla_decl_to_icode(d->dtype, il);

		if ( /*!err*/ sb != NULL) {
			for (tn = d->dtype->tlist;
				tn != NULL;
				tn = tn->next) {
				if (d->dtype->tlist->type
					== TN_POINTER_TO) {
					break;
				} else if (d->dtype->tlist->type
					== TN_VARARRAY_OF) {
					break;
				}
			}

			if (tn->type == TN_VARARRAY_OF) {
				/*
				 * This really is an array, not
				 * just e.g. a pointer to one -
				 * allocate storage!
				 */
				struct vreg	*size;

				size = backend->get_sizeof_vla_type(d->dtype, il);
				icode_make_put_vla_whole_size(
					size->pregs[0],
					sb,
					il);
				icode_make_alloc_vla(sb, il);
			}
		}
	}
}	

struct icode_list *
xlate_to_icode(struct statement *st, int inv_gprs_first) {
	struct icode_list	*il;
	struct icode_list	*il2;
	
	il = alloc_icode_list();
	if (inv_gprs_first) {
		backend->invalidate_gprs(il, 0, 0);
		/*
		 * XXXXXXXXXXXXXXXXXXXX 02/08/09: The PIC register was
		 * initialized wrongly; Any static variable access which
		 * did this set the ``initialized'' flag. This did not
		 * take into account whether it was a conditional access,
		 * e.g.     expr? x: y    or     if (expr) x; else y;
		 *
		 * To ensure the PIC register is unambiguously loaded
		 * correctly, we now do it at the beginning of every
		 * function.
		 *
		 * XXXXXXXX: Obviously we shouldn't do it for functions
		 * which do not access static variables. Proposed
		 * solution: Where icode_initialize_pic() was called
		 * (prepare_loadstore, etc), set a flag in the function
		 * structure indicating that static variables were
		 * accessed. Then, in the backend, perform initialization
		 * if the flag is set
		 */
		if (backend->need_pic_init && picflag) {
			backend->icode_initialize_pic(curfunc, il);
			curfunc->pic_initialized = 1;
		}
	}

	for (; st != NULL; st = st->next) {
		if (st->type == ST_CTRL) {
			struct control	*ctrl = st->data;

			il2 = ctrl_to_icode(ctrl);
			if (il2 != NULL) {
				merge_icode_lists(il, il2);
#if ! USE_ZONE_ALLOCATOR
				free(il2);
#endif
			}

			/*
			 * Save flag added.
			 * 11/26/07: Hmm, this yields nonsense saves for
			 * ``return'' and possibly break/continue?!
			 */
			if (ctrl->type == TOK_KEY_RETURN) {
				backend->invalidate_gprs(il, 0, 0);
			} else {	
				backend->invalidate_gprs(il, 1, 0);
			}
			il->res = NULL;
		} else if (st->type == ST_LABEL) {
			struct label	*l = st->data;

			append_icode_list(il, l->instr);

			/* Missed save flag */
			backend->invalidate_gprs(il, 1, 0);
			il->res = NULL;
		} else if (st->type == ST_CODE) {
			struct expr	*ex = st->data;
			struct vreg	*res;

			if (ex->op == 0 && ex->data == NULL) {
				/* Empty expression */
				;
			} else {
				icode_make_dbginfo_line(st, il);
				/*
				 * 04/12/08: Top-level expression, so the result
				 * is not used - set corresponding flag!
				 */
				if ((res = expr_to_icode(ex, NULL, il, 0, 1, 1))
					== NULL) {
					/* XXX free stuff */
					return NULL;
				}

				il->res = res;
				free_pregs_vreg(res, il, 0, 0);
			}
		} else if (st->type == ST_COMP || st->type == ST_EXPRSTMT) {
			struct scope	*s = st->data;

			curscope = s;
			il2 = xlate_to_icode(s->code, 0);

			if (il2 != NULL) {
				merge_icode_lists(il, il2);
#if ! USE_ZONE_ALLOCATOR
				free(il2);
#endif
			}
			if (st->type != ST_EXPRSTMT) {
				il->res = NULL;
			} else {
				if (il->res && il->res->pregs[0]) {
					/*
					 * There's a free_pregs_vreg()
					 * in xlate_to_icode(). That's
					 * bad because the value may
					 * still be used. So we have to
					 * ensure that the item remains
					 * associated with a register
					 * if it's a scalar type
					 */
					if (is_arithmetic_type(il->res->type)
						|| il->res->type->tlist) {
						if (il->res->pregs[0]->vreg == il->res) {
							vreg_map_preg(il->res, il->res->pregs[0]);
							if (il->res->is_multi_reg_obj) {
								vreg_map_preg(
								il->res, il->res->pregs[1]);
							}
						}
					}
				}
			}
			curscope = s->parent; /* restore scope */
		} else if (st->type == ST_DECL) {
			struct decl	*d = st->data;

			xlate_decl(d, il);
			if (!d->is_unrequested_decl) {
				/*
				 * 08/09/08: In
				 *
				 *    ({ expr; expr; int foo; })
				 *
				 * ... we correctly set the result to NULL.
				 *
				 * However in
				 *
				 *    struct s foo();
				 *    ({ foo(); expr; })
				 * 
				 * ... there will be a declaration appended
				 * after expr. That declaration allocates the
				 * anonymous struct to which the return value
				 * of foo() is assigned. If we have such an
				 * unrequested declaration, we do not want to
				 * consider it a value, and keep the result
				 * vreg of the last precedeing statement!
				 * Hence the surrounding check above now
				 */
				il->res = NULL;
			}
		} else if (st->type == ST_ASM) {
			asm_to_icode(st->data, il);
			il->res = NULL;
		} else {
			printf("UNKNOWN STATEMENT TYPE\n");
			printf("%d\n", st->type);
			abort();
		}
	}

	return il;
}

