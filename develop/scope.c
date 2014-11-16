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
 * Functions to create, populate and search scopes with and for
 * structure and enum definitions and variable declarations
 */ 
#include "scope.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "token.h"
#include "decl.h"
#include "misc.h"
#include "expr.h"
#include "features.h"

#if XLATE_IMMEDIATELY
#include "backend.h"
#endif

#include "error.h"
#include "type.h"
#include "symlist.h"
#include "functions.h"
#include "cc1_main.h"
#include "debug.h"
#include "n_libc.h"

struct scope global_scope = {
	0,
	NULL,
	0,
	0,
	{ NULL, NULL, 0, 0 },
	{ 0, 0, 0 },
	{ 0, 0, 0 },
	{ 0, 0, 0 },
	{ 0, 0, 0 },
	{ 0, 0, 0 },
	{ 0, 0, 0 },
	{ 0, 0, 0 },
	NULL, NULL,
	{ NULL, NULL, 0, 0 },
	{ NULL, NULL, 0, 0 },
	NULL, NULL, NULL
};
struct scope	*curscope;
static struct scope	*scopelist_tail = &global_scope;
struct decl		*static_init_vars;
struct decl		*siv_tail;
struct decl		*static_uninit_vars;
struct decl		*siuv_tail;

/* 02/02/08: TLS variables */
struct decl		*static_init_thread_vars;
struct decl		*sitv_tail;
struct decl		*static_uninit_thread_vars;
struct decl		*siutv_tail;

struct decl		*siv_checkpoint;

struct sym_entry	*extern_vars;
struct sym_entry	*extern_vars_tail;

/*
 * Looks up structure definition (not instance!) with tag ``tag''. If
 * ``nested'' is nonzero, the lookup will use scopes above the current
 * scope. If it's not, the lookup will only consider the current scope
 * On success, a pointer to the structure definition is returned, else a
 * null pointer
 */
struct ty_struct *
lookup_struct(struct scope *s, const char *tag, int nested) {
	do {
		struct ty_struct	*ts;

		for (ts = s->struct_defs.head; ts != NULL; ts = ts->next) {
			if (ts->tag != NULL) {
				if (strcmp(ts->tag, tag) == 0) {
					return ts;
				}
			} else if (ts->dummytag != NULL) {
				if (strcmp(ts->dummytag, tag) == 0) {
					return ts;
				}
			}	
		}
		if (nested == 0) {
			break;
		}
	} while ((s = s->parent) != NULL);

	return NULL;
}

struct ty_enum *
lookup_enum(struct scope *s, const char *tag, int nested) {
	int				i;
	char			*p;

	do {
		if (s->enum_defs.data) {
			for (i = 0; i < s->enum_defs.ndecls; ++i) {
				p = s->enum_defs.data[i]->tag;
				if (p != NULL && strcmp(p, tag) == 0) {
					return s->enum_defs.data[i];
				}
			}
		}
		if (nested == 0) {
			break;
		}
	} while ((s = s->parent) != NULL);
	return NULL;
}

struct type *
lookup_typedef(struct scope *s, const char *name, int nested, int flags) {
	struct decl	**d;
	int		i;
	size_t		namelen = strlen(name);

	do {
		/*
		 * 03/03/09: If there's a non-typedef of the same name
		 * in this scope, then it wins over the typedef. So if
		 * we have
		 *
		 *    typedef int foo;
		 *    {
		 *       int foo;
		 *       for (foo = 0; foo < 5; ++foo) {
		 *
		 * ... then the for loop shall only reference the inner
		 * declaration
		 * 
		 * 03/03/09: This must be overridable for cases like:
		 *
		 *    typedef int foo;
		 *    struct x {
		 *       int foo;
		 *       foo bar;
		 *    };
		 *
		 * ... otherwise the second declaration will not look
		 * up the typedef because the identifier wins.
		 */
		if ((flags & LTD_IGNORE_IDENT) == 0) {
			if (lookup_symbol(s, name, 0) != NULL) {
				/*
				 * This scope has a non-typedef declaration which
				 * wins over potential outside typedefs
				 */
				return NULL;
			}
		}

		if (s->typedef_hash.used) {
			struct sym_entry	*se;

#if 0
			se = lookup_hash(s->typedef_hash, s->n_typedef_slots,
				name, namelen);
#endif
			se = new_lookup_hash(&s->typedef_hash, name, namelen);
			if (se != NULL) {
				return se->dec->dtype;
			}	
		} else {	
#if FAST_SYMBOL_LOOKUP
			struct sym_entry	*se;

			se = fast_lookup_symbol_se(s, name, nested, 1);
			if (se != NULL) {
				return se->dec->dtype;
			} else {
				return NULL;
			}

#else
			if (s->typedef_decls.data) {
				d = s->typedef_decls.data;
				for (i = 0; i < s->typedef_decls.ndecls; ++i) {
					if (strcmp(d[i]->dtype->name, name)
						== 0) {
						return d[i]->dtype;	
					}
				}
			}
#endif
		}
		if (!nested) {
			break;
		}
	} while ((s = s->parent) != NULL);
	return NULL;
}

	
struct scope *
new_scope(int type) {
	struct scope	*ret;
	static struct scope	nullscope;
	static unsigned long	scopeno = 1; /* 0 = global scope */

#ifdef DEBUG2
	puts("opening new scope");
#endif
	ret = n_xmalloc(sizeof *ret);
	*ret = nullscope;

	ret->type = type;
	ret->parent = curscope;


	ret->scopeno = scopeno++;
	scopelist_tail->next = ret;
	scopelist_tail = ret;
	curscope = ret;

	return ret;
}

void
close_scope(void) {
#if defined(DEBUG) || defined(DEBUG2) || defined(DEBUG3)
	if ((curscope = curscope->parent) == NULL) {
		fprintf(stderr,
			"Fatal error: Attempting to close global scope\n");
		++errors; /* exit() handler checks this */
		abort();
	}
#else
	if (curscope->parent == NULL) {
		++errors;
		errorfl(NULL, "Attempt to close global scope!");
	} else {
		curscope = curscope->parent;
	}
#endif
}


