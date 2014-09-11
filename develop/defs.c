/*
 * Copyright (c) 2003 - 2010, Nils R. Weller
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
 * This module contains a keyword and an operator table and two
 * functions to initialize the LOOKUP_* macros exported by defs.h
 */

#include "defs.h"
#include <string.h>
#include <assert.h>
#include "token.h"
#include "debug.h"
#include "n_libc.h"


char key_lookup[256];


struct keyword	*key_hash[1024][9];

int
hash_keyword(const char *name) {
	int	key = 0;

	for (; *name != 0; ++name) {
		key = (33 * key + *name) & 1023;
	}
	return key;
}	

/*
 * Initializes lookup table for keywords. Note that you must extend
 * key_chars appropriately any time a new keyword is added that is not
 * yet covered by those characters
 */
void
init_keylookup(void) {
	int	i;
	int	j;

	for (i = 1; keywords[i + 1].name != NULL; ++i) {
		int	key = hash_keyword(keywords[i].name);

		for (j = 0; j < 8; ++j) {
			if (key_hash[key][j] == NULL) {
				break;
			}
		}
		assert(j < 8);
		key_hash[key][j] = &keywords[i];
	}
#if 0
	static char key_chars[] = {
		'c', 'd', 'e', 'f', 'i', 'v', 'r', 's', 
		'u', 'l', 'a', 'w', 'b', 'g', 't', '_',
		0
	};
	unsigned int	i;
	unsigned int	j;

	memset(key_lookup, 0, sizeof key_lookup);
	for (i = 0; i < sizeof key_chars; ++i) {
		for (j = 0; keywords[j].name != NULL; ++j) {
			if (keywords[j].name[0] == key_chars[i]) {
				key_lookup[ (unsigned)key_chars[i] ] = j;
				break;
			}
		}
	}
#endif
}
				

/*
 * NOTE: These keywords are ordered by their first character to make
 * lookup faster.
 * A LOOKUP_KEY macro in defs.h returns the index of the first keyword
 * with a matching character in the table. init_keylookup() must be
 * called before that can be used
 */
