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
 *
 * Parsing of declarations
 */
#include "decl.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "error.h"
#include "defs.h"
#include "token.h"
#include "type.h"
#include "misc.h"
#include "scope.h"
#include "typemap.h"
#include "symlist.h"
#include "expr.h"
#include "standards.h"
#include "icode.h" /* expr_to_icode() for evaluating typeof() argument */
#include "decl_adv.h"
#include "cc1_main.h"
#include "fcatalog.h"
#include "attribute.h"
#include "inlineasm.h"
#include "debug.h"
#include "backend.h"
#include "n_libc.h"

struct decl *
alloc_decl(void) {
	struct decl			*ret;
	static struct decl	nulldecl;

#if 1 
static int seqno;
++seqno;
#endif
	ret = n_xmalloc(sizeof *ret);
	*ret = nulldecl;
#if  1 
	ret->seqno = seqno;
#endif
	return ret;
}


void
append_decl(struct decl **head, struct decl **tail, struct decl *d) {
	if (*head == NULL) {
		*head = *tail = d;
	} else {
		(*tail)->next = d;
		*tail = (*tail)->next;
	}
}

static struct token *find_cast_start(struct token *pos, int mode);

/*
 * Searches for next occurance of identifier in list pointed to by pos. Returns
 * a pointer to the identifier on success, else a null pointer.
 * Used to determine identifier in declarations.
 */
static struct token *
find_ident(struct token *pos, int mode, struct type *ty) {
	struct token	*ret = NULL;
	struct token	*start;
	struct token	*prevtok = NULL;

	/* Characters that might occur in first part of declarator */
	static int		okay[] = {
		/* Pointer */
		TOK_OP_AMB_MULTI,
		TOK_PAREN_OPEN,

		/* Pointer qualifiers, as in ``char *restrict p;'' */
		TOK_KEY_CONST,
		TOK_KEY_RESTRICT, 
		TOK_KEY_VOLATILE,
		0
	};

	/* Might be needed if this is a function argument */
	start = pos;

#ifdef DEBUG2
	puts("Searching for identifier in declaration");
#endif
	for (; pos != NULL; pos = pos->next) {
		prevtok = pos;
#ifdef DEBUG2
		printf("%s ", pos->ascii);
#endif
		if (pos->type == TOK_IDENTIFIER) {
			/* Found! */
			ret = pos;
			break;
		} else if (pos->type == TOK_KEY_ATTRIBUTE) {	
			struct token	*start = pos;

			/*
			 * We have to parse and UNLINK any attributes
			 * here because of nonsense stuff like;
			 *
			 *     void *__attribute__((pure)) foo();
			 *
			 * Yes this is legal, you can put attributes
			 * anywhere, and regex of course uses this.
			 * The parser will get confused later if we
			 * don't remove this here
			 *
			 * XXX Attributes are thrown away at this
			 * point - are there any that we could
			 * support yet?
			 */
			(void) get_attribute(ty, &pos);
			if (pos) {
				pos = pos->prev;
			}	
			if (start->prev != NULL) {
				start->prev->next = pos;
			}
			if (pos != NULL) {
				pos->prev = start->prev;
			}
			continue;
		} else {
			/*
			 * Let's see whether the character is legal
			 * (catch syntax errors)
			 */
			int	i;
			int	type;

			if (pos->type == TOK_OPERATOR) {
				type = *(int *)pos->data;
			} else {
				type = pos->type;
			}
			for (i = 0; okay[i] != 0; ++i) {
				if (type == okay[i]) {
					/* Found */
					break;
				}
			}
			if (okay[i] == 0) {
				/*
				 * Character is illegal, but might be okay if
				 * we are parsing a function argument
				 */
				if (mode == DECL_FUNCARG) {
					/*
					 * Alright, perhaps this must be parsed
					 * cast-like, since function arguments
					 * in prototypes might be mere type 
					 * names, e.g. ``void f(int[]);''
					 * Caller must check for defintions -
					 * ``void f(int[]) { puts("hello"); }''
					 * is illegal!
					 */
					ret = find_cast_start(start,
						TOK_OP_COMMA);
					return ret;
				}

				errorfl(pos, "Syntax error at %s", pos->ascii);
				return NULL;
			}
		}
	}

	if (ret == NULL) {
		errorfl(prevtok, "Unexpected end of file");
	}

	return ret;
}


/*
 * Finds starting point to parse a cast, starting from position pos.
 * Returns a pointer to the starting point on success, else a null
 * pointer.
 * Since a cast MUST be a scalar type (that is, an integral, floating
 * point or pointer type), the token we are searching for is an
 * asterisk. This is because this routine might only be called if the
 * base type (such as ``int'' or ``unsigned long'') does NOT constitute
 * the entire cast. The effect is that we can only get a scalar out of
 * this declarator if it really is a pointer
 *
 * XXX C99 compound literals also permit stuff like array-casts!
 *
 * char *p = (char[4]){ 'a', 'b', 'c', 'd' };
 */
static struct token *
find_cast_start(struct token *pos, int mode) {
	struct token	*asterisk = NULL;

#ifdef DEBUG
	printf("Searching for start of cast ... ");
#endif

	for (; pos != NULL; pos = pos->next) {
		int	type;
#ifdef DEBUG2
		printf("%s ", pos->ascii); 
#endif
		if (pos->type == TOK_OPERATOR) {
			type = *(int *)pos->data;
		} else {
			type = pos->type;
		}

		if (type == TOK_PAREN_OPEN) {
			/* This is OK */
			if (mode == TOK_OP_COMMA) {
				/*
				 * Might be funtion pointer of form ``int()''
				 */
				asterisk = pos;
			}
			continue;
		} else if (type == TOK_PAREN_CLOSE) {
			if (asterisk != NULL) {
				/*
				 * The asterisk was found at the last
				 * iteration!
				 */
#ifdef DEBUG2
				printf(" - found!\n");
#endif
				return asterisk;
			} else {
				errorfl(pos, "Cast specifies non-scalar type");
				return NULL;
			}
		} else if (type == TOK_OP_AMB_MULTI) {
			/* 
			 * Might be beginning, but does not have to -
			 * Parentheses must be used to judge for
			 * pathological cases like a pointer to an
			 * array of pointers to an array, i.e.
			 *
			 * (char (*(*)[20])[40])foo;
			 *
			 * ...where in this example we are interested
			 * in the second asterisk. The strategy is to
			 * preserve a pointer to this token and check
			 * it at the next occurance of a closing
			 * parentheses
			 */
			asterisk = pos;
		} else if (type == TOK_OP_COMMA && mode == TOK_OP_COMMA) {
			/* function argument */
			return asterisk;
		} else if (type == TOK_PAREN_CLOSE && mode == TOK_OP_COMMA) {
			/* final function argument */
			return asterisk;
		} else {
			if (asterisk != NULL) {
				/* Qualifier in pointer cast */
				if (IS_QUALIFIER(type)) {
					/* Just ignore */
					continue;
				}
			}

			/*
			 * C99 permits array casts for compound literals -
			 * char *p = (char[]){ 'a', 'b', 'c', 0 };
			 * char (*p)[2] = (char[][2]) {
			 *		{ 'a', 'b' },
			 *		{ 'c', 'd' }
			 * };
			 */
			if (type == TOK_ARRAY_OPEN) {
#ifdef DEBUG2
				printf(" - found!\n");
#endif
				/*
				 * If ``*'' has been reached, we must return
				 * that
				 */
				if (asterisk) {
					return asterisk;
				} else {
					return pos;
				}
			}

			/* 
			 * This cast is definitely illegal
			 */
			errorfl(pos, "Syntax error at %s", pos->ascii);
			return NULL;
		}
	}
	errorfl(pos, "Unexpected end of file");
	return NULL;
}
					
			


/*
 * Parses the declarations base type, including storage class,
 * signedness specifiers and const/volatile/etc qualifiers. The result
 * is a pointer to a dynamically allocated ``struct type'' structure
 * on success, else a null pointer
 * On success, *curtok is also updated to point to the current token
 */

#define MAKE_TYPE(ty) \
	(ty == DECL_CAST ? "cast" : "declaration")

