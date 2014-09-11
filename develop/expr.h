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
 */
#ifndef EXPR_H
#define EXPR_H

struct token;
struct type;
struct vreg;
struct ty_string;
struct initializer;
struct label;

struct addr_const {
	/* If static variable */
	struct decl	*dec;


	/* If label address (for GNU C computed gotos) */
	struct token	*labeltok;
	char		*labelname;	/* Name */
	char		*funcname;	/* Parent function (needed for nasm - func.label) */

	long		diff;

	struct addr_const	*next;
};

struct tyval {
	struct type		*type;
	struct ty_string	*str;
	void			*value;
	struct addr_const	*address;
	struct vreg		*struct_member;
	struct initializer	*static_init;
	struct type		*inttype;
	int			alloc;
	int			not_constant;
	int			is_nullptr_const;
	struct token		*is_static_var;
};

struct initializer {
	int			type;
#define INIT_NESTED	1
#define INIT_EXPR	2
#define INIT_NULL	3
#define INIT_STRUCTEXPR	4
#define INIT_BITFIELD	5
	void			*data;
	/*
	 * If type = INIT_NULL and varinit != NULL, varinit is the
	 * variable initializer expression to be compiled
	 */
	   
	struct expr		*varinit;

	/*
	 * Left-hand type to which the initializer belongs. This is
	 * for offset calculations, which become necessary if there
	 * is a variable initializer which must be compiled and
	 * assigned to the correct offset
	 */
	struct type		*left_type;

	/*
	 * If left_type is not available (e.g. because this is an
	 * INIT_NULL initializer for part of a struct or array), then
	 * left_alignment contain the alignment which would otherwise
	 * be obtained from the type (``data'' already points to a
	 * size_t which keeps the size
	 */
	unsigned long		left_alignment;
	struct initializer	*next;

	/*
	 * 09/03/07: Prev member to unlink initializers!
	 */
	struct initializer	*prev;
};	

struct init_with_name {
	struct initializer	*init;
	char			*name;
	struct decl		*dec;
	struct init_with_name	*next;
};	

struct expr {
	int			op; /* unused (=0) in last node */
	int			used;
	int			debug;
	int			extype;
	int			is_const;
	struct token		*tok;
	struct tyval		*const_value;
	struct s_expr		*data; /* null if op non-0 */
	struct vreg		*res;
	struct type		*type;
	struct scope		*stmt_as_expr;
	struct expr		*left;
	struct expr		*right;
	struct expr		*next;
	struct expr		*prev;
};

struct parse_tree {
	struct expr	*tree;
	struct type	*type;
};


struct expr	*alloc_expr(void);
struct initializer *alloc_initializer(void);
struct initializer *get_init_expr(struct token **tok,
		struct expr *saved_first_expr0,
		int type, struct type *lvalue, int initial, int complit);
struct expr *parse_expr(struct token **tok, int delim, int delim2, int et, int
		initial);
#define EXPR_INIT		1
#define EXPR_CONSTINIT		2
#define EXPR_CONST		3

/*
 * Optionally constant. This is only for (AUTOMATIC!) array and structure 
 * initializers, where a constant expression is preferred, but not
 * mandatory since nwcc supports GNU C/C99/C++ variable initializers.
 * A warning is always printed, however, as it must be in C90 mode too
 */
#define EXPR_OPTCONSTINIT	4

/*
 * 07/22/07: This is used for the first dimension of an automatic array -
 * if constant, it becomes an ordinary array size constant expression,
 * otherwise we have a VLA
 */
#define EXPR_OPTCONSTARRAYSIZE	5

/*
 * 04/01/08: This is for evaluating supposedly constant sub-expressions.
 * It is only used for one operator applied to two (or three with ?:)
 * arithmetic constants (i.e. not even strings)!
 * Things like the * and [] operators make otherwise constant sub-
 * expressions non-constant, so this flag is needed for those corner
 * cases (e.g. *(int *)0x12345)
 */
#define EXPR_OPTCONSTSUBEXPR 6

/*
 * 07/21/07: This is a kludge to allow  ``char buf[restrict]'' as a
 * function parameter declaration. Internally parse_expr() will turn it
 * into EXPR_CONST if it is not a restrict declaration
 */
#define EXPR_CONST_FUNCARRAYPARAM	10




#if 0
/* 070107: Not used anywhere. This is apparently what 0 is used for now */
#define EXPR_NONCONST		4
#endif

#define IS_EMPTY_EXPR(e) (e->op == 0 && e->data == NULL)


int	convert_tyval(struct tyval *left, struct tyval *right);
int	expr_ends(struct token *t, int delim, int delim2);
int	eval_const_expr(struct expr *ex, int extype, int *not_constant);
void	append_expr(struct expr **head, struct expr **tail, struct expr *);
void	recover(struct token **tok, int delim, int delim2);
void    do_conv(struct tyval *, int);
struct expr	*dup_expr(struct expr *ex);

#endif

