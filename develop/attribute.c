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
 * This file contains the functions and data needed to parse GNU-C-style
 * __attribute__ arguments
 */ 

#include "attribute.h"
#include <string.h>
#include <stdlib.h>
#include "token.h"
#include "misc.h"
#include "scope.h"
#include "symlist.h"
#include "backend.h"
#include "expr.h"
#include "type.h"
#include "decl.h"
#include "typemap.h"
#include "error.h"
#include "debug.h"
#include "n_libc.h"

#define A_OK		1
#define A_UNIMPL	2
#define A_IGNORED	3

int	used_attribute_alias;

struct name_val_pair {
	char	*name;
	int	value;
};

struct attrarg {
	int			type;
	int			extended_type;
	struct name_val_pair	*identifiers;
};

struct name_val_pair	mode_strings[] = {
	{ "QI", ATTR_MODE_DI },
	{ "DI", ATTR_MODE_DI },
	{ "SI", ATTR_MODE_SI },
	{ "HI", ATTR_MODE_SI },
	{ "byte", ATTR_MODE_SI },
	{ "word", ATTR_MODE_SI },
	{ NULL, 0 }
};
struct attrarg		mode_args[] = {
	{ TOK_IDENTIFIER, 0, mode_strings }
};
struct attrarg		aligned_args[] = {
	{ TY_INT, 0, NULL }
};
struct attrarg		alias_args[] = {
	{ TOK_STRING_LITERAL, 0, NULL }
};
struct name_val_pair	format_type_strings[] = {
	{ "printf", ATTRF_FORMAT_PRINTF },
	{ "scanf", ATTRF_FORMAT_SCANF },
	{ "strftime", ATTRF_FORMAT_IGNORED },
	{ "gnu_printf", ATTRF_FORMAT_IGNORED },
	{ "gnu_scanf", ATTRF_FORMAT_IGNORED },
	{ "gnu_strftime", ATTRF_FORMAT_IGNORED },
	{ "strfmon", ATTRF_FORMAT_IGNORED }
};
struct attrarg		format_args[] = {
	{ TOK_IDENTIFIER, 0, format_type_strings },
	{ TY_INT, 0, NULL },
	{ TY_INT, 0, NULL }
};


