/*
 * Copyright (c) 2006, Nils R. Weller
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
 * Macro storage and substitution 
 */
#include "macros.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include "token.h"
#include "error.h"
#include "n_libc.h"
#include "preprocess.h"

struct macro *
alloc_macro(void) {
	struct macro	*ret = n_xmalloc(sizeof *ret);
	static struct macro	nullmacro;

	*ret = nullmacro;
	return ret;
}



struct predef_macro *
tmp_pre_macro(const char *name, const char *text) {
	static struct predef_macro      pm;
	struct predef_macro             *ret = n_xmalloc(sizeof *ret);

	*ret = pm;
	ret->name = n_xstrdup(name);
	ret->text = n_xstrdup(text);
	return ret;
}


/* XXX make hash table */
static struct macro	*macro_list;
static struct macro	*macro_list_tail;

/*
 * See macros.h for the definition of N_HASHLISTS - it must
 * be visible in all files for in-place hashing
 */
static char		macro_len_tab[N_HASHLISTS];
static struct macro	*macro_list_hash[N_HASHLISTS];
static struct macro	*macro_list_hash_tail[N_HASHLISTS];


/*
 * hash function to index into the hash table.
 *
 *!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * DANGER DANGER DANGER DANGER DANGER DAANGER!!!!!!!!!!!!!!!!!!
 * DANGER DANGER DANGER DANGER!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * !!!!!!!!!!!!!!!!!!!!!!!DANGER DANGER DANGER DANGER!!!!!!!!
 *
 * If you change this function you must also change the in-
 * place hashing in (as of this writing) get_identifier()
 * and get_string()!!!! Just search for every user of
 * N_HASHLIST_MOD
 *
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 */
static int
hash_name(const char *name, size_t *len) {
	int		key = 0;
	const char	*start = name;

	for (; *name != 0; ++name) {
		key = (key * 33 + *name) & N_HASHLIST_MOD;
	}
	if (len != NULL) {
		*len = name - start;
	}
	return key;
}	

static struct macro *
do_lookup_macro(const char *name, size_t len, struct macro **prev, int *idx) {
	struct macro	*m;
	int		firstch;
	int		lastch;
	int		hashidx;
	size_t		lastch_index;

	if (*idx == -1) {
		size_t		len_;
		hashidx = *idx = hash_name(name, &len_);
		len = len_;
	} else {
		hashidx = *idx;
	}	

	lastch_index = len - 1;
	firstch = *name;
	lastch = name[lastch_index];

	*prev = NULL;
	
	for (m = macro_list_hash[hashidx]; m != NULL; m = m->next) {
		if (m->name == NULL) { /* XXX bug?!!?? */
			*prev = m;
			continue;
		}
		if (m->namelen != len) {
			/* May happen with macro_hash_list[N_HASHLISTS - 1] */
			*prev = m;
			continue;
		}	
		if (firstch == *m->name
			&& lastch == m->name[lastch_index]) {
			if (memcmp(m->name, name, len) == 0) {
				return m;
			}
		}	
		*prev = m;
	}
	return NULL;
}	
	
struct macro *
lookup_macro(const char *name, size_t len, int key) {
	struct macro	*prev;
	int		idx = key;
	
	return do_lookup_macro(name, len, &prev, &idx);
}	

