/*
 * Copyright (c) 2004 - 2009, Nils R. Weller
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

#ifndef PREPROCESSOR
#    include "decl.h"
#    include "scope.h"
#    include "icode.h"
#    include "debug.h"
#    include "cc1_main.h"
#    include "functions.h"
#    include "analyze.h"
#    include "zalloc.h"
#    include "symlist.h"
#    include "backend.h"
#    include "reg.h"
#else
#    include "archdefs.h"
#    include "../features.h"  /* XXX */
#endif

#include "type.h"
#include "subexpr.h"
#include "typemap.h"
#include "evalexpr.h"
#include "libnwcc.h"
#include "n_libc.h"

void
append_expr(struct expr **head, struct expr **tail, struct expr *e) {
	if (*head == NULL) {
		*head = *tail = e;
	} else {
		(*tail)->next = e;
		e->prev = *tail;
		*tail = (*tail)->next;
	}
}

struct expr *
alloc_expr(void) {
#if USE_ZONE_ALLOCATOR
	return zalloc_buf(Z_EXPR);
#else
	struct expr	*ret = n_xmalloc(sizeof *ret);
	static struct expr	nullexpr;
	*ret = nullexpr;
	return ret;
#endif
}

struct expr *
dup_expr(struct expr *ex) {
	struct expr	*ret = n_xmalloc(sizeof *ret);
	*ret = *ex;
	return ret;
}


void	recover(struct token **tok, int delim, int delim2);

static int
ambig_to_binary(struct token *t) {
	int	op = *(int *)t->data;
	switch (op) {
	case TOK_OP_AMB_PLUS:
		op = TOK_OP_PLUS;
		break;
	case TOK_OP_AMB_MINUS:
		op = TOK_OP_MINUS;
		break;
	case TOK_OP_AMB_MULTI:
		op = TOK_OP_MULTI;
		break;
	case TOK_OP_AMB_BAND:
		op = TOK_OP_BAND;
		break;
	case TOK_OP_AMB_COND2:
		op = TOK_OP_COND2;
		break;
	}

	if (!IS_BINARY(op) && op != TOK_OP_COND2) {
		errorfl(t, "Invalid operator ``%s'' - "
			"binary or ternary operator expected", t->ascii);
		return -1;
	}
	*(int *)t->data = op; /* XXXXXXXXXXXXXXXXXXXXXXXXXXXX ok? */
	return op;
}


int
expr_ends(struct token *t, int delim, int delim2) {
	int	type;

	if (t->type == TOK_OPERATOR) {
		type = *(int *)t->data;
	} else {
		type = t->type;
	}
	if (type == delim || type == delim2) {
		return 1;
	}
	return 0; 
}

/*
 * Get lowest precedence operator in an expression. Note that the
 * conditional operator requires special handling; In
 * foo? bar: baz;
 * ... ? and : act like parentheses, i.e. lower precedence operators
 * such as the comma operator may occur between them.
 */
static struct operator *
get_lowest_prec(struct expr *ex, struct expr **endp, int want_condop) {
	int		lowest = 100;
	int		cond_expr = 0;
	struct expr	*lptr = NULL;

	*endp = NULL;
	for (; ex != NULL; ex = ex->next) {
		struct operator	*tmp;

		if (ex->op == 0 || ex->used) {
			continue;
		}
		tmp = &operators[LOOKUP_OP2(ex->op)];
		/*
		 * 08/22/07: This flag had to be added because otherwise
		 * the search loop for the second part of the conditional
		 * operator didn't work right in
		 *
		 *   1? 0:  1? 1,1   : 2;
		 */
		if (want_condop
			&& ex->op != TOK_OP_COND
			&& ex->op != TOK_OP_COND2) {
			continue;
		}	
		if (!cond_expr) {
			if (tmp->prec < lowest) {
				lowest = tmp->prec;
				lptr = ex;
			} else if (tmp->prec == lowest) {
				if (tmp->assoc != OP_ASSOC_RIGHT) {
					lptr = ex;
				}
			}
		}	

		if (ex->op == TOK_OP_COND) {
			++cond_expr;
		} else if (ex->op == TOK_OP_COND2) {
			if (cond_expr == 0) {
				if (want_condop) {
					/*
					 * Added... Otherwise stuff breaks
					 * because ? is selected rather than
					 * :
					 */
					lptr = ex;
					break;
				}
			}
			--cond_expr;
		}
	}
	*endp = lptr;
	return lptr? (void *)&operators[LOOKUP_OP2(lptr->op)]: (void *)NULL;
}


#if EVAL_CONST_EXPR_CT 

struct token *
replace_const_subexpr(struct expr *endp, struct token *restok0) {
	struct token	*restok;
	struct num	*n;
	struct type	*need_cast = NULL;

	if (endp->const_value->str != NULL
		|| endp->const_value->address != NULL) {
		return NULL;
	} else if (endp->const_value->type->tlist != NULL) {
		/*
		 * This means we have a constant value which is used as an
		 * address. This may be something like
		 *
		 *    &((struct foo *)0)->bar
		 *
		 * ... i.e. the canonical offsetof implementation. In this
		 * case the sub-expression is an integer, but we cannot
		 * just create a token of integer type since then the type
		 * would not be correct.
		 *
		 * So we use the token with the value and append a cast to
		 * the pointer type to the sub-expression
		 */
		need_cast = endp->const_value->type;
	} else if (!is_integral_type(endp->const_value->type)
		&& !is_floating_type(endp->const_value->type)) {
		/*
		 * 07/11/08: Need this to prevent compound literals (struct
		 * types) from being handled as constants
		 * XXX ... which I guess should be possible though!
		 */
		return NULL;
	}

	if (restok0 == NULL) {
		restok = alloc_token();
		restok->data = endp->const_value->value;
		restok->type = endp->const_value->type->code;
	} else {
		restok = restok0;
	}

	endp->op = 0;
	endp->left = NULL;
	endp->right = NULL;
	endp->data = alloc_s_expr();
	endp->data->meat = restok;


	if (need_cast != NULL) {
#ifndef PREPROCESSOR
		endp->data->type = backend->get_size_t();
#else
		endp->data->type = make_basic_type(TY_ULONG); /* XXX */
#endif
		endp->data->meat->type = endp->data->type->code;
	} else {	
		endp->data->type = endp->const_value->type;
	}


	/*
	 * If this is a floating point or long long
	 * constant, we may have to create a new
	 * symbolic label for it, so it must be
	 * saved in the corresponding constant list
	 */
	if (IS_FLOATING(restok->type)) {
		n = n_xmalloc(sizeof *n);
		n->value = restok->data;
		n->type = restok->type;
		restok->data = put_float_const_list(n);
	} else if ((IS_LLONG(restok->type)
#ifndef PREPROCESSOR
		&& backend->arch == ARCH_POWER)
#else
		&& target_info->arch_info->arch == ARCH_POWER)
#endif
		/*
		 * 10/17/08: Check for long on PPC64 was missing
		 */
#ifndef PREPROCESSOR
		|| (backend->abi == ABI_POWER64
#else
		|| (target_info->arch_info->abi == ABI_POWER64
#endif
			&& IS_LONG(restok->type))) {
		n = n_xmalloc(sizeof *n);
		n->value = restok->data;
		n->type = restok->type;
		/*restok->data = n;*/
		put_ppc_llong(n);
		restok->data2 = llong_const; /* 10/17/08: Missing! */
	}

	if (need_cast != NULL) {
		endp->data->operators[0] = make_cast_token(need_cast);
		endp->data->operators[1] = NULL;
	}
	return restok;
}




static void
eval_const_subexpr(struct expr *endp) {
	struct token	*ltok = endp->left->data->meat;
	int		rc;
	int		not_constant;

	if (ltok != NULL
		&& IS_CONSTANT(ltok->type)
		&& ltok->type != TOK_STRING_LITERAL) {
		struct vreg	*first;
		struct vreg	*second;
		struct type	*restype;
		struct tyval	tv1;
		struct tyval	tv2;
		struct expr	*winner;
		struct vreg	*winner_vr;

		rc = eval_const_expr(endp->left,
			EXPR_OPTCONSTSUBEXPR,
			&not_constant);
		if (rc == -1) {
			return;
		}
		/*
		 * 07/01/08: Condition is constant - we
		 * can already pick an operand
		 */

		first = expr_to_icode(endp->right->left,
			NULL, NULL, 0, 0, 0);
		second = expr_to_icode(endp->right->right,
			NULL, NULL, 0, 0, 0);

		if (first == NULL || second == NULL) {
			return;
		}

		/*
		 * Now that we have the types
		 * of both sides, it is possible
		 * to perform usual arithmetic
		 * conversions
		 */

		tv1.value = tv2.value = NULL;
		tv1.type = first->type;
		tv2.type = second->type;

		/*
		 * 07/15/08: Warn about ptr vs
		 * non-null integer constant
		 */
		if (tv1.type->tlist || tv2.type->tlist) {
			if (tv1.type->tlist == NULL) {
				if (const_value_is_nonzero(
					endp->right->left->const_value)) {
					warningfl(endp->tok,
					"Result of conditional operator has variable type");
				}
			} else if (tv2.type->tlist == NULL) {	
				if (const_value_is_nonzero(
					endp->right->right->const_value)) {
					warningfl(endp->tok,
					"Result of conditional operator has variable type");
				}
			} else if (tv2.type->code != tv1.type->code) {
				if (tv1.type->code == TY_VOID
					&& tv1.type->tlist->next == NULL) {
				} else if (tv2.type->code == TY_VOID
					&& tv2.type->tlist->next == NULL) {
				} else {
					warningfl(endp->tok,
					"Result of conditional operator has variable type");
				}
			}
		} else if ( (tv1.type->code == TY_STRUCT || tv1.type->code == TY_UNION)
			|| (tv2.type->code == TY_STRUCT || tv2.type->code == TY_UNION) ) {
			if (tv1.type->code != tv2.type->code
				|| tv1.type->tstruc != tv2.type->tstruc) {
				errorfl(endp->tok,
				"Result of conditional operator has variable type");
			}
		}

		cross_convert_tyval(&tv1, &tv2,
			&restype);
		if (const_value_is_nonzero(
			endp->left->const_value)) {
			/* Use first operand */
			winner = endp->right->left;
			winner_vr = first;
		} else {
			/* Use second operand */
			winner = endp->right->right;
			winner_vr = second;
		}
		endp->op = 0;
		endp->left = NULL;
		endp->right = NULL;

		/*
		 * We store the winning
		 * operand as a parenthesized
		 * sub-expression
		 */
		endp->data = alloc_s_expr();
		endp->data->is_expr = winner;
		endp->data->flags |= SEXPR_FROM_CONST_EXPR;
		if (restype != NULL
			&& (winner_vr->type->code != restype->code
			|| (winner_vr->type->tlist 
			!= restype->tlist))) { 
			/* 
			 * XXX that tlist comp.
			 * really what we want?
			 * may yield
			 * unnecessary conv 
			 */
			/*
			 * Prepend a cast op to
			 * the sub-expression,
			 * which is the easiest
			 * way to honor the
			 * usual arithmetic
			 * conversion!
			 */ 
			endp->data->operators[0] =
				make_cast_token(restype);
			endp->data->operators[1] = NULL;
		}
	}
}