struct keyword keywords[] = {
	{ "!dummy!",	0, 0, 0   },
	{ "case",	TOK_KEY_CASE,		0,	C89 },
	{ "char",	TOK_KEY_CHAR,		TY_CHAR, C89 },
	{ "const",	TOK_KEY_CONST,		0,	C89 },
	{ "continue",	TOK_KEY_CONTINUE,	0,	C89 },
	{ "do",		TOK_KEY_DO,		0,	C89 },
	{ "double",	TOK_KEY_DOUBLE,		TY_DOUBLE, C89 },
	{ "default",	TOK_KEY_DEFAULT,	0,	C89 },
	{ "extern",	TOK_KEY_EXTERN,		0,	C89 }, 
	{ "enum",	TOK_KEY_ENUM, 		0,	C89 },
	{ "else",	TOK_KEY_ELSE,		0,	C89 },
	{ "float",	TOK_KEY_FLOAT,		TY_FLOAT, C89 },
	{ "for",	TOK_KEY_FOR,		0,	C89 },
	{ "inline",	TOK_KEY_INLINE,		0,	C99 },
	{ "int",	TOK_KEY_INT,		TY_INT,	 C89 },
	{ "if",		TOK_KEY_IF,		0,	C89 },
	{ "void",	TOK_KEY_VOID, 		TY_VOID, C89 },
	{ "volatile",	TOK_KEY_VOLATILE,	0,	C89 },
	{ "register",	TOK_KEY_REGISTER,	0, 	C89 },
	{ "restrict",	TOK_KEY_RESTRICT,	0,	C99 },
	{ "return",	TOK_KEY_RETURN,		0,	C89 },
	{ "short",	TOK_KEY_SHORT,		TY_SHORT, C89	},
	{ "static",	TOK_KEY_STATIC,		0,	C89 },
	{ "signed",	TOK_KEY_SIGNED,		0,	C89 },
	{ "switch",	TOK_KEY_SWITCH,		0,	C89 },
	{ "struct",	TOK_KEY_STRUCT,		0,	C89 },
	{ "sizeof",	TOK_KEY_SIZEOF,		0,	C89 },
	{ "unsigned",	TOK_KEY_UNSIGNED,	0,	C89 },
	{ "union",	TOK_KEY_UNION,		0,	C89 },
	{ "long",	TOK_KEY_LONG,		TY_LONG, C89 },
	{ "auto",	TOK_KEY_AUTO,		0,	C89 },
	/* 07/21/08: asm() was missing! XXX Watch out for name clashes */
	{ "asm",	TOK_KEY_ASM,		0,	C89 },
	{ "while",	TOK_KEY_WHILE,		0,	C89 },
	{ "break",	TOK_KEY_BREAK,		0,	C89 },
	{ "goto",	TOK_KEY_GOTO,		0,	C89 },
	{ "typedef",	TOK_KEY_TYPEDEF,	0,	C89 },
	{ "typeof",	TOK_KEY_TYPEOF,		0,	C89 },
	{ "__asm__",	TOK_KEY_ASM,		0,	C89 },
	{ "__asm",	TOK_KEY_ASM,		0,	C89 },
	{ "__attribute__", TOK_KEY_ATTRIBUTE, 0,	C89 },
	{ "__attribute", TOK_KEY_ATTRIBUTE, 0,	C89 },
	/* 
	 * The following are for GNU C compatibility
	 */
	{ "__restrict__", TOK_KEY_RESTRICT, 0,	C99 },
	{ "__restrict", TOK_KEY_RESTRICT, 0, C99 },
	{ "__extension__", TOK_KEY_EXTENSION, 0, 0 },
	{ "__extension", TOK_KEY_EXTENSION, 0, 0 },
	{ "__const", TOK_KEY_CONST, 0, C89 },
	{ "__signed", TOK_KEY_SIGNED, 0, C89 },
	{ "__signed__", TOK_KEY_SIGNED, 0, C89 },
	{ "__thread", TOK_KEY_THREAD, 0, C99 }, /* not really */
	{ "__typeof__",	TOK_KEY_TYPEOF,		0,	C89 },
	{ "__typeof",	TOK_KEY_TYPEOF,		0,	C89 },
	{ "__alignof__", TOK_KEY_ALIGNOF,	0,	C89 },
	{ "__alignof", TOK_KEY_ALIGNOF,		0,	C89 },
	{ "__volatile__", TOK_KEY_VOLATILE, 0, C89 },
	{ "__volatile", TOK_KEY_VOLATILE, 0, C89 },
	{ "__inline", TOK_KEY_INLINE, 0, C99 },
	{ "__inline__", TOK_KEY_INLINE, 0, C99 },
	{ "_Bool",	TOK_KEY_BOOL,	0,	C99 },
#if 0
	/* Perhaps later .... */
	{ "_Complex",	TOK_KEY_COMPLEX,	0,	C99 },
	{ "_Imaginary",	TOK_KEY_IMAGINARY,	0,	C99 },
#endif

	/*
	 * This last element is used, because one otherwise would have
	 * to check for operators[x].name != NULL when traversing the
	 * list. If a last entry with a different first character is
	 * present, the test is eliminated and one can do
	 * while (operators[x].name[0] == first_char) { ... ++x; }
	 */
	{ "ZORG", 0, 0,	0 },
	{ NULL, 0, 0, 0	}
};


#if 0
static int
hash_opname(const char *name) {
	int	key = 0;

	while (*name) {
		key = *name + key; 
		++name;
	}
	return key & 127;
}	
#endif

/*
 * Keyword lookup function that maps the ``name'' argument to its
 * respective index in the keyword table on success, else -1.
 * This should be relatively fast now...
 */