struct macro *
put_macro(struct macro *new_macro, int len0, int key0) {
	struct macro	*m;
	size_t		len;
	int		key;
	
	if (key0 == -1) {
		len = strlen(new_macro->name);
		key = hash_name(new_macro->name, NULL);
	} else {	
		len = len0;
		key = key0;
	}

	new_macro->namelen = len;
	if (len == 7 && strcmp(new_macro->name, "defined") == 0) {
		lexerror("`defined' is not an allowed macro name");
		return NULL;
	}	
	if ((m = lookup_macro(new_macro->name, len, key)) != NULL) {
		/*
		 * Oops - already defined. This is OK if all
		 * tokens are the same only - Let's compare!
		 */ 
		struct token	*ot = m->toklist;
		struct token	*nt = new_macro->toklist;
		int		bad = 0;

		for (; ot != NULL && nt != NULL;
			ot = ot->next, nt = nt->next) {
			/*
			 * XXX Whitespace probably shouldn't be
			 * recorded at all. Above we should cut
			 * all outer WS from every new define
			 */ 
			while (ot && ot->type == TOK_WS) {
				ot = ot->next;
			}
			while (nt && nt->type == TOK_WS) {
				nt = nt->next;
			}
			if (ot == NULL || nt == NULL) {
				break;
			}

			if (ot->type != nt->type) {
				bad = 1;
				break;
			} /* else if (strcmp(ot->ascii,
			    nt->ascii) != 0) { */ 
		}
		while (ot && ot->type == TOK_WS) ot = ot->next;
		while (nt && nt->type == TOK_WS) nt = nt->next;
		if (ot || nt) {
			bad = 1;
		}
		if (bad) {
			lexerror("Redefinition of macro `%s' "
				"with different body",
				m->name);	
#if 0
			dump_toklist(m->toklist);
			dump_toklist(new_macro->toklist);
			exit(-rand());
#endif
			free(new_macro->name);
			free(new_macro);
			return NULL;
		}	

		/* Redefinition with same value - OK */
		return m;
	}	

	if (macro_list_hash_tail[key] == NULL) {
		macro_list_hash_tail[key] = macro_list_hash[key] = new_macro;
	} else {	
		macro_list_hash_tail[key]->next = new_macro;
		macro_list_hash_tail[key] = new_macro;
	}	
	new_macro->next = NULL;
	return new_macro;
}	

int 
count_macros() {
	int	i;
	int	count = 0;

	for (i = 0; i < N_HASHLISTS; ++i) {
		struct macro	*m;

		for (m = macro_list_hash[i]; m != NULL; m = m->next) {
			++count;
		}
	}
	return count;
}

int
drop_macro(const char *name, int slen, int hashkey) {
	struct macro	*m;
	struct macro	*prevm;
	int		hashidx = hashkey;

	if ((m = do_lookup_macro(name, slen, &prevm, &hashidx)) == NULL) {
		return -1;
	}

	/* found - remove! */
	if (prevm == NULL) {
		/* Head of list */
		macro_list_hash[hashidx] = macro_list_hash[hashidx]->next;
		if (m == macro_list_hash_tail[hashidx]) {
			/* Only node */
			macro_list_hash_tail[hashidx] = NULL;
		}
	} else {
		prevm->next = m->next;
		if (m == macro_list_hash_tail[hashidx]) {
			/* Last node */
			macro_list_hash_tail[hashidx] = prevm;
		}	
	}
	if (m->builtin) {
		lexwarning("Undefining predefined macro `%s'",
			m->name);	
	}	
	free(m->name);
	/*free_token_list(m->toklist);*/
	free(m);

	return 0;
}	

struct token *
builtin_to_tok(struct macro *m, struct token **tail) {
	struct token	*ret = alloc_token();

	if (strcmp(m->name, "__FILE__") == 0) {
		ret->type = TOK_IDENTIFIER;
		ret->data = ret->ascii = m->builtin;
	} else if (strcmp(m->name, "__LINE__") == 0) {
		ret->type = TY_INT;
		ret->ascii = n_xmalloc(32);
		sprintf(ret->ascii, "%d", *(int *)m->builtin);
		ret->data = n_xmemdup(m->builtin, sizeof(int));
	} else {
		printf("BUG: Unknown builtin macro `%s'\n", m->name);
		abort();
	}	
	if (tail != NULL) {
		*tail = ret;
	}	
	return ret;
}

/*
 * XXX this stuff is JUNK! The correct solution is to set maybe_funclike
 * in preprocess() and ``see what happens''
 */