static void
do_store_decl(struct scope *s, struct dec_block *destdec, struct decl *d) {
	struct statement	*st;
	
	if (destdec->ndecls >= destdec->nslots) {
		if (destdec->nslots == 0) {
			destdec->nslots = 8;
		}
		destdec->nslots *= 2;
		destdec->data = n_xrealloc(destdec->data,
			   	sizeof *destdec->data * destdec->nslots);
	}
	destdec->data[ destdec->ndecls++ ] = d;

	if (s != NULL) {
		st = alloc_statement();
		st->type = ST_DECL;
		st->data = d;
		append_statement(&s->code, &s->code_tail, st);
	}	
}

void
update_array_size(struct decl *d, struct type *ty) {
	if (ty->tlist != NULL && ty->tlist->type == TN_ARRAY_OF) {
		/* May have to update array information */
#if REMOVE_ARRARG
		if (!d->dtype->tlist->have_array_size) {
			d->dtype->tlist->arrarg_const = ty->tlist->arrarg_const;
			d->dtype->tlist->have_array_size = 1;
		}
#else
		if (d->dtype->tlist->arrarg->const_value == NULL) {
			d->dtype->tlist->arrarg->const_value
				ty->tlist->arrarg->const_value;
		}
#endif
	}
}


static void
remove_old_declaration(struct scope *s, struct sym_entry *se, struct decl *d, struct decl *newdec) {
	/*
	 * 03/29/08: Also remove the declaration if it is
	 * first extern, then not extern (so it suddenly
	 * requires a definition)
	 */
	se->dec->invalid = 1;

	/*
	 * 03/30/08: Save reference count (code may already
	 * have accessed the previous declaration)
	 */
	newdec->references = d->references;
	newdec->real_references = d->real_references; /* 12/24/08 */
	remove_symlist(s, se);
}


/*
 * 20141114: Quickfix hack to communicate to caller of check_redef that an inline declaration was dropped,
 * so that a new declaration needs to be stored in scope.
 * XXX All of this symbol management is insanely incomprehensible
 */
static int dropped_inline_decl;


