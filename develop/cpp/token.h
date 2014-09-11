/*
 * Copyright (c) 2003 - 2009, Nils R. Weller
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
#ifndef TOKEN_H
#define TOKEN_H

#include <stdio.h>


/*
 *******************************************************************
 *                              OPERATORS 
 *******************************************************************
 */

#define TOK_OP_MIN			1
#define TOK_OP_MAX			112


#define IS_OPERATOR(val) ((val) >= TOK_OP_MIN && (val) <= TOK_OP_MAX)


/*
 * Unary operators
 */

/* Unary plus / minus operators */
#define TOK_OP_UMINUS		1 /* unary minus */
#define TOK_OP_UPLUS		2 /* unary plus */

/* Bitwise unary operators */
#define TOK_OP_BNEG			3 /* bitwise negation */

/* Logical unary operators */
#define TOK_OP_LNEG			4 /* logical negation */

/* Unary indirection operators */ 
#define TOK_OP_DEREF		5 /* dereference pointer */

/* Address operator */
#define TOK_OP_ADDR			6 /* address of object */

/* Increment / decrement operators */
#define IS_INCDEC_OP(op) \
	((op) >= TOK_OP_INCPRE && (op) <= TOK_OP_DECPOST)

#define TOK_OP_INCPRE		7 /* prefix increment */
#define TOK_OP_INCPOST		8 /* postfix increment */
#define TOK_OP_DECPRE		9 /* prefix decrement */
#define TOK_OP_DECPOST		10 /* postfix decrement */

#define IS_UNARY(op) \
	( ((op) >= TOK_OP_UMINUS && (op) <= TOK_OP_DECPOST) \
	  || (op) == TOK_OP_ADDRLABEL )

/*
 * Binary operators
 */
/* Arithmetic operators */
#define TOK_OP_PLUS			20 /* addition */
#define TOK_OP_MINUS		21 /* substraction */
#define TOK_OP_DIVIDE		22 /* division */
#define TOK_OP_MULTI		23 /* multiplication */
#define TOK_OP_MOD			24 /* modulo */


/* Bitwise binary operators */
#define TOK_OP_BAND			30 /* bitwise and */
#define TOK_OP_BOR			31 /* bitwise or */
#define TOK_OP_BXOR			32 /* bitwise xor */
#define TOK_OP_BSHL			33 /* bitwise shift left */
#define TOK_OP_BSHR			34 /* bitwise shift right */

/* Assignment operators (+=, -=, <<=, etc) */
#define IS_ASSIGN_OP(op) \
	((op) >= TOK_OP_ASSIGN && (op) <= TOK_OP_COBSHR)

#define TOK_OP_ASSIGN	39 /* assignment */
#define TOK_OP_COPLUS	40 /* comp. plus assignment */
#define TOK_OP_COMINUS	41 /* comp. minus assignment */
#define TOK_OP_CODIVIDE	42 /* comp. divide assignment */
#define TOK_OP_COMULTI	43 /* comp. multiply assignment */
#define TOK_OP_COMOD	44 /* comp. modulo assignment */
#define TOK_OP_COBAND	45 /* comp. bitwise and assignment */
#define TOK_OP_COBOR	46 /* comp. bitwise or assignment */
#define TOK_OP_COBXOR	47 /* comp. bitwise xor assignment */
#define TOK_OP_COBSHL	48 /* comp. bitwise shift left assignment */
#define TOK_OP_COBSHR	49 /* comp. bitwise shift right assignment */

/* Logical binary operators (&&, ||, etc) */
#define TOK_OP_LAND	60 /* logical and */
#define TOK_OP_LOR	61 /* logical or */
#define TOK_OP_LEQU	62 /* logical equalty  */
#define TOK_OP_LNEQU		63 /* logical inequality */
#define TOK_OP_GREAT		64 /* greater than */
#define TOK_OP_SMALL		65 /* smaller than */
#define TOK_OP_GREATEQ		66 /* greater than or equal */
#define TOK_OP_SMALLEQ		67 /* smaller than or equal */