static void
eval_const_part_expr(struct expr *endp) { 
	struct token	*ltok = NULL;
	struct token	*rtok = NULL;
	int		rc;
	int		not_constant;

	if (endp->right->op != TOK_OP_COND2) {
		ltok = endp->left->data->meat;
		rtok = endp->right->data->meat;
	}

	if (ltok && rtok
		&& IS_CONSTANT(ltok->type)
		&& IS_CONSTANT(rtok->type)) {
		/*
		 * A constant sub-expression - evaluate it at
		 * this point already! Note that endp already
		 * has the sub-tree for the two items, so we
		 * can just pass it to the evaluation function
		 */
		rc = eval_const_expr(endp,
			EXPR_OPTCONSTSUBEXPR,
			&not_constant);

		/*
		 * There is a small possibility that the result
		 * is not constant, e.g. if it did something
		 * like  *(int *)1234, so check for that
		 */
		if (rc != -1) {  /* !not_constant */
			/*
			 * Evaluation succeeded! Replace
			 *
			 *     operand op operand
			 *
			 * with a single token containing the
			 * evaluated result
			 */
			struct token	*restok; /* = alloc_token();*/

			restok = replace_const_subexpr(endp, NULL);
		}
	} else if (endp->op == TOK_OP_LAND
		|| endp->op == TOK_OP_LOR) {
		/*
		 * 07/01/08: We have to optimize variable access in 
		 * constant expressions away if it can already be
		 * done at compile time! Examples
		 *
		 *     1? 123: var   123 wins anyway
		 *     0 && var      First operand already false anyway
		 *     1 || var      First operand already true anyway
		 *
		 * In particular, glibc depends on the conditional
		 * operator version being optimized! It uses this to
		 * flag compile-time errors at link time, i.e. if the
		 * compiler does something wrong (wrong macro settings
		 * or somesuch), the condition becomes false and the
		 * undefined, externally declared variable is accessed.
		 * This yields a linker error indicating the problem.
		 * If the condition is true instead, the variable
		 * access is optimized away and there is no linker
		 * error
		 */
		int	new_value = -1;
		int	not_zero;

		if (ltok && IS_CONSTANT(ltok->type)) {
			/*
			 * Left side is (probably) constant, but right
			 * isn't
			 */

			rc = eval_const_expr(endp->left,
				EXPR_OPTCONSTSUBEXPR,
				&not_constant);


			/*
			 * There is a small possibility that the result
			 * is not constant, e.g. if it did something
			 * like  *(int *)1234, so check for that
			 */
			if (rc != -1) {  /* !not_constant */
				/*
				 * Evaluation succeeded!
				 */
				not_zero = const_value_is_nonzero(
					endp->left->const_value);
				if (endp->op == TOK_OP_LOR) {
					if (not_zero) {
						/*
						 * First operand already
						 * nonzero, so we can
						 * ignore the second one
						 */
						new_value = 1;
						endp->const_value = 
							endp->left->const_value;
					}
				} else {
					if (!not_zero) {
						/*
						 * First operand already
						 * zero, so we can
						 * ignore the second one
						 */
						new_value = 0;
						endp->const_value = 
							endp->left->const_value;
					}
				}
			}
		} else if (rtok && IS_CONSTANT(rtok->type)) {
			/*
			 * Right side is constant, but left isn't
			 */
		}

		if (new_value != -1) {
			struct token	*tok;

			tok = const_from_value(&new_value,
				make_basic_type(TY_INT));
			replace_const_subexpr(endp, NULL);
			/* XXX delete old nodes */
		}
	}
}

#endif /* #if EVAL_CONST_EXPR_CT (disabled for preprocessor) */


static struct expr *
bind_operators(struct expr *ex) {
	struct operator	*op;
	struct expr	*endp;
	struct expr	*start = ex;
	int		rc;
	int		not_constant;


	if (ex == NULL) {
		return NULL;
	}
	if ((op = get_lowest_prec(ex, &endp, 0)) == NULL) {
		/* No more operators left */
		if (!ex->used) {
			endp = ex;
			endp->used = 1;
#if 0
			/*
			 * This must be a sub-expression, check whether it is (probably)
			 * constant
			 */
			if (endp->data == NULL) {
				warningfl(NULL, "BUG? Expression tree leaf has no data");
			} else {	
				endp->is_const = check_sexpr_is_const(endp->data);
			}
#endif
			/*
			 * 07/04/08: This was missing; Properly evaluate constant
			 * sub-expressions. Otherwise things like parenthesized
			 * sub-expressions are not combined properly, since the
			 * parentheses are confusing
			 *
			 * 11/08/08: Missed check for compound literals
			 */
			if ((endp->data->meat && endp->data->meat->type == TOK_STRING_LITERAL)
				|| (endp->data->meat && endp->data->meat->type == TOK_COMP_LITERAL)
				|| (endp->data->is_expr
				&& endp->data->is_expr->data
				&& endp->data->is_expr->data->meat
				&& endp->data->is_expr->data->meat->type == TOK_STRING_LITERAL)) {
				rc = -1;
			} else if (endp->op == 0 && endp->data != NULL) {	
				/* 
				 * 07/07/08: Have to check for op->data, since this may e.g. be
				 * stmt_as_expr instead of a normal sub-expression!
				 */
				rc = eval_const_expr(endp,
					EXPR_OPTCONSTSUBEXPR,
					&not_constant);
			} else {
				rc = -1;
			}

			/*
			 * There is a small possibility that the result
			 * is not constant, e.g. if it did something
			 * like  *(int *)1234, so check for that
			 */
			if (rc != -1) {  /* !not_constant */
#ifndef PREPROCESSOR /* XXX Should this be EVAL_CONST_EXPR_CT instead? */
				/*
				 * Evaluation succeeded! Replace
				 *
				 *     operand op operand
				 *
				 * with a single token containing the
				 * evaluated result
				 */
				struct token	*restok; /* = alloc_token();*/
				restok = replace_const_subexpr(endp, NULL);
#endif
			}
		}
		else endp = NULL;

		return endp;
	} else if (op->value == TOK_OP_COND) {
		struct expr	*endp2;
		struct expr	*ex2 = endp->next;

		/*
		 * Need to get second part of conditional operator. This
		 * requires skipping intermediate conditional operators.
		 *
		 * 08/22/07: This loop is nonsense since we added the
		 * want_condop flag to get_lowest_prec()
		 */
/*		for (;;) {*/
			if (ex2 == NULL 
				|| (op = get_lowest_prec(ex2, &endp2, 1))
				== NULL) {
				errorfl(ex2? ex2->tok: endp->tok,
					"Parse error - missing second part"
					" of conditional operator");
				return NULL;
			}
#if 0
			} else if (op->value == TOK_OP_COND) {
				++cond1;
			} else if (op->value == TOK_OP_COND2) {
				if (cond1 == 0) {
					/* done! */
					break;
				} else {
					--cond1;
				}	
			}	
#endif
	/*		ex2 = endp2->next;*/
/*		} */

		/*
		 * At this point, endp points to ``?'' and endp2 to ``:''. We
		 * bind them such that:
		 * Given x = ``?''
		 * ... x->left = cond-expr
		 * ... x->right = :
		 * ... x->right->left = expr-nonzero
		 * ... x->right->right = expr-zero
		 */
		endp->used = endp2->used = 1;
		ex = endp->next;
		endp->next = NULL;
		endp->left = bind_operators(start);
		endp->right = endp2; 
		ex2 = endp2->next;
		endp2->next = NULL;
		endp->right->left = bind_operators(ex);
		endp->right->right = bind_operators(ex2);

#if EVAL_CONST_EXPR_CT
		if (endp->left->op == 0 && endp->left->data != NULL) {
			eval_const_subexpr(endp);
		}
#endif
		return endp;
	}

	endp->used = 1;

	ex = endp->next;
	endp->next = NULL;

	endp->left = bind_operators(start);
	endp->right = bind_operators(ex);

#if EVAL_CONST_EXPR_CT
	if (endp->left->op == 0 && endp->right->op == 0
		&& endp->left->data != NULL && endp->right->data != NULL) {
		eval_const_part_expr(endp);
	}
#endif

	return endp;
}


#ifndef PREPROCESSOR

static void
append_init_list(
	struct initializer **init, 
	struct initializer **init_tail,
	struct initializer *i) {

	if (i->type == 0) abort();
	if (*init == NULL) {
		*init = *init_tail = i;
	} else {
		(*init_tail)->next = i;
		i->prev = *init_tail;
		*init_tail = (*init_tail)->next;
	}	
}	

struct initializer *
alloc_initializer(void) {
#if USE_ZONE_ALLOCATOR
	return zalloc_buf(Z_INITIALIZER);
#else
	static struct initializer	nullinit;
	struct initializer	*ret;
	ret = n_xmalloc(sizeof *ret);
	*ret = nullinit;
	return ret;
#endif
}	

struct initializer *
dup_initializer(struct initializer *init) {
	struct initializer	*ret = alloc_initializer();
	*ret = *init;
	return ret;
}	