static int
check_redef(struct scope *s, struct dec_block *destdec, struct decl *newdec, int *need_def) {
	struct decl		*d;
	struct type		*ty = newdec->dtype;
	struct sym_entry	*se;

	dropped_inline_decl = 0;
#if 0
	if ((d = lookup_symbol(s, ty->name, 0)) == NULL) {
		return 0;
	}
#endif
	if ((se = lookup_symbol_se(s, ty->name, 0)) == NULL) {
		return 0;
	}
	d = se->dec;
	
	/*
	 * 04/02/08: Also do redeclaration checking for local extern
	 * declarations and local function declarations instead of
	 * erroring for those immediately (GNU code loves to
	 * redeclare local function declarations)
	 */
	if (s == &global_scope
		|| newdec->dtype->storage == TOK_KEY_EXTERN
		|| newdec->dtype->is_func) {
		int	compres;
		int	is_extern_inline = 0;
		int	is_static_inline = 0;

		/*
		 * Declarations at file scope are tentative. They
		 * might be repeated as often as desired, as long
		 * as none of them differs
		 */
		if ((compres = compare_types(d->dtype, ty,
			CMPTY_SIGN|CMPTY_CONST|CMPTY_TENTDEC)) == -1
			|| (d->dtype->storage
				&& ty->storage
				&& d->dtype->storage != ty->storage)
			/*
			 * 05/25/09: The case below covers
			 *
			 *    static void foo();
			 *    void foo() {}
			 *
			 * ... which would otherwise not end up
			 * here because the storage flag of the
			 * redeclaration is 0 (for now)
			 */
			|| (d->dtype->storage == TOK_KEY_STATIC
				&& ty->storage == 0
				&& newdec->dtype->is_func)) {

			/*
			 * 09/30/07: gcc and tinycc allow arbitrary
			 * redeclarations of static/extern storage
			 * specifiers, e.g.
			 *
			 *     static int foo;
			 *     extern int foo;
			 *
			 * BitchX and probably other programs have
			 * come to depend on this, so we must support
			 * it too
			 *
			 * 03/29/08: Careful here, we only pay close
			 * attention to the redeclaration if we don't
			 * already have an initializer for the variable!
			 * Because if we have
			 *
			 *    static int foo = 123;
			 *    extern int foo;
			 *
			 * ... then the definition of ``foo'' will be
			 * name-mangled to something like ``_Static_foo0''
			 * and subsequent declarations must also use that
			 * name. So we cannot allow the extern declaration
			 * to override the name (externs are not mangled)
			 */
			if (compres != -1) {
				/*
				 * Yes, storage is only difference
				 */
				if (IS_INLINE(d->dtype->flags)) {
					/*
					 * 20141114: For inline functions, we need to
					 * remove the preceding declaration.
					 *
					 * Otherwise, in
					 *   inline void foo();
					 *   inline void foo() {}
					 * ... references (by function calls) to foo
					 *  may be recorded for the first decl. This
					 * will cause the function definition to be
					 * suppressed and result in linker errors
				 	 * (see is_deferrable_inline_function())
				 	 *
					 * This fixes compiler errors for some GNU
					 * programs (m4, tar), but it's not
					 * guaranteed that it is correct in all
					 * possible cases.
					 */
					dropped_inline_decl = 1;
					remove_old_declaration(s, se, d, newdec);
				}

				/*
				 *
				 * XXX symbol management is almost
				 * certainly wrong, and will cause
				 * problems for nasm, because the
				 * ``has_def'' stuff below is not
				 * handled
				 *
				 * 05/25/09: We're going from static to
				 * extern in this code path. This is now
				 * disallowed for functions! Even in
				 *
				 *   static void foo();
				 *   extern void foo() {}
				 *
				 * ... foo() remains static. The gcc
				 * source depends on this.
				 *
				 * XXX What about non-function symbols?
				 * Those just give an error later on. We
				 * should sort this mess out
				 */
				if (!newdec->dtype->is_func) {
					warningfl(d->tok, "Redeclaration of "
						"`%s' with different storage "
						"class, this may cause problems "
						"with some assemblers",
						ty->name);
					if (d->init == NULL) {
						d->dtype->storage = ty->storage;
					}
				} else {
					/*
					 * 05/25/09: Static function
					 * redeclared as extern. We have to
					 * kludge the new type to be static
					 * as well, or we will still generate
					 * a global symbol.
					 *
					 * XXX Shouldn't the caller take over
					 * the old type?
					 */
					newdec->dtype->storage = TOK_KEY_STATIC;
					return 1;
				}
			} else {
				errorfl(d->tok,
					"Redefinition of `%s' with "
					"conflicting type",
					ty->name);
				return -1;
			}

			if (d->init == NULL) {
				se->dec->invalid = 1;
				/*
				 * 03/30/08: Save reference count (code may already
				 * have accessed the previous declaration)
				 */
				newdec->references = d->references;
				newdec->real_references = d->real_references; /* 12/24/08 */
				remove_symlist(s, se);
				return 0;
			} else {
				*need_def = 0;
				return 1;
			}
		}
		
		if (d->dtype->is_func) {
			/*
			 * Might have to replace old function entry
			 * with new one if this is a definition
			 * and the old one is a declaration. In
			 * either case, one can be deallocated
			 */
			if (d->dtype->is_def && ty->is_def) {
				/*if (IS_INLINE(ty->flags) && ty->storage == TOK_KEY_STATIC) {
				 	...discard declaration AND definition, if any ...
					is_extern_inline = 1;
				} else*/ {
					errorfl(d->tok,
						"Multiple definitions of function `%s'",
						ty->name);
				}
			} else if (ty->is_def) {
				if (d->dtype->tlist->tfunc->nargs == -1) {
					ty->tlist->tfunc->was_just_declared = 1;
				}	

				d->dtype = ty;
			}
			if (IS_INLINE(ty->flags) && ty->storage == TOK_KEY_EXTERN) {
				/*
				 * 12/25/08: If we have an extern declaration
				 * which is later redeclared as ``extern
				 * inline'', the first declaration must be
				 * invalidated to make the function local to
				 * the module
				 */
				is_extern_inline = 1;
			} else if (IS_INLINE(ty->flags) && ty->storage == TOK_KEY_STATIC) {
				/*
				 * 03/01/09: Handle static inline too
				 */
				is_static_inline = 1;
			}
		}

		*need_def = 0;
		if (ty->storage != TOK_KEY_EXTERN && !ty->is_func) {
			if (!d->has_def) {
				/*
				 * This is a non-extern tentative object
				 * declaration, so we need one definition
				 * somewhere in the translation unit
				 */
				*need_def = 1;
			}	
		}

		update_array_size(d, ty);

		if ( (d->dtype->storage == TOK_KEY_EXTERN
			&& !d->was_not_extern	
			&& newdec->dtype->storage != TOK_KEY_EXTERN
			&& newdec->init == NULL
			&& d->init == NULL /* for extern init */)
			
			|| is_extern_inline
			|| is_static_inline) {

			remove_old_declaration(s, se, d, newdec);
			return 0;
		}

		if (newdec->init != NULL) {
			/*
			 * 03/21/08: The new declaration has an initializer,
			 * so that must be used or rejected if we already
			 * have one
			 *    int stuff, stuff = 123, stuff, stuff; // OK
			 *    int stuff = 123, stuff = 123;  // BAD
			 */
			if (d->init != NULL) {
				errorfl(d->tok,
					"Redefinition of `%s' with "
					"different initializer",
					ty->name);
				return -1;
			}

			/*
			 * OUCH... We now have to relocate this variable
			 * from the list of uninitialized to the one
			 * for initialized variables. So let's just
			 * remove the old declaration and pretend it
			 * didn't exist (so the caller adds it)
			 */
			se->dec->invalid = 1;
			/*
			 * 03/30/08: Save reference count (code may already
			 * have accessed the previous declaration)
			 */
			newdec->references = d->references;
			newdec->real_references = d->real_references; /* 12/24/08 */
			remove_symlist(s, se);

			return 0;
		}

		return 1; /* already has entry */
	} else {
		struct token	*ttmp;
		ttmp = errorfl_mk_tok(
			ty->file, ty->line, NULL);

		errorfl(ttmp,
			"Multiple definitions of `%s'", ty->name);
		return -1;
	}

	/* NOTREACHED */
	return 0;
}


/*
 * The stuff below handles a particularly ugly part of the C language;
 * local external declarations, particularly for functions, are
 * permitted! This sucks big time because some assemblers, such as
 * nasm, always require an explicit symbol declaration to get access
 * to an external object. This in turn means duplicate symbols will
 * cause problems, such that we have to check for them. On a per-
 * scope basis, multiple declarations have already been handled
 * neatly for some time. However, e.g. local declarations overriden by
 * subsequent global definitions in the same module, or just
 * subsequent declarations, caused errors.
 *
 * 04/08/08: Reactivated and completed this
 */
struct local_extern_decl {
	struct decl			*dec;
	struct local_extern_decl	*next;
};

#define LED_TABSIZE	32

static struct local_extern_decl		*local_extern_decls_head[LED_TABSIZE];
static struct local_extern_decl 	*local_extern_decls_tail[LED_TABSIZE];


static void
mark_as_shadow_decl(struct decl *d) {
	d->invalid = 1;
	/*
	 * Mark this as a ``shadow'' declaration, which
	 * causes no symbol declarations to be emitted,
	 * but which nonetheless is used for declaration
	 * lookup by lookup_symbol()!
	 *
	 * Example:
	 *
	 * int foo = 456;
	 * int main() { 
	 *     int foo = 123;
	 *     { extern int foo; printf("%d\n", foo); }
	 * }
	 *
	 * ... here we must keep the inner extern decl
	 * as a ``shadow'' declaration which wins over
	 * the outer automatic variable
	 */
	d->invalid |= 2;
}

int
is_shadow_decl(struct decl *d) {
	return (d->invalid & 2) != 0;
}

/*
 * Stores local extern declaration. If there already is another
 * local extern declaration in a different local or in the
 * global scope, this declaration is already marked invalid
 */
