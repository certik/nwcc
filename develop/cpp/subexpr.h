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
 */
#ifndef SUBEXPR_H
#define SUBEXPR_H

struct builtin;
struct icode_list;
struct token;
struct vreg;
struct ty_func;

struct var_access {
	struct icode_instr	*ii;
	struct var_access	*next;
	struct var_access	*prev;
};


struct fetch_data {
	int				type;
#define FETCH_STRUMEM	1
#define FETCH_CONSTANT	2
#define FETCH_VARIABLE	3
#define FETCH_ADDRESS	4
	struct decl		*dec;
	struct token	*constant;
};

struct store_data {
	int				type;
#define STORE_STRUMEM	1
#define STORE_CONSTANT	2	
#define STORE_VARIABLE	3
	struct reg		*fromreg;
	struct decl		*to_var;
};

struct fcall_data {
	struct builtin_data	*builtin;
	struct decl		*callto;
	struct vreg		*calltovr;
	struct expr		*args;
	struct ty_func		*functype;
	struct vreg		*lvalue;
	int			need_anon;
	int			nargs;
	int			was_just_declared;
};

struct comp_literal {
	struct initializer	*init;
	struct type		*struct_type;
};	
	

struct s_expr {
	struct s_expr		*next;
	struct icode_list	*code;
	struct type		*type;
	struct vreg		*load;
	struct vreg		*res;
	struct expr		*is_expr;
	struct decl		*var_lvalue;
	/*
	 * 03/10/09: WOW struct s_expr used more than 500 bytes because
	 * of this terrible huge (128 elements) operators array! That
	 * seems to slow down the zone allocator (which zeroes this and
	 * needed LOTS of pages for a handful of s_expr structs
	 *
	 * Now we do this:
	 *
	 *     - If there are fewer than 10 unary/post/pre operators
	 * (likely), then we use ``operators_buf'', by pointing 
	 * ``operators'' to it
	 *
	 *     - If there are more than 10 such operators (unlikely),
	 * we allocate storage for them dynamically and point 
	 * ``operators'' to it. NOTE: We do not use a zone allocator 
	 * for this and for now just keep the memory allocated 
	 * indefinitely. It's a memory leak if you will, but more 
	 * than 10 operators should be an extraordinarily unlikely case.   
	 *
	 *     foo->x.y.z.gnu.dummy[i][j][k]++
	 *
	 * ... would still fit into 10
	 */
/*	struct token		*operators[128];*/
	struct token		**operators;
	struct token		*operators_buf[10];

	struct token		*meat;
	struct token		*is_sizeof;

/* IMPORTANT: May not have same value as token.h macros ... */	
#define SIZEOF_EXPR 1000

	/*
	 * 07/03/08: New flags variable; SEXPR_FROM_CONST_EXPR indicates
	 * that the sub-expression is the result of cutting down a
	 * partially constant expression. For example,
	 *
	 *    0? foo: bar
	 *
	 * ... is now transformed to
	 *
	 *    bar
	 *
	 * ... but we may still have to perform actions such as doing
	 * array/function decay that would occur in the original operator
	 * context, but not in the modified version. In particular, given
	 * ``char buf[128]'',
	 *
	 *    sizeof (buf)          means ``sizeof(char[128])'' and
	 *
	 *    sizeof (0? buf: buf)  means ``sizeof(char *)''   
	 *
	 * Thus the new flag tells us whether decay has to take place even
	 * though it looks like it doesn't
	 */
	int			flags;
#define SEXPR_FROM_CONST_EXPR	1

	int			is_lvalue;
	int			only_load;
	int			extype;
	int			idiom;
#define IDIOM_STAR_INC		1 /*  *p++  */
#define IDIOM_STAR_DEC		2 /*  *p--  */
#define IDIOM_INC_STAR		3 /*  *++p  */
#define IDIOM_DEC_STAR		4 /*  *--p  */

#define IDIOM_PAR_STAR_INC	5 /* (*p)++ */
#define	IDIOM_PAR_INC_STAR	6 /* ++*p or ++(*p) */
#define IDIOM_PAR_STAR_DEC	7 /* (*p)-- */
#define IDIOM_PAR_DEC_STAR	8 /* --*p or --(*p) */ 

#define IDIOM_ADDR_OF_ELEM	10 /* &foo[bar] */

#define IDIOM_ADDR_OF_MEM	11 /* &foo.bar or &foo->bar */ 

};

struct token *
make_cast_token(struct type *ty);

struct s_expr *
get_sub_expr(struct token **tok, int delim, int delim2, int extype); 

struct vreg *
s_expr_to_icode(struct s_expr *s, struct vreg *lvalue,
	struct icode_list *il, int is_standalone, int eval);

struct fcall_data *
alloc_fcall_data(void);

struct s_expr *
alloc_s_expr(void);

#endif