/* Binary indirection operators */
#define TOK_OP_STRUMEMB	70 /* access structure member */
#define TOK_OP_STRUPMEMB 71 /* access structure member thru pointer */


/* Comma operator */
#define TOK_OP_COMMA		80	

#define IS_BINARY(op) \
	((op) >= TOK_OP_PLUS && (op) <= TOK_OP_COMMA)

/*
 * Ternary operators
 */
/* Conditional operator */
#define TOK_OP_COND		90 /* conditional operator (1) */
#define TOK_OP_COND2		91 /* conditional operator (2) */


/* Ambiguous operators (unary or binary) */
#define TOK_OP_AMB_PLUS		100	
#define TOK_OP_AMB_MINUS	101	
#define TOK_OP_AMB_MULTI	102	
#define TOK_OP_AMB_BAND		103	
#define TOK_OP_AMB_INCR		104	
#define TOK_OP_AMB_DECR		105	
#define TOK_OP_AMB_COND2	106	

/* Cast operator */
#define TOK_OP_CAST		110	
#define TOK_COMP_LITERAL	111
#define TOK_OP_ADDRLABEL	112	/* 07/19/08 */

#define IS_AMBIG(x) \
	((x) >= TOK_OP_AMB_PLUS && (x) <= TOK_OP_AMB_COND2)

/*
 *******************************************************************
 *                    DATA TYPES
 * Note that the order in which arithmetic types are defined is
 * VERY important; The usual arithmetic conversions for one depend
 * on it. The arithmetic type with the highest rank should also have
 * the highest value
 *******************************************************************
 */
#define TY_MIN			119 /* _Bool has lowest rank */
#define TY_MAX			140

#define IS_CONSTANT(val) ((val) >= TY_MIN && (val) <= TY_MAX)

#define TY_BOOL			119 /* C99 _Bool */

/*
 * DANGER DANGER DANGER!!!!!!!!!!!!!!!!!!!! XXX !!!!!!!!!!!!
 * IS_CHAR() triggers on TY_BOOL because that simplifies stuff
 * However that is an obscure side effect
 */
#define IS_CHAR(val) ((val) >= TY_BOOL && (val) <= TY_SCHAR)

#define TY_CHAR			120	
#define TY_UCHAR		121	
#define TY_SCHAR		122	

#define IS_SHORT(val) ((val) == TY_SHORT || (val) == TY_USHORT)

#define TY_SHORT		123	
#define TY_USHORT		124	

#define IS_INT(val) ((val) == TY_INT || (val) == TY_UINT)

#define TY_INT			125
#define TY_UINT			126

#define IS_LONG(val) ((val) == TY_LONG || (val) == TY_ULONG)

#define TY_LONG			127
#define TY_ULONG		128

#define IS_LLONG(val) ((val) == TY_LLONG || (val) == TY_ULLONG)

#define TY_LLONG		129	/* C99 long long */
#define TY_ULLONG		130 /* C99 unsigned long long */


#define TY_FLOAT		133
#define TY_DOUBLE		134
#define TY_LDOUBLE		135

#define IS_FLOATING(val) \
	((val) == TY_FLOAT \
	 || (val) == TY_DOUBLE \
	 || (val) == TY_LDOUBLE)

#define TY_STRUCT		136
#define TY_UNION		137
#define TY_ENUM			138
#define TY_VOID			139


/*
 * Keywords. Note: Order is significant (for IS_*() macros)
 */
/*
 *******************************************************************
 *     KEYWORDS (Note: Order is significant for IS_*() macros) 
 *******************************************************************
 */