static void
put_local_extern_decl(struct decl *d) {
	int	key = generic_hash(d->dtype->name, LED_TABSIZE);
	struct local_extern_decl	*ledp;
	struct decl	*prevdec;

	for (ledp = local_extern_decls_head[key];
		ledp != NULL;
		ledp = ledp->next) {
		if (strcmp(ledp->dec->dtype->name, d->dtype->name) == 0) {
			/* We already have an extern declaration */
			/*
			 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
			 * XXXXXXXXXXXXXXXXXXXX we may have to do some
			 * type merging here for extenr bug
			 */
			mark_as_shadow_decl(d);

			/*
			 * 07/21/08: If the outer local declaration has
			 * array size information, and the inner one doesn't,
			 * then take over the outer data
			 *
			 *    extern char bogus[128];
			 *    {
			 *       extern char bogus[];
			 *       sizeof bogus;
			 */
			if (ledp->dec->dtype->tlist != NULL
				&& ledp->dec->dtype->tlist->type == TN_ARRAY_OF
				&& ledp->dec->dtype->tlist->have_array_size
				
				&& d->dtype->tlist != NULL
				&& d->dtype->tlist->type == TN_ARRAY_OF
				&& !d->dtype->tlist->have_array_size) {

				d->dtype->tlist->have_array_size = 1;
				d->dtype->tlist->arrarg_const = 
					ledp->dec->dtype->tlist->arrarg_const;
			}
			
			return;
		}
	}

	/*
	 * Now check whether a PRECEDING global declaration already
	 * renders this declaration obsolete/invalid
	 */
	prevdec = lookup_symbol(&global_scope, d->dtype->name, 0);

	if (prevdec != NULL
		&& (prevdec->dtype->storage == TOK_KEY_STATIC
		|| prevdec->was_not_extern
		|| prevdec->init != NULL
		|| (prevdec->dtype->is_func && prevdec->dtype->is_def))) {
		/*
		 * One of the following cases is true, and therefore
		 * this redeclaration is already obsolete:
		 *
		 *     static int foo;  void f() { extern int foo; }
		 *     int foo; void f() { extern int foo; }
		 *     extern int foo = 123; void f() { extern int foo; }
		 *     extern void f2() {}  void f() { void f2(); f2(); }
		 *
		 * We don't have to store this declaration either;
		 * Subsequent local declarations will also see the
		 * global version
		 */
		mark_as_shadow_decl(d);
		return;
	} else if (prevdec != NULL) {
		/*
		 * 06/01/08: If there is no definition yet, we may still wish
		 * to keep a previous declaration if it is more complete (e.g.
		 * specifies array size)
		 */
		if (compare_types(prevdec->dtype, d->dtype,
			CMPTY_SIGN|CMPTY_CONST) == -1) {
			errorfl(d->tok, "Redeclaration of extern `%s' "
				"with conflicting type",
				d->dtype->name);
			return;
		} else {
			/*
			 * Types are compatible - but are they arrays?
			 */
			if (prevdec->dtype->tlist != NULL
				&& prevdec->dtype->tlist->type
				== TN_ARRAY_OF) {
				if (prevdec->dtype->tlist->have_array_size) {
					return;
				}
			}
		}
	}

	/* New declaration */
	ledp = n_xmalloc(sizeof *ledp); /* XXX zone allocator */
	ledp->dec = d;
	ledp->next = NULL;
	if (local_extern_decls_head[key] == NULL) {
		local_extern_decls_head[key] = local_extern_decls_tail[key] = 
			ledp;
	} else {
		local_extern_decls_tail[key]->next = ledp;
		local_extern_decls_tail[key] = ledp;
	}
}

/*
 * Checks whether a new global DEFINITION renders previous extern
 * declarations obsolete, and if so, marks them invalid
 */
static void
check_local_extern_decl(struct decl *newdec) {
	int	key = generic_hash(newdec->dtype->name, LED_TABSIZE);
	struct local_extern_decl	*ledp;

	for (ledp = local_extern_decls_head[key];
		ledp != NULL;
		ledp = ledp->next) {

		if (strcmp(ledp->dec->dtype->name, newdec->dtype->name) == 0) {
			/*
			 * We have an extern declaration, and it must
			 * be invalidated because we now have a global
			 * definition too
			 */
			mark_as_shadow_decl(ledp->dec);
			return;
		}
	}
}


char	*watchvar = NULL;  /*"stuff";*/ 


/*
 * Stores declaration referenced by ``d'' in current scope (curscope)
 */