static int
check_funclike(struct input_file *inf, FILE *out, struct macro *mp, int output) {
	char	buf[256]; /* XXX */
	char	*p = buf;
	int	ch;
	size_t	old_lex_chars_read = 0;
	void	*old_line_ptr = NULL;
	fpos_t	pos;
	char	*savedfilep = NULL;
	
	if (inf == NULL) { /* XXX */
		return 0;
	}	

	if (!output) {
#define RESTORE() do { \
	lex_chars_read = old_lex_chars_read; \
	lex_line_ptr = old_line_ptr; \
	if (inf->fd != NULL) { \
		fsetpos(inf->fd, &pos); \
	} else { \
		inf->filep = savedfilep; \
	} \
} while (0)
		old_lex_chars_read = lex_chars_read;
		old_line_ptr = lex_line_ptr;
		if (inf->fd != NULL) {
			fgetpos(inf->fd, &pos);
		} else {
			savedfilep = inf->filep;
		}
	}

	/*
	 * Function-like macro
	 * invocation if next token
	 * is an opening parenthesis
	 */
	while ((ch = FGETC(inf)) != EOF) {
		/* XXX this junk shit crap doesn't handle \n right :( */
		if (ch == '(') {
			/* YES, is function-like! */
			return 1;
		} else if (!isspace(ch)) {
			*p = 0;
			UNGETC(ch, inf);
			if (!output) {
				RESTORE();
			} else {	
				x_fprintf(out, "%s%s", mp->name, buf);
			}	
			return 0;
		} else {
			*p++ = ch;
		}	
	}
	*p = 0;

	if (!output) {
		RESTORE();
	} else {	
		x_fprintf(out, "%s%s", mp->name, buf);
	}	
	return 0;
}	


static int 
store_macro_arg(
	struct macro *m,
	struct macro_arg **cma0,
	struct token *toklist,
	int ends) {

	struct macro_arg	*cur_macro_arg = *cma0;

	if (cur_macro_arg != NULL) {
		/*
		 * Not all parameters in the list have
		 * been assigned
		 */
		cur_macro_arg->toklist = toklist;
#if 0
		/*
		 * 05/23/09: Allow empty macro arguments, which
		 * GNU cpp does as well. Whether this is allowed
		 * as per standard C is probably debatable and
		 * unclear
		 *
		 *   #define foo(x)
		 *   foo()
		 */
		if (toklist == NULL) {
			lexerror("Empty macro argument");
			return -1;
		}	
#endif
		if (cur_macro_arg->next != NULL && ends) {
			lexerror("Not enough arguments for "
				"function-like macro");	
			return -1;
		}
	} else {
		if (toklist != NULL) {
			lexerror("Too many arguments for "
				"function-like macro %s", m->name);
			return -1;
		}
	}	
	if (ends) {
		*cma0 = NULL;
	} else {
		*cma0 = (*cma0)->next;
	}	
	return 0;
}

int	collect_parens;