static struct type *
get_base_type(struct token **curtok, int type) {
	struct type		*ty;
	struct type		*tytmp;
	struct token		*tok;
	struct token		*prevtok = NULL;
#ifdef DEBUG2
	char			decascii[1024] = { 0 };
#endif
	char			*tag;
	int			repetitions = 0;
	int			curtype;
	int			errors = 0;
	struct ty_struct	*is_struct_def = NULL;

	ty = alloc_type();
	ty->line = (*curtok)->line;
	ty->file = (*curtok)->file;

	if (doing_fcatalog) {
		/* Can't be implicit declaration, may be dummy typedef */
		;
	} else if (!IS_KEYWORD((*curtok)->type)
		&& ((*curtok)->type != TOK_IDENTIFIER
			|| lookup_typedef(curscope, (*curtok)->data, 1,
					LTD_IGNORE_IDENT) == NULL)) {
		/* No base type specified - implicit int! ... as in main() {} */
		ty->code = TY_INT;
		ty->sign = TOK_KEY_SIGNED;
		ty->implicit |= IMPLICIT_INT;
		return ty;
	}

	/* Read base type and storage class/signess specifiers */
	for (tok = *curtok; tok != NULL; tok = tok->next) {
		int	was_attribute = 0;

		prevtok = tok;

		while (tok->type == TOK_KEY_ATTRIBUTE) {
			/* XXX */
			struct attrib	*a;
			struct token	*start = tok;

			was_attribute = 1;
			if ((a = get_attribute(ty, &tok)) == NULL) {
				continue;
			}

			if (is_struct_def) {
				stupid_append_attr(&is_struct_def->attrib,
					dup_attr_list(a));
			}

			/*
			 * Unlink attribute stuff so the declarator
			 * parsing can ignore it
			 * XXXXXXXXXXXXXXX WARNING ATTENTION DANGER
			 * This means we can't correctly retry this
			 * operation! Is that OK?
			 */
			if (start->prev != NULL) {
				start->prev->next = tok;
			}
			if (tok != NULL) {
				tok->prev = start->prev;
			}
		}
		if (was_attribute) {
			if (tok->prev) {
				tok = tok->prev;
			} else {
				/* XXX :-( */
				static struct token	dummy;
				dummy.next = tok;
				tok = &dummy;
			}
			continue;
		}	
		if (!IS_KEYWORD(tok->type) && tok->type != TOK_IDENTIFIER) {
			/* End of base type found */
			break;
		}

		switch (tok->type) {
		case TOK_KEY_UNSIGNED:
		case TOK_KEY_SIGNED:
			if (ty->sign) {
				/* Sign already specified */
				if (ty->sign != tok->type) {
					errorfl(tok,
				"Type cannot be both signed and unsigned");
					free(ty);
					return NULL;
				} else {
					warningfl(tok,
				"Duplicate use of signedness specifier");
				}
			}
			ty->sign = tok->type;
			break;
		case TOK_KEY_VOLATILE:
			if (IS_VOLATILE(ty->flags)) {
				warningfl(tok,
				"Duplicate use of `volatile' qualifier");
			}
			ty->flags |= FLAGS_VOLATILE;
			break;
		case TOK_KEY_INLINE:
			if (IS_INLINE(ty->flags)) {
				warningfl(tok,
					"Duplicate use of `inline' qualifier");
			}
			ty->flags |= FLAGS_INLINE;
			break;
		case TOK_KEY_RESTRICT:
			if (IS_RESTRICT(ty->flags)) {
				warningfl(tok,
				"Duplicate use of `restrict' qualifier");
			}
			ty->flags |= FLAGS_RESTRICT;
			break;
		case TOK_KEY_CONST:
			if (IS_CONST(ty->flags)) {
				warningfl(tok,
					"Duplicate use of const qualifier");
			}
			ty->flags |= FLAGS_CONST;
			break;
		case TOK_KEY_THREAD:
			if (IS_THREAD(ty->flags)) {
				warningfl(tok,
					"Duplicate use of __thread specifier");
			}
			ty->flags |= FLAGS_THREAD;
			break;
		case TOK_KEY_TYPEDEF:
			/* 
			 * typedefs are storage class specifiers for convenient
			 * syntax 
			 */
		case TOK_KEY_STATIC:
		case TOK_KEY_EXTERN:
		case TOK_KEY_AUTO:
		case TOK_KEY_REGISTER:
			/*
			 * For now the register keyword does not have an
			 * effect 
			 */
			if (ty->storage != 0) {
				if (ty->storage != tok->type) {
					errorfl(tok,
					"Multiple storage classes specified");
					free(ty);
					return NULL;
				}
			}

			ty->storage = tok->type;
			break;
		case TOK_KEY_STRUCT:
		case TOK_KEY_UNION:
		case TOK_KEY_ENUM:
			/* Sanity checking */
			if (ty->code != 0) {
				errorfl(tok,
					"Two or more types specified in %s",
					MAKE_TYPE(type));
				free(ty);
				return NULL;
			}

			if (prevtok->type == TOK_KEY_STRUCT) {
				curtype = ty->code = TY_STRUCT;
			} else if (prevtok->type == TOK_KEY_UNION) {
				curtype = ty->code = TY_UNION;
			} else {
				curtype = ty->code = TY_ENUM;
			}

			/* Proceed to tag or { */
			if (next_token(&tok) != 0) {
				free(ty);
				return NULL;
			}

			/* Check whether structure has tag */
			if (tok->type == TOK_COMP_OPEN) {
				/*
				 * Nope, is anonymous, as in struct
				 * { int x; } bar;
				 */
				tag = NULL; /* tag = NULL indicates anon */
			} else if (tok->type != TOK_IDENTIFIER) {
				errorfl(tok, "Syntax error at %s", tok->ascii);
				free(ty);
				return NULL;
			} else {
				tag = tok->data;

				/* Is this actually a definition? */
				if (tok->next == NULL) {
					errorfl(tok, "Unexpected end of file");
					free(ty);
					return NULL;
				}
				if (tok->next->type == TOK_COMP_OPEN) {
					/*
					 * Yes, definition! Prepare for
					 * parsing 
					 */
					tok = tok->next;
				}
			}

			/* 
			 * If this is a structure definition, parse it.
			 * This used to be done by the caller of this
			 * function, but we must also pick up
			 * definitions ``along the way'' with all the
			 * other base type information so we can handle
			 * pathological cases like
			 * ``struct foo { ... } typedef bar;''
			 *              ^ can't return at this point
			 *                already!
			 */
			if (curtype == TY_STRUCT || curtype == TY_UNION) {
				if (tok->type == TOK_COMP_OPEN) {
					/* This has got to be a definition */
					struct ty_struct	*ts;
					struct ty_struct	*inc = NULL;

					if (next_token(&tok) != 0) {
						free(ty);
						return NULL;
					}

					/*
				 	 * save name of structure in list
					 * of incomplete structures, else
				 	 * struct foo { struct foo *next;} 
					 * does not work!
					 */
					if (tag != NULL) {
						inc = lookup_struct(curscope,
							tag, 1);
						if (inc != NULL) {
							if (!inc->incomplete) {
								inc = NULL;
							}	
						}		
								
						if (inc == NULL) {
							inc = alloc_ty_struct();
							inc->tag = tag;
							inc->incomplete = 0;
							inc->is_union =
								curtype == 
								TY_UNION;
							inc->attrib =
								ty->attributes;
							store_def_scope(
								curscope,
								inc, NULL, tok);
							inc->incomplete = 1;
						}	
					}

					if ((ts = parse_struct(&tok, tag,
						curtype)) == NULL) {
						free(ty);
						return NULL;
					}
					ts->is_union =
						curtype == TY_UNION;
					
					if (inc != NULL) {
						ts->parentscope = curscope;
						complete_type(inc, ts);
					} else {
						ts->attrib= ty->attributes;
						store_def_scope(curscope, ts,
							NULL, tok);
					}	

					if (type == DECL_FUNCARG
						|| type == DECL_FUNCARG_KR) {
						warningfl(tok,
"Parameter structure type has prototype or block scope");
					}
					ty->tstruc = inc? inc: ts;
					is_struct_def = ty->tstruc;
				} else if (tok->next->type != TOK_SEMICOLON) {
					/*
					 * This declaration uses either
					 * an existing structure type or
					 * introduces a new incomplete
					 * declaration
					 */
					struct ty_struct	*ts;

					if ((ty->tstruc
						= lookup_struct(curscope, tag,
						SCOPE_NESTED)) == NULL) {
						/*
						 * This is a new incomplete type
						 */
						ts = alloc_ty_struct();
						ts->tag = tag;
						ts->incomplete = 1;
						ts->attrib = 
							dup_attr_list(ty->attributes);

						/*
						 * 07/21/08: Setting of union
						 * type was missing!
						 */
						ts->is_union =
							curtype == 
								TY_UNION;
						store_def_scope(curscope, ts,
							NULL, tok);
						if (type == DECL_FUNCARG
							|| type == DECL_FUNCARG_KR) {
							warningfl(tok,
"Parameter structure type has prototype or block scope");
						}	
						ty->tstruc = ts;
					} else if ((ty->code == TY_UNION)
						!= ty->tstruc->is_union) {
						errorfl(tok,
							"`%s' is not %s type",
							tag,
							ty->code == TY_UNION?
							"union": "structure");
						return NULL;
					}
				} else {
					/*
					 * This is a forward-declaration as in
					 * ``struct foo;''
					 */
					struct ty_struct	*ts;
					
					if (type == DECL_STRUCT
						|| type == DECL_FUNCARG) {
						errorfl(tok,
						"Invalid forward declaration");
						free(ty);
						return NULL;
					}
					ts = alloc_ty_struct();
					ts->tag = tag;
					ts->incomplete = 1;
					ts->is_union =
						curtype == TY_UNION;
					if (ty->storage || ty->sign) {
						warningfl(tok,
				"Useless specifier in forward declaration");
					}

					ts->attrib = ty->attributes;
					store_def_scope(curscope, ts,
						NULL, tok);
					free(ty);
					return NULL;
				}
			} else {
				/* TY_ENUM */
				if (tok->type == TOK_COMP_OPEN) {
					/* This has got to be a definition */
					struct ty_enum	*te;
					if (next_token(&tok) != 0) {
						free(ty);
						return NULL;
					}

					if ((te = parse_enum(&tok)) == NULL) {
						free(ty);
						return NULL;
					}
					te->tag = tag;
					store_def_scope(curscope,
						NULL, te, tok);
					ty->tenum = te;
				} else if (tok->next->type != TOK_SEMICOLON) {
					/*
					 * This declaration uses an existing
					 * enum type
					 */
					if ((ty->tenum =
						lookup_enum(curscope, tag, 1))
						== NULL) {
						/* Handle error */
						errorfl(tok, "Undefined enum"
							" `%s'", tag);
						free(ty);
						return NULL;
					}
				} else {
					errorfl(tok, "Forward declarations of "
						"enumerations not allowed");
					free(ty);
					return NULL;
				}
			}

			break;

		case TOK_KEY_INT:
			if (ty->code == TY_LONG ||
				ty->code == TY_SHORT ||
				ty->code == TY_LLONG) {
				if (repetitions > 0
					&& (ty->code != TY_LLONG
						|| repetitions > 1)) {
					errorfl(tok, "Two or more types "
						"specified in %s",
						MAKE_TYPE(type));
					free(ty);
					return NULL;
				}
				/* ``long int'' or ``short int''- ignore*/
				++repetitions;
				continue;
			} else if (ty->code != 0) {
				errorfl(tok,
					"Two or more types specified in %s",
						MAKE_TYPE(type));
				free(ty);
				return NULL;
			}
			ty->code = TY_INT;
			break;

		case TOK_KEY_CHAR:
			if (ty->code != 0) {
				errorfl(tok,
					"Two or more types specified in %s",
						MAKE_TYPE(type));
			}
			ty->code = TY_CHAR;
			break;

		case TOK_KEY_SHORT:
			if (ty->code == TY_INT) {
				/* ``short int'' */
				ty->code = TY_SHORT;
				++repetitions;
			} else if (ty->code != 0) {
				errorfl(tok,
					"Two or more types specified in %s",
						MAKE_TYPE(type));
				free(ty);
				return NULL;
			}
			ty->code = TY_SHORT;
			break;

		case TOK_KEY_LONG:
			if (ty->code == TY_INT) {
				/* ``long int'' */
				ty->code = TY_LONG;
				--repetitions;
			} else if (ty->code == TY_LONG) {
				/* C99 ``long long'' */
				ty->code = TY_LLONG;
				--repetitions;
			} else if (ty->code == TY_LLONG) {
				errorfl(tok,
					"Two or more types specified in %s",
						MAKE_TYPE(type));
				free(ty);
				return NULL;
			} else if (ty->code == TY_DOUBLE) {
				/* ``long double'' */
				ty->code = TY_LDOUBLE;
			} else if (ty->code != 0) {
				errorfl(tok,
					"Two or more types specified in %s",
						MAKE_TYPE(type));
				free(ty);
				return NULL;
			} else {
				ty->code = TY_LONG;
				continue;
			}

			if (IS_LLONG(ty->code)) {
				static int warned;
				if (!warned) {
					if (stdflag == ISTD_C89) {
						warningfl(tok,
						"ISO C90 has no `long "
						"long' type (suppressing "
						"further warnings of this "
						"kind)");
					}
					warned = 1;
				}
			}

			if (repetitions > 0) {
				errorfl(tok,
					"Two or more types specified in %s",
						MAKE_TYPE(type));
				free(ty);
				return NULL;
			}
			++repetitions;

			break;
		case TOK_KEY_FLOAT:
			if (ty->code != 0) {
				errorfl(tok,
					"Two or more types specified in %s",
						MAKE_TYPE(type));
				free(ty);
				return NULL;
			}
			ty->code = TY_FLOAT;
			break;
		case TOK_KEY_DOUBLE:
			if (ty->code == TY_LONG) {
				ty->code = TY_LDOUBLE;
			} else if (ty->code != 0) {
				errorfl(tok,
					"Two or more types specified in %s",
						MAKE_TYPE(type));
				free(ty);
				return NULL;
			} else {
				ty->code = TY_DOUBLE;
			}
			if (repetitions > 1) {
				errorfl(tok,
					"Two or more types specified in %s",
						MAKE_TYPE(type));
				free(ty);
				return NULL;
			}
			break;
		case TOK_KEY_BOOL:
			if (ty->code != 0) {
				errorfl(tok,
					"Two or more types specified in %s",
						MAKE_TYPE(type));
				free(ty);
				return NULL;
			}
			ty->code = TY_BOOL;
			break;
		case TOK_KEY_VOID:
			if (ty->code != 0) {
				errorfl(tok,
					"Two or more types specified in %s",
						MAKE_TYPE(type));
			}
			ty->code = TY_VOID;
			break;
		case TOK_IDENTIFIER:
			/*
			 * Typedef? Only check if no type specified yet,
			 * typedef int x; int x; should work
			 *
			 * XXX 03/03/09: The above comment is not quite
			 * correct because the construct should give an
			 * error if both declarations are done in the
			 * same scope! Currently nwcc wrongly doesn't do
			 * that
			 */
			if (doing_fcatalog) {
				tytmp = fcat_get_dummy_typedef(tok->data);
				if (tytmp == NULL) {
					/* Must be identifier */
					goto exit_swtch;
				}
			} else if (ty->code != 0
				|| (tytmp = lookup_typedef(curscope, tok->data, 1,
				LTD_IGNORE_IDENT))
				== NULL) {
				/* Must be identifier in declaration */
				goto exit_swtch;
			} else {
				/*
				 * This does not have to be typedef,
				 * see C99 6.7.7, 8
				 */
				if (ty->sign) {
					goto exit_swtch;
				}
			}

			/* Okay, so this is an instance of a typedef'ed type */
			/*
			 * XXX Argh - adhoc copy, source of nasty bugs,
			 * why not use copy_type() ... 
			 */
			if (ty->code) {
				errorfl(tok,
					"Two or more types specified in %s",
						MAKE_TYPE(type));
				++errors;
			}
			ty->code = tytmp->code;
			ty->incomplete = tytmp->incomplete;
			ty->attributes = dup_attr_list(tytmp->attributes);
			ty->fastattr = tytmp->fastattr;
			
			/*
			 * XXX typedef int foo; unsigned foo f; should
			 * yield a diagnostic
			 */
			if (tytmp->sign && ty->sign) {
				if (tytmp->sign != ty->sign) {
					errorfl(tok, "Type cannot be both "
						"signed and unsigned");
					++errors;
				} else {
					warningfl(tok,
						"Duplicate use of signedness "
						"specifier");
				}
			} else if (ty->sign == 0) {
				if (ty->code == TY_CHAR) {
					/*
					 * Let's not trash the distinction
					 * of ``char'' vs ``signed/unsigned
					 * char'' here
					 */
					;
				} else {
					ty->sign = tytmp->sign;
				}
			}
			if (IS_CONST(tytmp->flags) && IS_CONST(ty->flags)) {
				warningfl(tok,
					"Duplicate use of const qualifier");
			}
			ty->flags |= IS_CONST(tytmp->flags);
			if (IS_VOLATILE(tytmp->flags) && IS_CONST(ty->flags)) {
				warningfl(tok,
					"Duplicate use of volatile qualifier");
			}
			ty->flags |= IS_VOLATILE(tytmp->flags);
			if (IS_RESTRICT(tytmp->flags) && IS_VOLATILE(ty->flags)) {
				warningfl(tok,
					"Duplicate use of restrict qualifier");
			}
			ty->flags |= IS_RESTRICT(tytmp->flags);

			if (IS_THREAD(tytmp->flags) && IS_THREAD(ty->flags)) {
				warningfl(tok,
					"Duplicate use of thread specifier");
			}
			ty->flags |= IS_THREAD(tytmp->flags);
	
			if (errors) {
				free(ty);
				return NULL;
			}
			ty->tstruc = tytmp->tstruc;
			ty->tenum = tytmp->tenum;
			ty->tbit = tytmp->tbit;
			ty->tlist = tytmp->tlist;
			ty->is_def = tytmp->is_def;
			ty->is_func = tytmp->is_func;
			ty->flags |= IS_VLA(tytmp->flags);
			ty->tlist_tail = tytmp->tlist_tail;
			break;
		case TOK_KEY_TYPEOF: {	
			/*
			 * Is GNU C typeof() declaration;
			 * int *x;
			 * typeof(x) y;
			 * ... y now has type ``int *''
			 *
			 * Like with alignof and sizeof, typename and sub-expression
			 * operands are allowed, but unlike those typeof in GNU C
			 * always seems to require parentheses, so we do so too
			 */
			struct expr	*ex;
			struct vreg	*vr;
			struct decl	**dec;
			int		saved_storage = ty->storage;

			if (next_token(&tok) != 0) {
				return NULL;
			}
			if (tok->type != TOK_PAREN_OPEN) {
				errorfl(tok, "Syntax error - Opening parentheses "
					"expected, got %s", tok->ascii);
				return NULL;
			}
	
			if (next_token(&tok) != 0) {
				return NULL;
			}


			if (IS_TYPE(tok)) {
				/* typeof(typename) */
				dec = parse_decl(&tok, DECL_CAST);
				if (dec == NULL) {
					return NULL;
				}
				ty = dec[0]->dtype;
			} else {
				/* typeof(expr) */
				if ((ex = parse_expr(&tok, TOK_PAREN_CLOSE, 0, 0, 1))
					== NULL) {
					return NULL;
				}
				if ((vr = expr_to_icode(ex, NULL, NULL, 0, 0, 0))
					== NULL) {
					return NULL;
				}	

				ty = vr->type;
			}

			/*
			 * 04/24/08: Don't copy the storage class! gcc doesn't
			 * do this either. Be careful not to trash the original
			 * storage class, since things like
			 *
			 *    static __typeof(expr) static_var;
			 *
			 * ... are valid
			 */
			ty = dup_type(ty);
			ty->storage = saved_storage;

			/*
			 * 07/22/08: This did not handle a peculiarity about
			 * plain char; If plain char is signed, the sign
			 * is set to ``signed'', even though the type remains
			 * ``char''. By copying both sign and code here, we
			 * make it look like the requested type is ``signed
			 * char'', which will end up setting the type to
			 * ``schar'' below. Thus don't copy the sign if that's
			 * the case here. That way the post-processing below
			 * will correctly recognize this as plain char and
			 * also set the signed flag correctly
			 */
			if (ty->code == TY_CHAR) {
				ty->sign = 0;
			}

#if 0
			if (next_token(&tok) != 0) {
				return NULL;
			}
#endif
			break;
		}	
		case TOK_KEY_ASM:
			/* Assembler name given for variable */
			goto exit_swtch;
			break;
		default:
			printf("BUG: get_base_type: %s\n", tok->ascii);
			exit(EXIT_FAILURE);
		}
#ifdef DEBUG2
		strncat(decascii, prevtok->ascii,
			sizeof decascii - strlen(prevtok->ascii));
#endif
	}
	
exit_swtch:		

#ifdef DEBUG2
	printf("Base type is - %s\n", decascii);
#endif
	
	/*
	 * Do some sanity checking on correctness of type, use implicit
	 * int where necessary
	 */

	/*
	 * 07/21/08: Defaulting to int was only done if code = 0 and
	 * storage or sign != 0! That fails to handle the case where
	 * there is no storage or sign at all, e.g.
	 *
	 *     inline func() {}
	 */
	if (ty->code == 0) {
		if (ty->sign == 0) {
			/*
			 * Don't warn for ``unsigned foo'', but for
			 * ``register bar''
			 */
			warningfl(*curtok,
				"Implicit int declarations are illegal in C99");
		}
		ty->code = TY_INT;
	}

	/* XXX MIPS/PowerPC compatibiliy kludge */
	/* 07/15/09: Turned off since MIPS also got long double emulation now */
#if 0
	if ((backend->arch == ARCH_MIPS
		|| backend->arch == ARCH_POWER)
		&& ty->code == TY_LDOUBLE) {
#if 0
		/* the warning seems too verbose ... */
		static int	warned;

		if (!warned) {
			warningfl(prevtok,
				"This program uses `long double', which is "
				"unsupported on MIPS. I'll pretend it used "
				"`double' instead, which may cause the program to "
				"behave badly");
			warned = 1;
		}
#endif

		/*
		 * 11/13/08: Don't do this on non-AIX PPC systems (Linux)-
		 * where we have 128bit FP now
		 */
		if (sysflag != OS_AIX && backend->arch != ARCH_POWER) {
			ty->code = TY_DOUBLE;
		}
	}
#endif

#if 0
	if (standard == C89) {
		if ((IS_LLONG(ty->code) || ty->code == TY_BOOL)
			&& ty->tlist == NULL) {
			warningfl(*curtok, "`%s' isn't available in C89 (don't "
				"compile with -ansi or -std=c89)",
				ty->code == TY_BOOL? "_Bool": "long long");
		}
	}
#endif

	/* Sign specified? */
	if (ty->code == TY_STRUCT ||
		ty->code == TY_ENUM ||
		ty->code == TY_UNION ||
		ty->code == TY_VOID ||
		ty->code == TY_FLOAT ||
		ty->code == TY_DOUBLE ||
		ty->code == TY_LDOUBLE) {

		if (ty->code == TY_VOID && doing_fcatalog) {
			/*
			 * Ignore this.. the dummy void typedef for
			 * FILE seems to cause this. Unclear
			 */
		} else {
			if (ty->sign != 0) {
				errorfl(*curtok,
					"Invalid use of signedness specifier");
				free(ty);
				return NULL;
			}
		}
	} else {
		/* 
		 * Integral types except ``char'' and ``_Bool'' are signed by
		 * default 
		 */
		if (ty->sign == 0) {
			if (ty->code == TY_CHAR) {
				/*
				 * XXX temporary botch to avoid passing flags
				 * to libc
				 */
#if 0
				if (CHAR_MIN != SCHAR_MIN) {
					ty->sign = TOK_KEY_UNSIGNED;
				} else {
					ty->sign = TOK_KEY_SIGNED;
				}
#endif
				ty->sign = cross_get_char_signedness();
			} else if (ty->code == TY_BOOL) {
				ty->sign = TOK_KEY_UNSIGNED;
			} else {
				ty->sign = TOK_KEY_SIGNED; 
			}
		} else if (ty->sign == TOK_KEY_UNSIGNED) {
			/* Unsigned */
			if (ty->code == TY_CHAR) {
				ty->code = TY_UCHAR;
			} else if (ty->code == TY_SHORT) {
				ty->code = TY_USHORT;
			} else if (ty->code == TY_INT) {
				ty->code = TY_UINT;
			} else if (ty->code == TY_LONG) {
				ty->code = TY_ULONG;
			} else if (ty->code == TY_LLONG) {
				ty->code = TY_ULLONG;
			}	
		} else {
			/* Explicitly signed */
			if (ty->code == TY_CHAR) {
				ty->code = TY_SCHAR;
			}
		}	
	}

	if (tok == NULL) {
		errorfl(prevtok, "Unexpected end of file");
		free(ty);
		return NULL;
	}

	/*
	 * Make sure no storage class specifiers are used in structure
	 * members
	 */
	if (type == DECL_STRUCT) {
		if (ty->storage /*&& ty->storage != TOK_KEY_TYPEDEF*/) {
			errorfl(*curtok,
				"Invalid use of storage class specifier");
			return NULL;
		}
	} else if (type == DECL_FUNCARG
		|| type == DECL_FUNCARG_KR) {
		if (ty->storage && ty->storage != TOK_KEY_REGISTER) {
			errorfl(*curtok,
				"Invalid use of storage class specifier");
			return NULL;
		}	
	}

	/*
	 * 02/02/08: Thread-local variables may only be static or extern
	 */
	if (IS_THREAD(ty->flags)) {
		if (curscope == &global_scope) {
			; /* OK */
		} else if (ty->storage == TOK_KEY_STATIC
			|| ty->storage == TOK_KEY_EXTERN) {
			; /* OK */
		} else {
			errorfl(*curtok, "`__thread' variables have to have static "
				"storage duration");
			return NULL;
		}
	}

	*curtok = tok;
	return ty;
}