void
store_decl_scope(struct scope *s, struct decl **dec) {
	struct type		*t;
	struct dec_block	*destdec = NULL;
	int			i;

	for (i = 0; dec[i] != NULL; ++i) {
		t = dec[i]->dtype;

		if (t->storage == TOK_KEY_TYPEDEF) {
			/* Store dtype */
			struct type	*ot;
			
			ot = lookup_typedef(s, t->name, 0, 0);
			if (ot != NULL) {
				/*
				 * 04/20/08: The declarations reorganization
				 * apparently caused typedef redeclarations to
				 * break. This brought up the topic that gcc
				 * allows these redeclarations ONLY if they
				 * are performed in different headers! E.g. on
				 * FreeBSD, there are va_list redeclarations,
				 * and gcc allows them because it looks at the
				 * preprocessor info and finds they are done
				 * in different headers. But if you put two
				 * declarations in one header, gcc gives an
				 * error! This SUCKS! We now always allow
				 * compatible redeclarations, but give one
				 * warning per file for it
				 */
				if (compare_types(ot, t,
					CMPTY_SIGN|CMPTY_CONST) == -1
				|| (ot->storage
					&& t->storage
					&& ot->storage != t->storage)) {
			
					errorfl(dec[i]->tok, "Redefinition "
					"of typedef "
					"for `%s' with conflicting type",
					t->name);
				} else {
					static int	warned;

					if (!warned) {
						warningfl(dec[i]->tok,
						"Redefinition "
						"of typedef for `%s'",
						t->name);
						warned = 1;
					}
				}
				return;
			}

#if FAST_SYMBOL_LOOKUP
			if (1) {
				destdec = &s->typedef_decls;
				put_fast_sym_hash(s, make_sym_entry(dec[i]), 1);
			} else 
#endif
			if (s != &global_scope) {
				destdec = &s->typedef_decls;
			} else {
				if (!s->typedef_hash.used) {
					/* Global scope - need large table! */
					new_make_hash_table(&s->typedef_hash,
						SYM_HTAB_GLOBAL_SCOPE);	
				}
				new_put_hash_table(&s->typedef_hash,
					make_sym_entry(dec[i]));	
			}	
#ifdef DEBUG2
			printf("storing typedef %s\n",
				t->name ? t->name : "unnamed typedef");
#endif
		} else {
			int	debug_output = 0;

			/* This is a ``real'' declaration */
			if (t->name != NULL) {
				/*
				 * Not anonymous - protect against
				 * redefinition 
				 */
				int	need_def;
				int	rc = check_redef(s, destdec, dec[i], &need_def);
				
				if (watchvar && strcmp(t->name, watchvar) == 0) {
					debug_output = 1;
				}	
if (debug_output) {
	printf("Storing decl %s (%p) to scope %p   (global=%p)\n",
		watchvar, dec[i], s, &global_scope);
	printf("Storage = %s\n",
		dec[i]->dtype->storage == TOK_KEY_EXTERN?
			"extern":
			dec[i]->dtype->storage == TOK_KEY_STATIC?
				"static":
					"none");
}

				if (rc == 1) {
					/*
					 * Multiple tentative definitions -
					 * OK
					 */
if (debug_output) {
	printf("     %s already declared (but tentative) ...\n", watchvar);
}
#if XLATE_IMMEDIATELY
					/*
					 * 03/25/08: nasm (at least old versions)
					 * accept
					 *    symbol:
					 *    ....
					 *    global symbol
					 *
					 * ... but fail to create a global
					 * symbol table entry! So we have to
					 * perform the global declaration before
					 * defining the symbol. This condition
					 * was not fulfilled if there was a
					 * previous declaration preceding the
					 * definition;
					 *
					 *   void func();
					 *   void func() {}
					 *
					 * ... so now we handle this case here
					 */
					if (dec[i]->dtype->is_func
						&& dec[i]->dtype->is_def) {

						if (dec[i]->dtype->storage
							== TOK_KEY_EXTERN) {
if (debug_output) {
	printf("          %s Writing extern global decl?  %s\n",
		watchvar, dec[i]->has_symbol? "NO": "YES");
}
							emit->global_extern_decls(&dec[i], 1);
						} else {	
if (debug_output) {
	printf("          %s Writing static global decl?  %s\n",
		watchvar, dec[i]->has_symbol? "NO": "YES");
}
							if (sysflag != OS_OSX) {
								emit->global_static_decls(&dec[i], 1);
							}
						}
					}
#endif
					if (!need_def) {
						/*
						 * Need not be allocated because
						 * it is extern or tentative
						 */
if (debug_output) {
	printf("               %s NOT Writing definition! (extern or tentative)\n",
		watchvar);
}
						if (!dropped_inline_decl) {
							continue;
						}
					}

					/*
					 * This is a redeclaration of a tentative
					 * declaration which also requires a
					 * definition. 
					 */
					if (!dropped_inline_decl) {
						if (dec[i]->init == NULL) {
							continue;
						}
					}
					dec[i]->has_def = 1;
				} else if (rc == -1) {
					/* Redefinition - bad */
					return;
				}

			}


			if (!dropped_inline_decl      &&    (s->type == SCOPE_CODE
				&& (t->storage == TOK_KEY_EXTERN
				|| t->is_func)
				/*	&& t->is_def*/
			/*		&& t->storage != TOK_KEY_STATIC
					&& t->tlist->type == TN_FUNCTION*/)) {
#if 0
				/*
				 * XXX 07/22/07:  
				 * Local extern declarations are terrible to
				 * deal with.. for now we always put them
				 * into the global scope to avoid
				 * redeclarations of symbols
				 *
				 * 08/22/07: This was missing the TN_FUNCTION
				 * check; without function pointers don't
				 * work
				 */
				s = &global_scope;
#endif
				/*
				 * 07/21/08: Always call put_local_extern_decl(),
				 * not just for nasm! The reason is that we must
				 * do type-checking. E.g. in
				 *
				 *    extern char buf[128];
				 *    void f() { extern char buf[]; }
				 *
				 * ... we are not allowed to discard the size
				 * information in f() such that sizeof() would
				 * not work
				 */
				if (/*emit->need_explicit_extern_decls*/ 1
					&& s != &global_scope) {
					/*
					 * 04/08/08: We have to revive the local
					 * extern distinction for nasm and yasm to
					 * avoid duplicate definitions
					 */
					if (s != &global_scope) {
						put_local_extern_decl(dec[i]);
					} else {
					/*	check_local_extern_decl(dec[i]);*/
					}
				}
			}

			if (s == &global_scope) {
				int	is_fdef = 0;
				/* Global or file scope declaration */
#ifdef DEBUG2
				printf("Storing global declaration %s\n",
					t->name);
#endif
				if (t->storage == TOK_KEY_AUTO) {
					errorfl(dec[i]->tok /*XXX*/,
	"Bogus storage class specifier in global variable declaration");
					return;
				} else if (t->storage == TOK_KEY_EXTERN
					|| (t->is_func
						&& !t->is_def
						&& t->storage
							!= TOK_KEY_STATIC)) {
					if (dec[i]->init != NULL) {
						/*
						 * Initialized extern! Put
						 * this into the static list
						 */
						destdec = &s->static_decls;
					} else {
						if (IS_INLINE(t->flags)) {
							/*
							 * 12/25/08: extern inline seems
							 * to mean ``local function''
							 */
							t->storage = TOK_KEY_STATIC;
							destdec = &s->static_decls;
						} else {
							t->storage = TOK_KEY_EXTERN;
							destdec = &s->extern_decls;
						}
						if (t->is_func && t->is_def) {
							is_fdef = 1;
						}
					}
				} else if (t->storage == 0) {
					/*
					 * Need this to distinguish between
					 * global and local static variables.
					 */
					dec[i]->was_not_extern = 1;
					t->storage = TOK_KEY_EXTERN;
					destdec = &s->static_decls;
				} else if (t->storage == TOK_KEY_STATIC) { 
					destdec = &s->static_decls;
				} else {
					errorfl(dec[i]->tok,
						"Invalid storage class for "
						"`%s'", t->name);
					return;
				}	

				if ((destdec == &s->static_decls || is_fdef)
					&& emit->need_explicit_extern_decls) {
					/*
					 * 04/08/08:  
					 * This is a (possibly tentative)
					 * definition in global scope, so we
					 * may have to check whether this
					 * renders local extern declarations
					 * obsolete, e.g. in
					 *
					 *    int main() { void f(); f(); }
					 *    void f() {}
					 *
					 * ... we have to remove the local
					 * extern declaration for f() because
					 * otherwise it will clash with the
					 * GLOBAL declaration later
					 */
					check_local_extern_decl(dec[i]);
				}
			} else {
				/*
				 * Local declaration (static or 
				 * automatic or register)
				 */ 
#ifdef DEBUG2
				printf("Storing local declaration %s\n",
					t->name? t->name:
						"unknown declaration");
#endif

				if (t->storage == TOK_KEY_EXTERN) {
					destdec = &s->extern_decls;
				} else if (t->storage == TOK_KEY_STATIC) {
					destdec = &s->static_decls;
if (debug_output) {
	printf("                    (%s Will append to static block)\n",
		watchvar);
}
				} else if (t->storage == TOK_KEY_REGISTER) {
					/* destdec = &s->register_decls; */
					destdec = &s->automatic_decls;
				} else if (t->is_func && t->tlist->type == TN_FUNCTION) {
					/* XXX not working */
					t->storage = TOK_KEY_EXTERN;
					destdec = &s->extern_decls;
if (debug_output) {
	printf("                    (%s Will append to extern block)\n",
		watchvar);
}
				} else {
					/*
					 * 08/21/07: Removed because this is
					 * never used
					 */
/*					t->storage = TOK_KEY_AUTO;*/
					destdec = &s->automatic_decls;
				}	
				if (t->is_func
					&& t->tlist->type == TN_FUNCTION) {
					/* Record local function declaration */
#if 0
					printf("putting local fdec `%s'\n", t->name);
					put_local_func_decl(dec[i]);
#endif
				}
			}

			if (destdec == &s->extern_decls) {
if (debug_output) {
	printf("               %s Appending to extern vars list\n",
		watchvar);
}
				append_symlist(NULL, &extern_vars,
					&extern_vars_tail,
					dec[i]);
			}
						
#if XLATE_IMMEDIATELY
			/*
			 * We have to declare global symbols before they are
			 * defined because otherwise nasm will accept our code
			 * but, due to some bug, not create the requested
			 * global symbol. See comment at check_redef() call too
			 */
			if (dec[i]->dtype->is_func
				&& dec[i]->dtype->is_def) {
				if (dec[i]->dtype->storage
					== TOK_KEY_EXTERN) {
if (debug_output) {
	printf("               %s Function definition - writing "
		"extern global decl?   %s\n",
		watchvar, dec[i]->has_symbol? "NO": "YES");
}
					emit->global_extern_decls(&dec[i], 1);
				} else {
if (debug_output) {
	printf("               %s Function definition - writing "
		"static global decl?   %s\n",
		watchvar, dec[i]->has_symbol? "NO": "YES");
}
					if (sysflag != OS_OSX)
					emit->global_static_decls(&dec[i], 1);
				}
			}
#endif
			/*
			 * 03/27/08: Mark non-extern declaration
			 * as having a definition, so that
			 * subsequent redeclarations are not
			 * processed (to avoid multiple ``global''
			 * declarations).
			 *
			 * This does not filter initialized
			 * variables (which is correct) because
			 * those always let check_redef() return
			 * 0, so
			 *
			 *    int foo;
			 *    int foo = 123;
			 *
			 * .. will use the correct definition
			 * XXX this is probably still full of
			 * bugs
			 */
			if (dec[i]->dtype->storage != TOK_KEY_EXTERN
				&& (!dec[i]->dtype->is_func
				|| dec[i]->dtype->is_def)) {
if (debug_output) {
	printf("               %s Setting has_def (subsequent decls "
			"should not emit anything)!\n", watchvar);
}
				dec[i]->has_def = 1;
			} else if (dec[i]->dtype->storage == TOK_KEY_EXTERN
				&& dec[i]->init != NULL) {
				/* 04/03/08: This was missing! */
				dec[i]->has_def = 1;
			}


			if (destdec == &s->static_decls
				&& !dec[i]->dtype->is_func) {

#if XLATE_IMMEDIATELY
/*				++dec[i]->references;*/
#endif
#if 0
				if (dec[i]->dtype->storage == TOK_KEY_EXTERN) {
					/* 03/21/08: New - make tentative
					 * declarations work (whoops)
					 */
					dec[i]->has_def = 1;
				}
#endif


				assert(dec[i]->next == NULL);
				if (dec[i]->init != NULL) {
					if (IS_THREAD(dec[i]->dtype->flags)) {
if (debug_output) {
	printf("               %s Appending to init thread list\n", watchvar);
}
						append_decl(
							&static_init_thread_vars, 
							&sitv_tail, 
							dec[i]);
#if 0 /*XLATE_IMMEDIATELY*/
if (debug_output) {
	printf("               %s Writing init thread definition\n", watchvar);
}
						emit->static_init_thread_vars(
							dec[i]);	
#endif
					} else {
if (debug_output) {
	printf("               %s Appending to init list\n", watchvar);
}
						append_decl(
							&static_init_vars, 
							&siv_tail, 
							dec[i]);
#if 0 /*XLATE_IMMEDIATELY*/
						if (!IS_FUNCNAME(dec[i]->dtype->flags)) {
if (debug_output) {
	printf("               %s Writing init definition\n", watchvar);
}
							emit->static_init_vars(
								dec[i]);	
						}
#endif
					}
				} else {
					if (IS_THREAD(dec[i]->dtype->flags)) {
if (debug_output) {
	printf("               %s Appending to uninit thread list\n", watchvar);
}
						append_decl(
							&static_uninit_thread_vars,
							&siutv_tail,
							dec[i]);
					} else {
if (debug_output) {
	printf("               %s Appending to uninit list\n", watchvar);
}
						append_decl(
							&static_uninit_vars,
							&siuv_tail,
							dec[i]);
					}
				}
			}

			/*
			 * 03/23/08: Handle global declarations if already
			 * possible
			 */
#if XLATE_IMMEDIATELY
			if (destdec == &s->extern_decls) {
			} else if (destdec == &s->static_decls
				&& s == &global_scope
				&& (dec[i]->init != NULL
					|| dec[i]->was_not_extern)) {
if (debug_output) {
	printf("               %s Writing static global decl?  %s\n",
		watchvar, dec[i]->has_symbol? "NO": "YES");
}
				if (sysflag != OS_OSX)
				emit->global_static_decls(&dec[i], 1);
			}
#endif
			/*
			 * NOTE: A declaration need not have a name to be
			 * valid! (e.g. anonymous unions and bitfields)
			 */
if (debug_output) {
	printf("               %s Appending to symbol list of %p  (global=%p)\n",
		watchvar, s, &global_scope);
}
			if (0) {  /*dec[i]->dtype->tbit != NULL
				&& dec[i]->dtype->name == NULL) {*/
				/*
				 * 07/17/08: Don't append unnamed bitfield
				 * members to the symbol list! (Otherwise
				 * they are also used to match initializers,
				 * which is wrong
				 *
				 * 09/20/08: WRONG, we have to store them
				 * anyway so that storage allocation works
				 * as expected (otherwise unnamed delcarations
				 * have no effect at all)
				 */
				;
			} else {	
				append_symlist(s,
					&s->slist, &s->slist_tail, dec[i]);
			}
		}
		
		if (destdec) {
			if (dec[i]->dtype->storage == TOK_KEY_STATIC
				&& !dec[i]->dtype->is_func
				&& dec[i]->asmname == NULL) {
				char	*newname;
				size_t	len = strlen(dec[i]->dtype->name);

				/*
				 * Change variable name to avoid name clashes
				 * with other static variables. The old name
				 * will be kept in the symbol list. And since
				 * all lookups go through this list, the
				 * identifier is still visible as what it was
				 * originally called
				 */
				len += sizeof "_Static_ " + 8;
				newname = n_xmalloc(len+sizeof "_Static_"+8);
				sprintf(newname, "_Static_%s%lu",
					dec[i]->dtype->name, s->scopeno); 
				dec[i]->dtype->name = newname;
			} else if (dec[i]->dtype->storage == TOK_KEY_EXTERN
				|| dec[i]->dtype->storage == TOK_KEY_STATIC
				|| dec[i]->dtype->is_func) {
				if (dec[i]->asmname != NULL) {
					dec[i]->dtype->name = dec[i]->asmname;
					dec[i]->dtype->flags |= FLAGS_ASM_RENAMED;
				}
			} else if (dec[i]->dtype->storage == TOK_KEY_EXTERN) {
				/*
				 * 02/14/09: For OSX: Symbols that are renamed using
				 * __asm__ need to get this recorded so that symbol
				 * exports do not export the extra underscore
				 */
				if (dec[i]->asmname != NULL) {
					dec[i]->dtype->name = dec[i]->asmname;
					dec[i]->dtype->flags |= FLAGS_ASM_RENAMED;
				}
			}
			do_store_decl(s, destdec, dec[i]);
		} 
	}
}