static int
collect_args(struct macro *mp, struct token **tok, int *err,
	struct token **result, struct token **result_tail) {

	struct token		*t = *tok;
	struct token		*toklist = NULL;
	struct token		*toklist_tail = NULL;
	struct macro_arg	*ma = mp->arglist;
	int			doing_trailing_last = 0;


	collect_parens = 1;
	*err = 0;

	/* Skip identifier and ( */
	(void) next_token(&t);
	(void) next_token(&t);

	for (;;) {
		/*
		 * Note that arguments to function-like macros are not
		 * processed before being passed (at least in GNU cpp
		 * and ucpp), such that;
		 *
		 * #define foo 1, 2
		 * #define bar(x, y) x * y
		 * bar(foo)
		 *
		 * ... doesn't work!
		 */
		if (mp->trailing_last == ma) {
			doing_trailing_last = 1;
		}	
		if (t->type == TOK_PAREN_CLOSE) {
			if (--collect_parens == 0) {
				store_macro_arg(mp, &ma, toklist, 1);
				break;
			}
			append_token_copy(&toklist, &toklist_tail, t);
		} else if (t->type == TOK_PAREN_OPEN) {
			++collect_parens;
			append_token_copy(&toklist, &toklist_tail, t);
		} else if (t->type == TOK_OPERATOR
			&& *(int *)t->data == TOK_OP_COMMA) {
			if (collect_parens == 1 && !doing_trailing_last) {
				store_macro_arg(mp, &ma, toklist, 0);
				toklist = toklist_tail = NULL;
			} else {	
				append_token_copy(&toklist, &toklist_tail, t);
			}	
		} else {
			if (t->type == TOK_WS) {
				/*
			 	 * Whitespace before and after arguments
			 	 * is ignored
				 */
				if (t->next != NULL && toklist != NULL) {
					if (t->next->type == TOK_OPERATOR
						&& *(int *)t->next->data
						== TOK_OP_COMMA) {
						; /* ignore */
					} else if (t->next->type ==
						TOK_PAREN_CLOSE) {
						; /* ignore */
					} else {
						append_token_copy(&toklist,
							&toklist_tail, t);
					}
				}	
			} else {				
				append_token_copy(&toklist, &toklist_tail, t);
			}	
		}

		if (next_token(&t) != 0) {
			/*
			 * Return and have main loop read remaining tokens.
			 * This is infinitely inferior to having a get_token()
			 * function :-(
			 */
			struct token	*newlist = NULL;
			struct token	*newlist_tail = NULL;

			for (t = *tok; t != NULL; t = t->next) {
				append_token_copy(&newlist, &newlist_tail, t);
			}
			if (newlist) {
				if (*result) {
					(*result_tail)->next = newlist;
					*result_tail = newlist_tail;
				} else {
					*result = newlist;
					*result_tail = newlist_tail;
				}	
			}	
			return -1;
		}	
	}
	*tok = t;
	return 0;
}


struct token *
skip_ws(struct token *t) {
	for (; t != NULL; t = t->next) {
		if (t->type == TOK_WS) {
			; /* ignore */
		} else {
			return t;
		}
	}
	return NULL;
}	

static struct token *
skip_ws_backwards(struct token *t) {
	for (; t != NULL; t = t->prev) {
		if (t->type == TOK_WS) {
			; /* ignore */
		} else {
			return t;
		}
	}
	return NULL;
}	

static struct token *
find_hashhash(struct token *t) {
	if ((t = skip_ws(t)) != NULL) {
		if (t->type == TOK_HASHHASH) {
			return t;
		}
	}
	return NULL;
}

static struct token * 
do_hashhash(
	struct token **ret, struct token **ret_tail,
	struct token *t, struct token *dt) {

	struct token	*t2;
	struct token	*successor;
	char		*p;

	if ((t2 = skip_ws(dt->next)) == NULL) {
		lexerror("Invalid `##' at end of macro "
			"expansion");
		return NULL;
	}	
	if (t2->maps_to_arg != NULL) {
		successor = skip_ws(t2->maps_to_arg->toklist);
	} else {
		successor = t2;
	}	
	assert(successor != NULL);
	p = n_xmalloc(strlen(t->ascii) +
		strlen(successor->ascii)+1);
	sprintf(p, "%s%s", t->ascii, successor->ascii);
	store_token(ret, ret_tail, p, TOK_IDENTIFIER, 0, NULL);

	/*
	 * Write out rest of b token list in ``a ## b''. Easier than
	 * setting flags and stuff in main loop  
	 */
	if (t2->maps_to_arg) {
		for (successor = successor->next;
			successor != NULL;
			successor = successor->next) {
			append_token_copy(ret, ret_tail, successor);
		}
	}
	return t2;
}

static int 
fully_expand_macro_list(struct input_file *in,
	struct token **toklist, struct token **tres,
	int dontoutput, struct token **tailp);

/*
 * Expands macro body to token list and returns it. mp->args must be set
 * to the arguments of the current instantiation!
 */