static void
conv_init(struct tyval *tv, struct type *toty, struct token *t) {
	struct type		*fromty = tv->type;
	struct type_node	*ltn;
	struct type_node	*rtn;

	if (toty->tlist == NULL) {
		/* Simple basic type conversion */
		if (tv->address != NULL) {
			/* Must be pointer to int cast... Dangerous!!! */
			return;
		}	
		if (toty->code != fromty->code) {
			cross_do_conv(tv, toty->code, 1);
		}
		return;
	}

	if (fromty->tlist == NULL || 
		(toty->code != fromty->code
		&& (toty->code != TY_VOID && fromty->code != TY_VOID))) { 
	/* this bullshit doesn't work */		
		return;

#if 0
#if 0
/* XXX */	 && (!IS_CHAR(toty->code) || !IS_CHAR(fromty->code)))) {
#endif
		if (!tv->is_nullptr_const) {
			errorfl(t, "Initializer of incompatible type");
			return;
		} else {
			/* Explains various mysteries - Don't check tlists */
			return;
		}	
#endif
	}


	/* 
	 * Must be assignment to pointer since this function is only
	 * called for scalar types
	 */

	if (tv->is_nullptr_const) {
		return;
	}
	if (toty->code == TY_VOID
		&& toty->tlist->type == TN_POINTER_TO
		&& toty->tlist->next == NULL) {
		/* Assignment to void pointer */
		return;
	}	
		

	ltn = toty->tlist;
	rtn = fromty->tlist;

	if (rtn && rtn->type == TN_FUNCTION) {
		if (ltn->type == TN_POINTER_TO) {
			/* Probably pointer to function */
			ltn = ltn->next;
		}
	}	

	for (; ltn != NULL && rtn != NULL;
		ltn = ltn->next, rtn = rtn->next) {
		if (ltn->type != rtn->type) {
			if (rtn->type == TN_ARRAY_OF
				&& rtn == fromty->tlist) {
				/* OK - assign array address to ptr */
				;
			} else {
				errorfl(t, "Initializer of incompatible type");
				return;
			}	
		}	
	}

	if (ltn != rtn) {
		/* One type list is longer */
		errorfl(t, "Initializer of incompatible type");
		return;
	}
}

struct desig_init_data {
	struct initializer	**init_head;
	struct initializer	**init_tail;
	struct initializer	*cur_init_ptr;
	struct sym_entry	*highest_encountered_member;
	int			highest_encountered_index;
	/* 07/18/08: Record type (to distinguish union/struct) */
	struct type		*type;
	struct sym_entry	*last_encountered_member; /* For union null padding */
};

static void
replace_cur_init(struct desig_init_data *data, struct initializer *init) {	
	if (data->cur_init_ptr->prev) {
		data->cur_init_ptr->prev->next = init;
	} else {
		*data->init_head = init;
	}
	if (data->cur_init_ptr->next) {
		data->cur_init_ptr->next->prev = init;
	} else {
		*data->init_tail = init;
	}
	init->next = data->cur_init_ptr->next;
	init->prev = data->cur_init_ptr->prev;
	free/*initializer*/(data->cur_init_ptr);
	data->cur_init_ptr = init->next;
}

/*
 * Put an initializer into the designated initializer list. As soon as the
 * first designated initializer is encountered, this function also has to
 * be called for all subsequent initializers, since it may have trashed the
 * initializer order.
 *
 * There are four cases to handle here:
 *
 *    - The initializer is itself designated, and it jumps forward. If that
 * happens we create a null initializer for every skipped member (this element
 * may have to be replaced later if another designated initializer jumps back
 * in the order)
 *
 *    - The initializer is itself designated, and it jumps backward. In that
 * case we have to jump back and OVERWRITE an existing explicit data or null
 * initializer. Also, the current ``initializer pointer'' must be moved to
 * that location
 *
 *    - The initializer is not designated, but overwrites an existing
 * initializer because the current initializer pointer points to one
 *
 *    - The initializer is not designated and just appended at the end of
 * the list
 */
static void 
put_desig_init_struct(struct desig_init_data *data,
	struct sym_entry *desig_se,
	struct sym_entry *prev_se,
	struct sym_entry *whole_struct,
	struct initializer *init,
	struct token *tok,
	int *items_read) {

	int	skipped = 0;

	if (desig_se != NULL
		&& prev_se != NULL
		&& desig_se->dec->offset == prev_se->dec->offset) {
		/*
		 * Designating the initializer which would we done now
		 * anyway - ignore designation.
		 *
		 * 07/18/08: Note that this also applies to unions, where
		 * every member has offset 0!
		 */
		desig_se = NULL;
	}


	if (desig_se == NULL) {
		if (data->type->code == TY_UNION
			&& data->init_head != NULL) {
			if (*data->init_head != NULL) {	
				free(*data->init_head);
				*data->init_head = NULL;
			}
			append_init_list(data->init_head,
				data->init_tail, init);
		} else {
			/* Not designated, thank heaven */
			if (data->cur_init_ptr == NULL) {
				/* At end too! */
				append_init_list(data->init_head,
					data->init_tail, init);
			} else {
				/*
				 * We have to trash an existing initializer
				 */
				if (data->cur_init_ptr->type != INIT_NULL) {
					warningfl(tok, "Member `%s' already has an "
						"initializer",
						prev_se->dec->dtype->name);	
				}
				replace_cur_init(data, init);
			}
		}
	} else {
		/*
		 * Designated! But is it forward or backward?
		 */
		if (prev_se == NULL) {
			/*
			 * Brute-force search for the target
			 */
			struct initializer	*tmpinit;

			tmpinit = *data->init_head;
			for (prev_se = whole_struct;
				prev_se != NULL;
				prev_se = prev_se->next) {
				if (prev_se->dec->offset == desig_se->dec->offset) {
					break;
				}
				if (tmpinit->next == NULL) {
					/*
					 * Fill holes with null initializers
					 */
					struct initializer	*nullb;

					nullb = backend->make_null_block(NULL,
						prev_se->next->dec->dtype,
						NULL, 1);
					nullb->prev = tmpinit;
					tmpinit->next = nullb;
				}
				tmpinit = tmpinit->next;
				++skipped;
			}
			if (tmpinit->prev) {
				tmpinit->prev->next = init;
			} else {
				*data->init_head = init;
			}
			if (tmpinit->next) {
				tmpinit->next->prev = init;
			} else {
				*data->init_tail = init;
			}
			init->prev = tmpinit->prev;
			init->next = tmpinit->next;
			data->cur_init_ptr = tmpinit->next;
			free/*_initializer XXX */(tmpinit);
			*items_read = skipped;
			if (skipped == 0) {
				*data->init_head = init;
			} else if (skipped > *items_read) {
				*data->init_tail = init;
			}
		} else if (desig_se->dec->offset > prev_se->dec->offset) {
			int	highest_offset =
				data->highest_encountered_member?
				(int)data->highest_encountered_member->dec->offset:
				-1;
				
			/*
			 * Forward - create a null initializer for every
			 * skipped member if necessary
			 */
			while (prev_se->dec->offset < desig_se->dec->offset) {
				if ((signed long)prev_se->dec->offset
					> highest_offset) {
					struct initializer	*nullb;

					nullb = backend->make_null_block(NULL,
						prev_se->dec->dtype, NULL, 1);	
					append_init_list(data->init_head,
						data->init_tail, nullb);
				}
				if (data->cur_init_ptr) {
					data->cur_init_ptr =
						data->cur_init_ptr->next;
				}

				++skipped;
				prev_se = prev_se->next;
			}
			if (data->cur_init_ptr) {
				if (data->cur_init_ptr->type != INIT_NULL) {
					warningfl(tok, "Member `%s' already "
					"has an initializer",
					desig_se->dec->dtype->name);	
				}
			
			
				replace_cur_init(data, init);
			} else {	
				append_init_list(data->init_head,
					data->init_tail,
					init);
			}
			*items_read += skipped;
		} else {
			/*
			 * Backward. First search the target initializer.
			 * We can exploit the fact that every member behind
			 * us has one initializer already (null if skipped.)
			 */
			struct initializer	*tmp;

			tmp = *data->init_tail;
			prev_se = prev_se->prev;
			while (prev_se->dec->offset > desig_se->dec->offset) {
				prev_se = prev_se->prev;
				tmp = tmp->prev;
				++skipped;
			}
			*items_read -= skipped;

			if (tmp->prev) {
				tmp->prev->next = init;
			} else {
				*data->init_head = init;
			}	

			if (tmp->next) {
				tmp->next->prev = init;
			} else {
				*data->init_tail = init;
			}
			init->next = tmp->next;
			init->prev = tmp->prev;
			if (desig_se == whole_struct) {
				/* Start */
				*data->init_head = init;
			}

			free/*_initializer XXX */(tmp);
			data->cur_init_ptr = init->next;
		}
	}

	if (desig_se != NULL) {
		if (data->highest_encountered_member == NULL
			|| data->highest_encountered_member->dec->offset
				< desig_se->dec->offset) {
			data->highest_encountered_member = desig_se;
		}
		data->last_encountered_member = desig_se;
	} else {
		if (data->cur_init_ptr == NULL) {
			/* Appending at end */
			data->highest_encountered_member = prev_se;
			data->last_encountered_member = prev_se;
		}
	}	
}



