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
#ifndef BUILTINS_H
#define BUILTINS_H

#include <stddef.h>

struct fcall_data;
struct token;
struct icode_list;
struct vreg;
struct type;

extern struct type	*builtin_va_list_type;

typedef int 
(*parse_builtin_func_t)(struct token **t, struct fcall_data *fdat);

typedef struct vreg * 
(*builtin_to_icode_func_t)(struct fcall_data *fdat, struct icode_list *il, int eval);


struct builtin {
	char			*name;
	size_t			namelen;
	int			type;
	parse_builtin_func_t	parse;
	builtin_to_icode_func_t	toicode;
};	

struct builtin_data {
	struct builtin	*builtin;
#define BUILTIN_EXPECT 		1
#define BUILTIN_OFFSETOF	2
#define BUILTIN_VA_START	3
#define BUILTIN_STDARG_START	4
#define BUILTIN_NEXT_ARG	5
#define BUILTIN_VA_END		6
#define BUILTIN_VA_ARG		7
#define BUILTIN_VA_COPY		8
#define BUILTIN_ALLOCA		9
#define BUILTIN_MEMCPY		10
#define BUILTIN_MEMSET		11
#define BUILTIN_FRAME_ADDRESS	12
#define BUILTIN_CONSTANT_P	13
	int		type; /* optional */
	void		*args[5];
};

struct fcall_data	*get_builtin(struct token **tok, struct token *name);
int			builtin_to_be_renamed(const char *name);

#endif