static struct token * 
expand_function_macro(struct macro *mp, struct token **tail) {
	struct token		*t;
	struct token		*t2;
	struct token		*ret = NULL;
	struct token		*ret_tail = NULL;
	char			*p;


	for (t = mp->toklist; t != NULL; t = t->next) {
		if (t->type == TOK_HASH) {

			if (t->next == NULL
				|| (t = skip_ws(t->next)) == NULL
				|| t->maps_to_arg == NULL) {
				lexerror("`#' not followed by macro parameter");
				return NULL;
			}

			/*
			 * Stringizing token list - be careful to
			 * escape all " characters!
			 */
#if 0
			t = t->next;
#endif

			p = NULL;
			store_char(NULL, 0);
			store_char(&p, '"');
			for (t2 = t->maps_to_arg->toklist;
				t2 != NULL;
				t2 = t2->next) {
				char	*p2 = t2->ascii;

				for (; *p2 != 0; ++p2) {
					if (*p2 == '"') {
						store_char(&p, '\\');
					}
					store_char(&p, *p2);
				}
			}
			store_char(&p, '"');
			store_char(&p, 0);
			store_token(&ret, &ret_tail, p,
				TOK_STRING_LITERAL, 0, p);
		} else if (t->maps_to_arg != NULL) {
			struct token *t_res;
			struct token *t2_res;
			struct token *tailp;

			/*
			 * Expanding a macro parameter invocation.
			 * It is quite important to expand any macros
			 * in an argument token list because we need
			 * to make e.g. this work:
			 *
			 * #define foo(x) #x
			 * #define bar(x) foo(x)
			 * #define baz 123
			 * bar(baz);
			 *
			 * foo() has to receive expanded tokens, i.e.
			 * it may not see the macro ``baz''. Otherwise
			 * #x would yield "baz"
			 */
			t2_res = t->maps_to_arg->toklist;

			/*
			 * XXX returns indicator whether expansion is
			 * "not done" - how to handle this?
			 */
			(void) fully_expand_macro_list(NULL, &t2_res,
				&t_res, 1, &tailp);



#if 0
			for (t2 = t->maps_to_arg->toklist;
				t2 != NULL;
				t2 = t2->next) {
				append_token_copy(&ret, &ret_tail, t2);
			}
#endif

			for (t2 = t2_res; t2 != NULL; t2 = t2->next) {
				append_token_copy(&ret, &ret_tail, t2);
			}	
#if 0
			if (t2_res != NULL) {
				if (ret == NULL) {
					ret = t2_res;
				} else {
					ret_tail->next = t2_res;
				}
				if (tailp != NULL) {
					ret_tail = tailp;
				}	
			}	
#endif
		} else {
			append_token_copy(&ret, &ret_tail, t);
		}
	}
	*tail = ret_tail;
	return ret;
}


/*
 * It is important that the same macro is not expanded recursively in
 * an expansion chain. Consider:
 *
 * #define x y
 * #define y x
 * int x;
 *
 * ... here x expands to y, y expands to x, and x remains x because
 * multiple expansions are not allowed. This is a little difficult
 * to get right. Consider the token list:
 *
 * foo,foo,foo,foo
 *
 * ... where foo is a macro. In expand_token_list(), the ``dontexpand''
 * flag in foo's ``struct macro'' must be set before a recursive call
 * to itself, and unset afterwards, such that the next occurances of
 * foo are expanded correctly.
 *
 * Invoked macros are recorded in the expanded_macros array. Before a
 * token list is a reprocessed, do_macro_subst() needs this to re-mark
 * all invoked macros as unexpandable, and when all is done,
 * expandable.
 */
#if 0
static struct macro	**expanded_macros;
static int		expanded_macros_alloc;
static int		expanded_macros_idx;
#endif