static void
put_desig_init_array(struct desig_init_data *data,
	int desig_elem,
	int *items_read,
	struct initializer *init,
	struct type *elem_type,
	struct token *tok) {

	int			skipped = 0;
	int			real_items_read = 0;
	struct initializer	*temp;

	for (temp = *data->init_head; temp != NULL; temp = temp->next) {
		++real_items_read;
	}


	if (desig_elem == *items_read) {
		/*
		 * Designating the initializer which would we done now
		 * anyway - ignore designation.
		 */
		desig_elem = -1;
	}

	if (desig_elem == -1) {
		/* Not designated, thank heaven */
		if (data->cur_init_ptr == NULL) {
			/* At end too! */
			append_init_list(data->init_head,
				data->init_tail, init);	
		} else {
			/*
			 * We have to trash an existing initializer
			 */
			if (data->cur_init_ptr->type != INIT_NULL) {
				warningfl(tok, "Element %d already has an "
					"initializer",
					*items_read);	
			}

			replace_cur_init(data, init);
		}
	} else {
		/*
		 * Designated! But is it forward or backward?
		 */
		if (desig_elem > *items_read) {
			/*
			 * Forward - create a null initializer for every
			 * skipped member
			 */
			while (*items_read + skipped < desig_elem) {
				if (*items_read + skipped >
					data->highest_encountered_index) {	
					struct initializer	*nullb;

					nullb = backend->make_null_block(NULL,
						elem_type, NULL, 1);	
					append_init_list(data->init_head,
						data->init_tail, nullb);
				}
				if (data->cur_init_ptr) {
					data->cur_init_ptr =
						data->cur_init_ptr->next;
				}

				++skipped;
			}
			if (data->cur_init_ptr) {
				warningfl(tok, "Element %d already has an "
					"initializer", desig_elem);	
				replace_cur_init(data, init);
#if 0
			} else if (desig_elem < real_items_read) {
				/*
				 * 06/17/09: 
				 */
#endif
			} else {	
				append_init_list(data->init_head,
					data->init_tail, init);
			}
			*items_read += skipped;

			if (*items_read == real_items_read) { //at_end_of_list ?) {
				data->cur_init_ptr = NULL;
			}
		} else {
			/*
			 * Backward. First search the target initializer.
			 * We can exploit the fact that every member behind
			 * us has one initializer already (null if skipped.)
			 */
			struct initializer	*tmp;

			if (data->cur_init_ptr != NULL) {
				/*
				 * 06/15/09: We have to start somewhere in
				 * the list because there was already a
				 * designated backwards jump
				 */
				tmp = data->cur_init_ptr->prev;
			} else {
				/* Start at end */
				tmp = *data->init_tail;
			}
			++skipped;

			while (*items_read - skipped > desig_elem) {
				tmp = tmp->prev;
				++skipped;
			}
			*items_read -= skipped;

			if (tmp->prev) {
				tmp->prev->next = init;
			} else {
				*data->init_head = init;
			}	

			if (tmp->next) {
				tmp->next->prev = init;
			} else {
				*data->init_tail = init;
			}
			init->next = tmp->next;
			init->prev = tmp->prev;

			free/*_initializer XXX */(tmp);
			data->cur_init_ptr = init->next;
		}
	}

	if (desig_elem != -1) {
		if (desig_elem > data->highest_encountered_index) {
			data->highest_encountered_index = desig_elem;
		}
	} else {
		if (data->cur_init_ptr == NULL) {
			/* Appending at end */
			data->highest_encountered_index = *items_read;
		}
	}
}



static int
is_designated_initializer(struct token *t) {
	if ((t->type == TOK_OPERATOR
		&& *(int *)t->data == TOK_OP_STRUMEMB)
		|| (t->type == TOK_IDENTIFIER
			&& t->next
			&& t->next->type == TOK_OPERATOR
			&& *(int *)t->next->data == TOK_OP_AMB_COND2)) {
		return 1;
	} else {
		return 0;
	}
}


static void
complete_last_storage_unit(struct initializer *init,
	struct decl *storage_unit,
	unsigned char *buf) {

	struct initializer	*head = NULL;
	struct initializer	*tail = NULL;
	int			i;
	int			size = backend->get_sizeof_type(storage_unit->dtype, NULL);


	if (cross_get_target_arch_properties()->endianness == ENDIAN_BIG) {
		if (size > 1) {
			unsigned char		*start;
			unsigned char		*end;

			end = buf + size - 1;
			start = buf;
			do {
				unsigned char	temp = *start;
				*start = *end;
				*end = temp;
			} while (++start < --end);
		}
	}

	/*
	 * Replace bitfield initializer with (possibly partial) storage unit
	 * initializer
	 */
	for (i = 0; i < size; ++i) {
		struct initializer	*newinit = alloc_initializer();
		struct expr		*ex = alloc_expr();
		struct tyval		*tv = n_xmalloc(sizeof *tv);
		static struct tyval	nulltv;

		/*
		 * 10/08/08: We have to make this permanent because it may be
		 * used as a static variable initializer, which must live until
		 * all declarations are written
		 * XXX free properly
		 */
		ex = dup_expr(ex);

		*tv = nulltv;
		tv->type = make_basic_type(TY_UCHAR);
		tv->value = n_xmalloc(16); /* XXX */
		*(unsigned char *)tv->value = buf[i];
		ex->const_value = tv;

		newinit->left_type = make_basic_type(TY_UCHAR);
		newinit->data = ex;
		newinit->type = INIT_EXPR;
		append_init_list(&head, &tail, newinit);
	}
	init->type = INIT_NESTED;
	init->data = head;
}

static void
merge_bitfield_init(struct initializer **ret, struct initializer **ret_tail) {
	/*
	 * 10/04/08: Merge bitfield initializers, if any
	 */
	struct initializer	*init;
	struct decl		*last_storage_unit = NULL;
	struct initializer	*first_bf_init = NULL;
	unsigned char		buf[16];
	int			i;

#if 0 
printf("init list BEFORE processing\n");
puts("=====================");
{
	struct initializer	*init;
	for (init = *ret; init != NULL; init = init->next) {
		printf("    %d\n", init->type);
		if (init->type == 1) {
			struct initializer	*nest;
			for (nest = init->data; nest != NULL; nest = nest->next) {
				printf("             %d\n", nest->type);
			}
		}
	}
}
puts("=====================");
#endif

	memset(buf, 0, sizeof buf);
	for (init = *ret; init != NULL;) {
		if (init->type == INIT_BITFIELD
			|| (init->type == INIT_NULL 
				&& init->left_type != NULL
				&& init->left_type->tbit != NULL
				&& init->varinit)) {
			struct ty_bit		*t = init->left_type->tbit;
			struct expr		*data;
			int			byte_idx;
			int			bit_idx;
			long long		bfval;
			struct initializer	*continue_at;

			/*
			 * Unlink bitfield initializer! It is replaced
			 * with the storage unit initializer later
			 *
			 * We cannot do this for the very first bitfield
			 * because its initializer will be replaced with
			 * the merged result
			 */

			continue_at = init->next;
			if (t->bitfield_storage_unit == last_storage_unit) {
				if (init->type == INIT_NULL) {
					/*
					 * This is a variable initializer, i.e.
					 * an expression which must be computed
					 * and assigned at runtime! Therefore
					 * we cannot remove this initializer
					 */
					;
				} else {
					/*
					 * First field has already been passed - 
					 * remove initializer for later one 
					 */
					if (init->prev != NULL) {
						init->prev->next = init->next;
						if (*ret_tail == init) {
							/* Removing tail */
							*ret_tail = (*ret_tail)->prev;
						}
						if (init->next != NULL) {
							init->next->prev = init->prev;
						}
					} else {
						/* Must be head */
						if (init == *ret_tail) {
							/* Is also tail */
							*ret_tail = (*ret_tail)->prev;
						}
						*ret = (*ret)->next;
						if (*ret != NULL) {
							(*ret)->prev = NULL;
						}
					}
				}
			} else {
				/* Processing new storage unit */
				if (last_storage_unit != NULL) {
					complete_last_storage_unit(first_bf_init,
						last_storage_unit, buf);
					/* Reset buffer for next storage unit */
					memset(buf, 0, sizeof buf);
				}

				last_storage_unit = t->bitfield_storage_unit;
				first_bf_init = init;

				if (init->type == INIT_NULL) {
					/*
					 * Variable initializer - computed and
					 * assigned at runtime - so we have to
					 * keep this. Because ``init'' is
					 * overwritten with the new bitfield
					 * initializer, copy the old one!
					 */
					struct initializer	*temp;

					temp = dup_initializer(init);
					temp->prev = init;
					temp->next = init->next;
					if (temp->next) {
						temp->next->prev = temp;
					}
					init->next = temp;
					continue_at = temp->next;
				}
			}

			/*
			 * Store bitfield at corresponding offset
			 */
	#if 0
		printf("   abs byte offset = %lu - %lu\n", t->absolute_byte_offset,
			last_storage_unit->offset);
	#endif
			byte_idx = t->absolute_byte_offset - last_storage_unit->offset; 
			bit_idx = t->bit_offset;

			if (byte_idx >= (int)sizeof buf) {
				warningfl(NULL, "BUG: Bitfield bug encountered! The bitfield "
					"initializer will not work as expected");
				byte_idx = sizeof buf - 1;
			}





#if  0 
printf("doing bf    %s    at  %d,%d\n", init->left_type->name,
	byte_idx, bit_idx);
		printf(" at byte %d, bit %d\n", byte_idx, bit_idx);
		printf("    item is %d bits big\n", t->numbits);
		printf("       byte idx is %d - %d\n", t->absolute_byte_offset, last_storage_unit->offset);
#endif
			data = init->data;

			if (init->type != INIT_NULL) {
				bfval = cross_to_host_long_long(data->const_value);

#if 0
printf("   doing val %lld\n", bfval);
#endif
				if (1 || cross_get_target_arch_properties()->endianness == ENDIAN_LITTLE) {
					for (i = 0; i < t->numbits; ++i) {
						buf[byte_idx] |= !!(bfval & (1LL << i)) << bit_idx;
						if (bit_idx == 7) { /* XXX char bit count... */
							++byte_idx;
							bit_idx = 0;
						} else {
							++bit_idx;
						}
#if 0
		printf("    %02x %02x %02x\n",  buf[0], buf[1], buf[2]);
#endif
					}
				}
			}
			init = continue_at;
		} else {
			/* Is not bitfield type */
			if (last_storage_unit != NULL) {
				complete_last_storage_unit(first_bf_init,
					last_storage_unit, buf);
				/* Reset buffer for next storage unit */
				memset(buf, 0, sizeof buf);
				first_bf_init = NULL;
				last_storage_unit = NULL;
			}
			init = init->next;
		}
	}


#if  0 
	puts("DATA");
for (i = 0 ;i < sizeof buf; ++i) {
	printf("%02x ", buf[i]);
}
putchar('\n');
#endif

	if (last_storage_unit != NULL) {
		complete_last_storage_unit(first_bf_init, last_storage_unit, buf);
	}


#if 0 
puts("DATA AFTER REVERSAL");
for (i = 0 ;i < sizeof buf; ++i) {
	printf("%02x ", buf[i]);
}
putchar('\n');
#endif

#if 0 
printf("init list after processing\n");
puts("=====================");
{
	struct initializer	*init;
	for (init = *ret; init != NULL; init = init->next) {
		printf("    %d\n", init->type);
		if (init->type == 1) {
			struct initializer	*nest;
			for (nest = init->data; nest != NULL; nest = nest->next) {
				printf("             %d\n", nest->type);
			}
		}
	}
}
#endif
}


