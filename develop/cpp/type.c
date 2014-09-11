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
 *
 * Functions to allocate new instances of the types defined in type.h,
 * setting each member to zero, and function to compare type lists
 */
#include "type.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "misc.h"
#include "token.h"
#include "subexpr.h"
#include "typemap.h"

#    include "error.h"

#ifndef PREPROCESSOR
#    include "decl.h"
#    include "control.h"
#    include "functions.h"
#    include "backend.h"
#    include "icode.h"
#    include "attribute.h"
#    include "symlist.h"
#    include "scope.h"
#    include "debug.h"
#endif

#include "expr.h"
#include <sys/mman.h>
#include "n_libc.h"


struct ty_struct *
alloc_ty_struct(void) {
	struct ty_struct		*ret;
	static struct ty_struct	nulltys;
	ret = n_xmalloc(sizeof *ret);
	*ret = nulltys;
	return ret;
}

struct ty_bit *
alloc_ty_bit(void) {
	struct ty_bit			*ret;
	static struct ty_bit	nulltyb;
	ret = n_xmalloc(sizeof *ret);
	*ret = nulltyb;
	return ret;
}

struct ty_enum *
alloc_ty_enum(void) {
	struct ty_enum			*ret;
	static struct ty_enum	nulltye;
	ret = n_xmalloc(sizeof *ret);
	*ret = nulltye;
	return ret;
}

struct ty_llong *
alloc_ty_llong(void) {
	struct ty_llong		*ret;
	static struct ty_llong	nullll;
	ret = n_xmalloc(sizeof *ret);
	*ret = nullll;
	return ret;
}	
	

struct ty_func *
alloc_ty_func(void) {
	struct ty_func			*ret;
	static struct ty_func	nulltyf;
	ret = n_xmalloc(sizeof *ret);
	*ret = nulltyf;
	return ret;
}

struct type_node *
alloc_type_node(void) {
	struct type_node		*ret;
	static struct type_node	nulltyn;
	ret = n_xmalloc(sizeof *ret);
	*ret = nulltyn;
	return ret;
}

struct type *
alloc_type(void) {
	struct type			*ret;
	static struct type	nulltype;
	ret = n_xmalloc(sizeof *ret);
	*ret = nulltype;
	return ret;
}


#ifndef PREPROCESSOR

static int
compare_tfunc(struct ty_func *dest, struct ty_func *src) {
	struct sym_entry	*sd;
	struct sym_entry	*ss;
	int			i;

	if (dest->nargs == -1 || src->nargs == -1) {
		/* This is a mere declaration like void foo(); - assume equal */
		return 0;
	}
	if (dest->nargs != src->nargs) {
		return -1;
	}
	if (dest->ret && src->ret) {
		if (compare_types(dest->ret, src->ret, CMPTY_SIGN|CMPTY_CONST)
			== -1) {
			return -1;
		}
	}

	/* XXX this is a kludge ... use func data instead */
	sd = dest->scope->slist;
	ss = src->scope->slist;
	for (i = 0; i < dest->nargs; ++i) {
		struct decl		*d1;
		struct decl		*d2;
		struct type		*t1;
		struct type		*t2;
		static struct vreg	dummyvr;

		d1 = sd->dec;
		d2 = ss->dec;

		t1 = d1->dtype;
		t2 = d2->dtype;

		if (is_transparent_union(t1) && is_transparent_union(t2)) {
		} else if (is_transparent_union(t1)) {
			dummyvr.type = t2;
			t1 = get_transparent_union_type(d1->tok, t1, &dummyvr); 
			if (t1 == NULL) {
				return -1;
			}
		} else if (is_transparent_union(t2)) {
			dummyvr.type = t1;
			t2 = get_transparent_union_type(d2->tok, t2, &dummyvr); 
			if (t2 == NULL) {
				return -1;
			}
		}

		if (compare_types(t1, t2, CMPTY_SIGN|CMPTY_CONST) == -1) {
			return -1;
		}
		sd = sd->next;
		ss = ss->next;
	}
	
	return 0;
}

/*
 * XXX  warn about ``unsigned char *'' vs ``char *'',
 * unlike gcc
 */