static void
mark_not_expandable(struct token *t, struct macro *m) {
	int	i;

#if 0
	m->dontexpand = 1;

#endif
	/*
	 * 05/24/09: Check whether the macro is already marked unexpandable
	 */
	for (i = 0; i < t->expanded_macros_idx; ++i) {
		if (t->expanded_macros[i] == m) {
			/* Already unexpandable */
			return;
		}
	}

	if (t->expanded_macros_idx >= t->expanded_macros_alloc) {
		if (t->expanded_macros_alloc == 0) {
			t->expanded_macros_alloc = 16;
		} else {
			t->expanded_macros_alloc *= 2;
		}
#if 0 
		expanded_macros = n_xmalloc(expanded_macros_alloc *
				sizeof *expanded_macros);
#endif
		t->expanded_macros = n_xrealloc(t->expanded_macros,
				t->expanded_macros_alloc *
				sizeof *t->expanded_macros);
	}

	t->expanded_macros[t->expanded_macros_idx++] = m;
}

static int
check_dontexpand(struct token *t, struct macro *m) {
	int	i;

	for (i = 0; i < t->expanded_macros_idx; ++i) {
		if (t->expanded_macros[i] == m) {
			return 1;
		}
	}
	return 0;
}	


#if 0
static void
mark_all_unexpandable(void) {
	int	i;

	for (i = 0; i < expanded_macros_idx; ++i) {
		expanded_macros[i]->dontexpand = 1;
	}
}	
#endif

static void
mark_all_expandable(struct token *t) {
	int	i;

	for (i = 0; i < t->expanded_macros_idx; ++i) {
		t->expanded_macros[i]->dontexpand = 0;
	}
	t->expanded_macros_idx = 0;
}	


/*
 * XXX if we are to keep the concept of ``clean pass'', then tokens should
 * be copied copy-on-write; ie. copy only if a replacement takes place!!!
 */