static struct initializer *
make_unnamed_bitfield_init(struct type *type) {
	struct initializer	*ret = alloc_initializer();
	struct expr		*ex = alloc_expr();
	struct tyval		*tv = n_xmalloc(sizeof *tv);

	ret->type = INIT_BITFIELD;
	ret->left_type = type;
	tv = n_xmemdup(tv, sizeof *tv); /* XXX */

	tv->value = n_xmalloc(16); /* XXX */
	memset(tv->value, 0, 16);
	tv->type = type; 

	ex = dup_expr(ex); /* Permanence! */
	ex->const_value = tv;
	ret->data = ex;
	return ret;
}

struct initializer *
get_init_expr(struct token **tok, struct expr *saved_first_expr0,
	int type, struct type *lvalue, int initial,
	int complit) {

	struct token		*t = *tok;
	struct expr		*ex;
	struct initializer	*ret = NULL;
	struct initializer	*rettail = NULL;
	struct initializer	*init;
	struct sym_entry	*se = NULL;
	struct type		*curtype = NULL;
	struct type		arrtype;
	struct desig_init_data 	init_data;
	struct sym_entry	*prevse = NULL;
	struct expr		*saved_first_expr = NULL;
	int			items_read = 0;
	int			items_ok = 0;
	int			is_aggregate = 0;
	int			is_array = 0;
	int			is_char_array = 0;
	int			is_braced_string = 0;
	int			is_unnamed_bitfield = 0; /* 10/12/08 */
	int			warned = 0;
	int			needbrace = 0;
	int			have_desig = 0;
	int			struct_ends = 0;

	init_data.init_head = &ret;
	init_data.init_tail = &rettail;
	init_data.cur_init_ptr = NULL;
	init_data.highest_encountered_member = NULL;
	init_data.highest_encountered_index = -1;
	init_data.type = lvalue;

	if (lvalue->tlist != NULL
		&& lvalue->tlist->type == TN_ARRAY_OF) {
		is_aggregate = 1;
		is_array = 1;
		items_ok = lvalue->tlist->arrarg_const;
		if (!initial && items_ok == 0) {
			errorfl(t, "Array size for nested array dimension "
					"unspecified");
			return NULL;
		}	
		arrtype = *lvalue;
		curtype = &arrtype;
		arrtype.tlist = arrtype.tlist->next;
		if (IS_CHAR(lvalue->code) && lvalue->tlist->next == NULL) {
			is_char_array = 1;
		}
	} else if ((lvalue->code == TY_STRUCT || lvalue->code == TY_UNION)
		&& lvalue->tlist == NULL) {

		if (lvalue->code == TY_STRUCT) {
			is_aggregate = 1;
		} else {
			items_ok = 1;
		}
		se = lvalue->tstruc->scope->slist;
		curtype = se->dec->dtype;

		/* XXX should set items_ok ?!?!? */
		/*
		 * 04/02/08: The code below only handled the case:
		 *    struct s val = init;
		 * ... i.e. a struct-by-value assignment as initializer.
		 * Hence this was only done if the ``initial'' flag was
		 * set.
		 * However, we also have to do this for nested structs,
		 * e.g.
		 *
		 *     struct foo { struct bar { int x; } b; };
		 *     struct bar bs;
		 *     struct fs = { bs };
		 *
		 * ... here the old version recursed into the ``struct
		 * bar'' member list and tried to assign the bs struct
		 * to the x struct member. This behavior is only correct
		 * for unbraced nested struct values, like
		 *
		 *     struct foo { struct bar { int x, y, z; } b; };
		 *     struct foo f = { 1, 2, 3 };
		 *
		 * The solution: For unparenthesized structure init
		 * values, ALWAYS read the first expression and checks
		 * if its type indicates that it fills the entire struct
		 * by value. Otherwise, assume it's an unbraceded member
		 * value, and pass that expression to the function while
		 * recursing
		 */
		if ( /*initial &&*/ t->type != TOK_COMP_OPEN) {
			int	initializes_entire_struct = 0;

			/*
			 * 09/07/07: This always used EXPR_INIT instead of
			 * type!! (breaks for constant initializers like
			 * compound literals)
			 */
			if (type == EXPR_OPTCONSTINIT && initial) {
				type = EXPR_INIT;
			}	
			if (initial) {
				ex = parse_expr(&t, TOK_OP_COMMA,
					TOK_SEMICOLON,
					type, 1);
			} else {
				ex = parse_expr(&t, TOK_OP_COMMA,
					TOK_COMP_CLOSE,
					type, 1);
			}

			if (ex == NULL) {
				return NULL;
			}

			if (!initial) {
				/*
				 * 04/02/08: Check if this is a value
				 * initializing the entire struct. So
				 * get the type using expr_to_icode()
				 * with the eval flag set to 0!
				 */
				struct vreg	*vr;

				vr = expr_to_icode(ex, NULL, NULL, 0, 0, 0);
				if (vr != NULL
					&& check_types_assign(NULL, lvalue,
					vr, 1, 1) == 0) {
					initializes_entire_struct = 1;
				} else {
					saved_first_expr = ex;
				}
			} else {
				initializes_entire_struct = 1;
			}

			if (initializes_entire_struct) {
				if (type == EXPR_OPTCONSTINIT
					&& !ex->is_const) {
					/*
					 * Nested non-constant struct init
					 */
					   
					/*
					 * Initialize value to 0 for now
					 */
					init = backend->
						make_null_block(NULL, lvalue,
							NULL, 1);	
					init->varinit = ex;
					init->left_type = dup_type(lvalue);
				} else {	
					if (ex->const_value
						&& ex->const_value->static_init) {
						init = ex->const_value->static_init;
					} else {	
						init = alloc_initializer();
						init->type = INIT_STRUCTEXPR;
						init->left_type = dup_type(curtype);
						init->data = ex;
					}
				}
				*tok = t;
				return init;
			}
		}
	} else {
		struct initializer	*was_nullblock = NULL;
		int			was_varinit = 0;

		/* The following are for variable struct/array inits */
		struct token		*starttok;
		int			delim1;
		int			delim2;

		if (saved_first_expr0 != NULL) {
			ex = saved_first_expr0;
			goto skip_expr_reading;
		}

		/* Only one item */
		if (t->type == TOK_COMP_OPEN) {
			/*
			 * Compound initializers are also allowed for
			 * non-aggregate types;
			 * int x = { 0 };
			 */
			t = t->next;
			starttok = t;
			ex = parse_expr(&t, TOK_COMP_CLOSE, TOK_OP_COMMA,type,1);

			delim1 = TOK_COMP_CLOSE;
			delim2 = TOK_OP_COMMA;
			if (t->type == TOK_OP_COMMA
				&& t->next != NULL	
				&& t->next->type == TOK_COMP_CLOSE) {
				/* Trailing comma permitted in compound init */
				t = t->next->next;
			} else if (t->type != TOK_COMP_CLOSE) {		
				/*
				 * There may be a trailing comma here. BitchX
				 * uses that
				 */
				if (t->type == TOK_OPERATOR
					&& *(int *)t->data == TOK_OP_COMMA
					&& t->next
					&& t->next->type == TOK_COMP_CLOSE) {
					t = t->next->next;
				} else {	
					errorfl(t, "Invalid initializer");
					return NULL;
				}	
			} else {
				t = t->next;
			}	
		} else {
			starttok = t;
			delim1 = TOK_OP_COMMA;
			delim2 = initial? TOK_SEMICOLON: TOK_COMP_CLOSE;
			ex = parse_expr(&t, TOK_OP_COMMA,
				initial? TOK_SEMICOLON: TOK_COMP_CLOSE, type,1);
		}	
		if (ex == NULL) {
			*tok = t;
			return NULL;
		}

skip_expr_reading:

		if (type == EXPR_CONSTINIT || type == EXPR_OPTCONSTINIT) {
			if (ex->const_value != NULL) {
				if (ex->const_value->static_init != NULL) {
					/*
					 * Initialized with value of other static
					 * variable
					 * XXX 02/19/08: This is NONSENSE stuff
					 * isn't it?
					 */
					struct initializer	*tmp;
	
					tmp = ex->const_value->static_init;
					if (tmp->type == INIT_EXPR) {
						struct expr	*tmpex = tmp->data;
	
						ex->const_value = tmpex->const_value;
					} else {	
						/* INIT_NULL */
						was_nullblock = tmp->data;
					}
				}	
				/*
				 * Convert constant initializer to destination
				 * type
				 */
				if (!was_nullblock) {
					conv_init(ex->const_value, lvalue, t->prev);
					/*
					 * 08/03/07: To make
					 *  static long nonsense = (long)&addr;
					 * work
					 */
					if (ex->const_value->address != NULL
						&& is_integral_type(lvalue)) {
						;
					} else {	
					ex->const_value->type = ex->type
						= n_xmemdup(lvalue, sizeof *lvalue);
					}

					/*
					 * 08/09/08: Limit bitfield range for
					 * initializers
					 */
					if (lvalue->tbit != NULL) {
						struct token		*tok;
						static struct tyval	dest;
						static struct tyval	masktv;
						static struct tyval	temp;
						static struct token	optok;
						static int		andop = TOK_OP_BAND;

						if (ex->const_value->value == NULL) {
							errorfl(t, "Invalid "
							"bitfield initializer");
							return NULL;
						}

						optok.type = TOK_OPERATOR;
						optok.data = &andop;

						/*
						 * Bitwise AND with mask for
						 * bitfield size
						 */
						tok = make_bitfield_mask(lvalue, 1, NULL, NULL);
						/*
						 * Copy types. We cannot just set the original type
						 * because it may get promoted in cross_exec_op(),
						 * which will change the type!
						 */
						masktv.type = dup_type(lvalue);
						masktv.value = tok->data;
						dest.type = dup_type(lvalue);
						dest.value = ex->const_value->value;
						cross_exec_op(&optok, &temp, &dest, &masktv);
						/*
						 * The operation may have caused a promotion
						 * which has changed the type. So convert to
						 * the expected type
						 */
						conv_init(&temp, lvalue, NULL);
						ex->const_value->value = temp.value;
					}
				} else {
					/* XXX */
					unimpl();
				}	
			} else {
				if (type == EXPR_OPTCONSTINIT
					&& !ex->is_const) {
					/*
					 * This is a non-constant expression
					 * used as initializer for a struct
					 * or an array. We have to save the
					 * expression and evaluate it when
					 * the initializer assignment takes
					 * place
					 */
					was_varinit = 1;
					/*
					 * 07/18/08: The re-reading of this
					 * expression below was wrong and
					 * apparently useless; The first pass
					 * ``trashed'' token type values
					 * (e.g. TOK_PAREN_OPEN becomes
					 * TOK_OP_CAST), so a re-evaluation
					 * is not meaningful. Instead we can
					 * keep the old expression
					 */
#if 0
					ex = parse_expr(&starttok, delim1,
						delim2, 0,1);
					t = starttok;
#endif
					if (ex == NULL) {
						*tok = t;
						return NULL;
					}

					/*
					 * Initialize value to 0 for now
					 */
					was_nullblock = backend->
						make_null_block(NULL, lvalue,
							NULL, 1);	
					was_nullblock->varinit = ex;
				} else {
					return NULL;
				}	
			}
		}

		if (was_nullblock) {
			ret = init = was_nullblock;
		} else {
			ret = init = alloc_initializer();
			if (lvalue->tbit != NULL) {
				/* 10/04/08: Bitfield */
				conv_init(ex->const_value, lvalue, NULL);
				init->type = INIT_BITFIELD;
			} else {
				init->type = INIT_EXPR;
			}
#if XLATE_IMMEDIATELY
			init->data = dup_expr(ex);
#else
			init->data = ex;
#endif
		}
		init->left_type = dup_type(lvalue);

		*tok = t;
		return init;
	}

	if (t->type == TOK_COMP_OPEN && is_char_array) {
		if (t->next && t->next->type == TOK_STRING_LITERAL) {
			/*
			 * This must be a braces-enclosed string constant;
			 *    char buf[] = { "hello" };
			 * The GNU people love using unnecessary braces
			 * (and parentheses) like this
			 */
			t = t->next;
			is_braced_string = 1;
		}
	}

	/*
	 * 08/22/07: Null initializers for strings were completely ignored!
	 * E.g. in
	 *
	 *     static char buf[10] = "hehe";
	 *
	 * ... only 5 bytes would be allocated for the buffer, since there
	 * were only 5 bytes of initialized data! Same thing with string
	 * initializers for nested array initializers. So now we resize the
	 * string and add any necessary null bytes
	 */
	if (t->type == TOK_STRING_LITERAL) {
		struct ty_string	*ts = t->data;
		size_t			newlen;

		if (lvalue->tlist != NULL
			&& lvalue->tlist->type == TN_ARRAY_OF
			&& (newlen = lvalue->tlist->arrarg_const) > ts->size) {
			ts->str = n_xrealloc(ts->str, newlen);
			memset(ts->str + ts->size - 1, 0, newlen - ts->size);
			ts->size = newlen;
			ts->ty->tlist->arrarg_const = newlen;
#if REMOVE_ARRARG
			ts->ty->tlist->have_array_size = 1;
#endif
		}	
	}

	if (t->type != TOK_COMP_OPEN) {
		if (t->type == TOK_STRING_LITERAL && is_array) {
			struct token	*tmp = t;
			struct token	*stringtok = t;

			if (lvalue->tlist->next != NULL
				|| !IS_CHAR(lvalue->code)) {
				errorfl(t, "Invalid initializer");
				return NULL;
			} else if (lvalue->sign == TOK_KEY_UNSIGNED) {
#if 0
				warningfl(t,
		"Assigning string constant to array of `unsigned char'");
#endif
			}

			/*
			 * 09/01/07: The extra } check was needed because
			 * othrwise
			 *
			 *    char foo[1][2] = { "hello" };
			 * breaks, since "hello" is an un-braced array
			 * initializer (the braces belong to the outer
			 * array), and we ended up looking for a , or ;
			 *
			 * XXX MAybe this still isn't fully correct
			 */
			if (is_braced_string
				|| (t->next && t->next->type == TOK_COMP_CLOSE)) {
				/*
				 * Read up to closing brace
				 */
				ex = parse_expr(&t, TOK_COMP_CLOSE, 0, type, 1);
			} else {	
				/*
				 * Read up to comma or semicolon
				 */
				ex = parse_expr(&t, TOK_OP_COMMA, TOK_SEMICOLON, type,
					1);
			}
			if (ex == NULL) {
				return NULL;
			}	
			if (t != tmp->next) {
				tmp = tmp->next? tmp->next: t;
				errorfl(tmp, "Parse error at `%s'",
					tmp->ascii);
				return NULL;
			}
			if (is_braced_string && t != NULL) {
				/* Skip brace */
				t = t->next;
			}	
			init = alloc_initializer();
			/*
			 * 05/31/08: Don't duplicate the type here, since
			 * array size may be determined below, so we can
			 * only do it afterwards
			 */
		/*	init->left_type = dup_type(lvalue);*/
#if XLATE_IMMEDIATELY
			init->data = dup_expr(ex);
#else
			init->data = ex;
#endif
			init->type = INIT_EXPR;
			*tok = t;

#if ! REMOVE_ARRARG
			if (lvalue->tlist->arrarg->const_value != NULL) {
				(void) backend->get_sizeof_type(lvalue, NULL);
			}	
#endif
			if (lvalue->tlist->arrarg_const == 0) {
				struct ty_string	*ts = stringtok->data;

				/*
				 * 05/27/08: This used strlen(t->prev->ascii),
				 * thus breaking the size of "\0hello"
				 */
				lvalue->tlist->arrarg_const = ts->size;

#if REMOVE_ARRARG
				lvalue->tlist->have_array_size = 1;
#endif
			}
			/* 05/31/08: Now we can copy the completed type */
			init->left_type = dup_type(lvalue);
			return init;
		} else if (initial) {
			/* Initial array initializer requires braces */
			errorfl(t, "Parse error at `%s'", t->ascii);
			return NULL;
		} else if (is_aggregate) {
			warningfl(t,
		"Nested aggregate initializer not surrounded by braces");
			needbrace = 0;
		}
	} else {
		needbrace = 1;
		if (next_token(&t) != 0) {
			return NULL;
		}
	}



	do {
		struct token		*starttok = t;
		struct sym_entry	*desig_se = NULL;
		struct sym_entry	*prev_se = se;
		int			desig_elem = -1;
		int			desig_elem_end = -1;
		
		is_unnamed_bitfield = 0; /* 10/12/08 */

		if (se != NULL && se->dec->dtype->tbit != NULL && se->dec->dtype->name == NULL) {
			/*
			 * 10/12/08: Handle unnamed bitfields by skipping them
			 */
			is_unnamed_bitfield = 1;
		} else if (is_designated_initializer(t)) {	
			int	is_c99_style = 0;

			/*
			 * Designated initializer -
			 *
			 *   .membername = expr 
			 *
			 * ... as per C99 or
			 * 
			 *   membername: expr
			 *
			 * ... as per GNU C (deprecated)
			 */
			have_desig = 1;
			if (is_array) {
				/*
				 * Only allowed for structs! (Note that we
				 * can't check ``se'' because it may be
				 * NULL here;
				 *
				 *   struct foo { int x, y; }
				 *     f = { .y = 0, .x = 0 };
				 *                   ^ NULL here
				 */
				errorfl(t, "Invalid designated initializer");
				return NULL;
			}
			if (t->type != TOK_IDENTIFIER) {
				is_c99_style = 1;
				/* Skip . */
				if (next_token(&t) != 0) {
					return NULL;
				}
				if (t->type != TOK_IDENTIFIER) {
					errorfl(t, "Syntax error - "
						"identifier expected");
					return NULL;
				}
			} else {
				warningfl(t, "GNU C designated initializiers "
					"are deprecated, use the C99 way "
					"instead (`.member = expr' instead "
					"of `member: expr')");
			}	
			desig_se = lookup_symbol_se(
					lvalue->tstruc->scope, t->data, 0);
			if (desig_se == NULL) {
				errorfl(t, "Unknown member `%s'", t->data);
				return NULL;
			}
			if (next_token(&t) != 0) {
				return NULL;
			}
			if (is_c99_style) {
				if (t->type != TOK_OPERATOR
					|| *(int *)t->data != TOK_OP_ASSIGN) {
					errorfl(t, "Syntax error at `%s' - "
						"`=' expected", t->next->ascii);
					return NULL;
				}
			}

			/* Skip : (GNU C) or = (C99) */
			if (next_token(&t) != 0) {
				return NULL;
			}	
			se = desig_se;
			curtype = se->dec->dtype;
		} else if (t->type == TOK_ARRAY_OPEN) {
			struct expr	*startex;
			struct expr	*endex;
			size_t		elem;
			size_t		startelem;
			size_t		endelem;

			have_desig = 1;
			if (next_token(&t) != 0) {
				return NULL;
			}
			startex = parse_expr(&t, TOK_ARRAY_CLOSE, TOK_ELLIPSIS, EXPR_CONST, 1);
			if (startex == NULL) {
				return NULL;
			}

			/*
			 * 07/18/08: Allow range syntax;
			 *
			 *    [expr ... expr]
			 */
			if (t->type == TOK_ELLIPSIS) {
				/* Is range! */
				warningfl(t, "ISO C does not allow designated "
					"array initializer ranges");	
				if (next_token(&t) != 0) {
					return NULL;
				}
				endex = parse_expr(&t, TOK_ARRAY_CLOSE, 0, EXPR_CONST, 1);;
			} else {
				endex = NULL;
			}

			startelem = cross_to_host_size_t(startex->const_value);
			if (endex != NULL) {
				/* Is range */
				endelem = cross_to_host_size_t(endex->const_value);
				if (startelem >= endelem) {
					errorfl(t, "Invalid range - "
						"start is after end");
					return NULL;
				}
				/*
				 * [0 ... 1] covers 2 elements, so increment endelem
				 */
				++endelem;
			} else {
				/* Not range */
				endelem = startelem + 1;
			}

			elem = startelem;
			

			if ((ssize_t)elem < 0) {
				errorfl(t, "Negative and very large designated "
					"array elements are not allowed");	
				return NULL;
			} else if ((ssize_t)elem > items_ok && items_ok != 0) {
				errorfl(t, "Array index %lu out of bounds",
					(unsigned long)elem);
				return NULL;
			} else if (elem > INT_MAX) {
				errorfl(t, "Array index %lu too large",
					(unsigned long)elem);
				return NULL;
			}
			if (next_token(&t) != 0
				|| t->type != TOK_OPERATOR
				|| *(int *)t->data != TOK_OP_ASSIGN) {
				if (t != NULL) {
					errorfl(t, "Syntax error at `%s' - ",
						"`=' expected",
						t->ascii);
				}
				return NULL;
			}
			if (next_token(&t) != 0) {
				return NULL;
			}	
			desig_elem = (int)elem;
			desig_elem_end = (int)endelem;
		}

		if (is_unnamed_bitfield) {
			/*
			 * 10/12/08: Handle unnamed bitfields by creating an
			 * initializer of INIT_BITFIELD with value 0. That way
			 * named and unnamed bitfields will properly be combined
			 * into storage units at the end of this function
			 *
			 * The initializer is only read for the next named
			 * struct member
			 */
			if (se->dec->dtype->tbit->numbits == 0) {
				/* Terminates storage unit - ignore */
				goto skip_init_handling;
			} else {
				init = make_unnamed_bitfield_init(se->dec->dtype);
			}
		} else {
			init = get_init_expr(&t, saved_first_expr, type, curtype, 0, 0);
		}
		saved_first_expr = NULL;

		if (init == NULL) {
			return NULL;
		}	

		if (is_basic_agg_type(curtype)) {
			struct initializer	*init2;

			init2 = alloc_initializer();
			init2->left_type = dup_type(curtype);
			init2->type = INIT_NESTED;
			init2->data = init;
			init = init2;
			/*append_init_list(&ret, &rettail, init2);*/
		} else {	
			/*append_init_list(&ret, &rettail, init);*/
		}

		if (is_array) {
/*			append_init_list(&ret, &rettail, init);*/
			int	i;

			/*
			 * 07/18/08: Loop to allow ranges!
			 */
			if (desig_elem != -1) {
				/* Destination is designated */
				for (i = desig_elem; i < desig_elem_end; ++i) {
					put_desig_init_array(&init_data, i,
							&items_read,
							init,
							curtype,
							starttok);
					if (i + 1 < desig_elem_end) {
						/*
						 * There will be another one,
						 * so copy the current item.
						 * Otherwise the node will
						 * be appended multiple times,
						 * which trashes the data
						 * structures
						 */
						init = dup_initializer(init);
					}
				}
			} else {
				/* Destination is implicit */
				put_desig_init_array(&init_data, desig_elem,
						&items_read,
						init,
						curtype,
						starttok);
			}
		} else {
			put_desig_init_struct(&init_data, desig_se,
					prev_se,
					lvalue->tstruc->scope->slist,
					init,
					starttok, &items_read);	
		}

		++items_read;
		if (items_ok != 0) {
			/*
			 * 07/19/08: Don't give errors for multiple union
			 * initializers, but warn about it
			 */
			if (items_read > 1
				&& lvalue->code == TY_UNION
				&& lvalue->tlist == NULL) {
				if (!warned) {
					warningfl(starttok, "More than one "
						"initializer for union");
					warned = 1;
				}
			}

			if (items_read > items_ok && !warned) {
				if (!is_aggregate) {
					errorfl(starttok,
		"Invalid aggregate initializer");
				} else {
					errorfl(starttok,
		"Initializer for aggregate type too big");
				}
				warned = 1;
			} else if (!needbrace && items_read == items_ok) {
				/* We're done */
				t = t->prev;
				break;
			}
		}

skip_init_handling:
		if (se != NULL) {
			prevse = se;
			se = se->next;
			if (se != NULL) {
				/* XXX error msg if se = NULL maybe? */
				curtype = se->dec->dtype;
			} else {
				/*
				 * 05/08/09: This may be a struct
				 * initializer without braces -
				 * Record the fact that all struct
				 * members are exhausted, so the
				 * struct may end here
				 *
				 * XXX what about subsequent
				 * designated initializers?
				 */
				if (!needbrace) {
					struct_ends = 1;
				}
			}
		}

		/*
		 * 08/05/07: The code below was previously BEFORE the se 
		 * update above! Thus if there was a trailing comma, the
		 * make_null_block() call at the bottom would wrongly
		 * create a null initializer for this structure member
		 * even though it already has an initializer
		 *
		 * This broke some vim option structs
		 *
		 * 10/12/08: The token stuff does not apply to unnamed
		 * bitfields, which are just skipped
		 */
		if (!is_unnamed_bitfield) {
			if (t->type == TOK_OPERATOR
				&& *(int *)t->data == TOK_OP_COMMA	
				&& t->next != NULL	
				&& t->next->type == TOK_COMP_CLOSE) {
				/* Trailing comma permitted in compound init. */
				t = t->next;
				break;
			}	
			if (t->type == TOK_SEMICOLON) {
				break; /* XXXXXXXX */
			}
		}
	} while (!expr_ends(t, TOK_COMP_CLOSE, 0)
		&& !struct_ends /* 05/12/09: note: comes before t=t->next! now */
		&& (is_unnamed_bitfield || (t = t->next) != NULL)); /* 10/12/08: Bitfields */

	/*
	 * 05/12/09: This is only a separator token that can be
	 * skipped if we didn't break out of the loop because
	 * a struct array element initializer is complete!
	 *
	 *    struct foo { int x, y; } f[] = { 1, 2, 3, 4 };
	 */
	if (!struct_ends) {
		if (t->type != TOK_SEMICOLON) {/* XXXXXXXXXXX */
			t = t->next;
		}
	}
	*tok = t;

	if (is_array && items_ok == 0) {
		lvalue->tlist->arrarg_const = items_ok = items_read;
#if REMOVE_ARRARG
		lvalue->tlist->have_array_size = 1;
#endif
	}
	
	if (initial
		&& t->type != TOK_SEMICOLON
		&& (t->type != TOK_OPERATOR
		|| *(int *)t->data != TOK_OP_COMMA)) {
		if (!complit) {
			errorfl(t, "Parse error at `%s'- expected semicolon or comma",
				t->ascii);	
			return NULL;
		}
	}

	/*
   	 * Remaining fields are initialized to 0. This is very straightforward
	 * for normal initializers - just initialize the rest of the struct
	 * or array - and requires brute-force for designated initializers
	 *
	 * 07/19/08: This requires a different strategy for unions! A union
	 * has multiple members, but only a single initializer is allowed.
	 * Therefore we have to check whether that initializer takes up the
	 * whole size of the union, and if it doesn't, only pad the rest
	 */
	init = NULL;
	if (lvalue->code == TY_UNION && lvalue->tlist == NULL) {
		/* Union */
		if (*init_data.init_head != NULL) {
			int	union_size = backend->get_sizeof_type(lvalue, NULL);
			int	init_size = backend->get_sizeof_type((*init_data.init_head)->left_type, NULL);

			if (union_size > init_size) {
				int	remaining = union_size - init_size;

				init = backend->make_null_block(init_data.last_encountered_member, NULL, lvalue,
						remaining);
			}
		}
	} else {	
		/* Struct or array */

#if 1
		if (!is_array) {
			/*
			 * 10/12/08: If we've already initialized the first
			 * couple of bitfields in a bitfield storage unit,
			 * then we have to complete that unit before we
			 * create an initializer for the remaining fields
			 */
			struct sym_entry	*temp;
			struct sym_entry	*continue_at;
			struct decl		*storage_unit = NULL;

			if (have_desig) {
				/*
				 * 10/12/08: Moved here from below: If this
				 * has designated initializers, it may jump
				 * anywhere in the declaration list, so we
				 * first have to locate the last processed
				 * initializer (up to which point null
				 * initialization has already been handled
				 * where needed)
				 */
				struct initializer	*tmp;

				se = lvalue->tstruc->scope->slist;
				tmp = ret;
				for (; se != NULL; se = se->next) {
					if (tmp->next == NULL) {
						break;
					}
					tmp = tmp->next;
				}
				se = se->next;
			}

			/*
			 * Now check whether there are remaining bitfields in
			 * current storage unit
			 */
			for (temp = se; temp != NULL; temp = temp->next) {
				if (temp->dec->dtype->tbit != NULL) {
					if (storage_unit == NULL) {
						/*
						 * See whether there is a
						 * preceding storage unit
						 */
						struct sym_entry	*temp2;

						for (temp2 = se->prev;
							temp2 != NULL;
							temp2 = temp2->prev) {
							if (temp2->dec->dtype->tbit != NULL) {
								storage_unit =
									temp2->dec->dtype->tbit->
									bitfield_storage_unit;
								break;
							}
						}
						if (storage_unit == NULL) {
							/* No preceding bitfield storage unit */
							break;
						}
					}
					if (temp->dec->dtype->tbit->bitfield_storage_unit
						!= storage_unit) {
						/* OK this is a new bitfield */
						break;
					} else {
						/*
						 * This must be covered by the preceding
						 * initializer since it uses the same storage
						 * unit - skip it
						 */
						temp->has_initializer = 1;
					}
				}
			}
		}
#endif

		if (have_desig) {
			init = NULL;
			if (is_array) {
				if (items_ok != 0) {
					int	remaining =
						items_ok -
						(init_data.highest_encountered_index+1);
	
					if (remaining < 0) {
						puts("BUG: ??????");
						abort();
					} else if (remaining > 0) {
						init = backend->
							make_null_block(NULL, curtype,
								NULL, remaining);
					}
				}
			} else {
#if 0
				struct initializer	*tmp;

				se = lvalue->tstruc->scope->slist;
				tmp = ret;
				for (; se != NULL; se = se->next) {
					if (tmp->next == NULL) {
						break;
					}
					tmp = tmp->next;
				}
				se = se->next;
#endif
				if (se != NULL) {
					init = backend->make_null_block(se, NULL, lvalue, 0);
				}
			}
		} else {
			if (items_read < items_ok || se != NULL) {	
				/* Remaining fields are initialized to 0 */
				if (se != NULL) {
					/* Structure */
					init = backend->
						make_null_block(se, NULL, lvalue, 0);
				} else {
					/* Array */
					int remaining = items_ok - items_read;
					init = backend->
						make_null_block(NULL, curtype,
								NULL, remaining);
				}
			}
		}
	}
	if (init != NULL) {
		append_init_list(&ret, &rettail, init);
	}
	merge_bitfield_init(&ret, &rettail);

	return ret;
}

