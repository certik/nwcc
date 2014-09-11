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
 *
 * Expression parser
 */
#include "expr.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "token.h"
#include "error.h"
#include "misc.h"
#include "defs.h"
#include "type.h"

#ifndef PREPROCESSOR
#    include "decl.h"
#    include "builtins.h"
#    include "scope.h"
#    include "icode.h"
#    include "debug.h"
#    include "cc1_main.h"
#    include "functions.h"
#    include "control.h"
#    include "analyze.h"
#    include "symlist.h"
#    include "backend.h"
#    include "reg.h"
#else
#    include "macros.h"
#endif

#include "subexpr.h"
#include "typemap.h"
#include "libnwcc.h"
#include "zalloc.h"
#include "n_libc.h"
#include "evalexpr.h"



int
const_value_is_nonzero(struct tyval *cv) {
	int		size;
	int		i;
	unsigned char	*uc;


	if (cv->str != NULL) {
		/* 08/05/09: This was missing (gave a crash) */
		return 1;
	}

	size = cross_get_sizeof_type(cv->type);
	for (uc = (unsigned char *)cv->value, i = 0; i < size; ++i, ++uc) {
		if (*uc != 0) {
			return 1;
		}
	}
	return 0;
}

#ifndef PREPROCESSOR

/*
 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
 * doesn't handle cross-compilation :(
 * address->diff should probably become a ``struct tyval'' and then we
 * need a nice set of utility functions to convert host values from/to
 * target-independent values. tyval is probably our best bet for
 * abstracting these things because exec_op() and such already use it
 * in the interface
 */
static void					
addr_add_sub(struct tyval *tv, struct expr *ex, struct type *ty, int op) {
	long		val;
	long		origval;
	int		esize = backend->get_sizeof_elem_type(ty);
	void		*newval;

	/*
	 * 07/31/09: We have to create a copy of the constant value before
	 * we change it! Otherwise, if the expression is re-evaluated, the
	 * original token will have a ``long'' value with an ``int'' type
	 * (which breaks on 64bit big endian systems)
	 *
	 * So this was one of potentially various places where constant
	 * sub-expression evaluation can damage data structures if it turns
	 * out the expression has to be evaluated at runtime.
	 *
	 * (The bug occurred on PPC64 with:
	 *
	 *     char array[128];
	 *     unsigned long ul = (unsigned long)(array + 64);
	 *
	 * The int token 64 became 0 because the underlying value had been
	 * converted to unsigned long, and the value 64 was stored in the
	 * upper 4 bytes)
	 */
	newval = zalloc_buf(Z_CEXPR_BUF); /*n_xmalloc(16);*/ /* XXX */
	memcpy(newval, ex->const_value->value, 16);
	ex->const_value->value = newval;

	cross_do_conv(ex->const_value, TY_LONG, 1);
	/* XXX what to do here? :( */
	ex->const_value->type->code = TY_LONG;
#if 0
	val = *(long *)ex->const_value->value * esize;
#endif

	val = (long)cross_to_host_size_t(ex->const_value);
	origval = val;
	val *= esize;

	if (tv->address != NULL) {
		if (op == TOK_OP_PLUS) {
			tv->address->diff += val;
		} else {
			tv->address->diff -= val;
		}
	} else {
		static struct tyval	src;
		static struct tyval	tmpval;
		static struct token	optok;
		struct type		*saved_type;
		int			opval = TOK_OP_PLUS;

		optok.type = TOK_OPERATOR;
		optok.data = &opval;
		src.value = ex->const_value->value;

		/*
		 * 08/19/07: Store (and later restore!) the value val
		 * as source. This was missing, so the array element
		 * calculation above was ignored
		 */
		cross_from_host_long(src.value, val, TY_LONG);
		src.type = make_basic_type(TY_LONG);

		/*
		 * 08/19/07: This wasn't working because, as a comment
		 * here rightly said :-), we have to use a temporary
		 * tyval as target instead of using tv as target and
		 * destination 
		 */
		saved_type = tv->type;
		tv->type = src.type;
		cross_exec_op(&optok, /*tv*/&tmpval, tv, &src); 
		cross_from_host_long(src.value, origval, TY_LONG);
		tv->value = tmpval.value;
		tv->type = saved_type;
	}
}

static int
calc_const_addr_diff(struct tyval *tmp1, struct tyval *tmp2, long *diff) {
	if (tmp1->address != NULL && tmp2->address != NULL) {
		if (tmp1->address->dec == tmp2->address->dec) {
			/* OK, same declaration, only addend makes the difference */
			*diff = tmp1->address->diff - tmp2->address->diff;
			return 0;
		}

		/*
		 * This may be the same but re-declared identifier (XXX make sure
		 * this is really true), so compare by name
		 */
		if (strcmp(tmp1->address->dec->dtype->name,
			tmp2->address->dec->dtype->name) == 0) {
			/* OK, same declaration, only addend makes the difference */
			*diff = tmp1->address->diff - tmp2->address->diff;
			return 0;
		}
	} else {
		/*
		 * 07/17/08: Handle constants, like
		 *
		 *     (char *)123 - (char *)60
		 */
		struct tyval		tempdest = *tmp1;
		struct tyval		tempsrc = *tmp2;
		static struct tyval	tmpval;
		static struct token	optok;
		int			opval = TOK_OP_MINUS;
		int			scaling =
			tempdest.type->tlist != NULL?
			backend->get_sizeof_elem_type(tempdest.type):
			backend->get_sizeof_elem_type(tempsrc.type);

		optok.type = TOK_OPERATOR;
		optok.data = &opval;

		tempdest.type = dup_type(tempdest.type);
		tempsrc.type = dup_type(tempdest.type);
		cross_do_conv(&tempdest, backend->get_size_t()->code, 1);
		cross_do_conv(&tempsrc, backend->get_size_t()->code, 1);

		/* Remove typenods, since cross_do_conv() doesn't do that */
		tempdest.type->tlist = NULL;
		tempsrc.type->tlist = NULL;

		cross_exec_op(&optok, &tmpval, &tempdest, &tempsrc);
		*diff = cross_to_host_size_t(&tmpval);
		if (scaling > 1) {
			/* int* - int* yields val / sizeof(int) */
			*diff /= scaling;
		}
		return 0;
	}

	return -1;
}

static struct addr_const *
alloc_addr_const(void) {
	struct addr_const		*ret = n_xmalloc(sizeof *ret);
	static struct addr_const	nullconst;
	*ret = nullconst;
	return ret;
}


static struct addr_const	*labels_used_in_cexpr_head;
static struct addr_const	*labels_used_in_cexpr_tail;

void
process_labels_used_list(struct function *func) {
	/*
	 * First patch all entries
	 */
	struct addr_const	*c;

	for (c = labels_used_in_cexpr_head; c != NULL; c = c->next) {
		struct label	*l;

		if ((l = lookup_label(func, c->labeltok->data)) == NULL) {
			errorfl(c->labeltok, "Label `%s' not defined",
				c->labeltok->data);
		} else {
			c->labelname = n_xstrdup(l->instr->dat);
			c->funcname = func->proto->dtype->name;
		}
	}

	/*
	 * Reset lists for next function
	 */
	labels_used_in_cexpr_head = NULL;
	labels_used_in_cexpr_tail = NULL;
}

void
append_labels_used_list(struct addr_const *c) {
	if (labels_used_in_cexpr_head == NULL) {
		labels_used_in_cexpr_head = labels_used_in_cexpr_tail = c;
	} else {
		labels_used_in_cexpr_tail->next = c;
		labels_used_in_cexpr_tail = c;
	}
}