static struct token *
expand_macro_list(
	struct input_file *inf,	
	struct token *list,
	struct token **mtail,
	int *clean_pass,
	int *not_done,
	int dontoutput, /* for #if/#elif */
	struct token **tailp) {

	struct token	*t;
	struct token	*t2;
	struct token	*result = NULL;
	struct token	*ctail = NULL;
	struct token	*newtail;
	struct token	*expres;
	struct token	*dt;


	(void) clean_pass;

	for (t = list; t != NULL; t = t->next) {
		struct macro	*mp;

		if (t->type != TOK_WS
			&& (dt = find_hashhash(t->next)) != NULL) {
			t = do_hashhash(&result, &ctail, t, dt);
			if (clean_pass) {
				*clean_pass = 0;
			}
			continue;
		} else if (t->type != TOK_IDENTIFIER) {
			append_token_copy(&result, &ctail, t);
			continue;
		} else if (strcmp(t->data, "defined") == 0) {
			append_token_copy(&result, &ctail, t);

			if (!dontoutput) {
				continue;
			}

			t2 = skip_ws(t->next);
			if (t2 && t2->type == TOK_PAREN_OPEN) { 
				struct token	*t3 = skip_ws(t2->next);
				
				if (t3 && t3->type == TOK_IDENTIFIER) {
					struct token	*t4 = skip_ws(t3->next);

					if (t4 && t4->type == TOK_PAREN_CLOSE) {
						/*
						 * Valid defined(ident). Invalid
						 * ones are ignored because
						 * get_sub_expr() catches them
						 * anyway
						 */
						append_token_copy(&result,
							&ctail, t2);
						append_token_copy(&result,
							&ctail, t3);
						append_token_copy(&result,
							&ctail, t4);
						t = t4;
					}
				}
			} else if (t2 && t2->type == TOK_IDENTIFIER) {
				/*
				 * defined ident
				 */
				append_token_copy(&result, &ctail, t2);
				t = t2;
			}	
			continue;
		}

		mp = lookup_macro(t->ascii, /*t->slen, t->hashkey*/ 0, -1);

		if (mp != NULL
			&& !mp->dontexpand
			&& !check_dontexpand(t, mp)) {
			expres = NULL;
			if (mp->functionlike) {
				struct token	*t2;

				if (t->next == NULL) {
					/*
					 * Corner case - result of macro
					 * expansion may or may not introduce
					 * invocation of function-like macro;
					 *
					 * #define foo(x) x * x
					 * #define bar foo
					 * bar(123)
					 *
					 * We have to peak at the stream!
					 */
					append_token_copy(&result, &ctail, t);
					/* XXX check_funclike SUX */
					*not_done = 1;
#if 0
					if (check_funclike(inf, NULL, mp, 0)
						== 1) {
						t = store_token(&result, &ctail,
							0, TOK_PAREN_OPEN,
							t->line,
							NULL);
						collect_parens = 1;
						*not_done = 1;
						*tailp = ctail;
						if (mtail) {
							*mtail = ctail;
						}
						return result;
					}	
#endif
					break;
				}
				if ((t2 = skip_ws(t->next)) != NULL
					&& t2->type == TOK_PAREN_OPEN) {	
					/* Invocation! */
					int	err;

					t = t2->prev;
					if (clean_pass) {
						*clean_pass = 0;
					}

					if (collect_args(mp, &t, &err, &result,
						tailp) != 0) {
						if (!err) {
							/*
							 * Return what we got 
							 * so far
							 */
							if (mtail != NULL) {
								/* XXX */
								*mtail = *tailp;
							}	
							*not_done = 1;
							return result;
						}
						if (mtail != NULL) {
							/* XXX */
							*mtail = NULL;
						}	

						return NULL;
					}
					/*
					 * 05/23/09: The dontexpand setting was
					 * well-meaning but wrong.
					 *
					 * #define FOO(x) x
					 *
					 * FOO( FOO(123 ) )
					 *
					 *      ^^^^^^^ we are here
					 *
					 * Reading the macro arguments should be
					 * unaffected by the outer macro expansion
					 * because it is effectively an ``atomic''
					 * token list which should start from 0
					 * as far as expansions are concerned.
					 *
					 * What we were looking to avoid is
					 *
					 * #define foo() bar()
					 * #define bar() foo()
					 * bar()
					 *
					 * ... causing infinitely recursive
					 * expansion. However, that case is
					 * different and handled elsewhere
					 *
					 * XXXXXXXXXXXXXXXXXXXXXXXXX
					 * Don't we have to set ALL outer
					 * dontexpands to 0 too?!?!
					 */
#if 0
					mp->dontexpand = 1; 
#endif
					expres = expand_function_macro(mp,
						&newtail); 
#if 0
					mp->dontexpand = 0;
#endif
				} else {
					t = t->next;
					append_token_copy(&result, &ctail, t);
					t = t->prev;
				}	
			} else {
				if (mp->builtin) {
					/* __FILE__ or __LINE__ */
					expres = builtin_to_tok(mp, &newtail);
				} else {	
					if (clean_pass) {
						*clean_pass = 0;
					}
					mp->dontexpand = 1;
					expres = expand_macro_list(inf,
						mp->toklist, &newtail, NULL,
						not_done, dontoutput, tailp);
					mp->dontexpand = 0;
				}	
			}
			if (expres != NULL) {
				/*
				 * Expansion yielded tokens. Now we have to
				 * record which macro these tokens came from,
				 * such that the same macro is not expanded
				 * recursively
				 *
				 * 05/24/09: Now we also record the chain of
				 * previous expansions (if any) here!
				 *
				 * #define foo() bar()
				 * #define bar() foo()
				 *
				 * ... here the expansion in the recursive
				 * expand_macro_list() call does not continue
				 * to carry the information of previous
				 * expansions, i.e. we are passing tokens to
				 * expand_macro_list() that have one or more
				 * expanded macros recorded, but the function
				 * returns tokens which don't have this list.
				 *
				 * Thus we reset it below as well now
				 */
				for (t2 = expres; t2 != NULL; t2 = t2->next) {
					int	i;

					/*
					 * XXX hmm mark_not_expandable calls
					 * realloc, but the pointer to be
					 * realloced will be shared after
					 * append_token_copy(). this ok?!?
					 */
					mark_not_expandable(t2, mp);
					for (i = 0; i < t->expanded_macros_idx; ++i) {
						mark_not_expandable(t2, t->expanded_macros[i]);
					}
				}

				if (result == NULL) {
					result = expres;
				} else {
					ctail->next = expres;
				}
				ctail = newtail;
			}	
		} else {
			append_token_copy(&result, &ctail, t);
		}	
	}
	if (mtail != NULL) {
		*mtail = ctail;
	}	
	return result;
}