#endif /* #ifndef PREPROCESSOR */

/*
 * Parses an expression and returns a pointer to the parse tree.
 *
 * 09/07/07: Finally the ``nesting'' variable is gone! Instead the caller
 * passes an ``initial'' flag, which states whether this is the initial
 * call to get a particular expression.
 *
 * Only if ``initial'' is set, will constant expression results be
 * evaluated before returning
 */
struct expr *
parse_expr(struct token **tok, int delim, int delim2, int type, int initial) {
	struct token	*t;
	struct token	*tokstart = *tok;
	struct expr	*ret = NULL;
	struct expr	*rettail = NULL;
	struct expr	*ex = NULL;
	struct s_expr	*s_ex;
	int		condop_nesting = 0;

#ifdef NO_EXPR
	/* Don't use expression parser */
	recover(tok, delim, delim2);
	if (*tok == NULL) {
		lexerror("Unterminated expression");
		exit(1);
	}
	return NULL;
#endif

	t = *tok;

	if (t->type == TOK_SEMICOLON) {
		/* Empty expression */
		if ((delim != TOK_SEMICOLON && delim2 != TOK_SEMICOLON)
			|| type == EXPR_INIT
			|| type == EXPR_CONSTINIT
			|| type == EXPR_OPTCONSTINIT
			|| type == EXPR_OPTCONSTARRAYSIZE) {
			errorfl(t, "Parse error at `%s'", t->ascii);
			return NULL;
		}
		ret = alloc_expr();
		ret->is_const = 1;
		return ret;
	} else if (t->type == TOK_ARRAY_CLOSE
			&& (delim == TOK_ARRAY_CLOSE
				|| delim2 == TOK_ARRAY_CLOSE)) {
		ret = alloc_expr();
		ret->is_const = 1;
		return ret;
	} else if (delim == TOK_ARRAY_CLOSE
		&& type == EXPR_CONST_FUNCARRAYPARAM) {
		if ((t->type == TOK_KEY_RESTRICT
			|| t->type == TOK_KEY_CONST /* XXX honor this */
			|| t->type == TOK_KEY_VOLATILE)
			&& t->next != NULL
			&& t->next->type == TOK_ARRAY_CLOSE) {
			/*
			 * This is a construct of the form
			 *
			 *   char buf[restrict]
			 *
			 * which is equivalent to
			 *
			 *   char *restrict buf
			 */
			*tok = t->next;
			ret = alloc_expr();
			ret->is_const = 1;
			return ret;
		}
		type = EXPR_CONST;
#ifndef PREPROCESSOR
	} else if (t->type == TOK_COMP_OPEN) {
		struct scope	*scope;

		/* GNU C statement-as-expression */

		if (ansiflag) {
			warningfl(t,
				"Statement-as-expression (`({ ... })') isn't "
				"available in ISO C (don't use -ansi!)");
		}

		scope = new_scope(SCOPE_CODE);

		if (analyze(&t) != 0) {
			close_scope();
			return NULL;
		}
		close_scope();
		scope->code->type = ST_EXPRSTMT; 

		*tok = t->next;
		ret = alloc_expr();
		ret->stmt_as_expr = /*curscope->next*/scope;
		return ret;
#endif
	}


	/*
	 * Strategy: An expression is a number of sub-expressions
	 * connected through binary and ternary operators, so we
	 * just always need to read a sub-expression, a connecting
	 * operator - if any - and then the next sub-expression.
	 */
	while ((s_ex = get_sub_expr(&t, delim, delim2, type)) != NULL) {
		int	op;

		ex = alloc_expr();
		ex->op = 0;
		ex->data = s_ex;

		append_expr(&ret, &rettail, ex);

		if (expr_ends(t, delim, delim2)) {
			if (t != NULL
				&& t->type == TOK_OPERATOR
				&& *(int *)t->data == TOK_OP_COMMA
				&& condop_nesting > 0) {
				/*
				 * 05/04/09: Address the case
				 *
				 *    int foo = 0? 0,0: 0;
				 *
				 * Here the comma used to be interpreted as
				 * declaration list delimiter (as in
				 * ``int foo = 0, bar = 0, baz = 0;'') But
				 * with conditional operators, ? and : have
				 * to behave like parentheses. This occurs
				 * in the ``links'' browser!
				 * XXX Any other such things? And does this
				 * stuff work for all cases?
				 */
				;
			} else {
				if ((ex->tok = s_ex->meat) == NULL) {
					if ((ex->tok = s_ex->is_sizeof) == NULL) {
						/* Must be parenthesized expr */
						ex->tok = s_ex->is_expr->tok;
					}
				}	
				break;
			}
		} else if (t->type != TOK_OPERATOR) {
			errorfl(t, "Parse error at `%s'(#2)", t->ascii);
			ex = NULL;
			break;
		} else if (*(int *)t->data == TOK_OP_COND) {
			++condop_nesting;
		} else if (*(int *)t->data == TOK_OP_AMB_COND2) {
			/*
			 * XXX Note we're using TOK_OP_AMB_COND2,
			 * ambig_to_binary() called below!
			 */
			--condop_nesting;
		}

		/* Must be binary or ternary operator */
		ex = alloc_expr();
		ex->data = NULL;
		op = *(int *)t->data;
		ex->tok = t;
		if (op != TOK_OP_COND) {
			op = ambig_to_binary(t);
		}

		if (type == EXPR_CONST || type == EXPR_CONSTINIT) {
			int		err = 0;
			char	*is_what =
				type == EXPR_CONST? "expression": "initializer";

			if (op == TOK_OP_COMMA) {
				/* 06/01/08: Allow this. hmm */
				warningfl(t,
					"The comma operator is not allowed "
					"in constant %ss", is_what);
/*				err = 1;*/
			} else if (IS_ASSIGN_OP(op)) {
				errorfl(t,
					"Assignment operators are not allowed "
					"in constant %ss", is_what);
				err = 1;
			}
			if (err) {
				free(ex);
				ex = NULL;
				break;
			}
		}
		ex->op = op;

		append_expr(&ret, &rettail, ex);

		if (next_token(&t) != 0) {
			return NULL;
		}
		if (expr_ends(t, delim, delim2)) {
			errorfl(ex->tok, "Syntax error at `%s'",
				ex->tok->ascii);
			break;
		}
		if (op == TOK_OP_COND) { 
			/*
			 * 08/18/07: Allow GNU C's empty first conditional
			 * operator operand
			 */
			if (t->type == TOK_OPERATOR
				&& *(int *)t->data == TOK_OP_AMB_COND2) {
				warningfl(t, "Conditionals with omitted "
					"operands are not ISO C (GNU C only)");
				if (next_token(&t) != 0) {
					return NULL;
				}
				if (expr_ends(t, delim, delim2)) {
					errorfl(t, "Syntax error at `%s'",
						t->ascii);
					break;
				}
				ex = alloc_expr();
				ex->tok = t;
				ex->op = TOK_OP_COND2;
				append_expr(&ret, &rettail, ex);
			}
		}
	}

	*tok = t;

	if (ex == NULL || !expr_ends(t, delim, delim2)) {
		/* Try to recover from parse errors */
		if (initial) {
			recover(&tokstart, delim, delim2);
			*tok = tokstart;
		}
		return NULL;
	}

#ifndef PREPROCESSOR
	debug_print_expr(ret);
#endif
	fflush(stdout);

	if ((ret = bind_operators(ret)) == NULL) {
		puts("panic: cannot bind operators");
		exit(EXIT_FAILURE); /* XXX */
	}
#ifndef PREPROCESSOR
	debug_print_tree(ret);
#endif
	ret->extype = type;

	if (initial
		&& (type == EXPR_CONST
		|| type == EXPR_CONSTINIT
		|| type == EXPR_OPTCONSTINIT
		|| type == EXPR_OPTCONSTARRAYSIZE)) {
		int	not_constant;

		if (eval_const_expr(ret, type, &not_constant) != 0) {
			if ((type == EXPR_OPTCONSTINIT 
				|| type == EXPR_OPTCONSTARRAYSIZE)
				&& not_constant) {
				ret->is_const = 0;
			} else {
				return NULL;
			}
		} else {
			ret->is_const = 1;
		}
	}

	return ret;
}