static struct tyval
do_static_var(struct expr *tree,
		struct tyval tv0,
		struct token *t,
		struct vreg **builtinvr,
		int extype,
		int is_paren_subex) {

	struct decl	*d;
	struct tyval	ret = tv0;
	int		i;

	if (tree->data->operators[0] != NULL
		&& tree->data->operators[0]->type == TOK_PAREN_OPEN) {
		struct fcall_data	*fd;

		fd = tree->data->operators[0]->data;
		if (fd->builtin != NULL
			&& fd->builtin->type == BUILTIN_OFFSETOF) {
			struct expr	*ex;

			/*
			 * 07/17/08: Now that __bultin_offsetof() may be
			 * non-constant as well, check whether it is
			 */
			ex = fd->builtin->args[0];
			if (ex->const_value == NULL) {
				/* Not constant! */
				if (extype == EXPR_OPTCONSTINIT
					|| extype == EXPR_OPTCONSTARRAYSIZE) {
					if (extype == EXPR_OPTCONSTINIT) {
						warningfl(t, "Variable "
						"initializers are not "
						"allowed in ISO C90");
					} else {
						warningfl(t, "Variable-"
						"size arrays are not "
						"allowed in ISO C90");
					}	
				} else if (extype == EXPR_OPTCONSTSUBEXPR) {
					;
				} else {	
					errorfl(t, "Expression not constant");
				}
				ret.not_constant = 1;
				ret.type = NULL;
				return ret;
			}
			*builtinvr = fd->builtin->builtin->toicode(fd, NULL, 1);
			ret.type = (*builtinvr)->type;
			ret.value = (*builtinvr)->from_const->data;
			goto builtin_done;
		}
	} else if (tree->data->operators[0] != NULL
		&& tree->data->operators[0]->type == TOK_OP_ADDRLABEL) {
		/*
		 * 07/20/08: This must be a label address constant for
		 * computed gotos - &&label. Note that we cannot look up
		 * the name yet because it may occur later in the function
		 */
		ret.address = alloc_addr_const();
		ret.address->labeltok = t;
		append_labels_used_list(ret.address);
		ret.type = make_void_ptr_type();
		return ret;
	}

	if ((d = access_symbol(curscope, t->data, 1)) == NULL) {
		/*
		 * This may be a call to an undeclared
		 * function if this is a VLA size
		 * expression or variable initializer
		 */
		if (extype == EXPR_OPTCONSTINIT
			|| extype == EXPR_OPTCONSTARRAYSIZE
			|| extype == EXPR_OPTCONSTSUBEXPR) {
			ret.not_constant = 1;
			ret.type = NULL;
		} else {	
			errorfl(t,
			"Undeclared identifier `%s'",
			t->ascii);
			ret.type = ret.value = NULL;
		}
		return ret;
	}	
	if (d->dtype->storage != TOK_KEY_STATIC
		&& d->dtype->storage != TOK_KEY_EXTERN
		&& d->dtype->tenum == NULL
		&& (d->dtype->tlist == NULL
		|| d->dtype->tlist->type
			!= TN_FUNCTION)) {
		/*
		 * XXX seems totally bogus.. only
		 * functions should be allowed at all for
		 * *values*???
		 */
		if (extype == EXPR_OPTCONSTINIT
			|| extype == EXPR_OPTCONSTARRAYSIZE) {
			if (extype == EXPR_OPTCONSTINIT) {
				warningfl(t, "Variable "
				"initializers are not "
				"allowed in ISO C90");
			} else {
				warningfl(t, "Variable-"
				"size arrays are not "
				"allowed in ISO C90");
			}	
		} else if (extype == EXPR_OPTCONSTSUBEXPR) {
			;
		} else {	
			errorfl(t, "Use of non-static "
			"identifier `%s' in constant"
			" expression", d->dtype->name);
		}
		ret.not_constant = 1;
		ret.type = ret.value = NULL;
		return ret;
	}

	/*
	 * 06/01/08: Record that this is a static
	 * variable, i.e. it is only allowed for
	 * address constants
	 *
	 * 07/15/08: This also ruled out enum instances because
	 * there was no distinction between constant and instance
	 * by checking tenum_value.
	 *
	 * In
	 *
	 *    enum { VAL } foo;
	 *
	 * ... VAL and foo were both ruled now, now only VAL is.
	 */
	if ((d->dtype->tenum == NULL || d->tenum_value == NULL)
		&& (d->dtype->tlist == NULL
		|| d->dtype->tlist->type
			!= TN_FUNCTION)) {
		/* Not enum or function - variable */
		if (d->dtype->tlist != NULL
			&& d->dtype->tlist->type
			== TN_ARRAY_OF) {
			/*
			 * Array - decays into pointer
			 * to first element, i.e. an
			 * address constant!
			 */
			;
		} else {
			ret.is_static_var = t;
		}
	}

	ret.type = d->dtype;

	if (ret.type->code == TY_ENUM && ret.type->tlist == NULL) {
		/*
		 * 07/15/08: Didn't change the type from enum to
		 * int, so the emitters couldn't load these constants
		 * XXX don't use expensive memdup!
		 */
		ret.type = n_xmemdup(ret.type, sizeof *ret.type);
		ret.type->code = TY_INT;
		ret.type->sign = TOK_KEY_SIGNED;
		ret.type->tenum = NULL;
	}

	if (d->dtype->tlist == NULL
		&& d->dtype->tenum != NULL
		&& d->tenum_value != NULL) {

		/*
		 * The value is freed after use.  This is
		 * OK for tokens, but not for enum
		 * constants, whose values may be needed
		 * later - possibly in the same
		 * expression. So create a copy
		 */
		/* XXX Cross!!!!!!!! */
		ret.value = n_xmemdup(
			d->tenum_value->data,
			sizeof(int));
	} else {
		int		is_addr = 0 /*1*/ /*0*/;
		int		is_subscript = 0;
		struct type	curtype;

		copy_type(&curtype, d->dtype, 0);

		/*
		 * An identifier used in a constant
		 * expression may only be used for
		 * an address constant
		 * 
		 * 07/14/08: Revived address detection, since otherwise
		 * OPTSUBEXPR evaluation of static variables always
		 * considered those to be addresses
		 */
		for (i = 0; tree->data->operators[i] != NULL; ++i) {
			if (tree->data->operators[i]->type ==
				TOK_OPERATOR) {
				if (*(int *)tree->data->operators[i]->data
					== TOK_OP_ADDR) {
					/* Address-of operator */
					is_addr = 1;
				}
			} else if (tree->data->operators[i]->type ==
				TOK_ARRAY_OPEN) {
				is_subscript = 1;
			} else if (tree->data->operators[i]->type ==
				TOK_OP_STRUMEMB) {
				struct decl	*dp;
				struct token	*ident = tree->data->operators[i]->data;

				dp = access_symbol(curtype.tstruc->scope,
					ident->data, 0);
				if (dp != NULL) {
					if (dp->dtype->tlist != NULL
						&& dp->dtype->tlist->type == TN_ARRAY_OF) {
						is_addr = 1;
					}
				}
			}
		}
		if (!is_addr) {
			if (d->dtype->tlist != NULL
				&& (d->dtype->tlist->type == TN_ARRAY_OF
				|| d->dtype->tlist->type == TN_FUNCTION)) {
				if (!is_subscript) {
					/*
					 * OK - Static array or function
					 */
					is_addr = 1;
				} else if (d->dtype->tlist->type == TN_ARRAY_OF
					&& d->dtype->tlist->next != NULL
					&& d->dtype->tlist->next->type == TN_ARRAY_OF) {
					/*
					 * 02/28/09: For multi-dimensional arrays,
					 * a subscript yields one of the member
					 * arrays, which is allowed because it can
					 * be computed as an address constant of
					 * the base array plus offset
					 */
					is_addr = 1;
				} else {
					/*
					 * Not OK as constant - Subscripted
					 * array
					 */
					int	err = 0;
					if (extype == EXPR_OPTCONSTINIT
						|| extype == EXPR_OPTCONSTARRAYSIZE
						|| extype == EXPR_OPTCONSTSUBEXPR) {
						if (extype == EXPR_OPTCONSTINIT) {
							warningfl(t, "Variable "
							"initializers are not "
							"allowed in iso c90");
						} else if (extype == EXPR_OPTCONSTARRAYSIZE) {
							warningfl(t, "variable-"
							"size arrays are not "
							"allowed in iso c90");
						} else { /* expr_optconstsubexpr */
							;
						}
						err = 1;
					} else {
						/*
						 * 05/26/09: We can't error for
						 * parenthesized subexpressions
						 * yet, because that would rule
						 * out
						 *
						 *     &(foo[0])
						 */
						if (!is_paren_subex) {
							errorfl(t, "Invalid array subscript in "
								"constant expression");
							err = 1;
						} else {
							is_addr = 1;
						}
					}

					if (err) {
						ret.not_constant = 1;
						ret.type = NULL;
						return ret;
					}
				}
			}
		}
		if (!is_addr) {
			if (is_paren_subex) {
				/*
				 * Make things like &(var)
				 * work
				 */
				;
			} else {
				/*
				 * Must be subscript like buf[x],
				 * AND there is no address-of
				 * operator - So it can't be a
				 * constant expression
				 */
				if (extype == EXPR_OPTCONSTINIT
					|| extype == EXPR_OPTCONSTARRAYSIZE
					|| extype == EXPR_OPTCONSTSUBEXPR) {
					if (extype == EXPR_OPTCONSTINIT) {
						warningfl(t, "Variable "
						"initializers are not "
						"allowed in ISO C90");
					} else if (extype == EXPR_OPTCONSTARRAYSIZE) {
						warningfl(t, "Variable-"
						"size arrays are not "
						"allowed in ISO C90");
					} else { /* EXPR_OPTCONSTSUBEXPR */
						;
					}
				} else {
					errorfl(t,
						"Static variable value used "
						"in constant expression");
				}
				ret.not_constant = 1;
				ret.type = NULL;
				return ret;
			}
		} else {	
			/* Address constant */
			ret.address = alloc_addr_const();
			ret.address->dec = d;
		}
	}
builtin_done:	
	return ret;
}