void
complete_type(struct ty_struct *oldts, struct ty_struct *ts) {
	struct ty_struct	*oldlink;
	int			oldrefs;

	oldlink = oldts->next;
	oldrefs = oldts->references;
	if (ts->incomplete) {
		return;
	}	
	memcpy(oldts, ts, sizeof *ts);
	oldts->next = oldlink;
	oldts->incomplete = 0;
	oldts->references =
		oldrefs + ts->references;
}					

struct ty_enum *
create_enum_forward_declaration(const char *tag) {
	struct ty_enum	*ret = alloc_ty_enum();
	ret->tag = (char *)tag;
	ret->is_forward_decl = 1;
	return ret;
}


/*
 * Stores structure or enum definition (not instance!) in current scope.
 * If ts is not a null pointer, it will be stored. Otherwise, te will be
 * stored
 */
void
store_def_scope(struct scope *sc,
		struct ty_struct *ts,
		struct ty_enum *te,
		struct token *tok) {
	int		i;
	char		*p;
	struct sd	*s = NULL;
	struct ed	*e = NULL;

	if (ts != NULL) {
		/*
		 * We must first check whether we are completing an incomplete
		 * structure, i.e. one that was originally declared using e.g.
		 * ``struct foo;'' or ``typedef struct foo bar;''
		 */
		struct scope	*destscope = curscope;

		while (destscope->type == SCOPE_STRUCT) {
			destscope = destscope->parent;
		}
		
		ts->parentscope = destscope;
		if (ts->tag != NULL) {
			struct ty_struct	*oldts;

			if ((oldts = lookup_struct(destscope, ts->tag, 1))
				!= NULL) {
				if (oldts->incomplete) {
					complete_type(oldts, ts);
					return;
				} else if (destscope
					== oldts->parentscope) {
					if (!ts->incomplete) {
						errorfl(tok->prev,
							"Multiple definitions "
							"of structure or "
							"union `%s'", ts->tag);
					}	
					return;
				}	
			}
		} else {
			/*
			 * 07/17/08: Structure has no tag! So we create a dummy
			 * tag for it. This is needed for __builtin_offsetof();
			 *
			 *    __builtin_offsetof(struct { int nonsense; } ...
			 *
			 * ... is allowed by gcc. But since we internally
			 * rewrite this as
			 *
			 *    &((struct type *)0)->....
			 *
			 * ... we need a struct tag! Thus use the dummy one
			 */
			static unsigned long	dummycount;
			char			buf[128];

			sprintf(buf, "__dummytag%lu", dummycount++);
			ts->dummytag = n_xstrdup(buf);
		}

		/* Completely new structure type */
		s = &destscope->struct_defs;
		if (s->head == NULL) {
			s->head = s->tail = ts;
		} else {
			s->tail->next = ts;
			s->tail = s->tail->next;
		}	
		++s->ndecls;
#ifdef DEBUG2
		printf("stored structure with tag %s\n", ts->tag);
#endif
	} else if (te == NULL) {
		puts("Fatal error in store_def_scope - te = 0");
		abort();
	} else {
		struct scope	*destscope = curscope;

		while (destscope->type == SCOPE_STRUCT) {
			destscope = destscope->parent;
		}	

		e = & /*sc*/ destscope->enum_defs;
		if (te->tag != NULL) {
			/*
			 * Check whether we have a multiple definition error.
			 * 20141116: We now support enum forward declarations
			 * as an extension to ISO C (this makes GNU bison compile)
			 */
			for (i = 0; i < e->ndecls; ++i) {
				p = e->data[i]->tag;
				if (p && strcmp(p, te->tag) == 0) {
					if (e->data[i]->is_forward_decl) {
						/*
						 * We already have a slot in which to store
						 * this definition because a forward
						 * declaration has taken place
						 */
						e->data[i] = te;
						return;
					} else {
						errorfl(tok,
							"Multiple definitions of enum `%s'", p);
						return;
					}
				}
			}
			if (lookup_struct(sc, te->tag, 0) != NULL) {
				errorfl(tok,
				"Multiple definitions of structure `%s'",
					te->tag);
			}
		}
			
		if (e->ndecls >= e->nslots) {
			if (e->nslots == 0) {
				e->nslots = 1;
			}
			e->nslots *= 2;
			e->data = n_xrealloc(e->data,
					e->nslots * sizeof *e->data);
		}
		e->data[ e->ndecls++ ] = te;
	}
}