void
recover(struct token **tok, int delim, int delim2) {
	struct token	*t;
	int		parens = 0;
	int		brackets = 0;
	int		braces = 0;

	if (*tok == NULL) return; 

	if (delim == TOK_PAREN_CLOSE || delim2 == TOK_PAREN_CLOSE) {
		++parens;
	}
	if (delim == TOK_ARRAY_CLOSE || delim2 == TOK_ARRAY_CLOSE) {
		++brackets;
	}
	if (delim == TOK_COMP_CLOSE || delim2 == TOK_COMP_CLOSE) {
		++braces;
	}

	for (t = *tok; t != NULL; t = t->next) {
		int	ty = -1; /* 0 compared even with unused delim :( */

#ifdef DEBUG2
printf(" lol %s\n", t->ascii);
#endif
		if (t->type == TOK_OPERATOR) {
			ty = *(int *)t->data;
		} else if (t->type == TOK_PAREN_OPEN) {
			++parens;
			continue;
		} else if (t->type == TOK_PAREN_CLOSE) {
			/*
			 * The below was --parens < 0, but <=
			 * seems more permissive for parse 
			 * errors - so use that!
			 */
			if (--parens <= 0
				&& (delim == TOK_PAREN_CLOSE
					|| delim2 == TOK_PAREN_CLOSE)) {
				*tok = t;
				return;
			}
			continue;
		} else if (t->type == TOK_ARRAY_OPEN) {
			++brackets;
			continue;
		} else if (t->type == TOK_ARRAY_CLOSE) {
			if (--brackets <= 0
				&& (delim == TOK_ARRAY_CLOSE
					|| delim2 == TOK_ARRAY_CLOSE)) {
				*tok = t;
				return;
			}
			continue;
		} else if (t->type == TOK_COMP_OPEN) {
			++braces;
			continue;
		} else if (t->type == TOK_COMP_CLOSE) {
			if (--braces <= 0
				&& (delim == TOK_COMP_CLOSE
					|| delim2 == TOK_COMP_CLOSE)) {
				*tok = t;
				return;
			}
			continue;
		} else {
			ty = t->type;
		}
		if (ty == delim || ty == delim2) {
			if (ty == TOK_OP_COMMA) {
				/*
				 * We must take into account that someone
				 * could write
				 * int foo = bar(x, y, z);
				 * ... in which case we do not want to exit
				 * after x!
				 */
				if (parens != 0) {
					continue;
				}
			}
			*tok = t;
			return; 
		}
	}
	*tok = NULL;
}