#define TOK_KEY_CHAR		150
#define TOK_KEY_SHORT		151
#define TOK_KEY_INT			152
#define TOK_KEY_LONG		153
#define TOK_KEY_FLOAT		154
#define TOK_KEY_DOUBLE		155
#define TOK_KEY_VOID		156
#define TOK_KEY_CONST		157
#define TOK_KEY_VOLATILE	158
#define TOK_KEY_REGISTER	159
#define TOK_KEY_STATIC		160
#define TOK_KEY_AUTO		161
#define TOK_KEY_EXTERN		162
#define TOK_KEY_SIGNED		163
#define TOK_KEY_UNSIGNED	164
#define TOK_KEY_DO		165
#define TOK_KEY_WHILE		166
#define TOK_KEY_FOR		167
#define TOK_KEY_IF		168
#define TOK_KEY_ELSE		169
#define TOK_KEY_SWITCH		170
#define TOK_KEY_CASE		171
#define TOK_KEY_BREAK		172
#define TOK_KEY_CONTINUE	173
#define TOK_KEY_DEFAULT		174
#define TOK_KEY_RETURN		175
#define TOK_KEY_GOTO		176
#define TOK_KEY_ENUM		177
#define TOK_KEY_STRUCT		178
#define TOK_KEY_UNION		179
#define TOK_KEY_SIZEOF		180
#define TOK_KEY_TYPEDEF		181
#define TOK_KEY_ASM		182 /* GNU C __asm__ */
#define TOK_KEY_ATTRIBUTE	183 /* GNU C __attribute__ */


#define TOK_KEY_RESTRICT	184 /* C99 */
#define TOK_KEY_INLINE		185 /* C99 */
#define TOK_KEY_BOOL		186 /* C99  - note lowest rank! */
#define TOK_KEY_EXTENSION	187 /* GNU C __extension__ */
#define TOK_KEY_TYPEOF		188 /* GNU C */
#define TOK_KEY_ALIGNOF		189 /* GNU C */
#define TOK_KEY_THREAD		190 /* GNU C */

#define TOK_SIZEOF_VLA_TYPE	119
#define TOK_SIZEOF_VLA_EXPR	192


#define TOK_KEY_MIN		150
#define TOK_KEY_MAX		TOK_KEY_THREAD


#define IS_KEYWORD(val) ((val) >= TOK_KEY_MIN && (val) <= TOK_KEY_MAX)

/* XXX perhaps change this to take ``struct token''
 * and check for
 * t->type == TOK_IDENTIFIER &&
 * lookup_typedef(curscope, t->ascii)
 */
#define IS_TYPE(tok) ((\
	((tok)->type >= TOK_KEY_CHAR \
		&& (tok)->type <= TOK_KEY_UNSIGNED) || \
	((tok)->type == TOK_KEY_STRUCT) || \
	((tok)->type == TOK_KEY_ENUM) || \
	((tok)->type == TOK_KEY_UNION) || \
	((tok)->type == TOK_KEY_BOOL) || \
	((tok)->type == TOK_KEY_TYPEDEF) || \
	((tok)->type == TOK_KEY_TYPEOF) || /* GNU */ \
	((tok)->type == TOK_KEY_ATTRIBUTE) || /* GNU */ \
	((tok)->type == TOK_KEY_THREAD) || /* GNU */ \
	((tok)->type == TOK_KEY_TYPEOF)) \
		|| ((tok)->type == TOK_IDENTIFIER \
			&& lookup_typedef(curscope, (tok)->data, 1, 0)))


#define IS_QUALIFIER(val) (\
	(val) == TOK_KEY_RESTRICT || \
	(val) == TOK_KEY_CONST || \
	(val) == TOK_KEY_VOLATILE)

#define IS_CONTROL(val) \
	((val) >= TOK_KEY_DO && (val) <= TOK_KEY_GOTO) 


#define TOK_ARRAY_OPEN		200
#define TOK_ARRAY_CLOSE		201
#define TOK_COMP_OPEN		202
#define TOK_COMP_CLOSE		203
#define TOK_PAREN_OPEN		204
#define TOK_PAREN_CLOSE		205
#define TOK_OPERATOR		206
#define TOK_IDENTIFIER		207
#define TOK_CHAR_LITERAL	208
#define TOK_NUM_LITERAL		209
#define TOK_STRING_LITERAL 	210	
#define TOK_SEMICOLON		211
/*
 * 07/17/08: Finally a token for the ellipsis ``...''
 */
#define TOK_ELLIPSIS		212