struct decl *
lookup_symbol(struct scope *s, const char *name, int nested) {
	struct sym_entry	*se;

	se = lookup_symbol_se(s, name, nested);
	if (se == NULL) {
		return NULL;
	}
	return se->dec;
}

struct /*decl*/ sym_entry *
lookup_symbol_se(struct scope *s, const char *name, int nested) {
	size_t			len; 
	
	len = strlen(name);
#if FAST_SYMBOL_LOOKUP
	if (s->type != SCOPE_STRUCT) {
		return fast_lookup_symbol_se(s, name, nested, 0);
	}
#endif

	do {
		struct sym_entry	*se;

#if ! FAST_SYMBOL_LOOKUP
		if (!s->sym_hash.used) 
#endif
		{
			/* Linear scan */
			for (se = s->slist; se != NULL; se = se->next) {
				/* 04/08/08: Shadow declarations */
				if (se->dec->invalid
					&& !is_shadow_decl(se->dec)) {
					continue;
				}
				if (!se->inactive && se->namelen == len) {
					if (se->name[0] == name[0]) {
						if (strcmp(&se->name[1],
							&name[1]) == 0) {
							return se  /*->dec*/;
						}
					}
				}
			}
		} 
#if ! FAST_SYMBOL_LOOKUP
		else {
			/* Hash lookup (key is length) */
#if 0 
			se = lookup_hash(s->sym_hash,
					s->n_hash_slots, name, len);
#endif
			se = new_lookup_hash(&s->sym_hash, name, len);
			if (se != NULL) {
				return se /*->dec*/;
			}
		}
#endif

		if (nested == 0) {
			break;
		}
	} while ((s = s->parent) != NULL);

	return NULL;
}