static struct attrent {
	char		*name;
	int		value;
	int		type;
	int		nargs; /* 0 = no args, -1 = variable number */
	int		ignorable;
	unsigned	fastattrflag;
	struct attrarg	*argdata;
	int		warned;
} attrtab[] = {
	{ "alias", ATTRF_ALIAS, A_OK, 1, 0, 0, alias_args, 0 },
	{ "aligned", ATTRS_ALIGNED, A_OK, 1, 0, CATTR_ALIGNED, aligned_args, 0 },
	{ "always_inline", ATTRF_ALWAYS_INLINE, A_UNIMPL, 0, 1, 0, NULL, 0 },
	{ "bounded", ATTRV_BOUNDED, A_IGNORED, -1, 1, 0, NULL, 0 },
	{ "cdecl", ATTRF_CDECL, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "cleanup", ATTRV_CLEANUP, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "common", ATTRV_COMMON, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "const", ATTRF_CONST, A_IGNORED, 0, 1, 0, NULL, 0 },	
	{ "constructor", ATTRF_CONSTRUCTOR, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "deprecated", ATTRS_DEPRECATED, A_IGNORED, 0, 1, 0, NULL, 0 },
	{ "destructor", ATTRF_DESTRUCTOR, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "dllimport", ATTRV_DLLIMPORT, A_UNIMPL, 0, 0, 0, NULL, 0 },	
	{ "dllexport", ATTRV_DLLEXPORT, A_UNIMPL, 0, 0, 0, NULL, 0 },	
	{ "eightbit_data", ATTRF_EIGHTBIT_DATA, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "exception_handler", ATTRF_EXCEPTION_HANDLER, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "externally_visible", ATTRF_EXTERNALLY_VISIBLE, A_IGNORED, 0, 1, 0, NULL, 0 },
	{ "far", ATTRF_FAR, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "fastcall", ATTRF_FASTCALL, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "flatten", ATTRF_FLATTEN, A_IGNORED, 0, 1, 0, NULL, 0 },
	{ "force_align_arg_pointer", ATTRF_FORCE_ALIGN_ARG_POINTER, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "format", ATTRF_FORMAT, A_OK, 3, 0, CATTR_FORMAT, format_args, 0 },
	{ "format_arg", ATTRF_FORMAT_ARG, A_IGNORED, 1, 1, 0,NULL, 0 },
	{ "function_vector", ATTRF_FUNCTION_VECTOR, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "interrupt", ATTRF_INTERRUPT, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "interrupt_handler", ATTRF_INTERRUPT_HANDLER, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "longcall", ATTRF_LONGCALL, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "long_call", ATTRF_LONG_CALL, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "gnu_inline", ATTRF_GNU_INLINE, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "kspisusp", ATTRF_KSPISUSP, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "malloc", ATTRF_MALLOC, A_IGNORED, 0, 1, 0, NULL, 0 },
	{ "may_alias", ATTRS_MAY_ALIAS, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "mode", ATTRV_MODE, A_OK, 1, 0, CATTR_MODE, mode_args, 0 },
	{ "model", ATTRV_MODEL, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "naked", ATTRF_NAKED, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "near", ATTRF_NEAR, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "nesting", ATTRF_NESTING, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "nmi_handler", ATTRF_NMI_HANDLER, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "no_instrument_function",
		ATTRF_NO_INSTRUMENT_FUNCTION, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "nocommon", ATTRV_NOCOMMON, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "noinline", ATTRF_NOINLINE, A_IGNORED, 0, 1, 0, NULL, 0 },
	{ "nonnull", ATTRF_NONNULL, A_IGNORED, 0, 1, 0, NULL, 0 },
	{ "noreturn", ATTRF_NORETURN, A_IGNORED, 0, 1, 0, NULL, 0 },
	{ "nothrow", ATTRF_NOTHROW, A_IGNORED, 0, 0, 0, NULL, 0 },
	{ "packed", ATTRS_PACKED, A_OK, 0, 0, CATTR_PACKED, NULL, 0 },
	{ "pure", ATTRF_PURE, A_IGNORED, 0, 1, CATTR_PURE, NULL, 0 },
	{ "regparm", ATTRF_REGPARM, A_UNIMPL, 1, 0, 0, NULL, 0 },
	{ "returns_twice", ATTRF_RETURNS_TWICE, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "saveall", ATTRF_SECTION, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "section", ATTRF_SECTION, A_UNIMPL, 0, 0, CATTR_SECTION, NULL, 0 },
	{ "sentinel", ATTRF_SENTINEL, A_IGNORED, 0, 1, 0, NULL, 0 },
	{ "shared", ATTRV_SHARED, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "shortcall", ATTRF_SHORTCALL, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "short_call", ATTRF_SHORT_CALL, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "signal", ATTRF_SIGNAL, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "sp_switch", ATTRF_SP_SWITCH, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "sseregparam", ATTRF_SSEREGPARAM, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "stdcall", ATTRF_STDCALL, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "tiny_data", ATTRF_TINY_DATA, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "tls_model", ATTRV_TLS_MODEL, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "transparent_union", ATTRS_TRANSPARENT_UNION, A_OK, 0, 0, CATTR_TRANSPARENT_UNION, NULL, 0 },
	{ "trap_exit", ATTRF_TRAP_EXIT, A_UNIMPL, 0, 0, 0, NULL, 0 },
 	{ "unused", ATTRF_UNUSED, A_IGNORED, 0, 1, CATTR_UNUSED, NULL, 0 },
	{ "used", ATTRF_USED, A_IGNORED, 0, 1, CATTR_USED, NULL, 0 },
	{ "vector_size", ATTRV_VECTOR_SIZE, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "visibility", ATTRF_VISIBILITY, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ "warn_unused_result", ATTRF_WEAK, A_IGNORED, 0, 1, 0, NULL, 0 },
	{ "weak", ATTRF_WEAK, A_UNIMPL, 0, 0, CATTR_WEAK, NULL, 0 },
	{ "weakref", ATTRF_WEAK, A_UNIMPL, 0, 0, 0, NULL, 0 },
	{ 0, 0, 0, 0, 0, 0, NULL, 0 }
};

