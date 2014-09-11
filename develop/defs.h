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
 */
#ifndef DEFS_H
#define DEFS_H

#define NWCC_VERSION "0.8.3"

#include "token.h"

struct keyword {
	char		*name;	/* ASCII representation */
	int		value;	/* Numeric value */
	int		typeval; /* If data type, this is its value */
	enum standard	std;	/* Standard defining operator */
};

#define OP_CLASS_PRE	1
#define OP_CLASS_POST	2
#define OP_CLASS_BIN	3	
#define OP_CLASS_TER	4	

#define IS_OP_CLASS(x) \
	((x) >= OP_CLASS_PRE && (x) <= OP_CLASS_TER)

#define OP_ASSOC_LEFT	1
#define OP_ASSOC_RIGHT	2

struct operator {
	char	*name;		/* ASCII representation */
	int	value;		/* Numeric value of operator */
	int	is_ambig;	/* is ambiguous - context required */
	int	class;		/* prefix, postfix, binary, ternary */
	int	prec;		/* Precedence */
	int	assoc;		/* Associativity */
	int	is_seqpt;	/* Is sequence point? */
};

extern struct keyword keywords[]; 
extern struct operator operators[];

#if 0
char *lookup_builtin(int type);
#endif
char *lookup_operator(int value);

extern char key_lookup[256];
extern char	op_lookup[256];
extern char	op_lookup2[256];

#define LOOKUP_KEY(c) ((int) key_lookup[(c)])
#define LOOKUP_OP(c) ((int) op_lookup[(c)])
#define LOOKUP_OP2(val) ((int) op_lookup2[(val - TOK_OP_MIN)])

/*int*/  struct keyword *lookup_key(const char *name);
#if 0
int lookup_op(const char *name);
#endif
int	get_opind_by_value(char firstch, int value);

void init_keylookup(void);
void init_oplookup(void);

#include "config.h"

#ifndef INSTALLDIR
/* XXX whoa this is confusing ;l  ... this is really the install PREFIX */
#define INSTALLDIR "/usr/local"
#endif

#endif

