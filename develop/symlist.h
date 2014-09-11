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
#ifndef SYMLIST_H
#define SYMLIST_H

#include <stddef.h>
#include "features.h"

struct decl;
struct ty_func;
struct scope;

struct sym_entry {
	const char		*name;
	size_t			namelen;
	int			inactive;
	int			has_initializer;
	struct decl		*dec;
	struct sym_entry	*next;
	struct sym_entry	*prev;
};

void
append_symlist(
	struct scope *s,	
	struct sym_entry **head,
	struct sym_entry **tail,
	struct decl *dec);

struct sym_entry	*make_sym_entry(struct decl *);

struct hash_node;
void	put_hash_table(struct hash_node **, size_t, struct sym_entry *);
struct sym_entry *
lookup_hash(struct hash_node **, size_t, const char *, size_t);

struct sym_hash_table;
struct sym_hash;

void	new_make_hash_table(struct sym_hash_table *tab, int size);
void	new_put_hash_table(struct sym_hash_table *tab, struct sym_entry *);
struct sym_entry	*new_lookup_hash(struct sym_hash_table *, const char *,
		size_t len);

void	remove_symlist(struct scope *s, struct sym_entry *se);

#if FAST_SYMBOL_LOOKUP

void	put_fast_sym_hash(struct scope *s, struct sym_entry *se, int is_typedef);
void	reset_fast_sym_hash(void);

struct fast_sym_hash_entry {
	struct sym_entry        	*se;
	char                   		*name;
	int                  	 	namelen;
	int				is_typedef;
	struct scope            	*scope;
	struct fast_sym_hash_entry	*next;
};
struct sym_entry	*fast_lookup_symbol_se(struct scope *s, const char *name,
				int nested, int want_typedef);

#endif

#endif
	