static struct attrent *
find_attrent(char *name) {
	int	i;
	size_t	len = 0;
	char	*p = NULL;

	/*
	 * All attribute specifiers might either have the form ``attr'' or
	 * ``__attr__'', or ``__attr'', or ...????? so we have to determine
	 * which is the case here
	 */
	if (*name == '_') {
		if (name[1] != '_') {
			return NULL;
		}
		p = name + 2;
		if ((len = strlen(p)) < 3) {
			/* Bogus attribute, becase it can't match __*X*__ */
			return NULL;
		}
		if (p[len - 1] != '_' || p[len - 2] != '_') {
			/* Doesn't end with __ */
			;
		} else {
			/* Ends with __ */
			len -= 2;
		}
	}

	for (i = 0; attrtab[i].name != NULL; ++i) {
		if (p == NULL) {
			/* No trailing __ to take into account */
			if (strcmp(attrtab[i].name, name) == 0) {
				/* Found */
				return &attrtab[i];
			}
		} else if (strncmp(attrtab[i].name, p, len) == 0) {
			/* Found */
			return &attrtab[i];
		}
	}
	/* Not found */
	return NULL;
}
	

static void
free_attrib(struct attrib *attr) {
	struct attrib	*at;

	while (attr) {
		at = attr;
		attr = attr->next;
		free(at);
	}
}


static struct attrib *
alloc_attrib(void) {
	struct attrib	*ret = n_xmalloc(sizeof *ret);
	static struct attrib	nullattrib;
	*ret = nullattrib;
	return ret;
}	

void
append_attribute(struct attrib **head, struct attrib *attr) {
	int	is_packed = attr->code == ATTRS_PACKED;

	if (*head == NULL) {
		*head = attr;
	} else {
		/* Check whether this is a duplicated attribute */
		struct attrib	*tmp;
		struct attrib	*lastnode = NULL;

		for (tmp = *head; tmp != NULL; tmp = tmp->next) {
			if (tmp->next == NULL) {
				lastnode = tmp; 
			}
			if (tmp->code == attr->code) {
				/*
				 * 03/03/09: Just warn. Apparently the
				 * definition of snd_seq_ev_ext_t in
				 * alsa/seq_event.h causes a duplicate
				 * ``packed'' definition (but may be
				 * a false alarm too)
				 */
				warningfl(attr->tok,
					"Duplicated attribute");
				return;
			} else if (is_packed && tmp->code == ATTRS_ALIGNED) {
				/*
				 * 05/29/08: This means that a structure
				 * member is aligned, but the entire struct
				 * is packed. The aligned member remains
				 * aligned - Don't append packed attribute!
				 */
				return;
			}
		}

		/* Append */
		lastnode->next = attr;
	}
	attr->next = NULL;
}