struct decl *
access_symbol(struct scope *s, const char *name, int nested) {
	struct decl	*ret;

	if ((ret = lookup_symbol(s, name, nested)) != NULL) {
		if (ret->is_alias) {
			ret = ret->is_alias;
		}
		++ret->references;
		if (ret->dtype->tstruc != NULL) {
			++ret->dtype->tstruc->references;
		}	
	}	
	return ret;
}

/*
 * 12/25/08: Mark a (static) variable as really used, modulo
 * optimization. This is for architectures like PPC where we
 * aren't allowed to  declare UNUSED extern declarations. For
 * example,
 *
 *    extern int foo;
 *    0? foo: 123;
 *
 * ... is expected to work at least by glibc even if foo doesn't
 * exist. The access must be optimized away.
 *
 * On PPC this will cause a linker error even if we don't access
 * foo directly because we still create a TOC entry for the
 * undefined symbol. Instead of undoing reference increments
 * after having found that an access can be optimized away, we
 * now use a reference count of ACTUAL access in the emitter.
 */
void
really_accessed(struct decl *d) {
	if (d->is_alias != NULL) {
		/*
		 * This is an alias - increment actual item (e.g.
		 * __func__ for __PRETTY_FUNCTION__
		 */
		d = d->is_alias;
	}
	++d->real_references;
}


struct decl *
put_implicit(const char *name) {
	struct decl		*d[2];
	struct type		*ty;
	struct type_node	*tnode;

	d[0] = alloc_decl();
	ty = alloc_type();
	ty->name = (char *)name;
	ty->code = TY_INT;
	ty->storage = TOK_KEY_EXTERN;
	ty->is_func = 1;
	ty->implicit = IMPLICIT_FDECL;
	ty->sign = TOK_KEY_SIGNED;
	tnode = alloc_type_node();
	tnode->type = TN_FUNCTION;
	tnode->tfunc = alloc_ty_func();
	tnode->tfunc->scope = NULL;
	tnode->tfunc->nargs = -1;
	tnode->tfunc->ret = NULL;
	tnode->tfunc->type = FDTYPE_ISO;
	ty->tlist = tnode;

	d[0]->dtype = ty;
	d[0]->references = 1;
	d[0]->real_references = 0; /* 12/24/08 */
	d[1] = NULL;

	store_decl_scope(&global_scope, d);
	return d[0];
}