/*
 * Helper for parse_declarator() - Must be called with tok pointing to a
 * qualifier token. Returns 0 if this is a qualified pointer (and updates
 * tok to point to (*tok)->prev, else 1
 */
static int
try_qualified(struct token **tok) {
	struct token	*t;

	t = (*tok)->prev;
	if (t->type == TOK_OPERATOR) {
		if (*(int *)t->data == TOK_OP_AMB_MULTI) {
			/* Is qualified pointer indeed! */
			*tok = t;
			return 0;
		}
	}

	/* Not a pointer - syntax error */
	errorfl(*tok, "Syntax error at %s", (*tok)->ascii);
	return 1;
}


static void
merge_typedef_tlist(
	struct type *base,
	struct type_node *tlist,
	struct type_node *tlist_tail) {

	struct type_node	*orig_base_tlist = base->tlist;

	/*
	 * 08/01/07: OUCH the copy was missing!!! Thus
	 *
	 *    typedef struct __bogus {...} va_list[1];
	 *
	 *    void foo(va_list va) { .... }
	 *    void bar() { va_list va; ....}
	 *
	 * ... would make the bar() instance of va a pointer
	 * instead of an array because the typelist was trashed
	 * at foo() (since ``T[]'' becomes ``T *'' as a function
	 * parameter.)
	 */
	tlist_tail = copy_tlist(&tlist, tlist);
	if (base->tlist == NULL) {
		base->tlist = tlist;
		base->tlist_tail = tlist_tail;
	} else if (tlist != NULL) {
		base->tlist_tail->next = tlist;
		tlist->prev = base->tlist_tail;
		base->tlist_tail = tlist_tail;
	}

	/*
	 * 03/22/08: We have to unset the ``is_func'' flag! In:
	 * 
	 *    typedef void func();
	 *    func *p;
	 *
	 * ... we first take over the is_func flag, but we then
	 * (after processing the pointer-to designator) have to
	 * unset it. Otherwise it is considered to be a function
	 * declaration when it comes to storage allocation (so
	 * nothing will be allcoated)
	 *
	 * BEWARE: This may only be done if the new declaration
	 * itself isn't a function, e.g. in
	 *
	 *    typedef int stuff;
	 *    stuff func();
	 *
	 * ... so we must make it dependent on the source typedef
	 * tlist indicating a function.
	 *
	 * 03/03/09: This was wrongly checking for 
	 *
	 * 	orig_base_tlist == NULL
	 * 	  || orig_tlist->type != TN_FUNCTION) 
	 *
	 * Since orig_base_tlist is the currently read base type
	 * without the typedef additions, it means that
	 *
	 *    typedef int foo();
	 *    foo f;
	 *
	 * ALSO unset the is_func flag for ``f''. Of course the
	 * current tlist must be non-null, and non-TN_FUNCTION,
	 * to unset is_func
	 */
	if (tlist != NULL
		&& tlist->type == TN_FUNCTION
#if 0
		&& (orig_base_tlist == NULL
		|| orig_base_tlist->type != TN_FUNCTION)) {
#endif
		&& (orig_base_tlist != NULL
			&& orig_base_tlist->type != TN_FUNCTION)) {
		base->is_func = 0;
	}	
}