struct attrib *
get_single_attribute(struct token **tok, int *fatalerror) {
	struct token	*t;
	struct token	*start;
	struct attrent	*at;
	struct attrib	*ret;
	char		*attrname;
	int		parens = 0;
	struct token	*attrargs[128];
	int		intargs[128];
	struct attrarg	*arginfo;
	int		argidx = 0;
	int		error = 0;
	int		i;

	*fatalerror = 0;
#define N_ATTRARGS (sizeof attrargs / sizeof attrargs[0])
	
	t = start = *tok;
	attrname = t->data;
	if ((at = find_attrent(attrname)) == NULL) {
		warningfl(t, "Unknown attribute `%s'", attrname);
		*fatalerror = 1;
		return NULL;
	}

	if (next_token(&t) != 0) {
		*fatalerror = 1;
		return NULL;
	}

	if ((t->type == TOK_OPERATOR
		&& *(int *)t->data == TOK_OP_COMMA)
		|| t->type == TOK_PAREN_CLOSE) {
		/*
		 * This is a comma or parentheses, so the current
		 * attribute already ends here. Is that OK?
		 *
		 * 05/27/08: Added special case for ``aligned''
		 * attribute, where the operand is optional
		 */
		if ((at->nargs != 0 && at->nargs != -1)
			&& at->value != ATTRS_ALIGNED) {
			/* Error! (but only warn) */
			warningfl(start, "Not enough arguments for "
				"attribute `%s' - ignoring", attrname);
			return NULL;
		} else {
			t = t->prev;
			goto skipargs;
		}
	}	


	/* Read arguments to attribute */
	if (t->type != TOK_PAREN_OPEN) {
		warningfl(t, "Syntax error at `%s', expected "
			"opening parentheses",
			t->ascii);
		*fatalerror = 1;
		return NULL;
	}
		
	if (next_token(&t) != 0) {
		return NULL;
	}
	++parens;

	for (; t != NULL; t = t->next) {
		int	is_paren = 1;

		if (t->type == TOK_PAREN_OPEN) {
			++parens;
		} else if (t->type == TOK_PAREN_CLOSE) {
			--parens;
		} else {
			is_paren = 0;
		}

		if (parens == 0) {
			/* Done! */
			break;
		} else {	
			if (argidx > (int)N_ATTRARGS) {
				if (!error) {
					warningfl(start, "Too many arguments "
					"for attribute `%s' - ignoring ",
						attrname);
					error = 1;
				}
			} else if (!error) {
				if (!is_paren) {
					attrargs[argidx++] = t;
				}
			}
			if (t->next != NULL
				&& t->next->type == TOK_OPERATOR
				&& *(int *)t->next->data == TOK_OP_COMMA) {
				t = t->next;
			}
		}
	}
	if (error) {
		return NULL;
	}

skipargs:	
	/*
	 * Now we have an attribute declaration complete with arguments. Let's
	 * check if it looks valid
	 */
	if (at->nargs != -1 && at->nargs != argidx) {
		int	bad = 1;

		if (!(at->warned & 1) && at->type != A_IGNORED) {
			if (argidx == 0 && at->value == ATTRS_ALIGNED) {
				bad = 0;
			} else {	
				warningfl(t, "Attribute `%s' used with wrong "
					"number of arguments (%d instead of "
					"%d)", attrname, argidx, at->nargs);
				at->warned |= 1;
			}
		}
		if (bad) {
			*tok = t;
			return NULL;
		}
	} else if (at->type == A_UNIMPL) {
		if (!(at->warned & 2)) {
			warningfl(t, "Attribute `%s' is not implemented - this "
				"code may not work correctly without it",
				attrname);
			at->warned |= 2;
		}
		*tok = t;
		return NULL;
	} else if (at->type == A_IGNORED) {
		/* Safe to ignore */
		*tok = t;
		return NULL;
	}

	/*
	 * Check if the arguments have the correct type
	 */
	arginfo = at->argdata;
	for (i = 0; i < argidx; ++i) {
		int	is_num = 0;

		if (attrargs[i]->type != arginfo[i].type) {
			if (IS_CONSTANT(attrargs[i]->type)
				&& attrargs[i]->type !=
				TOK_STRING_LITERAL) {
				/*
				 * This may be some sort of wicked non-int
				 * type (e.g. unsigned long) while the
				 * expected type is ``int''
				 */
				if (!IS_FLOATING(attrargs[i]->type)
					&& arginfo[i].type == TY_INT) {
					is_num = 1;
				} else {
					error = 1;
				}
			} else {
				error = 1;
			}
			if (error) {
				warningfl(start, "Argument %d for "
					"attribute %s has wrong type",
					i+1, attrname);
				return NULL;
			}
		} else if (attrargs[i]->type == TY_INT) {
			is_num = 1;
		}

		if (is_num) {
			static struct tyval	tv;
			size_t			val;

			tv.type = make_basic_type(attrargs[i]->type);
			tv.value = attrargs[i]->data;
			val = cross_to_host_size_t(&tv);
			intargs[i] = val;
		} else if (attrargs[i]->type == TOK_IDENTIFIER) {
			if (arginfo->identifiers != NULL) {
				/*
				 * This identifier must correspond to some
				 * expected string
				 */
				struct name_val_pair	*tmpnv;
				char			*p = attrargs[i]->data;
				int			len;

				if (p[0] == '_' && p[1] == '_') { 
					p = (char *)attrargs[i]->data + 2;
				}
				len = strlen(p);
				if (p[len - 1] == '_' && len > 1
					&& p[len - 2] == '_') {	
					len -= 2;
				}

				for (tmpnv = arginfo->identifiers;
					tmpnv->name != NULL;
					++tmpnv) {
					if (memcmp(p, tmpnv->name, len) == 0) {
						intargs[i]= tmpnv->value;
						break;
					}
				}
				if (tmpnv->name == NULL) {
					warningfl(start, "Argument %d for "
						"attribute %s not recognized",
						i+1, attrname);
					return NULL;
				}
			}
		}
	}

	ret = alloc_attrib();
	ret->tok = start;
	ret->fastattrflag = at->fastattrflag;
	switch (ret->code = at->value) {
	case ATTRV_MODE:
		ret->iarg = intargs[0];
		break;
	case ATTRS_TRANSPARENT_UNION:
		break;
	case ATTRS_ALIGNED:
		if (argidx == 0) {
			/*
			 * 05/27/08: Attribute without operand; This means
			 * highest possible alignment on the given platform
			 */
			ret->iarg = backend->get_align_type(NULL);
		} else {	
			/* Supplied alignment value */
			ret->iarg = intargs[0];
		}
		break;
	case ATTRS_PACKED:
		/*
		 * 05/29/08: Packed implemented!
		 */
		break;
	case ATTRF_ALIAS:
		{
			struct ty_string	*ts = attrargs[0]->data;
			ret->parg = ts->str;
			used_attribute_alias = 1;
		}

		break;
	case ATTRF_FORMAT:
		/*
		 * 02/01/10: Format checking
		 */
		if (intargs[0] == ATTRF_FORMAT_IGNORED) {
			/* Something which isn't supported yet */
			free(ret);
			*tok = t;
			return NULL;
		}
		ret->iarg = intargs[0];
		ret->iarg2 = intargs[1];
		ret->iarg3 = intargs[2];
		break;
	}
	
	*tok = t;
	return ret;
}