/*
 * XXX this interface kinda sucks big time..but the loop is used more than
 * once!
 * ``in'' - file to read more data from if needed (NULL for none)
 * ``toklist'' - list as input, last valid expansion as output (!not_done)
 * ``tres'' - as output the expanded result list
 * ``dontoutput''
 * ``in_tailp'' - input tail pointer corresponding to toklist
 * ``out_tailp'' - output tail pointer
 * XXX I'm not sure why expand_macro_list() separates between
 */
static int 
fully_expand_macro_list(struct input_file *inf,
	struct token **toklist, struct token **tres,
	int dontoutput, struct token **tailp) {

	int		clean_pass;
	int		not_done;
	struct token	*t;
	struct token	*t2 = *toklist;
	struct token	*new_tail = NULL;

	do {
		clean_pass = 1;
		not_done = 0;

		t = expand_macro_list(inf, t2, &new_tail, &clean_pass, &not_done,
				dontoutput, tailp);
		/*mark_all_unexpandable();*/ /* Don't expand again */
		if (!not_done /* && !clean_pass */ ) {
			free_token_list(t2);
		}	
	} while (!clean_pass && (t2 = t) && !not_done);	

	if (new_tail != NULL && !not_done) {
		/*
		 * 05/24/09: Assume it's ``not done'' if the last expanded
		 * token is an identifier. In that case, it may be the
		 * beginning of another macro expansion;
		 *
		 *    #define foo() 123
		 *    #define bar() foo
		 *
		 *    bar()()   <-- yields 123 eventually
		 */
		if (new_tail->type == TOK_IDENTIFIER) {
			not_done = 1;
		}
	}

	*toklist = t2;
	*tres = t;
	*tailp = new_tail;

	return not_done;
}	

struct token *
do_macro_subst(struct input_file *inf, FILE *out,
	struct token *toklist,
	struct token **tailp,
	int dontoutput) {

	struct token	*t;
	struct token	*t2;
	int		clean_pass;
	int		not_done;

	if (g_ignore_text && !dontoutput) {
		return NULL;
	}

	t2 = toklist;

	not_done = fully_expand_macro_list(inf, &t2, &t, dontoutput, tailp);

	/*mark_all_expandable();*/
	
	if (not_done) {
		/*
		 * Main loop has to read some more tokens for processing
		 */
		return t2? t2: toklist;
	}

	if (dontoutput) {
		if (t == NULL) {
			t = toklist;
		}
		return t;
	}

	for (t2 = t; t2 != NULL; t2 = t2->next) {
		/*
		 * Ignore initial and final whitespace
		 */
		if (t2->type == TOK_WS) {
			if (t2 == t || t2->next == NULL) {
				continue;
			}
		}	
		x_fprintf(out, "%s", t2->ascii);
#if 0
static struct macro *
check_funclike(FILE *in, FILE *out, struct macro *mp) {
#endif
	}	
	
	/* XXX remember we have to return a ``struct macro''
	 * if the last expanded token is possibly a function-like
	 * macro! but we do not know whether a ( will follow, so
	 * perhaps in the loop above we shouldn't output the last
	 * token if it is such an identifier, but instead return
	 * the ``struct tokne'', or else peek at the input
	 */
	return 0;
}	