struct keyword *
lookup_key(const char *name) {
	int	hashkey = hash_keyword(name);
	int	i;

	for (i = 0;; ++i) {
		if (key_hash[hashkey][i] == NULL) {
			return NULL;
		} else if (strcmp(key_hash[hashkey][i]->name, name) == 0) {
			return key_hash[hashkey][i];
		}
	}
	return NULL;

#if 0
	int		i;
	char	c = *name;

	if ((i = LOOKUP_KEY((unsigned)c)) == 0) {
		return -1;
	}
	for (; keywords[i].name[0] == c; ++i) {
		if (strcmp(keywords[i].name, name) == 0) {
			return i;
		}
	}
	return -1;
#endif

}



/*
 * Refer to the keywords table comment - This one works the same except
 * that LOOKUP_OP() and init_oplookup() are used 
 */
struct operator operators[] = {
	{ "dummy", 0, 0, 0, 0, 0, 0 },
	/* unary minus */
	{ "-", TOK_OP_UMINUS, TOK_OP_AMB_MINUS,
		OP_CLASS_PRE, 15, OP_ASSOC_RIGHT, 0 },
	/* compound minus assignment */
	{ "-=",	TOK_OP_COMINUS, 0,
		OP_CLASS_BIN, 2, OP_ASSOC_RIGHT, 0 },
	/* prefix decrement */
	{ "--",	TOK_OP_DECPRE, TOK_OP_AMB_DECR, 
		OP_CLASS_PRE, 15, OP_ASSOC_RIGHT, 0 },
	/* access s. / u. m. through pointer */
	{ "->",	TOK_OP_STRUPMEMB, 0,
		OP_CLASS_POST, 16, OP_ASSOC_LEFT, 0 },
	/* substract */
	{ "-", TOK_OP_MINUS, TOK_OP_AMB_MINUS,
		OP_CLASS_BIN, 12, OP_ASSOC_LEFT, 0 },
	/* postfix decrement */
	{ "--",	TOK_OP_DECPOST, TOK_OP_AMB_DECR,
		OP_CLASS_POST, 16, OP_ASSOC_LEFT, 0 },
	/* unary plus */
	{ "+", TOK_OP_UPLUS, TOK_OP_AMB_PLUS,
		OP_CLASS_PRE, 15, OP_ASSOC_RIGHT, 0 },
	/* prefix increment */
	{ "++",	TOK_OP_INCPRE, TOK_OP_AMB_INCR,
		OP_CLASS_PRE, 15, OP_ASSOC_RIGHT, 0 },
	/* add */
	{ "+", TOK_OP_PLUS, TOK_OP_AMB_PLUS,
		OP_CLASS_BIN, 12, OP_ASSOC_LEFT, 0 },
	/* compound plus assignemnt */
	{ "+=", TOK_OP_COPLUS, 0,
		OP_CLASS_BIN, 2, OP_ASSOC_RIGHT, 0 },
	/* postfix increment */
	{ "++", TOK_OP_INCPOST, TOK_OP_AMB_INCR,
		OP_CLASS_POST, 16, OP_ASSOC_LEFT, 0 },
	/* divide */
	{ "/", TOK_OP_DIVIDE, 0,
		OP_CLASS_BIN, 13, OP_ASSOC_LEFT, 0 },
	/* compound divide assignment */
	{ "/=", TOK_OP_CODIVIDE, 0,
		OP_CLASS_BIN, 2, OP_ASSOC_RIGHT, 0 },
	/* multiply */
	{ "*", TOK_OP_MULTI, TOK_OP_AMB_MULTI,
		OP_CLASS_BIN, 13, OP_ASSOC_LEFT, 0 },
	/* compound multiply assignment */
	{ "*=", TOK_OP_COMULTI, 0,
		OP_CLASS_BIN, 2, OP_ASSOC_RIGHT, 0 },
	/* dereference pointer */
	{ "*", TOK_OP_DEREF, /*TOK_OP_AMB_BAND???????*/TOK_OP_AMB_MULTI,
		OP_CLASS_PRE, 15, OP_ASSOC_RIGHT, 0 },
	/* modulo */
	{ "%", TOK_OP_MOD, 0,
		OP_CLASS_BIN, 13, OP_ASSOC_LEFT, 0 },
	/* compound modulo assignment */
	{ "%=", TOK_OP_COMOD, 0,
		OP_CLASS_BIN , 2, OP_ASSOC_RIGHT, 0 },
	/* bitwise and */
	{ "&", TOK_OP_BAND, TOK_OP_AMB_BAND,
		OP_CLASS_BIN, 8, OP_ASSOC_LEFT, 0 },
	/* compound bitwise and assignment */
	{ "&=", TOK_OP_COBAND, 0,
		OP_CLASS_BIN, 2, OP_ASSOC_RIGHT, 0 },
	/* logical and */
	{ "&&", TOK_OP_LAND, 0,
		OP_CLASS_BIN, 5, OP_ASSOC_LEFT, 1 },
	/* address-of */
	{ "&", TOK_OP_ADDR, TOK_OP_AMB_BAND,
		OP_CLASS_BIN, 15, OP_ASSOC_RIGHT, 0 },
	/* bitwise or */
	{ "|", TOK_OP_BOR, 0,
		OP_CLASS_BIN, 6, OP_ASSOC_LEFT, 0 },
	/* compound bitwise or assignment */
	{ "|=", TOK_OP_COBOR, 0,
		OP_CLASS_BIN, 2, OP_ASSOC_RIGHT, 0 },
	/* logical or */
	{ "||", TOK_OP_LOR, 0,
		OP_CLASS_BIN, 4, OP_ASSOC_LEFT, 1 },
	/* bitwise xor */
	{ "^", TOK_OP_BXOR, 0,
		OP_CLASS_BIN, 7, OP_ASSOC_LEFT, 0 },
	/* compound bitwise xor assignment */
	{ "^=", TOK_OP_COBXOR, 0,
		OP_CLASS_BIN, 2, OP_ASSOC_RIGHT, 0 },
	/* bitwise negation */
	{ "~", TOK_OP_BNEG, 0,
		OP_CLASS_PRE, 15, OP_ASSOC_RIGHT, 0 },
	/* smaller than */
	{ "<", TOK_OP_SMALL, 0,
		OP_CLASS_BIN, 10, OP_ASSOC_LEFT, 0 },
	/* smaller than or equal */
	{ "<=", TOK_OP_SMALLEQ, 0,
		OP_CLASS_BIN, 10, OP_ASSOC_LEFT, 0 }, 
	/* bitwise shift left */
	{ "<<", TOK_OP_BSHL, 0,
		OP_CLASS_BIN, 11, OP_ASSOC_LEFT, 0 },
	/* compound bitwise shift left assignment */
	{ "<<=", TOK_OP_COBSHL, 0,
		OP_CLASS_BIN, 2, OP_ASSOC_RIGHT, 0 },
	/* greater than */
	{ ">", TOK_OP_GREAT, 0,
		OP_CLASS_BIN, 10, OP_ASSOC_LEFT, 0 },
	/* greater than or equal */
	{ ">=", TOK_OP_GREATEQ, 0,
		OP_CLASS_BIN, 10, OP_ASSOC_LEFT, 0 },
	/* bitwise shift right */
	{ ">>", TOK_OP_BSHR, 0,
		OP_CLASS_BIN, 11, OP_ASSOC_LEFT, 0 },
	/* compound bitwise shift right assignment */
	{ ">>=", TOK_OP_COBSHR, 0,
		OP_CLASS_BIN, 2, OP_ASSOC_RIGHT, 0 },
	/* assignment */
	{ "=", TOK_OP_ASSIGN, 0,
		OP_CLASS_BIN, 2, OP_ASSOC_RIGHT, 0 },
	/* logical equality */
	{ "==", TOK_OP_LEQU, 0,
		OP_CLASS_BIN, 9, OP_ASSOC_LEFT, 0 },
	/* logical negation */
	{ "!", TOK_OP_LNEG, 0,
		OP_CLASS_PRE, 15, OP_ASSOC_RIGHT, 0 },
	/* logical inequality */
	{ "!=", TOK_OP_LNEQU, 0,
		OP_CLASS_BIN, 9, OP_ASSOC_LEFT, 0 },
	/* access structure / union member */
	{ ".", TOK_OP_STRUMEMB, 0,
		OP_CLASS_POST, 16, OP_ASSOC_LEFT, 0 },
	/* comma operator */
	{ ",", TOK_OP_COMMA, 0,
		OP_CLASS_BIN, 1, OP_ASSOC_LEFT, 1 },
	/* conditional operator 1 */
	{ "?", TOK_OP_COND, 0,
		OP_CLASS_TER, 3, OP_ASSOC_RIGHT, 1 },
	/* conditional operator 2 */
	{ ":", TOK_OP_COND2,
		TOK_OP_AMB_COND2, OP_CLASS_TER, 3,
		OP_ASSOC_RIGHT, 0 },
	{ "ZORG", 0, 0, 0, 0, 0, 0 },
	{ NULL, 0, 0, 0, 0, 0, 0 }
};