static void
array_arg_to_ptr(struct type *ret) {	
	/* There are no array arguments; Only pointers */
	if (ret->tlist != NULL) {
		if (ret->tlist->type == TN_ARRAY_OF) {
			ret->tlist->type = TN_POINTER_TO;
			if (ret->tlist->next
				&& ret->tlist->next->type == TN_ARRAY_OF) {
			}	
		} else if (ret->tlist->type == TN_FUNCTION) {
			/*
			 * 03/04/09: A function turns into a pointer to
			 * a function
			 * XXX This only seems to show up with typedefs?!?
			 * Normal declarations are already handled
			 * elsewhere, this has been working forever
			 */
			struct type_node	*temp;

			temp = alloc_type_node();
			temp->type = TN_POINTER_TO;
			temp->next = ret->tlist;
			ret->tlist = temp;
			ret->is_func = 0; /* Now just a pointer */
		}
	}
}	

/*
 * Does actual parsing of declarator in declaration, i.e. in
 * char (*p)[20] = foobar;
 * ... the part (*p)[20] would be parsed here.
 * Used by parse_decl(), which also grabs the optional initializer
 */
static struct type *
parse_declarator(struct token **curtok, struct type *base, int type) {
	struct token		*t;
	struct token		*start;
	struct token		*left;
	struct token		*right;
	struct token		*arstart;
	struct type		*ret; 
	struct ty_func		*tfunc = NULL;
	struct expr		*ex;
	struct type_node	*base_tlist;
	struct type_node	*base_tlist_tail;
	char			*name = NULL;
	int			parens;
	int			newrparen = 1;
	int			left_type;
	int			right_was_func;
	int			array_const;
	int			ac_for_all_dims = 1;
	int			ac_node;
	int			is_vla = 0;


	/*
	 * 07/21/07: This is a kludge to allow this construct:
	 *
	 *    void foo(char buf[restrict]);
	 *
	 * ... which has the same effect as ``char *restrict buf''.
	 * We introduce a new expression type -
	 * EXPR_CONST_FUNCARRAYPARAM - because parse_expr() can
	 * most conveniently handle this
	 */
	if (type == DECL_FUNCARG || type == DECL_FUNCARG_KR) {
		array_const = EXPR_CONST_FUNCARRAYPARAM;
		/* XXX hmmmm not really true, huh? */
		ac_for_all_dims = 0;
#if 0
	} else if (type == -3333) {	
#endif
	} else if (type == DECL_VARINIT) {
		array_const = EXPR_CONST;
		if (curscope != &global_scope) {
			/*
			 * 07/22/07: XXX (It would probably better to do this
			 * in analyze() and use DECL_CONSTINIT there if
			 * required!)
			 *
			 * Allow automatic variable arrays 
			 */
			if (base->storage != TOK_KEY_EXTERN
				&& base->storage != TOK_KEY_STATIC
				&& base->storage != TOK_KEY_REGISTER) {
				array_const = EXPR_OPTCONSTARRAYSIZE;
			}
		}
	} else {
		array_const = EXPR_CONST;
	}



	/*
	 * When using an instance of a typedef'ed type, the base type may
	 * already have a tlist;
	 * typedef char *p;
	 * p array[5];
	 * ... and that tlist stuff should be appended after the declarator's
	 * tlist such that the above reads ``array of 5 pointers to char''.
	 */
	base_tlist = base->tlist;
	base_tlist_tail = base->tlist_tail;
	
	/*
	 * Of course, *curtok is the actual start, but when parsing it is
	 * easier to exclude the previous element, because *curtok must be
	 * included in parsing
	 */
	start = (*curtok)->prev;
	if (start == NULL) {
		/*
		 * Pathological case encountered: First token in file is part
		 * of an implicit declaration, as in  ``main() {}''. We set
		 * start to *curtok for now - the later code will detect this
		 * curiosity and set the ``left'' pointer to NULL
		 */
		start = *curtok;
	}

	/* Allocate declaration structure to be returned */
	ret = alloc_type();

	*ret = *base;
	ret->tlist = NULL;
	ret->tlist_tail = NULL;
	ret->line = (*curtok)->line;

	if (type == DECL_FUNCARG || type == DECL_FUNCARG_KR) {
		/*
		 * Before we try to get the identifier, make sure this is an
		 * extended type, i.e. pointer, array, function-pointer or a
		 * combination thereof, because find_ident() would reject e.g.
		 * a plain ``int''
		 */
		int	tmp;
		
		if ((*curtok)->type == TOK_OPERATOR) {
			tmp = *(int *)(*curtok)->data;
		} else {
			tmp = (*curtok)->type;
		}
		if (tmp != TOK_OP_COMMA && tmp != TOK_PAREN_CLOSE) {
			if ((t = find_ident(*curtok, type, ret)) == NULL) {
				free(ret);
				return NULL;
			}
		} else {
			/* We are done */
			merge_typedef_tlist(ret, base_tlist, base_tlist_tail);
			array_arg_to_ptr(ret); /* 01/27/08: This was missing */
			return ret;
		}
	}

	if (type == DECL_CAST ||
		((type == DECL_FUNCARG || type == DECL_FUNCARG_KR)
			&& t->type != TOK_IDENTIFIER)) {
		/* Find beginning of cast or function argument */

		if (type == DECL_CAST && (*curtok)->type == TOK_PAREN_CLOSE) {
			/* 
			 * The cast ends here, so we are done parsing the cast's
			 * ``declarator'' already! 
			 */
			merge_typedef_tlist(ret, base_tlist, base_tlist_tail);
			return ret;
		}
		if (type == DECL_CAST) {
			if ((t = find_cast_start(*curtok, TOK_PAREN_CLOSE))
				== NULL) {
				free(ret);
				return NULL;
			}
		}
		left = t;

		if (left->type == TOK_OPERATOR) {
			left_type = *(int *)left->data;
		} else {
			left_type = left->type;
		}	

		/* 
		 * Since this function might only be called if this is a cast
		 * or function argument of extended type, there are only two
		 * possiblities:
		 * 1) the left side is a ``*''. In this case, the right must
		 * either be a closing parentheses/comma or array designator
		 * 2) the left side is a ``[]''. In this case, the right must
		 * be a closing parentheses/comma
		 */

		if (left_type == TOK_OP_AMB_MULTI) { 
			if (next_token(&t) != 0) {
				free(ret);
				return NULL;
			}
			if (t->type == TOK_PAREN_CLOSE) {
				right = t;
			} else if (t->type == TOK_ARRAY_OPEN) {
				right = t;
			} else if (t->type == TOK_KEY_CONST
				|| t->type == TOK_KEY_RESTRICT
				|| t->type == TOK_KEY_VOLATILE) {
				right = t; /* This is changed by code below */
			} else if (	t->type != TOK_SEMICOLON &&
					!(t->type == TOK_OPERATOR
					&& *(int *)t->data == TOK_OP_COMMA)) {
				errorfl(t, "Syntax error at %s", t->ascii);
				free(ret);
				return NULL;
			} else {
				right = t;
			}
		} else {
			/*
			 * The left side is the leftmost dimension of a possibly
			 * multidimensional array. Because of this, it makes
			 * more sense to use the right pointer in order to parse
			 * it, because it steps left-to-right, which is the
			 * natural way of reading array dimensions as well;
			 * char buf[5][6]; <-- 5 arrays of arrays
			 */
			right = left;
			if (start != *curtok) {
				left = left->prev;
			}
		}
	} else {
		/* Find identifier to start from there */
		if (type == DECL_STRUCT
			&& (*curtok)->type == TOK_OPERATOR
			&& *(int *)(*curtok)->data == TOK_OP_AMB_COND2) {
			/* Anonymous bitfield */
			t = *curtok;
			goto do_bitfield;
		}
		if (type != DECL_FUNCARG && type != DECL_FUNCARG_KR) {
			if ((t = find_ident(*curtok, type, ret)) == NULL) {
				free(ret);
				return NULL;
			}
		}
		ret->name = t->data;
		ret->line = t->line;

		if (start != *curtok) {
			/* left not first token */
			left = t->prev;
		} else {
			left = t;
		}
		if ((right = t->next) == NULL) {
			errorfl(t, "Unexpected end of file");
			free(ret);
			return NULL;
		}
		name = t->data;
	}

	/* XXX does this work? */
	if (left->type == TOK_OPERATOR
	   	&& *(int *)left->data == TOK_OP_AMB_MULTI) {
		if (IS_QUALIFIER(left->next->type)) {
			left = left->next;
			right = right->next;
		}
	}

#ifdef DEBUG2
	printf("Left = %s\n", left->ascii);
	printf("Right = %s\n", right->ascii);

	printf("Parsing %s for ``%s''\n", 
		type == DECL_CAST ? "cast" :
			type == DECL_FUNCARG || type == DECL_FUNCARG_KR
				? "function argument" :
				"declaration",
		(char *)t->ascii);
#endif

	if (t->next == NULL) {
		errorfl(t, "Unexpected end of file");
		free(ret);
		return NULL;
	}

	/*
	 * 10/03/07: This was missing a check for DECL_CAST! Otherwise
	 * things like
	 *
	 *    foo? __builtin_va_arg(bla, void *): NULL
	 *
	 * ... would break since the second part of the conditional
	 * operator was considered a potential candidate for a bitfield
	 * declarator
	 */
	if ((t->next->type == TOK_OPERATOR &&
		*(int *)t->next->data == TOK_OP_AMB_COND2)
		&& type != DECL_CAST) {
		int		bitcount;
		struct token	*bitfield_start = t->next;

		/* This is a colon indicating a bitfield! */
		/* XXX only supports foo:bar, not (foo):bar */
do_bitfield:		
		if (type != DECL_STRUCT) {
			/* Attempt to declare bitfield outside of structure! */
			errorfl(t, "Syntax error at `%s'", t->ascii);
			free(ret);
			return NULL;
		}

		/* 
		 * ISO C only allows bitfield members of type int, unsigned
		 * int and _Bool
		 */
		if (ansiflag) {
			if (ret->code != TY_INT &&
				ret->code != TY_UINT &&
				ret->code != TY_BOOL) {
				errorfl(t, "Bitfield member must be int, "
					"unsigned int or _Bool");
				/* XXX recover :( */
				free(ret);
				return NULL;
			}
		}

		/*
		 * 07/21/08: Perform sanity checking
		 */
		if (!is_integral_type(ret)) {
			errorfl(t, "Bitfield member must have integral type "
				"(and for portability int, unsigned int or "
				"_Bool)");
			free(ret);
			return NULL;
		}

		/*
		 * Okay, this is a valid bitfield. The number of bits is a
		 * constant expression that might contain operators, so we
		 * cannot rely on it being just a number, as is usually the
		 * case
		 */
		if (name != NULL) {
			t = t->next; /* Now at ``:'' */
		}	
		if (next_token(&t) != 0) {
			free(ret);
			return NULL;
		}

		if ((ex = parse_expr(&t, TOK_OP_COMMA,
			TOK_SEMICOLON, EXPR_CONST, 1)) == NULL) {
#ifndef NO_EXPR /* parse_expr() always returns null if no_expr */
			free(ret);
			return NULL;
#endif
		}

		bitcount = (int)cross_to_host_size_t(ex->const_value);
		if (bitcount < 0) {
			errorfl(bitfield_start,
				"Bitfield has negative size");
			/* Fallthru to avoid syntax errors */
		} else if (bitcount == 0) {
			if (name == NULL) {
				/*
				 * OK, this zero-sized bitfield terminates
				 * a storage unit
				 */
				;
			} else {
				/* Named zero-size bitfield, invalid! */
				errorfl(bitfield_start,
					"Named bitfield has size 0");
			}
		} else if (0  /* bitcount > ... */) {
			errorfl(bitfield_start,
				"Bitfield size exceeds maximum");
		}

		/* 
		 * XXXX ... 03/22/08: what the... why wasn't ex used?!?!?!?!
		 */
		ret->tbit = n_xmalloc(sizeof *ret->tbit);
		ret->tbit->name = name;
		ret->tbit->numbits = bitcount;
		*curtok = t;
		merge_typedef_tlist(ret, base_tlist, base_tlist_tail);
		return ret;
	}

#ifdef DEBUG2
	if (ret->name) {
		printf("%s is:\n", ret->name);
	} else {
		printf("Cast is:\n");
	}
#endif

	/*
	 * Loop until end of both sides of declatator is reached. 
	 *
	 * Let us recall the rules for C's declaration syntax briefly ...
	 * 
	 * A declaration basically consists of a theoretically unlimited
	 * number of combined pointer-to, array-of and function call
	 * designators which are applied by placing them on either side of
	 * the identifier. Pointer-to is always placed on the left, array-of
	 * and function call on the right side. To read a declaration, you
	 * have to begin at the identifier (or in the case of a cast, where
	 * the identifier would be placed if it were a declaration!). You
	 * then look on the left side and on the right side. If either side
	 * is a parentheses, you pick the other side as ``significant''
	 * designator and advance this side. If it is a parentheses as well,
	 * both sides are advanced. 
	 * If you have a pointer-to on the left side and an array-of or
	 * function call on the right side, the right side wins because
	 * those have higher precedence.
	 * Example:
	 * char (*(*buf)[256])[256];
	 * ...step to buf. Left side is ``*'', right side is parentheses,
	 * thus read ``buf is a pointer to...''
	 *
	 * ...advance left side. This is a parentheses. Advance both sides
	 *
	 * ...left side is ``*'', right side is ``[256]''. [] has higher
	 * precedence, so read ``buf is a pointer to an array of 256...''
	 *
	 * ...advance right side. This is a parentheses, so pick left side 
	 * now and read ``buf is a pointer to an array of 256 pointers''
	 *
	 * ...advance left side. This is a parentheses. Advance both sides.
	 * The left side has reached the end of the declaration, the right
	 * side is ``[256]'', so read ``buf is a pointer to an array of
	 * 256 pointers to an array of 256 chars''
	 *
	 * On a final note, everything encountered after a function
	 * designator has been read is treated as the return type of that
	 * function
	 */
	if (type == DECL_CAST) {
		parens = 1;
	} else {
		parens = 0;
	}
	for (t = *curtok; t != right; t = t->next) {
		if (t->type == TOK_PAREN_OPEN) {
			++parens;
		}
	}

	/*
	 * 04/11/08: Removed the parentheses handling below. It seems
	 * incomplete because it doesn't handle commas, for one. The
	 * reason it's removed now is that it broke
	 *
	 *    int (foo);
	 *
	 * ... which gave us an unimpl() somewhere below. It's not
	 * clear whether removing it just disguises or sovles the
	 * problem. We have to check that unimpl()
	 *
	 * CANOFWORMS: I don't know how much other stuff breaks due
	 * to removing the case below
	 */
#if 0
	if (right->type == TOK_PAREN_CLOSE) {
		if (right->next && 
			(right->next->type == TOK_SEMICOLON ||
			right->next->type == TOK_COMP_OPEN)) {
			*curtok = right;
			right = NULL;
			--parens;
		}
	}
#endif

	while (left != NULL || right != NULL) {
		int	right_paren_was_accounted_for = 0;
		
		if (left == start) {
			/* left end of declarator reached */
			left = NULL;
		}

		right_was_func = 0;

		if (right) {
			if (right->type == TOK_SEMICOLON) {
				/* right end of declarator reached */
				*curtok = right;
				right = NULL;
			} else if (right->type == TOK_OPERATOR) { 
				int	op = *(int *)right->data;

				if (op == TOK_OP_COMMA || op == TOK_OP_ASSIGN) {
					/* right end of declarator reached */
					*curtok = right;
					right = NULL;
				}
			} else if (right->type == TOK_PAREN_CLOSE) {
				/*
				 * If this is a cast, let's see whether
				 * this closing parentheses is the end
				 * of it!
				 * It is important to check that this
				 * code is not ran more than once on
				 * the same parentheses, which turned
				 * into an obscure bug ... When a
				 * parentheses is kept for more than
				 * one run because the other side is
				 * significant and evaluated first,
				 * only one parentheses might be
				 * counted
				 */
				if (newrparen) {
					--parens;
					/*
					 * 08/22/07: This variable was missing!
					 * Otherwise the code for left = ( and
					 * right ) decremented parens once more
					 * and stuff like ``int (foo)[20];''
					 * failed
		 			 */
					right_paren_was_accounted_for = 1;
					if (type == DECL_CAST) {
						if (parens == 0) {
							/* Yes! */
							*curtok = right;
							right = NULL;
						}
					} else if (type == DECL_FUNCARG) {
						if (parens == -1) {
							/*
							 * Closing parentheses
							 * reached
							 */
							*curtok = right;
							right = NULL;
						}	
					}	
					newrparen = 0;
				}
			}
		}
		

		if (left && right) {
			/* Precedence or parentheses must decide */
			if (left->type == TOK_PAREN_OPEN) {
				if (right->type == TOK_PAREN_CLOSE) {
					/* Skip parentheses */
					if (right->next == NULL) {
						*curtok = right;
					}	

					right = right->next;
					newrparen = 1;
					left = left->prev;
					if (!right_paren_was_accounted_for
						&& --parens  == -1) {
						if (right) *curtok = right;
						if (right
							&& right->type
							== TOK_PAREN_OPEN) {
							;
						} else {	
							right = NULL;
						}	
					}	
						
					continue;
				} else {
					/* Right is significant */
					if (right->type == TOK_ARRAY_OPEN) {
#ifdef DEBUG2
						puts("[right] array of N ...");
#endif
						arstart = right;
						if (next_token(&right) != 0) {
							free(ret);
							return NULL;
						}
						ex = parse_expr(&right,
							TOK_ARRAY_CLOSE, 0,
							
							(ac_for_all_dims
							||
							ret->tlist == NULL)?
							array_const:
							EXPR_CONST, 1);
						if (ex == NULL) {
							free(ret);
							return NULL;
						}
						if (ex->is_const) {
							ac_node = TN_ARRAY_OF; 
						} else {
							ac_node = TN_VARARRAY_OF;
							is_vla = FLAGS_VLA;
						}
						append_typelist(ret,
							ac_node, ex, NULL,
							arstart);
					} else if (right->type
						== TOK_PAREN_OPEN) {
						struct token	*tstart = right;

#ifdef DEBUG2
						puts("[right] function...");
#endif
						right_was_func = 1;
						if ((tfunc=parse_func(&right, name))
							== NULL) {
							free(ret);
							return NULL;
						}
						if ((type == DECL_FUNCARG
							|| type == DECL_FUNCARG_KR)
							&& ret->tlist == NULL) {
							append_typelist(ret,
								TN_POINTER_TO,
								0, NULL, tstart);
						}
						append_typelist(ret,
							TN_FUNCTION, 0, tfunc,
							tstart);
					} else if (right->type == TOK_OPERATOR
						&& *(int *)right->data
						== TOK_OP_AMB_MULTI) {
#ifdef DEBUG2
						puts("[right] pointer to...");
#endif
						append_typelist(ret,
							TN_POINTER_TO, 0,
							NULL, NULL);
					} else {
#ifdef DEBUG2
						printf("[right] syntax error? "
							"code = %d\n",
							right->type);
#endif
						/*
						 * XXX is this an
						 * improvement?
						 */
						*curtok = right;
						right = NULL;
					}
				}
			} else if (right->type == TOK_PAREN_CLOSE) {
				/* Left is significant */
				if (left->type == TOK_ARRAY_OPEN) {
#ifdef DEBUG2
					puts("[left] array of N...");
					puts("XXX what 2 do here");
#endif
					arstart = left;
					if (next_token(&left) != 0) {
						free(ret);
						return NULL;
					}
					ex = parse_expr(&left, TOK_ARRAY_CLOSE,
						0,
						(ac_for_all_dims || ret->tlist == NULL)?
						array_const:
						EXPR_CONST, 1);
					if (ex == NULL) {
						free(ret);
						return NULL;
					}
					if (ex->is_const) {
						ac_node = TN_ARRAY_OF;
					} else {
						ac_node = TN_VARARRAY_OF;
						is_vla = FLAGS_VLA;
					}
					append_typelist(ret, ac_node,
						ex, NULL, arstart);
				} else if (left->type == TOK_OPERATOR &&
					*(int *)left->data
					== TOK_OP_AMB_MULTI) {
#ifdef DEBUG2
					puts("[left] pointer to...");
#endif
					append_typelist(ret, TN_POINTER_TO,
						0, NULL, NULL);
				} else {
#ifdef DEBUG2
					printf("[left] syntax error? code "
						"= %d\n", left->type);
#endif
					/* Qualified pointer? */
					if (IS_QUALIFIER(left->type)) {
						int *qualifier = &left->type;
						if (try_qualified(&left) == 1) {
							free(ret);
							return NULL;
						} else {
							append_typelist(ret,
								TN_POINTER_TO,
								qualifier,
								NULL,
								NULL);
						}
					}
				}
				if (newrparen && --parens == -1) {
					*curtok = right;
					right = NULL;
				}
			} else {
				/* 
				 * The right side always has precedence
				 * if both are not parentheses, because
				 * it must either be a function or array
				 * designator, both of which ``bind
				 * tighter'' than pointer-to
				 */
				if (right->type == TOK_ARRAY_OPEN) {
#ifdef DEBUG2
					puts("[right] array of N...");
#endif
					arstart = right;
					if (next_token(&right) != 0) {
						free(ret);
						return NULL;
					}
					ex = parse_expr(&right, TOK_ARRAY_CLOSE,
						0,
						(ac_for_all_dims ||
						 ret->tlist == NULL)? array_const:
						EXPR_CONST, 1);
					if (ex == NULL) {
						free(ret);
						return NULL;
					}
					if (ex->is_const) {
						ac_node = TN_ARRAY_OF;
					} else {
						ac_node = TN_VARARRAY_OF;
						is_vla = FLAGS_VLA;
					}	
					append_typelist(ret, ac_node,
						ex, NULL, arstart); 
				} else if (right->type == TOK_PAREN_OPEN) {
					struct token	*tstart = right;
#ifdef DEBUG2
					puts("[right] function...");
#endif
					right_was_func = 1;
					if ((tfunc = parse_func(&right, name))
						== NULL) {
						free(ret);
						return NULL;
					}
					if ((type == DECL_FUNCARG
						|| type == DECL_FUNCARG_KR)
						&& ret->tlist == NULL) {
						append_typelist(ret,
							TN_POINTER_TO,
							NULL, NULL, NULL);
					}
					append_typelist(ret, TN_FUNCTION,
						NULL, tfunc, tstart);
				} else {
#ifdef DEBUG2
					printf("[right] syntax error? "
						"code = %d\n", right->type);
#endif
					/* XXX is this an improvement? */
					*curtok = right;
					right = NULL;
				}
				if (right && right->next == NULL) {
					*curtok = right;
				}	
				if (right) right = right->next;
				newrparen = 1;
				continue;
			}
		} else if (left != NULL || right != NULL) {
			/* Either left or right is significant */
#ifdef DEBUG2
			char		*side = NULL;
#endif
			struct token	*tmp = NULL;

			if (left) {
				/* Left is sig. */
#ifdef DEBUG2
				side = "left";
#endif
				tmp = left;
			} else {
				/* Right is sig. */
#ifdef DEBUG2
				side = "right";
#endif
				tmp = right;
			}
			if (tmp->type == TOK_ARRAY_OPEN) {
#ifdef DEBUG2
				printf("%s array of N...\n", side);
#endif
				arstart = tmp;
				if (next_token(&tmp) != 0) {
					free(ret);
					return NULL;
				}

				ex = parse_expr(&tmp, TOK_ARRAY_CLOSE,
					0,
					(ac_for_all_dims || ret->tlist == NULL)?
					array_const
					: EXPR_CONST, 1);
				if (ex == NULL) {
					return NULL;
				}	
				if (ex->is_const) {
					ac_node = TN_ARRAY_OF;
				} else {
					ac_node = TN_VARARRAY_OF;
					is_vla = FLAGS_VLA;
				}
				append_typelist(ret, ac_node, ex, NULL,
					arstart); 
			} else if (tmp->type == TOK_OPERATOR &&
				*(int *)tmp->data == TOK_OP_AMB_MULTI) {
#ifdef DEBUG2
				printf("%s pointer to...\n", side);
#endif
				append_typelist(ret, TN_POINTER_TO, NULL, NULL,
						NULL);
			} else if (tmp->type == TOK_PAREN_OPEN) {
				if (tmp == right) {
					struct token	*tstart = tmp;
#ifdef DEBUG2
					printf("%s function...\n", side);
#endif
					right_was_func = 1;

					if ((tfunc = parse_func(&tmp, name))
							== NULL) {
						free(ret);
						return NULL;
					}
					if ((type == DECL_FUNCARG
						|| type == DECL_FUNCARG_KR)
						&& ret->tlist == NULL) {
						append_typelist(ret,
							TN_POINTER_TO,
							NULL, NULL, NULL);
					}
					append_typelist(ret, TN_FUNCTION,
						NULL, tfunc, tstart);
				} else {
					/* XXX handle error! Can't happen! */
					unimpl(); /* 01/26/08 */
				}
			} else if (tmp->type == TOK_PAREN_CLOSE) {
				if (tmp == right) {
					if (newrparen && --parens == -1) {
						*curtok = right;
						right = NULL;
					}
				} else {
#ifdef DEBUG2
					printf("%s syntax error? code = %d\n",
						side, tmp->type);
#endif
					/* XXX is the below an improvement? */
					if (tmp == right) *curtok = right;
					right = NULL;
					
				}
			} else {
#ifdef DEBUG2
				printf("%s syntax error? code = %d\n",
					side, tmp->type);
#endif
				/* XXX is this an improvement? */
				if (tmp == right) {
					*curtok = right;
					right = NULL;
				} else {
					if (IS_QUALIFIER(tmp->type)) {
						int	*qualifier
							= &tmp->type;

						if (try_qualified(&left) == 1) {
							free(ret);
							return NULL;
						} else {
							append_typelist(ret,
								TN_POINTER_TO,
								qualifier,
								NULL, NULL);
							tmp = left;
						}
					}
				}
			}
			if (left) {
				left = tmp;
			} else if (right) {
				right = tmp;
				newrparen = 1;
			}
		}

		/*
		 * We only proceed either side if it isn't a parentheses. The
		 * case where both sides are parentheses is handled above
		 */
		if (left) {
			if (left->type != TOK_PAREN_OPEN || right == NULL) {
				left = left->prev;
				if (left->type == TOK_PAREN_OPEN) {
					++parens;
				}
			}
		}
		if (right) {
			/*
			 * 06/30/07: right_was_func added. Lack of this
			 * check caused things like:
			 *
			 *     void foo(int (bar()), void *p);
			 *
			 * ... to break (GNU tar uses this.)
			 *
			 * The problem was that the parse_func() for the
			 * bar() declaration left a ) token, which has
			 * to be skipped here despite being a parentheses.
			 * Because otherwise it will incorrectly match
			 * with the ( before ``bar'', thus yielding
			 * parentheses mismatch
			 */
			if ((right->type != TOK_PAREN_CLOSE
				|| right_was_func)
				|| left == NULL) {
				if (right->next) *curtok = right;
				right = right->next;
				newrparen = 1;
			}
		}
	}

	while ((*curtok)->type == TOK_KEY_ATTRIBUTE) {
		(void) get_attribute(ret, curtok);
	}

	/*
	 * Finally we can check all attributes for correctness and set the
	 * fastattr flags
	 */
	merge_attr_with_type(ret);

	/*
	 * 073107: Merge typelist with the possible base typelist if this
	 * is a typedef'ed type. This was wrongly done after the ``array''
	 * parameter transformation below, such that
	 *
	 *    typedef struct blabla { } va_list[1];
	 *
	 * void foo(va_list v) {
	 *
	 * ... wrongly declared v as an array instead of a pointer
	 */
	merge_typedef_tlist(ret, base_tlist, base_tlist_tail);
	if (type != DECL_FUNCARG
		&& type != DECL_FUNCARG_KR
		&& type != DECL_CAST) {
		if ((*curtok)->type == TOK_PAREN_CLOSE) {
			*curtok = (*curtok)->next;
		}
	} else if (type == DECL_FUNCARG || type == DECL_FUNCARG_KR) {
		array_arg_to_ptr(ret);
	}
	ret->flags |= is_vla; /* typedef'ed base may already be VLA! */

	return ret;
}



