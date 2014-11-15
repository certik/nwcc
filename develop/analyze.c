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
 * This file contains the hierarchial source analyzer, called
 * analyze(). This function uses the global variable
 * ``toklist'' which must point to the complete translation unit
 */

#include "analyze.h"
#include <string.h>
#include <stdlib.h>
#include "token.h"
#include "decl.h"
#include "decl_adv.h"
#include "error.h"
#include "evalexpr.h"
#include "backend.h"
#include "scope.h"
#include "symlist.h"
#include "type.h"
#include "expr.h"
#include "misc.h"
#include "functions.h"
#include "debug.h"
#include "zalloc.h"
#include "builtins.h"
#include "inlineasm.h"
#include "features.h"
#include "icode.h"
#include "control.h"
#include "n_libc.h"

static void
check_main(struct token *t, struct type *ty) {
	static struct type 	*maindef = NULL;
	struct ty_func		*tfunc = ty->tlist->tfunc;
	struct sym_entry	*se;
	struct type		*tytmp;

	if (maindef != NULL) return;
	if (ty->name[0] != 'm' || strcmp(ty->name + 1, "ain") != 0) {
		return;
	}
	maindef = ty;

	/* XXX why is it always null??? */
	if (ty->code != TY_INT
		|| ty->tlist->next != NULL) {
		warningfl(t,
	"Illegal return type for main(). ISO C says it must be `int'!");
		return;
	} else if (ty->storage == TOK_KEY_STATIC) {
		warningfl(t, "Static (file scope) declaration of main()");
		return;
	}	

	switch (tfunc->nargs) {
	case 0:
	case -1:
		/* int main(void) or int main() */
		break;
	case 1:
		warningfl(t,
			"main() should take no, two or three arguments");
		break;
	case 2:
	case 3:
		/* XXX check arg1 + 2 = int,char** */

		se = tfunc->scope->slist;
		tytmp = make_basic_type(TY_INT);

		if (compare_types(se->dec->dtype, tytmp,
			CMPTY_SIGN|CMPTY_CONST) != 0) {
			warningfl(t, "First argument of main is not \
`int' as it should be");
		}

		tytmp = n_xmemdup(make_basic_type(TY_CHAR), sizeof *tytmp);
		append_typelist(tytmp, TN_POINTER_TO, 0, NULL, NULL);
		append_typelist(tytmp, TN_POINTER_TO, 0, NULL, NULL);

		if (compare_types(se->next->dec->dtype, tytmp,
			CMPTY_SIGN|CMPTY_CONST) != 0) {
			warningfl(t, "Second argument of main is not \
`char **' as it should be");
		}
		free(tytmp);

		if (tfunc->nargs == 3) {
			/* XXX check arg3 = char** */
		}
	}
}	

static int
is_deferrable_inline_function(struct function *func) {
	if (IS_INLINE(func->proto->dtype->flags) &&
		(func->proto->dtype->storage == TOK_KEY_EXTERN
			|| func->proto->dtype->storage == TOK_KEY_STATIC)) {
		return 1;
	} else {
		return 0;
	}
}


/* 
 * Do semantic analysis of translation unit. 
 * Initialize functions, structure, globals subsystems
 * Returns 0 if no errors were found, else 1
 */