void
merge_attr_with_type(struct type *ty) {
	struct attrib	*last_node = NULL;
	struct attrib	*attr;

	for (attr = ty->attributes; attr != NULL;) {
		int	del = 0;

		switch (attr->code) {
		case ATTRS_TRANSPARENT_UNION:
			if (ty->code != TY_UNION
				|| ty->tlist != NULL) {
				errorfl(attr->tok, "Attribute "
					"`transparent_union' applied "
					"to non-union type");
				del = 1;
			} else {
				/*
				 * Check whether all members of the
				 * union have the same representation
				 */
				struct sym_entry	*se;
				struct decl		*first;

				se = ty->tstruc->scope->slist;
				first = se->dec;

				for (; se != NULL; se = se->next) {
					if (!backend->
				same_representation(first->dtype,
					se->dec->dtype)) {
						errorfl(attr->tok,
						"Not all members in "
						"`transparent_union'-"
						"attributed union have the "
						"same representation");
						del = 1;
					}
				}
			}
			break;
		case ATTRV_MODE:
			if (attr->iarg == ATTR_MODE_DI) {
				if (ty->code == TY_FLOAT) {
					ty->code = TY_DOUBLE;
				} else if (is_integral_type(ty)) {
					/*
					 * 07/06/09: This was missing the
					 * unsigned distinction!
					 *
					 *    unsigned int foo __attribute__((__mode__(__DI__)));
					 *
					 * XXX Can this break if the unsigned
					 * is placed after the attribute?
					 */
					if (ty->sign == TOK_KEY_UNSIGNED) {
						ty->code = TY_ULLONG;
					} else {
						ty->code = TY_LLONG;
					}
				}
			}
			break;
		case ATTRF_ALIAS: /* 05/17/09 */
			/*
			 * 05/17/09: Initial support for attribute alias. For
			 * now we treat it like asm renaming, e.g.
			 *
			 *    void foo() __asm__("hello");
			 */
			break;
		case ATTRF_FORMAT: /* 02/01/10 */
			break;
		case ATTRS_ALIGNED:
			if ((attr->iarg & (attr->iarg - 1)) != 0) {
				errorfl(attr->tok, "Alignment is not "
					"power of two");
				del = 1;
			}
			break;
		case ATTRS_PACKED:
			/*
			 * 05/29/08: Implemented packed attribute
			 */
			if ((ty->code != TY_STRUCT && ty->code != TY_UNION) 
				|| ty->tlist != NULL) {
				warningfl(attr->tok, "Packed attribute to "
					"non-struct type, ignoring");
				del = 1;
			} else {
				struct sym_entry	*se;
				struct attrib		*a;
				int			new_alignment;

				a = alloc_attrib();
				a->type = ATTR_STRUCT;
				a->code = ATTRS_ALIGNED;
				a->is_impl = 1;
				a->fastattrflag = CATTR_ALIGNED;
				a->iarg = 1;
				a->tok = attr->tok;
				a->next = NULL;

				new_alignment = 0;
				for (se = ty->tstruc->scope->slist;
					se != NULL;
					se = se->next) {
					int	temp_align;
					
					/*
					 * Set alignment to 1 for all struct
					 * members
					 * XXX: Could this cause problems
					 * anywhere due to the types being
					 * shared somewhere?
					 */
					append_attribute(&se->dec->dtype->
						attributes, a);
					/*
					 * XXXXXXXXXXXXXX manual fastattr
					 * setting is error prone, append and
					 * set flag both in same function!
					 */
					se->dec->dtype->fastattr |= CATTR_ALIGNED;
					temp_align = backend->get_align_type(
						se->dec->dtype);
					if (temp_align > new_alignment) {
						new_alignment = temp_align;
					}
				}

				/*
				 * Reset alignment for entire struct.
				 * We have to do this instead of assuming
				 * 1 automatically because some members may
				 * have an __attribute__((aligned)) which
				 * wins over the packed attribute
				 */
				if (new_alignment != 0) {
					ty->tstruc->alignment = new_alignment;
				} else {
					/*
					 * Must be empty struct?!
					 * struct foo {} What to do???
					 */
					;
				}
			}
			break;
		default:
			del = 1;
		}
		if (del) {
			/*
			 * Attribute makes no sense, delete it
			 */
			struct attrib	*tmp = attr->next;

			if (last_node == NULL) {
				/* Must be head */
				ty->attributes = ty->attributes->next;
			} else {
				last_node->next = attr->next;
			}
			free(attr);
			attr = tmp;
		} else {
			ty->fastattr |= attr->fastattrflag;
			last_node = attr;
			attr = attr->next;
		}
	}
}