static void
store_decl(struct decl ***d, struct decl *dec, int *nalloc, int *index) {
	if (*index >= (*nalloc - 2)) {
		*nalloc += 8;
		*d = n_xrealloc(*d, *nalloc * sizeof **d);
	}
	(*d)[*index] = dec;
	(*d)[++*index] = NULL;
}


static struct decl	**incomplete_tentative_decls;
static int		inc_tenta_alloc = 0;
static int		inc_tenta_index = 0;

static void
add_incomplete_tentative_decl(struct decl *d) {
	store_decl(&incomplete_tentative_decls, d, &inc_tenta_alloc, &inc_tenta_index);
}

void
check_incomplete_tentative_decls(void) {
	int	i;

	for (i = 0; i < inc_tenta_index; ++i) {
		struct decl	*d = incomplete_tentative_decls[i];
		if (d->dtype->tstruc->incomplete) {
			errorfl(d->tok, "Incomplete type of `%s' never completed",
				d->dtype->name);
		}
	}
}

/*
 * Parses declaration beginning from token list beginning at *curtok
 * and, if the mode permits it, the optional initializer. The end of a
 * declaration is indicated by either a semicolon or a comma (,). If the
 * latter is encountered, the function just uses the same base type to
 * parse the next declarator.
 * On success, a pointer to a newly allocated declaration structure is
 * returned, else NULL. *curtok is updated to point to the next token
 * after this declaration.
 */ 
