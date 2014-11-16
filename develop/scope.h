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
#ifndef SCOPE_H
#define SCOPE_H

struct decl;
struct token;
struct ty_struct;
struct ty_enum;
struct sym_entry;
struct statement;

/*
 * The next member is only used for static variables;
 * global_scope.static_decls is a list of all static variable
 * declarations in the entire program
 */
struct dec_block {
	struct decl	**data;
	int		ndecls;
	int		nslots;
};


extern struct decl	*static_init_vars;
extern struct decl	*siv_tail;
extern struct decl	*static_uninit_vars;
extern struct decl	*siuv_tail;

/* 02/02/08: TLS-variables */
extern struct decl	*static_init_thread_vars;
extern struct decl	*sitv_tail;
extern struct decl	*static_uninit_thread_vars;
extern struct decl	*siutv_tail;

extern struct decl	*siv_checkpoint;

struct hash_node {
	void			*item;
	struct hash_node	*next;
	struct hash_node	*skip;
};	

struct sym_hash_table {
	struct sym_entry	**hash_slots_head;
	struct sym_entry	**hash_slots_tail;
	int			n_hash_slots; /* must be power of 2 */
#define SYM_HTAB_GLOBAL_SCOPE	1024 /* many symbols are in global scope */
#define SYM_HTAB_FUNC_TOP_BLOCK	128 /* some are in the function block */
#define SYM_HTAB_BLOCK		32 /* not that many in local block */
	int			used;
};


struct scope {
	int		type;
	struct scope	*parent;
	unsigned long	scopeno;

	/*
	 * 05/13/09: Flag to warn about mixed declarations and code
	 * (though declarations are called ST[atement]_DECL in nwcc,
	 * they are usually not considered ``statements'')
	 */
	int		have_stmt;
	
	/* Structure/union definitions */
	struct sd {
		struct ty_struct	*head;
		struct ty_struct	*tail;
		int			ndecls;
		int			nslots;
	}	struct_defs;

	/* Enum definitions */
	struct ed {
		struct ty_enum		**data;
		int			ndecls;
		int			nslots;
	}	enum_defs;

	/* Function declarations */
	struct {
		struct ty_func	*data;
		int		ndecls;
		int		nslots;
	}	function_decls;

	/* Automatic declarations */
	struct dec_block	automatic_decls;

	/* Register declarations */
	struct dec_block	register_decls;

	/* Static declarations */
	struct dec_block	static_decls;

	/* Extern declarations */
	struct dec_block	extern_decls;

	/* Typedefs */
	struct dec_block	typedef_decls;

	/* Symbol list for all declarations */
	struct sym_entry	*slist;
	struct sym_entry	*slist_tail;
#if 0
	struct hash_node	**sym_hash;
	struct hash_node	**typedef_hash;
	int			n_hash_slots;
	int			n_typedef_slots;
#endif
	struct sym_hash_table	sym_hash;
	struct sym_hash_table	typedef_hash;

	struct statement	*code;
	struct statement	*code_tail;
	struct scope		*next;
};

extern struct scope	*curscope;
extern struct scope global_scope;
extern struct sym_entry	*extern_vars;


#define SCOPE_NESTED	1

struct ty_struct *
lookup_struct(struct scope *s, const char *tag, int nested);

struct ty_enum *
lookup_enum(struct scope *s, const char *tag, int nested);

struct ty_enum *
create_enum_forward_declaration(const char *tag);

#define LTD_IGNORE_IDENT	1

struct type *
lookup_typedef(struct scope *s, const char *name, int nested, int flags);

void
complete_type(struct ty_struct *dest, struct ty_struct *src);


void
store_def_scope(
	struct scope *s, 
	struct ty_struct *ts, 
	struct ty_enum *te, 
	struct token *tok);

void
store_decl_scope(struct scope *s, struct decl **d);

#define SCOPE_STRUCT 1
#define SCOPE_CODE 2

struct scope *
new_scope(int type);

void
close_scope(void);

struct decl *
lookup_symbol(struct scope *s, const char *name, int nested);

struct sym_entry *
lookup_symbol_se(struct scope *s, const char *name, int nested);

struct decl *
access_symbol(struct scope *s, const char *name, int nested);

void
really_accessed(struct decl *d);

struct decl *
put_implicit(const char *name);

void
stmt_list_append(struct scope *s, struct statement *stmt);

void
update_array_size(struct decl *d, struct type *ty);

/*
 * 04/08/08: New function which tells us whether the declaration
 * is local and extern, but overriden by some outer definition
 */
int
is_shadow_decl(struct decl *d);

#endif /* #ifndef SCOPE_H */