#ifdef PREPROCESSOR
#    define TOK_WS                  250
#    define TOK_NEWLINE             251
#    define TOK_HASH                252
#    define TOK_HASHHASH            253
#    define TOK_DEFINED             254
#endif



enum standard { C89 = 1, C99 = 2 };

#ifdef PREPROCESSOR
struct macro_arg;
#endif

struct token {
	char		*ascii;
	void		*data;
	void		*data2;
	int		type;
	int		line;
	char		*line_ptr;
	char		*tok_ptr;
	char		*file;
	int		fileid; /* XXX maybe void * if more info needed? */
#ifdef PREPROCESSOR
	struct macro            *is_funclike;
	struct macro_arg        *maps_to_arg;
	struct macro            **expanded_macros;
	int                     expanded_macros_alloc;
	int                     expanded_macros_idx;
	int                     slen;
	int                     hashkey;
#endif
	struct token	*next;
	struct token	*prev;
#if 0 
int seqno;
#endif
};

extern struct token		*toklist;


struct num {
	void	*value;
	int	type;
#ifdef PREPROCESSOR
	char	*ascii;
#endif
};


extern size_t	lex_chars_read;
extern char	*lex_file_map;
extern char	*lex_file_map_end;
extern char	*lex_line_ptr;
extern char	*lex_tok_ptr;


#ifdef PREPROCESSOR

#define FGETC(file) \
        (++lex_chars_read, \
        get_next_char(file))

#define UNGETC(ch, file) \
        (--lex_chars_read, /*ngetc((ch), (file))*/ unget_char((ch), (file)))

#else

#define FGETC(file) \
	( ++lex_chars_read, \
	get_next_char(file) )

#define UNGETC(ch, file) \
	(--lex_chars_read,  ungetc((ch), (file) ) )

#endif


struct ty_string;
struct ty_float;
struct ty_llong;
struct type;

extern struct ty_string	*str_const;
extern struct ty_float	*float_const;
extern struct ty_llong	*llong_const;

struct token		*alloc_token(void);
struct token		*dup_token(struct token *tok);
void			free_tokens(struct token *, struct token *, int);
#define FREE_DECL	1
#define FREE_CTRL	2


#ifdef PREPROCESSOR
struct input_file;
#endif

/*
 * 05/21/09: XXX Silly FILE vs input_file distinction for preprocessor...
 * Also in FGETC()/UNGETC(). Can we unify it?
 */
#ifndef PREPROCESSOR
int			 get_char_literal(FILE *f, int *err);
int 			get_trigraph(FILE *f);
struct ty_string	*get_string_literal(FILE *f);
int			get_operator(int ch, FILE *f, char **ascii);
struct num		*get_num_literal(int ch, FILE *f);
char			*get_identifier(int ch, FILE *f);
#else
int 			get_char_literal(struct input_file *inf, int *err, char **text);
int 			get_trigraph(struct input_file *f);
struct ty_string	*get_string_literal(struct input_file *f);
int			get_operator(int ch, struct input_file *f, char **ascii);
struct num		*get_num_literal(int ch, struct input_file *f);
char			*get_identifier(int ch, struct input_file *f, int *slen, int *hash_key);
void			append_token_copy(struct token **d, struct token **d_tail, struct token *t);
#endif



void		rv_setrc_print(void *ptr, int type, int verbose);
void		token_setfile(char *file);
void		token_setfileid(int id);
struct token	*store_token(struct token **dest,
#ifdef PREPROCESSOR
		struct token **dest_tail,
#endif
		void *data, int type, int l, char *ascii);
void		destroy_toklist(struct token **dest);
int		next_token(struct token **tok);
int		expect_token(struct token **tok, int value, int skip);
int		store_char(char **, int);
int		store_string(char **, char *);
void		put_ppc_llong(struct num *n);
struct token	*make_bitfield_mask(struct type *ty, int for_reading, int *shiftbits,
			struct token **invtok);

void            free_token_list(struct token *); /* whoops destroy_toklist()!? */
void            output_token_list(FILE *, struct token *);
char            *toklist_to_string(struct token *);
void            dump_toklist(struct token *);


#endif