struct decl **
parse_decl(struct token **curtok, int mode) {
	struct decl		**ret = NULL;
	struct decl		*tmp;
	struct type		*ty;
	struct initializer	*init;
	int			alloc = 0;
	int			index = 0;

#ifdef DEBUG2
	printf("Reading decl -- %s\n", (*curtok)->ascii);
#endif
	if ((ty = get_base_type(curtok, mode)) == NULL) {
#if 0
			/* XXX */
			recover(curtok, TOK_OP_COMMA, TOK_SEMICOLON);
#endif
		return NULL;
	}

	if ((*curtok)->type == TOK_SEMICOLON) {
		if (ty->tstruc == NULL
			&& ty->code != TY_ENUM) {
			errorfl(*curtok, "Syntax error at `;'");
			return NULL;
		} else {
			static struct decl	*dummy[2];
			static struct decl	dummy2;

			/*
			 * 08/18/07: Wow, this always ended up using
			 * the same declaration for every anonymous
			 * union! So now the declaration is copied.
			 * Was probably wrong for lots eof other
			 * things too, how come it seemed to work?!?
			 */
			dummy[0] = n_xmemdup(&dummy2, sizeof dummy2);
			/* XXX misleading for struct foo; */
			dummy[0]->dtype = ty;
			dummy[0]->dtype->is_def = 1;
			dummy[0]->dtype = ty;
			dummy[1] =  NULL;
			merge_attr_with_type(ty);
			return dummy;
		}	
	}

	/* 
	 * Now that we have the base type, the declarator list can be
	 * read - separated by commas in case there are more than one
	 */
	for (;;) {
		struct type	*decty;
		struct token	*declstart = *curtok;
		int		is_incomplete_kr_func = 0;
		struct decl	*dummy[2];

		decty = parse_declarator(curtok, ty, mode);

		if (decty != NULL) {
			char		*asmname = NULL;
			int		is_incomplete_tentative = 0;

			/* Store declaration */

			debug_print_type(decty, mode, 0);

			if ((*curtok)->type == TOK_KEY_ASM) {
				if (decty->storage == TOK_KEY_REGISTER) {
					warningfl(*curtok,
						"Ignoring __asm__ name "
						"request");
					(void) parse_asm_varname(curtok);
				} else if (curscope != &global_scope
					&& decty->storage != TOK_KEY_EXTERN
					&& decty->storage != TOK_KEY_STATIC) {
					errorfl(*curtok,
						"Cannot give asm name to "
						"nonstatic variable");
					return ret;
				} else {
					asmname = parse_asm_varname(curtok);
					if (asmname == NULL) {
						return NULL;
					}	
				}
			}

			if (decty->code == TY_VOID
				&& decty->tlist == NULL
				&& mode != DECL_CAST
				&& decty->storage != TOK_KEY_TYPEDEF) {
				errorfl(declstart,
					"Cannot create object of type `void'");
				return ret;
			} else if ((decty->code == TY_STRUCT
					|| decty->code == TY_UNION)	
				&& decty->storage != TOK_KEY_TYPEDEF	
				&& decty->storage != TOK_KEY_EXTERN
				&& decty->tstruc->incomplete
				&& !decty->is_def
				&& (decty->tlist == NULL
					|| (decty->tlist->type != TN_FUNCTION
					&& !is_arr_of_ptr(decty)))) {
				/*
				 * 03/03/09: Tentative declarations of
				 * incomplete struct types are allowed
				 */
				if (curscope != &global_scope) {
					errorfl(declstart,
					"Cannot instantiate object of incomplete type");
					return ret;
				} else {
					/*
					 * Save this declaration for later so
					 * we can check whether the type was
					 * completed
					 */
					is_incomplete_tentative = 1;
				}
			}
			if (decty->implicit
				&& decty->name != NULL) {
				if (decty->is_func
					&& (*curtok)->type == TOK_COMP_OPEN) {
					warningfl(*curtok,
						"Return type of `%s' defaults "
						"to `int'", decty->name);
				} else {
					if (0/* c99*/) {
						errorfl(*curtok,
				"No type or storage class specified for `%s'",
						decty->name);
						return ret;
					} else {
						warningfl(*curtok,
				"No type or storage class specified for `%s' -"
				" assuming `int' (illegal in C99)", decty->name);
						decty->code = TY_INT;
					}
				}
			}

			if (decty->storage == 0) {
#if 0
				if (curscope == &global_scope) {
					decty->storage = TOK_KEY_EXTERN;
				}
#endif
			} else if (decty->storage == TOK_KEY_EXTERN) {
				if (curscope != &global_scope) {
					/*
					 * ``extern'' declarations with block
					 * scope may not have an initializer!
					 */
#if 0
					errorfl(*curtok,
	"Invalid initializer for ``extern'' declaration at block scope"); 
#endif
				}
			}

			tmp = alloc_decl();
			tmp->tok = declstart; 
			tmp->dtype = decty;

			if (is_incomplete_tentative) {
				add_incomplete_tentative_decl(tmp);
			}

			if (decty->is_func
				&& decty->tlist->type == TN_FUNCTION) {
				tmp->asmname = decty->tlist->tfunc->asmname; 
			} else {
				tmp->asmname = asmname;
			}

			if (decty->attributes != NULL
				&& decty->storage == TOK_KEY_STATIC) {
				struct attrib	*a;


				/*
				 * 05/17/09: Extern alias declarations also
				 * require us to export global symbols, so
				 * those are handled by the emitters. For
				 * static declarations, we avoid the
				 * assembler .set declaration and just make
				 * it equivalent to
				 *
				 *    static void foo() __asm__("bar");
				 *
				 * instead
				 */
				if ((a = lookup_attr(decty->attributes, ATTRF_ALIAS)) != NULL) {
					tmp->asmname = a->parg;
				}
			}

#if 0
			merge_attr_with_decl(tmp, decty->attributes);
#endif
			if (mode == DECL_VARINIT) {
				dummy[0] = tmp;
				dummy[1] = NULL;
				
				/*
				 * 04/11/08: Added handling of K&R functions,
				 * since we now don't read the parameter
				 * declaration list in parse_func() already,
				 * but here in parse_decl().
				 *
				 * The declarator has already been read, so
				 * for a function definition we must be here
				 * now:
				 *    ISO: void (*f(int x))[1]  { ... }
				 *                             ^ here
				 *    K&R: void (*f(x))[1] int x; { ... }
				 *                         ^^^ here
				 *
				 * The K&R case only applies if there is a
				 * non-empty paramter list!
				 */
				if (tmp->dtype->is_func
					&& tmp->dtype->tlist->tfunc->type
						== FDTYPE_KR
					&& tmp->dtype->tlist->tfunc->ntab
						!= NULL) {
					is_incomplete_kr_func = 1;
				}

				if (((*curtok)->type == TOK_COMP_OPEN
					&& tmp->dtype->is_func)
					|| is_incomplete_kr_func 
					
					/*(tmp->dtype->is_func
					&& tmp->dtype->tlist->tfunc->type
						== FDTYPE_KR
					&& tmp->dtype->tlist->tfunc->ntab
						!= NULL)*/) {
					/*
					 * 04/09/08: This is a particularly
					 * nasty bug:
					 *
					 *   void (*f(void))[1] { ... }
					 *
					 * ... this fails because the
					 * simplistic old function declaration
					 * code always closes the function
					 * prototype scope if the next token
					 * isn't a brace. I.e.
					 *
					 *   void foo(int x);  // close scope
					 *   void foo(int x) { } // keep open
					 *
					 * The ``nexttok != {'' check fails
					 * with more complex declarations.
					 *
					 * -> We have to reset the scope to
					 * the function prototype one to be
					 * sure
					 */
					curscope = tmp->dtype->tlist->
						tfunc->scope;

					tmp->dtype->is_def = 1;

					/*
					 * 04/11/08: We cannot store the
					 * declaration yet if this is a K&R
					 * function definition, because in
					 * that case we still have to read
					 * parameter declarations.
					 *
					 * (store_decl_scope() needs param
					 * information to check for
					 * redeclarations).
					 */
					if (!is_incomplete_kr_func) {
						store_decl_scope(curscope->parent,
							dummy);
					}
				} else if (!tmp->dtype->is_def
					|| (tmp->dtype->code != TY_ENUM
					&& tmp->dtype->code != TY_STRUCT
					&& tmp->dtype->code != TY_UNION)) {
					if ((*curtok)->type == TOK_OPERATOR
						&& *(int *)(*curtok)->data
						== TOK_OP_ASSIGN) {
						/*
						 * store_decl_scope() will look
						 * at the initializer, which
						 * has not been read yet ...
						 * So use dummy initializer to
						 * fool it
						 */
						static struct initializer in;

						tmp->init = &in;
					}	
					store_decl_scope(curscope, dummy);
				}	
			}	
			store_decl(&ret, tmp, &alloc, &index);

			/*
			 * If this is an array declaration, check whether the
			 * size was specified (if necessary!). important: Need
			 * to take initializer into account;
			 * int foo[] = { ... }; is OK
			 */
			if (decty->storage != TOK_KEY_EXTERN
				&& decty->storage != TOK_KEY_TYPEDEF
				&& decty->tlist != NULL
				&& decty->tlist->type == TN_ARRAY_OF
#if REMOVE_ARRARG
				&& !decty->tlist->have_array_size) {
#else
				&& decty->tlist->arrarg->const_value == NULL) {
#endif

				if (mode != DECL_FUNCARG
					&& mode != DECL_FUNCARG_KR
					&& mode != DECL_STRUCT
					&& mode != DECL_CAST
					&& ((*curtok)->type != TOK_OPERATOR
						|| *(int *)(*curtok)->data !=
						TOK_OP_ASSIGN)
					&& curscope != &global_scope) {
					/*
					 * 03/27/08: Added global scope check, since
					 * ``static int ar[];'' is allowed in gcc as
					 * well, just not extern
					 */
					errorfl(declstart,
						"Array declaration misses size");
					return ret;
				} else if (decty->storage == TOK_KEY_STATIC
					&& (*curtok == NULL
					|| (*curtok)->type != TOK_OPERATOR
					|| *(int *)(*curtok)->data !=
					TOK_OP_ASSIGN)) {
						
					/*
					 * For gcc compatibility global non-extern
					 * declarations without size are allowed
					 */
					warningfl(declstart,
					"Static array declaration misses "
					"size - not allowed in ISO C");
				}
			}	

			if (mode == 0) {
				/* Is init */
				return ret;
			}
		} else {
			return ret;
		}

		if (*curtok == NULL) return ret;
		if ((*curtok)->type == TOK_SEMICOLON) {
			break; /* done */
		} else if ((*curtok)->type == TOK_PAREN_CLOSE &&
			(mode == DECL_CAST
				|| mode == DECL_FUNCARG
				|| mode == DECL_FUNCARG_KR)) {
			return ret;
		} else if ((*curtok)->type == TOK_OPERATOR &&
			*(int *)(*curtok)->data == TOK_OP_COMMA) {
			/* 
			 * If this is an ISO function argument, the base
			 * type cannot be reused, so we must start
			 * from scratch again - return. However, in K&R
			 * definitions, it can be reused!
			 */
			if (mode == DECL_FUNCARG) {
				return ret;
			}	
			if (next_token(curtok) != 0) {
				return ret;
			}
			continue;
		} else if ((*curtok)->type == TOK_OPERATOR &&
			*(int *)(*curtok)->data == TOK_OP_ASSIGN) {
			/*
			 * 05/31/11: This was incorrectly done for ALL
			 * constructs involving VLAs - including pointers
			 * to them;
			 *     char (*x)[N] = &ar;  // ok
			 */
			/*if (IS_VLA(decty->flags)) {*/
			if (is_immediate_vla_type(decty)){
				errorfl(*curtok, "Variable-length arrays "
					"may not have initializers");
				return ret;
			} else if (mode == DECL_STRUCT) {
				errorfl(*curtok, "Invalid initializer for "
					"structure member");
				return ret;
			} else if (mode == DECL_FUNCARG
				|| mode == DECL_FUNCARG_KR) {
				errorfl(*curtok, "Initializer given for "
					"function argument");
				return ret;
			} else {
				/* Read initializer */

				if (tmp->dtype->storage == TOK_KEY_TYPEDEF) {
					errorfl(*curtok, "Initializer "
						"specified for typedef");
					;
				} else if ((tmp->dtype->code == TY_STRUCT
					|| tmp->dtype->code == TY_UNION)
					&& tmp->dtype->tlist == NULL
					&& tmp->dtype->tstruc
					&& tmp->dtype->tstruc->incomplete) {
					errorfl(*curtok, "Initializer "
						"specified for incomplete "
						"type");
					return ret;
				}

				if (next_token(curtok) != 0) {
					return ret;
				}

				if (((tmp->dtype->tlist
					&& tmp->dtype->tlist->type
						== TN_ARRAY_OF)
					|| ((tmp->dtype->code == TY_STRUCT
					|| tmp->dtype->code == TY_UNION)
					&& tmp->dtype->tlist == NULL))
					&& tmp->dtype->storage != TOK_KEY_STATIC
					&& tmp->dtype->storage != TOK_KEY_EXTERN) {
					/*
					 * Automatic struct or array. These are
					 * now allowed to have variable
					 * initializers (as per GNU C/C99), but
					 * constants are preferred - thus use
					 * EXPR_OPTCONST
					 */
					init = get_init_expr(curtok,
							NULL,
							EXPR_OPTCONSTINIT,
							tmp->dtype, 1, 0);
				} else if (tmp->dtype->storage == TOK_KEY_STATIC
					|| tmp->dtype->storage
						== TOK_KEY_EXTERN) {
					init = get_init_expr(curtok,
							NULL,
							EXPR_CONSTINIT,
							tmp->dtype, 1, 0);
				} else {
					init = get_init_expr(
						curtok, NULL, EXPR_INIT,
						tmp->dtype, 1, 0); 
				}
				if (init == NULL) {
					return ret;
				}
				tmp->init = init;
				if (tmp->dtype->tlist != NULL
					&& tmp->dtype->tlist->type
						== TN_ARRAY_OF	
#if REMOVE_ARRARG
					&& !tmp->dtype->tlist->have_array_size) {
#else
					&&tmp->dtype->tlist->arrarg->const_value
						== NULL) {
#endif
					/* 
					 * Array size determined by
					 * initializer
					 * XXX this is completely bogus and
					 * only works for 1d arrays ... need
					 * to do this in get_init_expr()?!
					 */
					init_to_array_size(tmp->dtype, init);
				}
					
#ifdef DEBUG2
				if (*curtok) {
					printf("returned at %s\n",
						(*curtok)->ascii);
				}
#endif
				if ((*curtok)->type == TOK_SEMICOLON) {
					break;
				} else if ((*curtok)->type == TOK_OPERATOR
					&& *(int *)(*curtok)->data
						== TOK_OP_COMMA) {
					if (next_token(curtok) != 0) {
						return ret;
					}
				}	
			}
		} else if ((*curtok)->type == TOK_COMP_OPEN) {
			/*
			 * 04/11/08: Maybe this is a K&R function with
			 * all-implicit int parameters, so we still have
			 * to create those implicit int declaration data
			 * structures
			 */
			if (decty->tlist->tfunc->ntab != NULL) {
				(void) get_kr_func_param_decls(
				decty->tlist->tfunc,
				NULL, decty->tlist->tfunc->ntab);
				store_decl_scope(curscope->parent,
					dummy);
			}	
			return ret;
		} else if (IS_TYPE(*curtok)
				&& decty->is_func
				&& decty->tlist->tfunc->type == FDTYPE_KR
				&& decty->tlist->tfunc->ntab != NULL) {
			/*
			 * 04/11/08: This must be the parameter type
			 * declaration list in a K&R function definition;
			 *
			 *    void foo(x, y, z)
			 *         int x;
			 *         ^^^-------- we are here
			 *         char *p;
			 *    {
			 */
			if (get_kr_func_param_decls(decty->tlist->tfunc,
					curtok, decty->tlist->tfunc->ntab) == -1) {
				return ret;
			}

			store_decl_scope(curscope->parent,
				dummy);
			/*
			 * We are guaranteed to be at the { now, so all is well
			 */
			next_token(curtok);
			return ret;
		} else {
			errorfl(*curtok, 
				"Syntax error at `%s'", (*curtok)->ascii);
			return ret;
		}
	}
	/**curtok = tok;*/
	return ret;
}