#endif /* #ifndef PREPROCESSOR */


static struct tyval
do_eval_const_expr(struct expr *tree, int extype, int is_paren_subex) {
	struct tyval		ret;
	struct tyval		tmp1;
	struct tyval		tmp2;
	struct tyval		*tv;
	struct tyval		*left;
	struct tyval		*right;
	size_t			i;
	int			rc = 0;
	int			nullok = 1;
	int			member_addr_const = 0;
	static struct tyval	nulltv;
	struct vreg		*builtinvr = NULL;

	ret = nulltv;
	tmp1 = nulltv;
	tmp2 = nulltv; 


	if (tree->op == 0) {
#ifndef PREPROCESSOR
		static struct vreg	structvr;
#endif
		struct decl		*dp;
		int			is_address_const = 0;
		int			has_struct_op = 0;

		if (tree->stmt_as_expr) {
			/*
			 * 07/07/08: This was missing; We always assumed this
			 * has to be a normal sub-expression. With our more
			 * aggressive constant expression evaluation, this
			 * issue came up. For now, treat all stmt-as-expr as
			 * not constant! (Hope this doesn't break a great
			 * deal)
			 * XXX !!
			 */
			ret.type = NULL;
			ret.not_constant = 1;
			return ret;
		} else if (tree->data->is_expr) {
			/*
			 * 07/14/08: Only set the is_paren_subex flag if there
			 * is only a single sub-expression! I.e. set it for
			 *
			 *    (x)
			 * 
			 * but not
			 *
			 *    (x + y)
			 *
			 * ... since for all intents and purposes, the flag is
			 * only intended for the former case, to allow address
			 * constants
			 */
			ret = do_eval_const_expr(tree->data->is_expr,
				extype, /*1*/
				tree->data->is_expr->op == 0);
			if (ret.type == NULL) {
				return ret;
			}
#ifndef PREPROCESSOR
			if (ret.is_static_var != NULL) {
				/*
				 * 07/14/08: Perform address checking. This
				 * can only be done now because this is a 
				 * parenthesized expression like
				 *
				 *    &(foo)
				 */
				ret = do_static_var(tree, ret, ret.is_static_var, &builtinvr, extype, is_paren_subex);
				if (ret.type == NULL) {
					return ret;
				}	
			}
#endif
		} else if (tree->data->meat != NULL) {
			struct token	*t = tree->data->meat;
			
			if (t->type == TOK_STRING_LITERAL) {
				ret.str = t->data;
				ret.type = ret.str->ty;
			} else if (t->type == TOK_IDENTIFIER) {		
#ifdef PREPROCESSOR
				int     zero = 0;

				/*
				 * All macro expansion has already been
				 * done earlier, so this identifier is
				 * not a macro, so it becomes the value
				 * 0
				 */
				ret.type = make_basic_type(TY_INT);
				ret.value = n_xmemdup(&zero, sizeof zero);
#else
				ret = do_static_var(tree, ret, t, &builtinvr, extype, is_paren_subex);
				if (ret.type == NULL) {
					return ret;
				}
#endif
#ifdef PREPROCESSOR
                        } else if (t->type == TOK_DEFINED) {
				struct token    *tmp = t->data;
				int             val;

				ret.type = make_basic_type(TY_INT);
				if (lookup_macro(tmp->data, 0, -1) != NULL) {
					val = 1;
				} else {
					val = 0;
				}
				ret.value = n_xmemdup(&val, sizeof val);
#endif
			} else {
				/* Must be constant */
				if (t->type == TOK_COMP_LITERAL) {
					/*
					 * 06/28/08: This was missing, so we passed TOK_COMP_LITERAL to
					 * make_basic_type()!
					 */
					struct comp_literal	*lit;
					lit = t->data;
					ret.type = n_xmemdup(lit->struct_type, sizeof *lit->struct_type);
				} else {
					ret.type = make_basic_type(t->type);
					ret.type = n_xmemdup(ret.type,
						sizeof *ret.type);
				}

				if (t->type == TOK_COMP_LITERAL) {
					struct comp_literal	*lit;

					lit = t->data;
					if (tree->data->operators[0] != NULL) {
						/*
						 * Don't handle things like
						 *
						 *   static struct *foo =
						 *     &(struct foo) {...};
						 *
						 * just yet
						 * 
						 * 07/10/08: Our more aggressive
						 * const expr evaluation now
						 * requires this not to yield
						 * an error
						 */
					/*	unimpl();*/

						ret.not_constant = 1;
						ret.type = NULL;
						return ret;
					}
					ret.static_init = lit->init; 
				} else if (IS_FLOATING(ret.type->code)) {
					struct ty_float	*fc;

					fc = t->data;
					ret.value = fc->num->value;

					/*
					 * This constant is not needed
					 * in the fp list for fp memory
					 * access
					 *
					 * 02/03/09: This seems to be a
					 * little too optimistc. It will
					 * also execute in
					 *
					 *    const_float * non_const_int
					 *
					 * ... so when we fall back to
					 * executing the operation at
					 * runtime, the float constant has
					 * wrongly been removed
					 *
					 * XXX Where is the right place
					 * to put this?
					 */
#if 0
					remove_float_const_from_list(fc);
#endif
				} else {
					ret.value = t->data;
				}
#ifndef PREPROCESSOR
				ret.is_nullptr_const =
					is_nullptr_const(t, ret.type);
#endif

			}
			ret.alloc = 0;
		} else if (tree->data->is_sizeof) {
#ifndef PREPROCESSOR
			struct vreg	*vr;

			vr = tree->data->is_sizeof->data;
			ret.type = vr->type;

			if (vr->from_const == NULL) {
				/*
				 * 07/12/08: This happens for VLAs
				 */
				if (extype == EXPR_OPTCONSTINIT
					|| extype == EXPR_OPTCONSTARRAYSIZE
					|| extype == EXPR_OPTCONSTSUBEXPR) {
					if (extype == EXPR_OPTCONSTINIT) {
					warningfl(tree->data->is_sizeof, "Variable "
						"initializers are not "
						"allowed in ISO C90");
					} else if (extype == EXPR_OPTCONSTARRAYSIZE) {
						warningfl(tree->tok, "Variable-"
						"size arrays are not "
						"allowed in ISO C90");
					} else { /* EXPR_OPTCONSTSUBEXPR */
						;
					}
				} else {
					errorfl(tree->data->is_sizeof,
						"Invalid non-constant "
						"sizeof");
				}

				ret.not_constant = 1;
				ret.type = NULL;
				return ret;
			}	
			ret.value = vr->from_const->data;
#endif /* #ifndef PREPROCESSOR */
		} else {
			abort();
		}

		/* Now apply all unary/postfix/prefix operators */
		tv = &ret;
		if (builtinvr != NULL) {
			/*
			 * Meat of subexpression is builtin function -
			 * ignore first (function call) operator
			 */
			i = 1;
		} else {
			i = 0;
		}	
		for (/*i = 0*/; tree->data->operators[i] != NULL; ++i) {
			struct token	*op;
			struct decl	*d;
			struct token	*ident;
			int		opval;
			int		was_nullptr_const = 0;

			op = tree->data->operators[i];

			if (op->type == TOK_PAREN_OPEN) {
				/*
				 * 07/14/08: Function call - cannot be constant
				 */
				if (extype == EXPR_OPTCONSTINIT
					|| extype == EXPR_OPTCONSTARRAYSIZE
					|| extype == EXPR_OPTCONSTSUBEXPR) {
					if (extype == EXPR_OPTCONSTINIT) {
					warningfl(op, "Variable "
						"initializers are not "
						"allowed in ISO C90");
					} else if (extype == EXPR_OPTCONSTARRAYSIZE) {
						warningfl(op, "Variable-"
						"size arrays are not "
						"allowed in ISO C90");
					} else { /* EXPR_OPTCONSTSUBEXPR */
						;
					}
				} else {
					errorfl(op, "Function call in constant expression");
				}	
				ret.type = NULL;
				ret.not_constant = 1;
				return ret;
			}


			if (ret.value == NULL
				&& ret.address == NULL
				&& op->type != TOK_OP_CAST
				&& (op->type != TOK_OPERATOR
				|| *(int *)op->data !=TOK_OP_ADDR)
				/* 07/11/08: Union below was missing! */
				&& ( (ret.type->code != TY_STRUCT && ret.type->code != TY_UNION)
				|| (op->type != TOK_OP_STRUMEMB
					&& op->type != TOK_OP_STRUPMEMB))) {

				if (extype == EXPR_OPTCONSTINIT
					|| extype == EXPR_OPTCONSTARRAYSIZE
					|| extype == EXPR_OPTCONSTSUBEXPR) {
					if (extype == EXPR_OPTCONSTINIT) {
					warningfl(op, "Variable "
						"initializers are not "
						"allowed in ISO C90");
					} else if (extype == EXPR_OPTCONSTARRAYSIZE) {
						warningfl(op, "Variable-"
						"size arrays are not "
						"allowed in ISO C90");
					} else { /* EXPR_OPTCONSTSUBEXPR */
						;
					}
				} else {
					errorfl(op, "Invalid use of operator in constant "
						"expression");
				}
				ret.not_constant = 1;
				ret.type = NULL;
				return ret;
			}

			switch (op->type) {
			case TOK_OP_CAST:
#ifndef PREPROCESSOR
				d = op->data;
				if (tv->is_nullptr_const) {
					if (d->dtype->tlist != NULL
						&& d->dtype->tlist->type ==
						TN_POINTER_TO
						&& d->dtype->tlist->next
						== NULL
						&& d->dtype->code == TY_VOID) {
						/* Remains nullptr */
						;
					} else {
						tv->is_nullptr_const = 0;

						/*
						 * 11/05/20: For nonsensical constant
						 * expressions such as
						 *    (char)((void *)0)
						 * which are used in some real life
						 * programs - record that the original
						 * value was a null pointer constant,
						 * such that the operation is
						 * possible as a no-op
						 */
						was_nullptr_const = 1;
					}
				} else {
					/*
					 * 07/12/08: The cast operation is only
					 * allowed if we have an arithmetic
					 * constant, or it's an address constant
					 * which is (nonsensically) cast to an
					 * integer type
					 */
					if (tv->value == NULL) {
						int	bad = 1;


						if (extype == EXPR_OPTCONSTINIT
							|| extype == EXPR_OPTCONSTARRAYSIZE) {
							if (extype == EXPR_OPTCONSTINIT) {
								warningfl(op, "Variable "
								"initializers are not "
								"allowed in ISO C90");
							} else {
								warningfl(op, "Variable-"
								"size arrays are not "
								"allowed in ISO C90");
							}	
						} else if (extype == EXPR_OPTCONSTSUBEXPR) {
							;
						} else {	
							if (tv->type->tlist != NULL
								&& d->dtype->tlist == NULL) {
								/*
								 * This must be something like
								 * (long)&addr (handled below)
								 */
								bad = 0;
							} else if (tv->type->tlist != NULL
								&& d->dtype->tlist != NULL) {
								/*
								 * Pointer to pointer can't do
								 * any damage as far as const-
								 * ness goes
								 */
								bad = 0;
							} else {
								errorfl(op, "Cast makes expression "
									"non-constant");
							}
						}

						if (bad) {
							ret.not_constant = 1;
							ret.type = NULL;
							return ret;
						}
					}
				}

				if (d->dtype->code == TY_VOID && d->dtype->tlist == NULL) {
					/*
					 * 07/12/08: Cast to void; This cannot possibly be
					 * a constant expression
					 */
					if (extype == EXPR_OPTCONSTSUBEXPR) {
						;
					} else {
						errorfl(op, "Invalid void expression");
					}
					ret.not_constant = 1;
					ret.type = NULL;
					return ret;
				}

				if (d->dtype->tlist != NULL) {
					if (tree->data->meat &&
						tree->data->meat->type
						== TOK_STRING_LITERAL) {
						;
					} else {
#if 0
						do_conv(tv, TY_ULONG); /* XXX */
#endif
						if (ret.type->tlist == NULL) {
							if (ret.value != NULL) {
								cross_do_conv(tv, TY_ULONG, 0); /* XXX */
							} else if (ret.address != NULL) {
								;
							} else {
								ret.type = NULL;
								ret.not_constant = 1;
								return ret;
							}	
						tv->type = make_basic_type(
							TY_ULONG);
						tv->type = n_xmemdup(tv->type,
							sizeof *tv->type);
						}	
					}
					ret.type = d->dtype;
				} else {
					if (tv->type->tlist != NULL) {
						/*
						 * Very dangerous! A pointer
						 * cast to an integer as a
						 * *constant* expression! This
						 * only makes sense if source
						 * and target are the same
						 * size. We have to support
						 * this for programs like 
						 * xterm
						 */
						size_t	dest_size = 
					backend->get_sizeof_type(d->dtype,0);
						size_t	src_size =
					backend->get_ptr_size();		

						if (src_size == dest_size) {
							/*
							 * 07/07/09: Only enable this
							 * warning for mandatory constant
							 * expressions
							 *
							 * XXX Doesn't work :-(
							 *
							 * OPTCONSTSUBEXPR is always used.
							 * Clean this up
							 */
							if (extype == EXPR_CONST
								|| extype == EXPR_CONSTINIT
								|| extype == EXPR_OPTCONSTSUBEXPR) {
								warningfl(op,
			"Nonportable bogus cast of pointer to integer in "
			"constant expression");
							}
						} else {
							if (extype == EXPR_OPTCONSTINIT
							|| extype == EXPR_OPTCONSTARRAYSIZE) {
							if (extype == EXPR_OPTCONSTINIT) {
							warningfl(op, "Variable "
							"initializers are not "
							"allowed in ISO C90");
							} else if (extype ==
						EXPR_OPTCONSTSUBEXPR) {
								;
							} else {
							warningfl(op, "Variable-"
							"size arrays are not "
							"allowed in ISO C90");
							}	
							ret.not_constant = 1;
							ret.type = NULL;
							return ret;
							} else {
								/*
								 * 11/05/20: Support this construct for
								 * null pointer constants with a warning,
								 * for apps like PostgreSQL
								 */
								(was_nullptr_const? warningfl: errorfl) (op,
			"Invalid bogus cast of pointer to integer in "
			"constant expression");
							}
						}	

						/*
						 * BEWARE!!! We keep the type as
						 * ``pointer'' because the backend
						 * otherwise gets confused and
						 * thinks this really is a long. Hm
						 *
						 * 08/18/07 If it is an array, we
						 * change the type to pointer
						 * because otherwise the backends
						 * do not understand this kludged
						 * initializer
						 */
						if (tv->type->tlist->type ==
							TN_ARRAY_OF) {
							tv->type =
								dup_type(tv->type);
							tv->type->tlist->type =
								TN_POINTER_TO;
						}
						tv->type = d->dtype;
					} else {
						cross_do_conv(tv,
							d->dtype->code, 0);
						ret.type = d->dtype;
					}
				}	
				if (ret.type->code == TY_ENUM
					&& ret.type->tlist == NULL) {
					/* XXX is this really desirable? */
					ret.type = n_xmemdup(make_basic_type(TY_INT),
						sizeof(struct type));
				}
#endif
				break;
			case TOK_ARRAY_OPEN:
#ifndef PREPROCESSOR
#if 0
				if (ret.address != NULL
					&& (op=tree->data->operators[i+1])
						!= NULL
					&& op->type == TOK_OPERATOR
					&& *(int *)op->data == TOK_OP_ADDR) {
#endif
				{
					/* make &foo[x] work */
					struct expr	*ex
						= tree->data->operators[ i /*++ */]->
						data;

					if (eval_const_expr(ex, extype,
						&ret.not_constant) != 0) {
						ret.type = NULL;
						return ret; 
					}

					{
						/*
						 * 12/23/08: Create a copy of the value
						 * because it may be associated with a
						 * token, e.g. in
						 *
						 *    &a[0]
						 *
						 * addr_add_sub() will convert the index
						 * to type size_t. If it turns out that
						 * we really aren't dealing with a constant
						 * expression, then it will have to be
						 * reevaluated. And if a token has type
						 * ``int'' set but the value was converted
						 * to size_t, things would break.
						 */
						void *p = zalloc_buf(Z_CEXPR_BUF);  /*n_xmalloc(16);*/ /* XXX */
						memcpy(p, ex->const_value->value, 16);
						ex->const_value->value = p;
					}

					addr_add_sub(&ret, ex, tv->type,
						TOK_OP_PLUS);
					ret.type = dup_type(ret.type);
					ret.type->tlist = ret.type->tlist->next;
					if (member_addr_const) is_address_const = 0; /* XXX 2011 - ok? */
				}	
#endif

				break;
			case TOK_PAREN_OPEN:
				/* Must be function call operator */
#ifndef PREPROCESSOR
				if (extype == EXPR_OPTCONSTINIT
					|| extype == EXPR_OPTCONSTARRAYSIZE) {
					if (extype == EXPR_OPTCONSTINIT) {
						warningfl(op, "Variable "
						"initializers are not "
						"allowed in ISO C90");
					} else {
						warningfl(op, "Variable-"
						"size arrays are not "
						"allowed in ISO C90");
					}	
					ret.not_constant = 1;
				} else if (extype == EXPR_OPTCONSTSUBEXPR) {
					ret.not_constant = 1;
				} else {	
					errorfl(op, "Invalid function call "
					"in constant expression");
				}
				ret.type = NULL;
				return ret;
#endif
				break;
			case TOK_OP_STRUMEMB: /* XXX !??why not TOK_OPERATOR */
			case TOK_OP_STRUPMEMB:
#ifndef PREPROCESSOR
				if (ret.type == NULL
					|| ((ret.type->code != TY_STRUCT
					&& ret.type->code != TY_UNION)
					 /* || ret.type->tlist != NULL */  )) {
					errorfl(tree->data->operators[i],
						"Incorrect type for "
						"operator `%s'",
						tree->data->operators[i]->ascii);
					ret.type = NULL;
					return ret;
				}

				ident = op->data;
				if ((dp = access_symbol(ret.type->tstruc->
					scope, ident->data, 0)) == NULL) {
					errorfl(tree->data->operators[i],
						"Unknown structure member "
						"`%s'", ident->data);
					ret.type = NULL;
					return ret;
				}	
				/*
				 * 04/25/11: Handle address constants better
				 * (probably still not flawlessly). Previously
				 * we've mostly been looking for the & operator
				 * to determine whether something is an address
				 * constant (all of this const-expr evaluation
				 * stuff is really weak). This breaks down for
				 * static array struct members;
				 *
				 *    static struct {
				 *       char buf[128];
				 *    } s;
				 *    static char *p = s.buf;  // this err'ed
				 *
				 * So, upon encountering a struct member, we
				 * check this. Hopefully this doesn't break
				 * anything
				 */
				if (dp->dtype->tlist != NULL
					&& dp->dtype->tlist->type == TN_ARRAY_OF) {
					is_address_const = member_addr_const = 1;
				}



				/*
				 * XXX this is quite ad-hocly kludged to make
				 * nwcc work with the gtar source... it does
				 * not understand named address constants
				 * yet and is generally incomplete
				 */
				if (ret.value) {
					static struct vreg	parentvr;

					/*
					 * 07/09/08: This was missing; If we
					 * change the original value, then
					 * we can't fall back to a non-const
					 * evaluation in case the expression
					 * isn't really constant
					 */
					void	*orig_value = ret.value;
					ret.value = zalloc_buf(Z_CEXPR_BUF); /*n_xmalloc(16);*/ /* XXX */
					memcpy(ret.value, orig_value, 16);

					parentvr.type = ret.type;
					structvr.parent = &parentvr; 
					structvr.memberdecl = dp;	
					*(long *)ret.value += calc_offsets(&structvr);
				} else if (ret.address != NULL) {
					static struct vreg	parentvr;

					parentvr.type = ret.type;
					structvr.parent = &parentvr; 
					structvr.memberdecl = dp;	
					ret.address->diff +=
						calc_offsets(&structvr);
				} else {
					/*unimpl();*/
					/*
					 * 07/11/08: XXX Compound literals have
					 * static_init set but nothing else.
					 * That should probably be supported
					 * too
					 */
					ret.not_constant = 1;
					ret.type = NULL;
					return ret;
				}
				ret.type = dp->dtype;
				has_struct_op = 1;
#endif
				break;
			case TOK_OPERATOR:
				opval = *(int *)op->data;
				if (opval == TOK_OP_ADDR) {
#ifndef PREPROCESSOR
					is_address_const = 1;
					if (i == 0) {
						; /* already taken care of */
					} else {
					}	
					/* XXX this sucks */
					ret.type =
						addrofify_type
						(ret.type);
#endif
				} else if (opval == TOK_OP_LNEG
					|| opval == TOK_OP_BNEG
					|| opval == TOK_OP_UPLUS
					|| opval == TOK_OP_UMINUS) { 
					int	oldtype = op->type; /* XXX needed? */
					static struct tyval	tytmp;

					if (tv->value == NULL) {
						/*
						 * 07/12/08: These operators
						 * can only be applied to
						 * arithmetic constants. If
						 * this is an address/static
						 * variable instead, it can't
						 * be constant
						 */
						if (extype == EXPR_OPTCONSTINIT
							|| extype == EXPR_OPTCONSTARRAYSIZE
							|| extype == EXPR_OPTCONSTSUBEXPR) {
							if (extype == EXPR_OPTCONSTINIT) {
								warningfl(op, "Variable "
								"initializers are not "
								"allowed in ISO C90");
							} else if (extype == EXPR_OPTCONSTARRAYSIZE) {
								warningfl(op, "Variable-"
								"size arrays are not "
								"allowed in ISO C90");
							}
						} else {
							errorfl(op, "Invalid use of `%s' operator "
								"in constant expression",
								op->ascii);
						}
						ret.not_constant = 1;
						ret.type = NULL;
						return ret;
					}

					op->type = opval;
					if (opval == TOK_OP_LNEG
						&& tv->is_nullptr_const) {
						/*
						 * 03/03/09: Very important null
						 * pointer negation feature -
						 * !((void *)0)
						 * needed by emacs!!
						 */
						tytmp.value = zalloc_buf(Z_CEXPR_BUF); /*n_xmalloc(16);*/ /* XXX */
						tytmp.type = tv->type;
						if (tv->type->code < TY_INT) {
							tv->type = n_xmemdup(
								make_basic_type(TY_INT),
								sizeof(struct type));
						}
						*(int *)tytmp.value = 1;
					} else {
						cross_exec_op(op, &tytmp, tv, NULL);
					}
					op->type = oldtype;
					*tv = tytmp;
				} else if (opval == TOK_OP_DEREF) {
					if (member_addr_const) is_address_const = 0; /* XXX 2011 - ok? */
					if (extype == EXPR_OPTCONSTINIT
						|| extype == EXPR_OPTCONSTARRAYSIZE) {
						if (extype == EXPR_OPTCONSTINIT) {
							warningfl(op, "Variable "
							"initializers are not "
							"allowed in ISO C90");
						} else {
							warningfl(op, "Variable-"
							"size arrays are not "
							"allowed in ISO C90");
						}	
						ret.not_constant = 1;
					} else if (extype == EXPR_OPTCONSTSUBEXPR) {
						;
					}
					ret.type = NULL;
					return ret;
				} else if (opval == TOK_OP_INCPRE
					|| opval == TOK_OP_INCPOST
					|| opval == TOK_OP_DECPRE
					|| opval == TOK_OP_DECPOST) {
					/*
					 * 07/12/08: Allow these operators
					 */
					ret.not_constant = 1;
					ret.type = NULL;
					return ret;
				} else {
					printf("unknown operator %d\n",
						opval);
					abort();
				}	
				break;
			case TOK_KEY_SIZEOF:
			case TOK_KEY_ALIGNOF:	
				; /* taken care of already */
				break;
			case TOK_OP_ADDRLABEL:
#ifndef PREPROCESSOR
				if (i == 0
					&& ret.address != NULL
					&& ret.address->labeltok != NULL) {
					/* OK - &&label */
					;
				} else {
					errorfl(op, "Invalid use of operator "
						"`%s'", op->ascii);
				}
#endif
				break;
			default:
				printf("unknown operator %d\n", op->type);
				unimpl();
			}
		}

		/*
		 * 06/01/08: Now that all unary operators have been applied,
		 * do sanity checking; If the ``meat'' of this sub-expression
		 * is a static variable, it is only allowed to be used as an
		 * address constant!
		 *
		 * Note that we cannot do this here yet if we're inside of a
		 * parenthesized sub-expression such as;
		 *
		 *    &(foo)
		 *
		 * ... where the outer (not visible at this point) address-of
		 * operator makes it valid. Thus the caller has to do it
		 */
		if ( (ret.is_static_var
			&& !is_address_const
			&& !is_paren_subex)
			/*
			 * 07/22/08: Disallow    foo.bar   and  foo->bar
			 * without address-of operator
			 */
			|| (ret.address
			&& has_struct_op
			&& !is_address_const) ) {
			/* Cannot be constant */
			if (extype == EXPR_OPTCONSTINIT
				|| extype == EXPR_OPTCONSTARRAYSIZE) {
				if (extype == EXPR_OPTCONSTINIT) {
					warningfl(ret.is_static_var, "Variable "
					"initializers are not "
					"allowed in ISO C90");
				} else {
					warningfl(ret.is_static_var, "Variable-"
					"size arrays are not "
					"allowed in ISO C90");
				}	
				ret.not_constant = 1;
			} else if (extype == EXPR_OPTCONSTSUBEXPR) {
				ret.not_constant = 1;
			} else {	
				errorfl(ret.is_static_var, "Invalid use of static "
					"variable in constant "
					"expression");
			}
			ret.type = NULL;
			return ret;
		} else if (ret.is_static_var && is_address_const) {
			/*
			 * This is a static variable which is correctly used
			 * as an address constant; We can remove the static
			 * marker (otherwise   (&foo)  breaks when the outer
			 * call around the nested sub-expression reevaluates
			 * the static setting)
			 */
			ret.is_static_var = NULL;
		}

		return ret;
	}

	/*
	 * 07/22/08: Don't set is_paren_subex flag, since for all intents and
	 * purposes
	 *
	 *     (foo op bar)
	 *
	 * ... is not constant if foo or bar is not constant. The flag is only
	 * intended to make
	 *
	 *     &(foo)
	 *
	 * work
	 */
	tmp1 = do_eval_const_expr(tree->left, extype, 0 /*is_paren_subex*/);
	if (tmp1.type == NULL) {
		return tmp1;
	}	

	if (tree->op == TOK_OP_LAND) {
		int	res = 0;
		int	not_zero = 0;

		/*
		 * 07/21/08: Handle address constants
		 */
		if (tmp1.address != NULL) {
			if (tmp1.address->diff == 0) {
				/*
				 * OK - &static_var or &&label is definitely
				 * nonzero
				 */
				not_zero = 1;
			} else {
				/*
				 * &addr +/- offset may be zero (by invoking
				 * undefined behavior, but it's a possibility
				 * nonetheless) - wait until runtime to find
				 * out
				 */
				ret.type = NULL;
				ret.not_constant = 1;
				return ret;
			}
		} else if (const_value_is_nonzero(&tmp1)) {
			not_zero = 1;
		}

		if (not_zero) {
			/*
			 * 07/22/08: Don't set is_paren_subex flag, since for all intents and
			 * purposes
			 *
			 *     (foo op bar)
			 *
			 * ... is not constant if foo or bar is not constant. The flag is only
			 * intended to make
			 *
			 *     &(foo)
			 *
			 * work
			 */
			tmp2 = do_eval_const_expr(tree->right, extype,
					0 /*is_paren_subex*/);
			if (tmp2.type == NULL) {
				/* 07/15/08: This was missing */
				return tmp2;
			}

			/*
			 * 07/21/08: Handle address constants
			 */
			if (tmp2.address != NULL) {
				if (tmp2.address->diff == 0) {
					/*
					 * OK - &static_var or &&label is definitely
					 * nonzero
					 */
					res = 1;
				} else {
						/*
					 * &addr +/- offset may be zero (by invoking
					 * undefined behavior, but it's a possibility
					 * nonetheless) - wait until runtime to find
					 * out
					 */
					ret.type = NULL;
					ret.not_constant = 1;
					return ret;
				}
			} else if (const_value_is_nonzero(&tmp2)) {
				res = 1;
			}


#if 0
			if (const_value_is_nonzero(&tmp2)) {
				res = 1;
			}
#endif
		}
		ret.type = make_basic_type(TY_INT);
		ret.type = n_xmemdup(ret.type, sizeof *ret.type);
		ret.value = n_xmemdup(&res, sizeof res);
		goto out;
	} else if (tree->op == TOK_OP_LOR) {	
		int	res = 0;
		int	not_zero = 0;


		/*
		 * 07/21/08: Handle address constants
		 */
		if (tmp1.address != NULL) {
			if (tmp1.address->diff == 0) {
				/*
				 * OK - &static_var or &&label is definitely
				 * nonzero
				 */
				not_zero = 1;
			} else {
					/*
				 * &addr +/- offset may be zero (by invoking
				 * undefined behavior, but it's a possibility
				 * nonetheless) - wait until runtime to find
				 * out
				 */
				ret.type = NULL;
				ret.not_constant = 1;
				return ret;
			}
		} else if (const_value_is_nonzero(&tmp1)) {
			not_zero = 1;
		}

		if (/*!const_value_is_nonzero(&tmp1)*/  !not_zero) {
			/*
			 * 07/22/08: Don't set is_paren_subex flag, since for all intents and
			 * purposes
			 *
			 *     (foo op bar)
			 *
			 * ... is not constant if foo or bar is not constant. The flag is only
			 * intended to make
			 *
			 *     &(foo)
			 *
			 * work
			 */
			tmp2 = do_eval_const_expr(tree->right, extype, 
				0 /*is_paren_subex*/);
			if (tmp2.type == NULL) {
				/* 07/17/08: Missing! */
				return tmp2;
			}


#if 0
			if (const_value_is_nonzero(&tmp2)) {
				res = 1;
			}
#endif
			/*
			 * 07/21/08: Handle address constants
			 */
			if (tmp2.address != NULL) {
				if (tmp2.address->diff == 0) {
					/*
					 * OK - &static_var or &&label is definitely
					 * nonzero
					 */
					res = 1;
				} else {
						/*
					 * &addr +/- offset may be zero (by invoking
					 * undefined behavior, but it's a possibility
					 * nonetheless) - wait until runtime to find
					 * out
					 */
					ret.type = NULL;
					ret.not_constant = 1;
					return ret;
				}
			} else if (const_value_is_nonzero(&tmp2)) {
				res = 1;
			}
		} else {
			res = 1;
		}
		ret.type = make_basic_type(TY_INT);
		ret.type = n_xmemdup(ret.type, sizeof *ret.type);
		ret.value = n_xmemdup(&res, sizeof res);
		goto out;
	} else if (tree->op != TOK_OP_COND) {
		/* XXX broken */
		/*
		 * 07/22/08: Don't set is_paren_subex flag, since for all intents and
		 * purposes
		 *
		 *     (foo op bar)
		 *
		 * ... is not constant if foo or bar is not constant. The flag is only
		 * intended to make
		 *
		 *     &(foo)
		 *
		 * work
		 */
		tmp2 = do_eval_const_expr(tree->right, extype, 0 /*is_paren_subex*/);
		nullok = 0;
	} else {
		/* Is TOK_OP_COND */
		int	not_zero = 0;

		if (tree->right->op != TOK_OP_COND2) {
			errorfl(tree->right->tok,
				"Parse error - expected second part "
				"of conditional operator");
			ret.type = NULL;
			return ret;
		}


		/*
		 * 07/21/08: Handle address constants
		 */
		if (tmp1.address != NULL) {
			if (tmp1.address->diff == 0) {
				/*
				 * OK - &static_var or &&label is definitely
				 * nonzero
				 */
				not_zero = 1;
			} else {
					/*
				 * &addr +/- offset may be zero (by invoking
				 * undefined behavior, but it's a possibility
				 * nonetheless) - wait until runtime to find
				 * out
				 */
				ret.type = NULL;
				ret.not_constant = 1;
				return ret;
			}
		} else if (const_value_is_nonzero(&tmp1)) {
			not_zero = 1;
		}

		/*
		 * 07/22/08: Don't set is_paren_subex flag, since for all intents and
		 * purposes
		 *
		 *     (foo op bar)
		 *
		 * ... is not constant if foo or bar is not constant. The flag is only
		 * intended to make
		 *
		 *     &(foo)
		 *
		 * work
		 */
		if (/*const_value_is_nonzero(&tmp1)*/ not_zero) {
			tmp2 = do_eval_const_expr(tree->right->left, extype,
					/*is_paren_subex*/0);
		} else {
			tmp2 = do_eval_const_expr(tree->right->right, extype,
					/*is_paren_subex*/0);
		}
		if (tmp2.type == NULL) {
			return ret;
		}
		if (tmp2.type->code < TY_INT) {
			/*
			 * XXX we also need some typechecking ... And
			 * usual arithmetic conversions!!! E.g.
			 * foo < bar? 0ll: 0;
			 * ... should convert 0 to 0ll
			 */
			cross_do_conv(&tmp2, TY_INT, 1);
		}
		ret.type = tmp2.type;
		ret.value = tmp2.value;
		goto out;
	}

	tv = &ret;
	left = &tmp1;
	right = &tmp2;
		
	/*
	 * 07/17/08: Do things below for all pointer types, including
	 * constant values like (char *)0x835353, i.e. don't demand
	 * the address member being non-null
	 */
#ifndef PREPROCESSOR
	if (tmp1.type != NULL && tmp2.type != NULL
		&& (tmp1.type->tlist != NULL || tmp2.type->tlist != NULL)	
	/*	&& (tmp1.address != NULL || tmp2.address != NULL)*/
		&& tree->op != TOK_OP_COMMA) { /* 06/01/08: Added this */ 

		int	err = 0;

		/*
		 * XXX we need more typechecking
		 */
		if (tree->op == TOK_OP_PLUS
			|| tree->op == TOK_OP_MINUS) {
			static struct expr	dum;
			int			op = tree->op;

			/* Must be addr + integer */
			if (tmp1.address == NULL && tmp2.address != NULL) {
				dum.const_value = &tmp1;
				addr_add_sub(&tmp2, &dum,
					tmp2.type,
					op);
				return tmp2;
			} else if (tmp2.address == NULL && tmp1.address != NULL) {
				dum.const_value = &tmp2;
				addr_add_sub(&tmp1, &dum,
					tmp1.type,
					op);
				return tmp1;
			} else {
				/*
				 * Both are pointers! Only valid for
				 * minus
				 */
				if (tree->op != TOK_OP_MINUS) {
					err = 1;
				} else {
					long	diff;

					if (calc_const_addr_diff(&tmp1, &tmp2,
						&diff) != 0) {
						if (extype == EXPR_OPTCONSTINIT
							|| extype == EXPR_OPTCONSTARRAYSIZE) {
							if (extype == EXPR_OPTCONSTINIT) {
								warningfl(tree->tok, "Variable "
								"initializers are not "
								"allowed in ISO C90");
							} else {
								warningfl(tree->tok, "Variable-"
								"size arrays are not "
								"allowed in ISO C90");
							}	
							ret.not_constant = 1;
							ret.type = NULL;
							return ret;
						} else if (extype == EXPR_OPTCONSTSUBEXPR) {
							ret.not_constant = 1;
							ret.type = NULL;
							return ret;
						}
						errorfl(tree->tok, "Invalid "
						"use of `-' operator "
						"on distinct object addresses");
						err = 1;
					} else {
						ret.value = zalloc_buf(Z_CEXPR_BUF); /*n_xmalloc(16);*/ /* XXX */
						/* XXXXX cross, this sucks! */
						*(int *)ret.value = (int)diff;
						ret.address = NULL;
						ret.str = NULL;
						ret.struct_member = NULL;
						/* XXXX use ptrdiff_t */
						ret.type = n_xmemdup(
							make_basic_type(TY_INT),
							sizeof(struct type));
						cross_do_conv(&ret,
							TY_LONG, 1);	
						return ret;
					}
				}
			}
		} else if ( (tmp1.address && tmp2.address)
			|| (tmp1.value && tmp2.value) ) {
			long	diff;

			if (calc_const_addr_diff(&tmp1, &tmp2,
				&diff) != 0) {
				errorfl(tree->tok, "Invalid "
				"use of `%s' operator "
				"on distinct object addresses",
				tree->tok->ascii);
			} else {
				int	result;

				ret.value = zalloc_buf(Z_CEXPR_BUF); /*n_xmalloc(16);*/ /* XXXXXX */

				/*
				 * Must be relational or equality operator
				 */
				switch (tree->op) {
				case TOK_OP_LEQU:	
					result = diff == 0;
					break;
				case TOK_OP_LNEQU:
					result = diff != 0;
					break;
				case TOK_OP_GREAT:
					result = diff > 0;
					break;
				case TOK_OP_SMALL:
					result = diff < 0;
					break;
				case TOK_OP_GREATEQ:
					result = diff >= 0;
					break;
				case TOK_OP_SMALLEQ:
					result = diff <= 0;
					break;
				default:	
					err = 1;
				}
				if (!err) {
					/* XXXXXXXXXXX cross */
					*(int *)ret.value = result;
					ret.type = n_xmemdup(
						make_basic_type(TY_INT),
						sizeof(struct type));
					return ret;
				}
			}	
		} else {
			/*
			 * 07/17/08: If we have two address constants, and one is
			 * bound to an object and the other is a constant value,
			 * then a comparison cannot meaningfully be made at compile
			 * time, so the expression is not constant.
			 * E.g. in
			 *
			 *    &foo == (void *)0x....)
			 *
			 * ... even if foo is static, we cannot tell whether both
			 * values are the same
			 */
			if (tmp1.type->tlist != NULL && tmp2.type->tlist != NULL
				&& compare_types(tmp1.type, tmp2.type, CMPTY_ARRAYPTR|CMPTY_ALL) == 0) {
				if (extype == EXPR_OPTCONSTINIT
					|| extype == EXPR_OPTCONSTARRAYSIZE) {
					if (extype == EXPR_OPTCONSTINIT) {
						warningfl(tree->tok, "Variable "
						"initializers are not "
						"allowed in ISO C90");
					} else {
						warningfl(tree->tok, "Variable-"
						"size arrays are not "
						"allowed in ISO C90");
					}	
				} else if (extype == EXPR_OPTCONSTSUBEXPR) {
					;
				} else {
					errorfl(tree->tok, "Expression not constant");
				}
				ret.type = NULL;
				ret.not_constant = 1;
				return ret;
			} else {
				/*
				 * Pointer type with arithmetic type, and not
				 * using plus or minus - not allowed
				 * 
				 * 02/24/09: Handle null pointer constants
			 	 */
				if (tmp1.type->tlist != NULL && tmp2.is_nullptr_const) {
					;
				} else if (tmp2.type->tlist != NULL && tmp1.is_nullptr_const) {
					;
				} else {
					err = 1;
				}
				if (!err) {
					/* XXX We should evaluate here */
					ret.type = NULL;
					ret.not_constant = 1;
					return ret;
				}
			}
		}

		if (err) {
			/*
			 * 05/31/09: This fails for e.g.
			 *
			 *     (int *)0 + 123
			 *
			 * Instead of evaluating it as a constant, we use the
			 * workaround of treating it as nonconstant for now.
			 * This fixes at least those cases where it is not
			 * required to be treated as a constant expression
			 * (e.g. in Perl source)
			 * XXX Fix this the right way
			 */
			if (extype == EXPR_OPTCONSTINIT
				|| extype == EXPR_OPTCONSTARRAYSIZE) {
				if (extype == EXPR_OPTCONSTINIT) {
					warningfl(tree->tok, "Variable "
					"initializers are not "
					"allowed in ISO C90");
				} else {
					warningfl(tree->tok, "Variable-"
				"size arrays are not "
					"allowed in ISO C90");
				}	
			} else if (extype == EXPR_OPTCONSTSUBEXPR) {
				;
			} else {
				errorfl(tree->tok, "Expression not constant");
			}
			ret.type = NULL;
			ret.not_constant = 1;
			return ret;
#if 0
			errorfl(tree->tok, "Incompatible types in expression");
			ret.type = NULL;
			return ret;
#endif
		}
	}
#endif /* #ifndef PREPROCESSOR */

	if (tmp1.type == NULL
		|| (!nullok && tmp2.type == NULL)
		/*|| (!nullok && (rc = cross_convert_tyval(&tmp1, &tmp2, NULL)) != 0)*/) {
		ret.type = NULL;
		ret.value = NULL;
		ret.not_constant = tmp1.not_constant || tmp2.not_constant;
		if (rc != 0) {
			errorfl(tree->tok, "Incompatible types in expression");
			ret.type = NULL;
			return ret;
		}
		return ret;
	}

	/*
	 * 08/05/08: Only perform usual arithmetic conversions if the used
	 * operator does in fact require this! (It used to be done
	 * unconditionally, eg. in foo << bar, where the type of bar should
	 * not affect foo)
	 */
	if (!nullok) {
		if (tree->op == TOK_OP_COMMA
			|| tree->op == TOK_OP_BSHL
			|| tree->op == TOK_OP_BSHR
			|| tree->op == TOK_OP_LAND
			|| tree->op == TOK_OP_LOR) {
			/*
			 * Does not need usual arithmetic conversion
			 */
			if (tree->op == TOK_OP_BSHL
				|| tree->op == TOK_OP_BSHR) {
				/* Promote for shift */
				if (tmp1.type->code < TY_INT) {
					cross_do_conv(&tmp1, TY_INT, 1);
				}
			}
		} else {
			/* Needs usual arithmetic conversion */
			if (cross_convert_tyval(&tmp1, &tmp2, NULL) != 0) {
				errorfl(tree->tok, "Incompatible types in expression");
				ret.type = NULL;
				return ret;
			}
		}
	}
	
	if ((left->address == NULL && left->value == NULL)
		|| (right->address == NULL && right->value == NULL)) {
		if (tree->op != TOK_OP_COND) {
			errorfl(tree->right->tok, "Incompatible types for "
				"operator");	
			ret.type = NULL;
			return ret;
		}	
	}	

	/*
	 * 05/25/09: Somehow, static addresses are still slipping through to
	 * this point.
	 *
	 *     if (((tab)[0] & 1)) {
	 *
	 * ... here we are getting to TOK_OP_BAND even though a subscripted
	 * static array isn't constant! The code works correctly if either
	 * set of parens is removed, i.e.:
	 *
	 *     if ((tab[0] & 1)) {
	 *
	 * ... or
	 *
	 *     if ((tab)[0] & 1) {
	 *
	 * This is too obscure for me for now, so I'm putting in a check here.
	 * XXX Do it right above
	 */
	if ( (left->address != NULL && left->value == NULL)
		|| (right->address != NULL && right->value == NULL) ) {
		if (extype == EXPR_OPTCONSTINIT
			|| extype == EXPR_OPTCONSTARRAYSIZE) {
			if (extype == EXPR_OPTCONSTINIT) {
				warningfl(tree->tok, "Variable "
				"initializers are not "
				"allowed in ISO C90");
			} else {
				warningfl(tree->tok, "Variable-"
				"size arrays are not "
				"allowed in ISO C90");
			}	
		} else if (extype == EXPR_OPTCONSTSUBEXPR) {
			;
		} else {
			errorfl(tree->tok, "Expression not constant");
		}
		ret.type = NULL;
		return ret;
	}

	switch (tree->op) {
	case TOK_OP_PLUS:
		if (left->address || right->address) {
			unimpl();
		} else {	
			cross_exec_op(tree->tok, tv, left, right); 
		}	
		break;
	case TOK_OP_MINUS:
		if (left->address || right->address) {
			unimpl();
		} else {	
			cross_exec_op(tree->tok, tv, left, right); 
		}	
		break;
	case TOK_OP_MULTI:
		cross_exec_op(tree->tok, tv, left, right); 
		break;
	case TOK_OP_DIVIDE:
		cross_exec_op(tree->tok, tv, left, right); 
		break;
	case TOK_OP_SMALL:
		cross_exec_op(tree->tok, tv, left, right); 
		break;
	case TOK_OP_GREAT:
		cross_exec_op(tree->tok, tv, left, right); 
		break;
	case TOK_OP_GREATEQ:
		cross_exec_op(tree->tok, tv, left, right); 
		break;
	case TOK_OP_SMALLEQ:
		cross_exec_op(tree->tok, tv, left, right); 
		break;
	case TOK_OP_LEQU:
		cross_exec_op(tree->tok, tv, left, right); 
		break;
	case TOK_OP_LNEQU:
		cross_exec_op(tree->tok, tv, left, right); 
		break;
	case TOK_OP_MOD:
		cross_exec_op(tree->tok, tv, left, right); 
		break;
	case TOK_OP_BAND:
		cross_exec_op(tree->tok, tv, left, right); 
		break;
	case TOK_OP_BOR:
		cross_exec_op(tree->tok, tv, left, right); 
		break;
	case TOK_OP_BXOR:
		cross_exec_op(tree->tok, tv, left, right); 
		break;
	case TOK_OP_BSHL:
		cross_exec_op(tree->tok, tv, left, right); 
		break;
	case TOK_OP_BSHR:
		cross_exec_op(tree->tok, tv, left, right); 
		break;
	case TOK_OP_COMMA:
		/*
		 * 03/31/08: This was missing... In ordinary constant
		 * expressions, the comma operator isn't allowed
		 * anyway, but we now use this function for evaluating
		 * constant sub-expressions in non-constant contexts
		 * too, so we must handle it
		 */
		ret = *right;
		break;
	case TOK_OP_ASSIGN:
	case TOK_OP_COBSHL:
	case TOK_OP_COBSHR:
	case TOK_OP_COPLUS:
	case TOK_OP_COMINUS:
	case TOK_OP_CODIVIDE:
	case TOK_OP_COMULTI:
	case TOK_OP_COBAND:
	case TOK_OP_COBOR:
	case TOK_OP_COBXOR:
	case TOK_OP_COMOD:
		errorfl(tree->tok, "Assignment operator applied to "
			"something that is not an lvalue");
		ret.type = NULL;
		return ret;
	default:
		printf("Badness to the maximum %d\n", tree->op);
		abort();
	}

	if (tree->op != TOK_OP_COND && tree->op != TOK_OP_COMMA) {
		free(left->value);
		free(right->value);
	}	

out:
	
	return ret;
}

int
eval_const_expr(struct expr *ex, int extype, int *not_constant) {
	struct tyval	tv;
	struct tyval	*dtv;

	if (ex->const_value != NULL) {
		/* already been evaluated */
		return 0;
	}	
	tv = do_eval_const_expr(ex, extype, 0);
	if (tv.type == NULL) {
		if (not_constant != NULL) {
			*not_constant = tv.not_constant;
		}	
		return -1;
	} else {
		rv_setrc_print(tv.value, tv.type->code, 0);
		dtv = n_xmalloc(sizeof tv);
		memcpy(dtv, &tv, sizeof tv);
		ex->const_value = dtv;
	}

	return 0;
}