int
analyze(struct token **curtok) {
	struct token		*t;
	struct decl		**d;
	struct expr		*ex;
	struct function		*func = NULL;
	struct token		*builtin_tok = NULL;
	struct decl		**builtin_decl = NULL;
	static int		level;
	int			braces = 0;
	int			is_func;
	int			i;

	if (++level == 1) {
		static int	ptropval = TOK_OP_AMB_MULTI;
		static int	num = 1;
		static const struct {
			void	*p;
			int	tokval;
		} normal_va_list[] = {
			{ "typedef", TOK_IDENTIFIER },
			{ "char", TOK_IDENTIFIER },
			{ &ptropval, TOK_OPERATOR },
			{ "__builtin_va_list", TOK_IDENTIFIER },
			{ NULL, TOK_SEMICOLON },
			{ NULL, 0 }
		}, amd64_va_list[] = {
			/* typedef struct { */
			{ "typedef", TOK_IDENTIFIER },
			{ "struct", TOK_IDENTIFIER },
			{ NULL, TOK_COMP_OPEN },

			/* unsigned gp_offset; */
			{ "unsigned", TOK_IDENTIFIER },
			{ "gp_offset", TOK_IDENTIFIER },
			{ NULL, TOK_SEMICOLON },

			/* unsigned fp_offset; */
			{ "unsigned", TOK_IDENTIFIER },
			{ "fp_offset", TOK_IDENTIFIER },
			{ NULL, TOK_SEMICOLON },

			/* void *overflow_arg_area; */
			{ "void", TOK_IDENTIFIER },
			{ &ptropval, TOK_OPERATOR },
			{ "overflow_arg_area", TOK_IDENTIFIER },
			{ NULL, TOK_SEMICOLON },

			/* void *reg_save_area; */
			{ "void", TOK_IDENTIFIER },
			{ &ptropval, TOK_OPERATOR },
			{ "reg_save_area", TOK_IDENTIFIER },
			{ NULL, TOK_SEMICOLON },

			/* } __builtin_va_list[1]; */
			{ NULL, TOK_COMP_CLOSE },
			{ "__builtin_va_list", TOK_IDENTIFIER },
			{ NULL, TOK_ARRAY_OPEN },
			{ &num, TY_INT },
			{ NULL, TOK_ARRAY_CLOSE },
			{ NULL, TOK_SEMICOLON },
			{ NULL, 0 }
		}, *vatok;

		/* Initial call; Otherwise called by parse_expr() */
		curscope = &global_scope;

#if 0
		store_token(&builtin_tok, 
			n_xstrdup("typedef"), TOK_IDENTIFIER, 0);
		store_token(&builtin_tok,
			n_xstrdup("char"), TOK_IDENTIFIER, 0);
		i = TOK_OP_AMB_MULTI;
		store_token(&builtin_tok, &i, TOK_OPERATOR, 0);
		store_token(&builtin_tok, "__builtin_va_list",
			TOK_IDENTIFIER, 0);
		store_token(&builtin_tok, 0, TOK_SEMICOLON, 0);
		store_token(&builtin_tok, NULL, 0, 0);
		if ((builtin_decl = parse_decl(&builtin_tok, 0)) != NULL) {
			store_decl_scope(&global_scope, builtin_decl);
		} else {
			warningfl(NULL, "Cannot create builtins");
		}
#endif
		if (backend->arch == ARCH_AMD64) {
			vatok = amd64_va_list;
		} else {
			vatok = normal_va_list;
		} 
		for (i = 0; vatok[i].p || vatok[i].tokval; ++i) {
			void	*p;
			int	*dummyptr = n_xmalloc(sizeof *dummyptr);

			if (vatok[i].tokval == TOK_IDENTIFIER) {
				p = n_xstrdup(vatok[i].p);
			} else if (vatok[i].tokval == TY_INT) {
#if 0
				p = n_xmemdup(vatok[i].p, sizeof(int));
#endif
				p = zalloc_buf(Z_CEXPR_BUF); /*n_xmalloc(16);*/ /* XXX */
				memcpy(p, vatok[i].p, sizeof(int)); /* XXX */
			} else {
				if (vatok[i].p != NULL) {
					p = vatok[i].p;
				} else {
					p = dummyptr;
				}
			}
			store_token(&builtin_tok, p, vatok[i].tokval, 0, NULL); 
		}
		if ((builtin_decl = parse_decl(&builtin_tok, 0)) != NULL) {
			store_decl_scope(&global_scope, builtin_decl);
			builtin_va_list_type = builtin_decl[0]->dtype;
		} else {
			warningfl(NULL, "Cannot create builtins");
		}
		t = toklist; /* Start at beginning */
	} else {
		t = *curtok; /* Start at current token */
	}
	
	for (; t != NULL; t = t->next) {
		if (t->prev
			&& (t->prev->type == TOK_COMP_OPEN
			|| t->prev->type == TOK_COMP_CLOSE
			|| t->prev->type == TOK_SEMICOLON)) {
			if (!errors) {
				/*
				 * 09/01/07: XXX This token is apparently  
				 * still sometimes used if we have parse
				 * errors!!!!
				 */
				free(t->prev);
				t->prev = NULL;
			}	
		}

		/*
		 * 08/19/07: Check with lookup_symbol() added. Otherwise
		 * stuff like
		 *
		 *    typedef int foo;
		 *    {
		        int foo;
		 *       foo = 0;    <--- bad!!
		 *    }
		 *
		 * ... yields a syntax error because ``foo = 0;'' is
		 * interpreted as a declaration
		 *
		 * 05/13/09: Handle labels! For
		 *
		 *     typedef int foo;
		 *     foo: ;
		 *
		 * ... we want ``foo:'' to be a label.
		 */
		if (IS_TYPE(t)
			&& (t->type != TOK_IDENTIFIER
			|| (lookup_symbol(curscope, t->data, 1) == NULL
				&& !is_label(&t) ) ) ) {
do_decl:
			if (curscope->have_stmt) {
				warningfl(t, "Mixed declarations and "
					"statements are not allowed "
					"in ISO C90 (please declare "
					"all variables at the top of a "
					"block (`{ ... }') instead!)");
			}
			d = parse_decl(&t, DECL_VARINIT);
			if (d == NULL) {
				/*
				 * XXX call recover() instead? 
				 */
				 t = t->next;
				 while (t != NULL
					 && t->type != TOK_SEMICOLON
					 && !IS_KEYWORD(t->type)) {
					t = t->next;
				 }
				 if (t == NULL) {
					break;
				 }
				 if (IS_KEYWORD(t->type)) {
					 t = t->prev;
				 }	 
				 continue;
			} else {
				/*
				 * 07/23/07: Don't free tokens if there's a
				 * VLA involved somewhere, cuz otherwise we
				 * still need to evaluate the expression(s)
				 *
				 * 02/25/08: Don't do this either for
				 * implicit non-function declarations such
				 * as   ``bs;''; This causes memory errors
				 *
				 * 01/31/10: Turn this off entirely because
				 * this still caused errors in some cases,
				 * such as
				 *
				 *    char (*p)[128] = malloc(sizeof "gnu");
				 *    puts(strcpy(*p, "gnu"));
				 *
				 * ... WITHOUT including string.h and
				 * stdlib.h. This is apparently because the
				 * function catalog type checking references
				 * deleted tokens to print warnings
				 */
#if 0
				for (k = 0; d[k] != NULL; ++k) {
					if (IS_VLA(d[k]->dtype->flags)) {
						break;
					} else if (d[k]->dtype->implicit
						&& !d[k]->dtype->is_func) {	
						break;
					}
				}	
				if (d[k] == NULL) {
					free_tokens(starttok, t->prev, FREE_DECL);
				}	
#endif
			}

			is_func = 0;
				
			if ((d[0]->dtype->code == TY_STRUCT
				|| d[0]->dtype->code == TY_ENUM
				|| d[0]->dtype->code == TY_UNION)
				&& d[0]->dtype->is_def) {
				if (!d[0]->dtype->is_func) {
					continue;
				}	
			}	
#ifdef DEBUG2
			printf("Parsed declaration successfully!\n");
#endif
			if (t->type == TOK_COMP_OPEN) {
				++braces;
				for (i = 0; d[i] != NULL; ++i) {
					if (d[i]->dtype->is_func) {
						is_func = 1;
					}
				}
				if (is_func && i > 1) {
					/* 
					 * This must be illegal, as in
					 * int foo, bar(void) { ... }
					 */
					errorfl(t,
"Syntax error at %s - function definitions may not be paired with "
"other declarations!", t->ascii);
					/* XXX what to do here? */
				} else if (is_func) {
					if (curscope->parent
						!= &global_scope) {
						/* 
						 * ->parent because
						 * parse_func() already
						 * opens new scope
						 */
						errorfl(t,
			"Nested function definitions are not allowed");
						/*XXX what to do here?*/
					} else {
						struct sym_entry	*se;
						if (IS_INLINE(d[0]->dtype->flags) &&
							(d[0]->dtype->storage == TOK_KEY_EXTERN
							|| d[0]->dtype->storage == TOK_KEY_STATIC)) {
							/*
							 * 03/01/09: Disable zone allocation
							 * for inline function because we
							 * only generate the function body
							 * at the very end of the program
							 */
							zalloc_enable_malloc_override();
						}

						d[0]->dtype->is_def = 1;
						func = alloc_function();

						/*
						 * 04/09/08: Moved assignment
						 * of siv_tail here - is that
						 * ok?
						 */
						siv_checkpoint = siv_tail;

						func->static_init_vars_checkpoint =
							siv_checkpoint;

						/*func->scope = curscope;*/
						func->proto = d[0];
						/* 06/17/08: Save return type */
						func->rettype = func_to_return_type(func->proto->dtype); 
						func->fty = d[0]->dtype->tlist->tfunc;
						/* XXX complete ... */
						check_main(t, d[0]->dtype);
						curfunc = func;

						/*
						 * Note that in cases like
						 * void (*foo())() { ...
						 * (function returning
						 * function pointer), the
						 * current scope will now
						 * be that of the return
						 * type function declaration.
						 * So it has to be set to that
						 * of the function definition
						 * here
						 *
						 * 04/09/08: Duplicated, done
						 * in parse_decl() too now
						 */
						curscope = d[0]->dtype->
							tlist->tfunc->scope;
						func->scope = curscope;

						/*
						 * 02/03/10: Disallow
						 * unnamed parameters
						 */
						for (se = curscope->slist;
							se != NULL;
							se = se->next) {
							if (se->dec->dtype->name
								== NULL) {
								errorfl(t,
			"Unnamed function parameters are not allowed in C");
							}
						}

						/*
						 * 04/09/08: Save function
						 * name arrays here, so we
						 * have a deterministic
						 * scope to put them in (see
						 * above)
						 */
						put_func_name(func->proto->
							dtype->name);	
						
					}
				} else {
					errorfl(t,
					"Syntax error at `%s'", t->ascii);
					/* XXX what to do here? */
				}
			}
		} else if (t->type == TOK_KEY_ASM) {
			/* Inline assembler statement */
			struct statement	*st;
			struct inline_asm_stmt	*inl;
			int			is_global = 0;

			curscope->have_stmt = 1;
			if (curscope == &global_scope) {
#if ! XLATE_IMMEDIATELY
				errorfl(t, "asm statement at global "
					"scope not supported in this "
					"mode of operation");
#else
				is_global = 1;
#endif
			}

			if ((inl = parse_inline_asm(&t)) != NULL) {
				if (is_global) {
					errorfl(t, "Invalid asm statement at top level");
				} else {	
					st = alloc_statement();
					st->type = ST_ASM;
					st->data = inl;
					append_statement(&curscope->code,
						&curscope->code_tail, st);
					t = t->next;
				}
			}
		} else if (IS_CONTROL(t->type)) {
#if 0
			struct scope *junk = curscope;
#endif
			curscope->have_stmt = 1;
			(void) parse_ctrl(curcont, &t, 1, NULL);
#if 0
			debug_print_statement(junk->code);
#endif
#ifdef NO_EXPR
/* 
 * If the expression parser is used, it will parse casts on its own -
 * if it's not, we still want to parse casts in order to debug the
 * code responsible for it!
 */
		} else if (t->type == TOK_PAREN_OPEN
			&& curscope != &global_scope) {
			if (t->next == NULL) {
				errorfl(t, "Unexpected end of file");
				break; /* XXX */
			}
			if (IS_TYPE(t->next)) {
				/* This has got to be a cast */
				t = t->next;
				d = parse_decl(&t, DECL_CAST);
				if (d != NULL) {
#ifdef DEBUG2
					printf("Parsed cast successfully!\n");
#endif
				} else {
					/* Recover from error */
					while (t != NULL
						&& t->type != TOK_SEMICOLON) {
						t = t->next;
					}
					if (t == NULL) {
						break;
					}
				}
			} 
#endif
		} else if (t->type == TOK_COMP_OPEN) {
			/* Opening a compound statement - new scope! */
			struct statement	*st;

			if (curscope == &global_scope) {
				errorfl(t,
				"Invalid compound statement at top level");
				/* Create new scope anyway, to match with } */
			}
			if (curcont != NULL && curcont->stmt == NULL) {
				curcont->compound_body = 1;
				curcont->stmt = alloc_statement();
				curcont->stmt->type = ST_COMP;
				curcont->stmt->data = new_scope(SCOPE_CODE);
				curcont->braces = braces;
			} else {
				st = alloc_statement();
				st->type = ST_COMP;
				append_statement(&curscope->code,
					&curscope->code_tail, st);
				st->data = new_scope(SCOPE_CODE);
			}
			++braces;
		} else if (t->type == TOK_COMP_CLOSE) {
			/* Closing compound statement - leaving scope! */
			close_scope();
			if (--braces == 0 && func) {
				/* This is the end of a function definition */
				/*
				 * 03/01/09: XXX Is this really needed with
				 * XLATE_IMMEDIATELY enabled? We do need it
				 * for inline functions now, but perhaps not
				 * for others. At least it will requore
				 * func->proto and func->proto->dtype to be
				 * allocated WITHOUT zone allocator if the
				 * list is to be processed at the end of
				 * the program
				 *
				 * 03/04/09: Answer: Yes, we need the function
				 * list in all cases for some purposes. On
				 * PPC it is traversed to generate TOC
				 * entries
				 */
#if 1 /*! XLATE_IMMEDIATELY*/
				func->next = funclist;
				funclist = func;
#endif

				/* 07/20/08: Handle labels for computed gotos */
				process_labels_used_list(curfunc);

#if XLATE_IMMEDIATELY
				if (/*IS_INLINE(func->proto->dtype->flags) &&
					(func->proto->dtype->storage == TOK_KEY_EXTERN
					|| func->proto->dtype->storage == TOK_KEY_STATIC)*/
					is_deferrable_inline_function(func)) {
					/*
					 * 03/01/09: Don't generate inline function
					 * definitions before the end of the program.
					 * This is needed so that we can suppress
					 * unused function bodies, which is in turn
					 * needed to avoid references to variables
					 * which may not be linked into the program
					 * (this is gcc behavior which some programs
					 * rely on)
					 */
					zalloc_disable_malloc_override();
#if 0
					func->next = funclist;
					funclist = func;
#endif
					goto skip_fdef;
				}
#endif


#if XLATE_IMMEDIATELY
				{
					struct scope	*temp = curscope;
					struct decl	*d;

					curscope = &global_scope;
					allow_vreg_map_preg();
					if (func->static_init_vars_checkpoint) {
						d = func->
						static_init_vars_checkpoint;
					} else {
						d = static_init_vars;
					}

					xlate_func_to_icode(func);
					if (d != NULL) {
#if 0
						emit->static_init_vars(d);
#endif
					}
					forbid_vreg_map_preg();
					(void) backend->generate_function(func);
#if    USE_ZONE_ALLOCATOR
					/* Reset function data structures */
					zalloc_reset();
#      endif
#      if FAST_SYMBOL_LOOKUP
					reset_fast_sym_hash();
#      endif
					curscope = temp;
				}
#endif

				curfunc = func = NULL;
			}
skip_fdef:
			if (curcont && braces == curcont->braces) {
				complete_ctrl(&t, curcont);
			}

			if (braces == 0 && level > 1) {
				/* GNU statement-as-expression ends here */
				*curtok = t;
				break;
			}
		} else {
			if (curscope == &global_scope) {
				/* Must be implicit function declaration */
				if (t->type == TOK_SEMICOLON) {
					warningfl(t,
						"Empty expression used outside"
						" of function - illegal in ISO"
						" C");
					continue;
				}	
				goto do_decl;
			}

			if (t->type == TOK_IDENTIFIER) {
				if (try_label(&t, NULL)) {
					/* was label */
					if (t->next != NULL
						&& t->next->type == TOK_KEY_ATTRIBUTE) {
						/*
						 * 03/01/09: Allow attributes after labels
						 * (needed by PostgreSQL)
						 */
						t = t->next;
						(void) get_attribute(NULL, &t); /* XXX */
						if (t) {
							t = t->prev;
						}
					}

					continue;
				}
			}
			curscope->have_stmt = 1;

			ex = parse_expr(&t, TOK_SEMICOLON, 0, 0, 1);
			if (ex != NULL) {
				put_expr_scope(ex);
			}
		}
		if (t == NULL) {
			break;
		}
	}

	if (level == 1) {
		curscope = &global_scope;
	}

	if (braces > 0) {
		errorfl(NULL, "Parse error at end of input - "
			"%d missing `}' tokens", braces);
	}
	--level;

	if (level == 0) {
		check_incomplete_tentative_decls();
	}

	if (!errors) {
		if (level == 0) {
#if ! XLATE_IMMEDIATELY
			for (func = funclist; func != NULL; func = func->next) {
				xlate_func_to_icode(func);
			}
#else
			/*
			 * 03/01/09: Now we can generate local inline
			 * functions - but only do it if they were
			 * referenced. That's the point of keeping them
			 * back until now
			 */
			for (func = funclist; func != NULL; func = func->next) {
				curscope = &global_scope;

				if (is_deferrable_inline_function(func)
					&& func->proto->references > 0) {

					/*IS_INLINE(func->proto->dtype->flags) &&
					(func->proto->dtype->storage == TOK_KEY_EXTERN
					|| func->proto->dtype->storage == TOK_KEY_STATIC)
					&& func->proto->references > 0) { */

					allow_vreg_map_preg();
					xlate_func_to_icode(func);
					forbid_vreg_map_preg();
					(void) backend->generate_function(func);
				}
			}
#endif
		}
	} else if (level != 0) {
		return -1;
	}	

#if 0
	if (/* check_unused */1) {
		struct decl	*dp;
		int			i;
		struct decl	*darr[] = {
			static_init_vars, static_uninit_vars, NULL
		};

		for (i = 0; darr[i] != NULL; ++i) {
			for (dp = darr[i]; dp != NULL; dp = dp->next) {
				if (dp->dtype->storage != TOK_KEY_EXTERN
					&& dp->references == 0
					&& !is_basic_agg_type(dp->dtype)) {
					warningfl(dp->tok,
						"Unused variable `%s'",
						dp->dtype->name);
				}
			}
		}
	}
#endif
	return 0;
}