static int
compare_tlist(struct type_node *dest, struct type_node *src, int flag) {
	struct type_node	*dest_start = dest;

	for (;
		dest != NULL && src != NULL;
		dest = dest->next, src = src->next) {

		if (src->type == TN_FUNCTION
			|| dest->type == TN_FUNCTION) {
			if (dest->type != src->type) {
				/* XXX fix this later */
				if (dest == dest_start) {
					/*
					 * Ordinary function symbols are
					 * compatible with pointers to
					 * functions
					 */
					if (dest->type == TN_FUNCTION) {
						if (src->type
							== TN_POINTER_TO) {
							src = src->next;
						} else {
							return -1;
						}	
					} else {
						if (dest->type
							== TN_POINTER_TO) {
							dest = dest->next;
						} else {
							return -1;
						}	
					}	
				}	
			}
		}

		if (dest->type != src->type) {
			/* Pointer vs array vs function */
			if (flag & CMPTY_ARRAYPTR) {
				if ((dest->type == TN_ARRAY_OF
					|| src->type == TN_ARRAY_OF
					|| dest->type == TN_VARARRAY_OF
					|| src->type == TN_VARARRAY_OF)

					&& (dest->type == TN_POINTER_TO
					|| src->type == TN_POINTER_TO)) {
					continue;
				}	
			}
			return -1;
		}

		switch (dest->type) {
		case TN_ARRAY_OF:
		case TN_VARARRAY_OF:	
			if (flag & CMPTY_TENTDEC) {
#if REMOVE_ARRARG
				if (!dest->have_array_size
					|| !src->have_array_size) {	
#else
				if (dest->arrarg->const_value == NULL
					|| src->arrarg->const_value == NULL) {
#endif
					/*
					 * probably
					 * extern int foo[];
					 * int foo[123];
					 * -> OK!
					 */
					break;
				}
			}
			if (dest->arrarg_const != src->arrarg_const
				&& ((flag & CMPTY_ARRAYPTR) == 0
				|| dest_start != dest)) {
#if REMOVE_ARRARG
				if (!src->have_array_size
					|| !dest->have_array_size) {	
#else
				if (src->arrarg->const_value == NULL
					|| dest->arrarg->const_value == NULL) {
#endif
					/*
					 * One side has unspecified size, this
					 * is OK!
					 * extern char foo[];
					 * char (*p)[5] = &foo;
					 * char bar[5];
					 * char (*p2)[] = &bar;
					 */
					break;
				} else {
					/* Array sizes differ */
					return -1;
				}	
			}
			break;
		case TN_POINTER_TO:
			break;
		case TN_FUNCTION:
			if (compare_tfunc(dest->tfunc, src->tfunc) == -1) {
				return -1;
			}
			break;
		}
	}
	if (dest != NULL || src != NULL) {
		/* One list is longer, so it differs by definition */
		return -1;
	}
	return 0;
}
#endif /* #ifndef PREPROCESSOR */

int
compare_types(struct type *dest, struct type *src, int flag) {
	int	is_void_ptr = 0;

	/* 04/08/08: Changed this (for the better, hopefully!) */
	if (dest->tlist != NULL
		&& dest->tlist->type == TN_POINTER_TO	
		&& dest->tlist->next == NULL
		&& dest->code == TY_VOID) {
		is_void_ptr = 1;
	} else if (src->tlist != NULL
		&& src->tlist->type == TN_POINTER_TO
		&& src->tlist->next == NULL
		&& src->code == TY_VOID) {
		is_void_ptr = 1;
	}
	
	if (dest->code != src->code) {
		/*
		 * Differing base type - This is ok if we have a void
		 * pointer vs a non-void pointer, otherwise return error
		 */
		if (!is_void_ptr || src->tlist == NULL || dest->tlist == NULL) {
			return -1;
		}
	}

	if (flag & CMPTY_SIGN) {
		if (dest->sign != dest->sign) {
			/* Differing sign */
			return -1;
		}
	}
	if (flag & CMPTY_CONST) {
		if (IS_CONST(dest->flags) != IS_CONST(src->flags)) {
			/* One is const-qualified */
			/*return -1;*/
		}
	}

	/*
	 * 04/08/08: Skip the tlist comparison if this is void pointer
	 * vs non-void pointer; Otherwise tlists of different length
	 * will compare uneven, as in void * vs int **, which is wrong
	 */
	if (is_void_ptr) {
		return 0;
	}

#ifndef PREPROCESSOR
	return compare_tlist(dest->tlist, src->tlist, flag);
#else
	return -1;
#endif
}


int
check_init_type(struct type *ofwhat, struct expr *init) {
	if (ofwhat->tlist == NULL) {
		if (init->next != NULL) {
		}
	} else if (ofwhat->tlist->type == TN_ARRAY_OF) {
		if (init->type->code == TOK_STRING_LITERAL) {
			return 0;
		} else {
			struct expr	*ex;
			for (ex = init; ex != NULL; ex = ex->next) {
		}
		}
	}
	return 0;
}

void
copy_type(struct type *dest, const struct type *src, int fullcopy) {
	if (fullcopy) {
		memcpy(dest, src, sizeof *dest);
	} else {
		memcpy(dest, src, sizeof *dest);
	}
}

struct type_node *
copy_tlist(struct type_node **dest, const struct type_node *src) {
	struct type_node	*head;
	struct type_node	*tail;
	struct type_node	*tn;

	if (src == NULL) {
		*dest = NULL;
		return NULL;
	}
	head = tail = NULL;
	do {
		tn = n_xmalloc(sizeof *tn);
		memcpy(tn, src, sizeof *tn);
		if (head == NULL) {
			head = tail = tn;
		} else {
			tail->next = tn;
			tail = tail->next;
		}	
	} while ((src = src->next) != NULL);	
	*dest = head;
	return tail;
}
		

void
set_type_sign(struct type *ty) {
	if (ty->code == TY_UCHAR
		|| ty->code == TY_USHORT
		|| ty->code == TY_UINT
		|| ty->code == TY_ULONG
		|| ty->code == TY_ULLONG) {
		ty->sign = TOK_KEY_UNSIGNED;
	} else if (!IS_FLOATING(ty->code)
		&& ty->code != TY_STRUCT
		&& ty->code != TY_UNION) {
		ty->sign = TOK_KEY_SIGNED;
	}
}
			

struct type *
make_basic_type(int code) {
#define N_TYPES (TY_MAX - TY_MIN)
#if 0
	static struct type	basic_types[N_TYPES];
#endif
	static int		inited;
	static struct type	*basic_types;


	if (!inited) {
		int	i;
		int	nbytes = N_TYPES * sizeof(struct type);
		int	need_mprotect = 1;

		basic_types = debug_malloc_pages(nbytes);
		if (basic_types == NULL) {
			/*
			 * Probably debug_malloc_pages() doesn't work
		 	 * on this system
			 */
			basic_types = n_xmalloc(nbytes);
			need_mprotect = 0;
		}
		memset(basic_types, 0, nbytes);
		for (i = 0; i < N_TYPES; ++i) {
			basic_types[i].code = i + TY_MIN;
			set_type_sign(&basic_types[i]);
		}		
		inited = 1;

		if (need_mprotect) {
			/*
			 * We make the array unwritable because it really
			 * should not be written to; Modifying it is a bug
			 * that has happend more than once. 
			 * 
			 * The void cast is necessary because of a broken
			 * Solaris prototype that takes caddr_t :-/
			 */
			mprotect((void *)basic_types, nbytes, PROT_READ); 
		}
	}
	if (code < 0 || (code - TY_MIN) >= N_TYPES) {
		printf("BUG: bad code for make_basic_type: %d\n", code);
		abort();
	}
#if 0
	/* As of Jan 6 2007, the basic types may not be modified anymore */
	basic_types[code - TY_MIN].tlist = NULL;
#endif
	return &basic_types[code - TY_MIN];
}

struct type *
make_void_ptr_type(void) {
	static struct type	*ty;

	if (ty == NULL) {
		ty = make_basic_type(TY_VOID);
		ty = n_xmemdup(ty, sizeof *ty);
		append_typelist(ty, TN_POINTER_TO, NULL, NULL, NULL);
	}
	return ty;
}

struct type *
make_array_type(int size) {
	struct type	*ret = alloc_type();

	ret->code = TY_CHAR;
	ret->storage = TOK_KEY_STATIC;
	if (CHAR_MAX == UCHAR_MAX) { /* XXX */
		ret->sign = TOK_KEY_UNSIGNED;
	} else {
		ret->sign = TOK_KEY_SIGNED;
	}
	ret->tlist = alloc_type_node();
	ret->tlist->type = TN_ARRAY_OF;
	ret->tlist->arrarg_const = size;
#if REMOVE_ARRARG
	ret->tlist->have_array_size = 1;
#endif
	return ret;
}


/*
 * Helper function for parse_declarator()- stores pointer/array-of/function
 * property (specified by ``type'' argument) with optional arguments type_arg
 * (for pointer/array-of) and tf (for function) in type specified by t
 *
 * 01/26/08: Extended to do some sanity checking (functions may not return
 * functions or arrays). This means some type constructions are now REQUIRED
 * to go through append_typelist()! May not be the best approach, needs
 * testing?!
 */
void
append_typelist(struct type *t, 
		int type,
		void *type_arg,
		struct ty_func *tf,
		struct token *tok) {
	struct type_node	*te;

	(void) tok; /* XXX unneeded?!?! */

	/* Allocate and insert new type node */
	if (t->tlist == NULL) {
		te = t->tlist = t->tlist_tail = alloc_type_node();
		te->prev = NULL;
		if (type == TN_FUNCTION) {
			/*
			 * If the first node in the type list is a function
			 * designator, this means we are dealing with a genuine
			 * function declaration/definition (as opposed to a
			 * pointer)
			 */
			t->is_func = 1;
		}
	} else {
		/*
		 * 01/26/08: Some sanity checking!
		 */
		int	tailtype = t->tlist_tail->type;

		if (tailtype == TN_ARRAY_OF || tailtype == TN_VARARRAY_OF) {
			if (type == TN_FUNCTION) {
				errorfl(tok, "Invalid declaration of `array of "
					"functions' - Maybe you meant `array "
					"of pointer to function'; `void (*ar[N])();'?");
				return /* -1  XXX */   ;
			}
		} else if (tailtype == TN_FUNCTION) {
			if (type == TN_ARRAY_OF || type == TN_VARARRAY_OF) {
				errorfl(tok, "Invalid declaration of `function "
					"returning array' - If you really want "
					"to return an array by value, put it "
					"into a structure!");
				return /* -1  XXX */   ;
			} else if (type == TN_FUNCTION) {
				errorfl(tok, "Invalid declaration of `function "
					"returning function' - You can at most "
					"return a pointer to a function; "
					"`void (*foo())();'");
				return /* -1 XXX */   ;
			}
		}

		te = alloc_type_node();
		te->prev = t->tlist_tail;
		t->tlist_tail->next = te;
		t->tlist_tail = t->tlist_tail->next;
	}

	te->next = NULL;

	te->type = type;

	switch (type) {
	case TN_VARARRAY_OF:	
	case TN_ARRAY_OF:
#if REMOVE_ARRARG
		ex = type_arg;
		if (ex->const_value == NULL) {
			/* Size not specified - extern char buf[]; */
			te->have_array_size = 0;
		} else {
			te->have_array_size = 1;
			ex->const_value->type =
				n_xmemdup(ex->const_value->type,
						sizeof(struct type));
			cross_convert_tyval(ex->const_value, NULL, NULL);
			te->arrarg_const = cross_to_host_size_t(
					ex->const_value);
			if (te->arrarg_const == 0) {
				/*
				 * In GNU C,
				 * int foo[0];
				 * may be a flexible array member
				 */
				te->have_array_size = 0;
#if 0
				errorfl(tok,
					"Cannot create zero-sized arrays");
#endif
			}	
		}
		if (type == TN_VARARRAY_OF) {
			te->variable_arrarg = ex;
		}
#else /* Using arrarg */
		te->arrarg = type_arg;
		if (te->arrarg->const_value) {
			te->arrarg->const_value->type =
				n_xmemdup(te->arrarg->const_value->type,
				sizeof(struct type));
			cross_convert_tyval(te->arrarg->const_value, NULL, NULL);
			te->arrarg_const = /* *(size_t *) */
				cross_to_host_size_t(
				te->arrarg->const_value);  /*->value; */
			if (te->arrarg_const == 0) {
				/*
				 * In GNU C,
				 * int foo[0];
				 * may be a flexible array member
				 */
				te->arrarg->const_value = NULL;
#if 0
				errorfl(tok,
					"Cannot create zero-sized arrays");
#endif
			}	
		}	
#endif /* REMOVE_ARRARG is disabled */
		break;
	case TN_POINTER_TO:
		te->ptrarg = type_arg? *(int *)type_arg: 0;
		break;
	case TN_FUNCTION:
		te->tfunc = tf; 
		break;
	}
}

static struct {
	char	*name;
	int	code;
} basic_type_names[] = {
	{ "char", TY_CHAR },
	{ "unsigned char", TY_UCHAR },
	{ "signed char", TY_SCHAR },
	{ "short", TY_SHORT },
	{ "unsigned short", TY_USHORT },
	{ "int", TY_INT },
	{ "unsigned int", TY_UINT },
	{ "long", TY_LONG },
	{ "unsigned long", TY_ULONG },
	{ "float", TY_FLOAT },
	{ "double", TY_DOUBLE },
	{ "long double", TY_LDOUBLE },
	{ "struct", TY_STRUCT },
	{ "union", TY_UNION },
	{ "enum", TY_ENUM },
	{ "void", TY_VOID },
	{ "long long", TY_LLONG },
	{ "unsigned long long", TY_ULLONG },
	{ "_Bool", TY_BOOL },
	{ NULL, 0 }
};
	
char *
type_to_text(struct type *dt) {
	struct type_node	*t;
	char			*buf = NULL;
	char			*p = NULL;
	size_t			size = 0;
	size_t			used = 0;
	int			i;

	for (t = dt->tlist; t != NULL; t = t->next) {
		switch (t->type) {
		case TN_ARRAY_OF:
		case TN_VARARRAY_OF:
			make_room(&buf, &size, used + 64);
			used += sprintf(buf+used, "an array of %d ",
				(int)t->arrarg_const);
			break;
		case TN_POINTER_TO: {
			char	*quali = "";
			if (t->ptrarg != 0) {
				switch (t->ptrarg) {
				case TOK_KEY_VOLATILE:
					quali = "volatile";
					break;
				case TOK_KEY_CONST:
					quali = "constant";
					break;
				case TOK_KEY_RESTRICT:
					quali = "restricted";
					break;
				}
			}
			make_room(&buf, &size, used + 32);
			used += sprintf(buf+used, "a %s pointer to ", quali);
			break;
		}
		case TN_FUNCTION:
			make_room(&buf, &size, used + 32);
			used += sprintf(buf+used, "a function (with %d args) returning ", t->tfunc->nargs);
			break;
		}
	}	

#if 0
	p = basic_type_names[dt->code - TY_MIN];
#endif
	for (i = 0; basic_type_names[i].name != NULL; ++i) {
		if (dt->code == basic_type_names[i].code) {
			p = basic_type_names[i].name;
			break;
		}
	}
	make_room(&buf, &size, strlen(p) + 5);
	used += sprintf(buf+used, "%s", p);
	if (dt->code == TY_STRUCT) {
		if (dt->tstruc && dt->tstruc->tag) {
			make_room(&buf, &size,
				used + strlen(dt->tstruc->tag) + 2);
			sprintf(buf+used, " %s", dt->tstruc->tag);
		}	
	}	

	return buf;
}

#ifndef PREPROCESSOR

extern void	put_ppc_llong(struct num *);

/*
 * XXX same stupid size_t cross-compilaion bug as const_from_value()..
 * this stuff SUCKS!!!
 */
struct token *
const_from_type(struct type *ty, int from_alignment, int extype, struct token *t) {
	struct token	*ret = alloc_token();
	size_t		size;
	int		size_t_size;

#if 0
	ret->type = TY_ULONG; /* XXX size_t */
#endif
	ret->type = backend->get_size_t()->code;
	if (from_alignment) {
		size = backend->get_align_type(ty);
	} else {	
		size = backend->get_sizeof_type(ty, t);
	}
	/*ret->data = n_xmemdup(&size, sizeof size);*/
	ret->data = n_xmalloc(16); /* XXX */

	size_t_size = backend->get_sizeof_type(backend->get_size_t(), NULL);
	if (sizeof size == size_t_size) {
		memcpy(ret->data, &size, sizeof size);
	} else if (sizeof(int) == size_t_size) {
		unsigned int	i = (unsigned int)size;
		memcpy(ret->data, &i, sizeof i);
	} else if (sizeof(long) == size_t_size) {
		unsigned long	l = (unsigned long)size;
		memcpy(ret->data, &l, sizeof l);
	} else if (sizeof(long long) == size_t_size) {
		unsigned long long ll = (unsigned long long)size;
		memcpy(ret->data, &ll, sizeof ll);
	} else {
		unimpl();
	}	
	  
	if (backend->abi == ABI_POWER64
		&& extype != EXPR_CONST
		&& extype != EXPR_CONSTINIT
		/* What about EXPR_OPTCONSTINIT?! */ ) {
		struct num	*n = n_xmalloc(sizeof *n);
		
		/*
		 * XXX see definition of put_ppc_llong() for an
		 * explanation of this mess
		 */
		n->type = ret->type;
		n->value = ret->data;
		put_ppc_llong(n);
		/*ret->data = llong_const;*/
		ret->data2 = llong_const;
	}	
	return ret;
}


/*
 * XXX this interface is ROTTEN!!
 * too easy to pass a ``size_t'' for value with ty=NULL by accident!!
 *
 * XXXX WOAH this was totally broken WRT cross-compilation! ``type''
 * is interpreted as host type when dealing with ``value'', and as
 * target type too by making it the type of the token! Current ad-hoc
 * kludge sucks!
 */
struct token *
const_from_value(void *value, struct type *ty) {
	struct token	*ret = alloc_token();
	size_t		size;

	if (ty == NULL) {
		ret->type = TY_INT;
		size = backend->get_sizeof_type(make_basic_type(
			TY_INT), NULL);;
	} else {
		ret->type = ty->code;
		size = backend->get_sizeof_type(ty, NULL);
	}
	if (ty && (IS_LONG(ty->code) || IS_LLONG(ty->code))) {
		if (sizeof(long) == size) {
			/* Size matches - nothing to do */
			;
		} else {
			static long long	llv;
			llv = *(int *)value;
			value = &llv;
		}
	}
	ret->data = n_xmemdup(value, size);
	if (backend->abi == ABI_POWER64
		&& ty != NULL
		&& is_integral_type(ty)
		&& size == 8) {
		struct num	*n = n_xmalloc(sizeof *n);
		static struct num nullnum;
		*n = nullnum;
		n->type = ret->type;
		n->value = ret->data;
		put_ppc_llong(n);
		ret->data2 = llong_const;
	}
	return ret;
}	

/*
 * Construct a floating point constant token of type ``type'' 
 * containing ``value'' (which must be a string parsable by sscanf().)
 */
struct token *
fp_const_from_ascii(const char *value, int type) {
	struct num	*n;
	struct token	*ret = n_xmalloc(sizeof *ret);

	n = cross_scan_value(value, type, 0, 0, 1);
	if (n == NULL) {
		return NULL;
	}
	
	/*
	 * XXX token.data is ``struct ty_float'', not 
	 * ``struct num''. Because the interfaces are
	 * still messed up, we have to get the current
	 * ty_float corresponding to ``n'' from the
	 * float list. This SUCKS!
	 */
	ret->data = float_const/*n->value*/;
	ret->type = type;
	ret->ascii = n_xstrdup(value);
	return ret;
}

int
is_integral_type(struct type *t) {
	if (t->tlist != NULL) {
		return 0;
	}
	if (IS_CHAR(t->code)
		|| IS_SHORT(t->code)
		|| IS_INT(t->code)
		|| IS_LONG(t->code)
		|| IS_LLONG(t->code)
		|| t->code == TY_ENUM) {
		return 1;
	}
	return 0;
}

int
is_floating_type(struct type *t) {
	if (t->tlist != NULL) {
		return 0;
	}
	if (t->code == TY_FLOAT
		|| t->code == TY_DOUBLE
		|| t->code == TY_LDOUBLE) {
		return 1;
	}
	return 0;
}	
			

int
is_arithmetic_type(struct type *t) {
	if (t->tlist != NULL) {
		return 0;
	}
	if (IS_FLOATING(t->code)
		|| is_integral_type(t)) {
		return 1;
	}
	return 0;
}


int
is_array_type(struct type *t) {
	struct type_node	*tn;

	if (t->tlist == NULL) {
		return 0;
	}	
	for (tn = t->tlist; tn != NULL; tn = tn->next) {
		if (tn->type != TN_ARRAY_OF) {
			return 0;
		} else {
			break;
		}	
	}
	return 1;
}	


int
is_basic_agg_type(struct type *t) {
	if (t->tlist == NULL) {
		if (t->code == TY_STRUCT || t->code == TY_UNION) {
			return 1;
		}
	} else if (is_array_type(t)) {
		return 1;
	}
	return 0;
}

int
is_scalar_type(struct type *t) {
	if (t->tlist == NULL
		&& (t->code == TY_STRUCT
		|| t->code == TY_UNION
		|| t->code == TY_VOID)) {
		return 0;
	}
	return 1;
}	

int
is_arr_of_ptr(struct type *t) {
	struct type_node	*tn;

	for (tn = t->tlist; tn != NULL; tn = tn->next) {
		if (tn->type == TN_POINTER_TO) {
			return 1;
		} else if (tn->type == TN_FUNCTION) {
			return 0;
		}	
	}
	return 0;
}

int
is_nullptr_const(struct token *constant, struct type *ty) {
	if (IS_INT(ty->code)
		&& *(unsigned *)constant->data == 0) {
		return 1;
	} else if (IS_LONG(ty->code)
		&& *(unsigned long *)constant->data == 0) {
		return 1;
	}
	return 0;
}



/*
 * The source type must be passed with a vreg because we need the null
 * pointer constant and object backing information it gives us
 */
int
check_types_assign(
	struct token *t,
	struct type *left,
	struct vreg *right,
	int to_const_ok,
	int silent) {

	struct type	*ltype = left;
	struct type	*rtype = right->type;

	if (ltype == NULL || rtype == NULL) {
		printf("attempt to assign to/from value without type :(\n");
		abort();
	}

	/*
	 * 01/26/08: Changed this to call is_modifyable(), which also
	 * rules out assignment to const-qualified pointers
	 */
	/*if (ltype->tlist == NULL && ltype->is_const && !to_const_ok) { */
	if (!is_modifyable(ltype) && !to_const_ok) {
		if (!silent) {
			errorfl(t,
				"Assignment to const-qualified object");	
		}
		return -1;
	}	

	if (is_arithmetic_type(ltype)) {
		if (!is_arithmetic_type(rtype)) {
			if (ltype->code == TY_BOOL
				&& rtype->tlist != NULL) {
				/* ok - pointer to bool */
				return 0;
			} else {
				int	allow = 0;

				if (rtype->tlist != NULL
					&& is_integral_type(ltype)) {
					/*
					 * 03/09/09: Give in and allow pointer
					 * to integer assignment with a warning
					 */
					allow = 1;
				}

				if (!silent) {
					if (allow) {
						warningfl(t,
						"Assignment from non-arithmetic to "
						"arithmetic type");
					} else {
						errorfl(t,
						"Assignment from non-arithmetic to "
						"arithmetic type");
					}
				}
				if (allow) {
					return 0;
				} else {
					return -1;
				}
			}	
		} else if (ltype->sign != rtype->sign
			&& !right->from_const) {
			/*
			 * Do not warn about signedness differences if the
			 * right side is a constant!
			 */
#if 0
			/* XXX Too verbose */
			warningfl(t,
			"Assignment from type of differing signedness");
#endif
			return 0;
		}
		return 0;		
	} else if (ltype->tlist == NULL) {
		/* Must be struct/union */
		if (rtype->tlist != NULL) {
			if (ltype->code == TY_BOOL) {
				return 0;
			} else {	
				if (!silent) {
					/* 06/01/08: Warn, not error */
					warningfl(t,
				"Assignment from pointer to non-pointer type");
				}

				/*
				 * 07/20/08: The return below was commented out!
				 * That's wrong because pointer to struct will
				 * compare assignable to struct
				 * Why was this removed?
				 */
				return -1;
			}	
		} else if (ltype->code == TY_BOOL) {
			return 0; /* _Bool b = ptr; is OK */
		} else if (rtype->code != ltype->code
			|| rtype->tstruc != ltype->tstruc) {
			if (!silent) {
				errorfl(t,
				"Assignment from incompatible type");
			}
			return -1;
		} else {
			return 0;
		}
	} else {
		/* Left is pointer of some sort */
		if (right->is_nullptr_const) {
			; /* ok */
		} else if (rtype->tlist == NULL) {
			if (!silent) {
				warningfl(t, "Assignment from non-pointer "
					"to pointer type");
			}
/*			return -1;*/
		} else if (rtype->code == TY_VOID
			&& rtype->tlist->type == TN_POINTER_TO
			&& rtype->tlist->next == NULL) {
			; /* void pointer - compatible */
		} else if (ltype->code == TY_VOID
			&& ltype->tlist->type == TN_POINTER_TO
			&& ltype->tlist->next == NULL) {
			; /* void pointer - compatible */
		} else if (compare_tlist(ltype->tlist, rtype->tlist,
				CMPTY_ARRAYPTR)) {
			if (!silent) {
			warningfl(t, "Assignment from incompatible pointer type"
					" (illegal in ISO C, and very "
					"probably not what you want)");
			} else {
				/*
				 * This is only used for transparent_union
				 * right now... in that case we do not want
				 * to allow this assignment because type-
				 * checking is the whole point of that
				 * language extension
				 */
				return -1;
			}
			return 0;
		} else if (!IS_CONST(ltype->flags) && IS_CONST(rtype->flags)) {
			if (!silent) {
				warningfl(t,
					"Assignment from const-qualified type "
					"to unqualified one");
			}
			return 0;
		} else if (rtype->code != ltype->code
			&& rtype->code != TY_VOID
			&& ltype->code != TY_VOID
		/* XXX */ && (!IS_CHAR(ltype->code) || !IS_CHAR(rtype->code))) {
			if (type_without_sign(ltype->code)
				== type_without_sign(rtype->code)) {
				if (!silent) {
					warningfl(t, "Assignment from pointer of "
						"differing signedness");
				} else {
					return -1;
				}	
				return 0;
			} else {	
				if (!silent) {
					warningfl(t, "Assignment from incompatible "
					"pointer type (illegal in ISO C, and "
					"very probably not what you want)");
				} else {
					return -1;
				}
#if 0
				return -1;
#endif
				return 0;
			}	
		} else if (IS_CONST(ltype->flags) && !IS_CONST(rtype->flags)
			&& ltype->tlist != NULL
			&& ltype->tlist->next != NULL) {
			if (!silent) {
				warningfl(t, "ISO C does not allow assignment "
					"from `T **' to `const T **' without a "
					"cast (otherwise invalid code like "
					"`const char dont_modify; char *p; const "
					"char **cp = &p; *cp = &dont_modify; *p = 0;' "
					"would pass without warning)");
			}
			return 0;
		}
	}	

	return 0;
}

struct type *
addrofify_type(struct type *ty) {
	struct type		*ret = n_xmemdup(ty, sizeof *ty);
	struct type_node	*tn;

	copy_tlist(&ret->tlist, ret->tlist);
	tn = alloc_type_node();
	tn->type = TN_POINTER_TO;
	tn->next = ret->tlist;
	ret->tlist = tn;
	return ret;
}

int
type_without_sign(int code) {
	int	rc = code;

	if (code == TY_UCHAR) rc = TY_CHAR;
	else if (code == TY_USHORT) rc = TY_SHORT;
	else if (code == TY_UINT) rc = TY_INT;
	else if (code == TY_ULONG) rc = TY_LONG;
	else if (code == TY_ULLONG) rc = TY_LLONG;
	return rc;
}	

#endif /* #ifndef PREPROCESSOR */

struct ty_string *
alloc_ty_string(void) {
	struct ty_string        *ret = n_xmalloc(sizeof *ret);
	static struct ty_string nulltys;
	*ret = nulltys;
	return ret;
}


static unsigned long	ts_count;

unsigned long
ty_string_count(void) {
	return ts_count++;
}	

struct ty_string *
make_ty_string(const char *str, size_t size) {
	struct ty_string	*ret = alloc_ty_string();
	ret->str = (char *)str;
	ret->count = ts_count++;
	ret->size = size;
	ret->ty = make_array_type(size);
	return ret;
}	


#ifndef PREPROCESSOR

/*
 * Turns a function or pointer-to-function type into the return type
 * of that (pointed to) function. Trashes ty->tlist
 */
void
functype_to_rettype(struct type *ty) {
	while (ty->tlist->type == TN_POINTER_TO) {
		ty->tlist = ty->tlist->next;
	}
	ty->tlist = ty->tlist->next; /* Skip TN_FUNCTION */
}


struct type *
dup_type(struct type *src) {
	struct type	*ret = alloc_type();

	copy_type(ret, src, 0);
	copy_tlist(&ret->tlist, src->tlist);
	return ret;
}	

struct type *
get_transparent_union_type(struct token *t, struct type *dest, struct vreg *src) {
	struct sym_entry	*se;

	for (se = dest->tstruc->scope->slist; se != NULL; se = se->next) {
		if (check_types_assign(NULL, se->dec->dtype, src, 0, 1) == 0) {
			/* We have a match */
			return se->dec->dtype;
		}
	}

	errorfl(t, "No matching argument type found for transparent "
		"union type!");
	return NULL;
}	



void					
init_to_array_size(struct type *ty, struct initializer *init) {
	int			nelem = 0;
	struct initializer	*in;
	struct tyval		*tv;

	for (in = init; in; in = in->next) {
		++nelem;
	}	
	tv = n_xmalloc(sizeof *tv);
	tv->str = NULL;
	tv->type = n_xmemdup(make_basic_type(TY_INT),
		sizeof(struct type));
	tv->value = n_xmalloc(16); /* XXX */
	/* XXXXXXXXXXXXXXXX ?!?!?!?!? CROSS???!!? ?WHAT THE ... ?!?!?!?!? */
	memcpy(tv->value, &nelem, sizeof nelem);
	tv->alloc = 1;

#if REMOVE_ARRARG
	if (ty->tlist->arrarg_const == 0) {
		/*
		 * Only set new calculated value if size hasn't been set
		 * yet! In particular, if we initialize with a string:
		 *
		 *     char buf[] = "hello";
		 *
		 * ... get_init_expr() already sets the size to the string
		 * size for us, whereas nelem would yield 1 (since it's an
		 * initializer list consisting of just one element)
		 */
		ty->tlist->arrarg_const = nelem; 
	}
	ty->tlist->have_array_size = 1;
#else
	ty->tlist->arrarg->const_value = tv;
#endif
}

int
func_returns_void(struct function *f) {
	struct type_node	*tn;

	if (f->proto->dtype->code != TY_VOID) {
		return 0;
	}

	/*
	 * XXX Why is f->fty->ret never set? I am tired of this bullsh%t
	 */
	for (tn = f->proto->dtype->tlist; tn != NULL; tn = tn->next) {
		if (tn->type == TN_FUNCTION) {
			break;
		}
	}
	if (tn->next != NULL) {
		return 0;
	}
	return 1;
}

/*
 * Tells whether a type is modifyable as far as ONLY const-correctness is
 * concerned
 */
int
is_modifyable(struct type *ty) {
	if (IS_CONST(ty->flags)) {
		if (ty->tlist == NULL) {
			return 0;
		}
	}
	if (ty->tlist != NULL
		&& ty->tlist->type == TN_POINTER_TO
		&& ty->tlist->ptrarg == TOK_KEY_CONST) {
		return 0;
	}
	return 1;
}

struct type *
func_to_return_type(struct type *ty) {
	struct type	*ret = dup_type(ty);
	ret->tlist = ret->tlist->next;
	return ret;
}	


void
ppcify_constant(struct token *res) {
	/* 10/17/08: This was missing! */
	if ((IS_LLONG(res->type) && backend->arch == ARCH_POWER)
		|| (IS_LONG(res->type) && backend->abi == ABI_POWER64)) {
		struct num      *n = n_xmalloc(sizeof *n);
		n->value = res->data;
		n->type = res->type;
		put_ppc_llong(n);
		res->data2 = llong_const;
	}
}


int
is_transparent_union(struct type *dtype) {
	if (dtype->code == TY_UNION
		&& dtype->tlist == NULL) {
		/*
		 * This may be a paramter with multiple
		 * possible argument types
		 */
		struct attrib   *attr = dtype->attributes;

		for (; attr != NULL; attr = attr->next) {
			if (attr->code == ATTRS_TRANSPARENT_UNION) {
				return 1;
			}
		}
	}
	return 0;
}
#endif /* #ifndef PREPROCESSOR */