void
merge_attr_with_decl(struct decl *dec, struct attrib *attr) {
	for (; attr != NULL; attr = attr->next) {
		if (attr->code == ATTRF_ALIAS) {
			dec->asmname = attr->parg;
		}
	}
}

struct attrib *
lookup_attr(struct attrib *attr, int type) {
	for (; attr != NULL; attr = attr->next) {
		if (attr->code == type) {
			return attr;
		}
	}
	return NULL;
}

struct attrib *
dup_attr_list(struct attrib *attr) {
	struct attrib	*ret = NULL;
	struct attrib	*ret_tail = NULL;

	for (; attr != NULL; attr = attr->next) {
		struct attrib	*tmp = n_xmemdup(attr, sizeof *attr);

		if (ret == NULL) {
			ret = ret_tail = tmp;
		} else {
			ret_tail->next = tmp;
			ret_tail = tmp;
		}
	}
	return ret;
}

struct attrib *
get_attribute(struct type *ty, struct token **tok) {
	struct token		*t = *tok;
	struct attrib		*ret = NULL;
	int			err;

#if 0
	/* XXX temporary */
	*tok = ignore_attr(*tok);
	return NULL;
#endif

	/* 
	 * Function might be called at ``__attribute__'' so as to avoid
	 * cluttering the caller with next_token() calls
	 */
	if (t->type == TOK_KEY_ATTRIBUTE) {
		if (next_token(&t) != 0) {
			return NULL;
		}
	}

	/* Every attribute must look like ``__attribute__((attr-list))'' */
	if (t->type != TOK_PAREN_OPEN) {
		errorfl(t, "Syntax error at %s", t->ascii);
		*tok = t;
		return NULL;
	}

	if (next_token(&t) != 0) {
		*tok = t;
		return NULL;
	}

	if (t->type != TOK_PAREN_OPEN) {
		errorfl(t, "Syntax error at %s", t->ascii);
		*tok = t;
		return NULL;
	}

#if 0
	ret = n_xmalloc(sizeof *ret);
	*ret = nullattr;
#endif

	if (next_token(&t) != 0) {
		*tok = t;
		return NULL;
	}
	err = 0;
	for (; t != NULL; t = t->next) {
		if (t->type == TOK_PAREN_CLOSE) {
			/* Ends here */
			if (next_token(&t) != 0) {
				*tok = t;
				free_attrib(ret);
				return NULL;
			}

			/* ... )) */
			if (t->type != TOK_PAREN_CLOSE) {
				errorfl(t, "Syntax error at %s", t->ascii);
				*tok = t;
				free_attrib(ret);
				return NULL;
			}

			if (err) {
				return NULL;
			} else {
				*tok = t->next;
				return ret;
			}
		} else {
			/* This must be another attribute */
			struct attrib	*tmp;
			int		fatal;

			tmp = get_single_attribute(&t, &fatal);
			if (tmp == NULL && fatal) {
				*tok = ignore_attr(*tok);
				return NULL;
			} else if (tmp != NULL) {
				if (ty != NULL) {
					if (ty->attributes == NULL) {
						ty->attributes = ty->attributes_tail =
							tmp;
					} else {
						ty->attributes_tail->next = tmp;
						ty->attributes_tail = tmp;
					}
				}

				if (ret == NULL) {
					ret = tmp;
				}
			}

			if (t->next
				&& t->next->type == TOK_OPERATOR
				&& *(int *)t->next->data == TOK_OP_COMMA) {
				t = t->next;
			}
		}
	}
	errorfl(*tok, "Unexpected end of file");
	*tok = t;
	return NULL;
}


struct token *
ignore_attr(struct token *tok) {
	int	parens = 0;

	if (tok == NULL) {
		return NULL;
	}
	if (tok->type != TOK_KEY_ATTRIBUTE) {
		return tok;
	}
	for (tok = tok->next; tok != NULL; tok = tok->next) {
		if (tok->type == TOK_PAREN_OPEN) {
			++parens;
		} else if (tok->type == TOK_PAREN_CLOSE) {
			if (--parens == 0) {
				return tok->next;
			}
		}
	}
	return NULL;
}

void
stupid_append_attr(struct attrib **dest, struct attrib *src) {
	struct attrib	*d = *dest;

	while (d && d->next) {
		d = d->next;
	}
	if (d) {
		d->next = src;
	} else {
		*dest = src;
	}	
}