char op_lookup[256];
char op_lookup2[256];

		
/*
 * Initializes operator lookup table. Refer to init_keylookup()
 */
void
init_oplookup(void) {
	static char op_chars[] = {
		'-', '+', '/', '*', '%', '&', '|', '^', 
		'~', '<', '>', '=', '!', '.', ',', '?',
		':', 0
	};
	unsigned int	i;
	unsigned int	j;

	memset(op_lookup, 0, sizeof op_lookup);
	for (i = 0; i < sizeof op_chars; ++i) {
		for (j = 0; operators[j].name != NULL; ++j) {
			if (operators[j].name[0] == op_chars[i]) {
				op_lookup[ (unsigned)op_chars[i] ] = j;
				break;
			}
		}
	}

	memset(op_lookup2, 0, sizeof op_lookup2);
	for (i = 0; i <= TOK_OP_MAX - TOK_OP_MIN; ++i) {
		for (j  = 0; operators[j].name != NULL; ++j) {
			if ((unsigned)operators[j].value == (i + TOK_OP_MIN)) {
				op_lookup2[i] = j; 
				break;
			}
		}
	}
}

#if 0
/*
 * Maps operator in ascii specified by ``name'' to according index in
 * operator table. Returns index on success, else -1
 */
int
lookup_op(const char *name) {
	int	key = hash_opname(operators[i].name);
	int	i;

	for (i = 0;; ++i) {
		if (op_hash[key][i] == NULL) {
			return -1;
		} else if (strcmp(op_hash[key][i]->name, name) == 0) {
			return 
	
#if 0
	int		i;
	char	c = *name;

	if ((i = LOOKUP_OP((unsigned)c)) == 0) {
		return -1;
	}

	for (; operators[i].name[i] == c; ++i) {
		if (strcmp(operators[i].name, name) == 0) {
			return i;
		}
	}
#endif
	return -1;
}
#endif

/*
 * Maps numeric operator code to operator name in ascii and returns a
 * pointer to that on success, else NULL
 */
char *
lookup_operator(int value) {
	int	i;
	for (i = 0; operators[i].name != NULL; ++i) {
		if (value == operators[i].value ||
			value == operators[i].is_ambig) {
			return operators[i].name;
		}
	}
	return NULL;
}
 
 /*
  * Gets index for operator ``value'' in operator table - using the
  * first character of its ascii representation as starting point to
  * search
  */
int
get_opind_by_value(char firstch, int value) {
	int	i;

	if ((i = LOOKUP_OP((unsigned)firstch)) == 0) {
		return -1;
	}
	do {
		if (operators[i].value == value) {
			return i;
		}
	} while (operators[++i].name[0] == firstch);
	return -1;
}

