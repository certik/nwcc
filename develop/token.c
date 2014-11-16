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
 * Helper functions for parsing string and character constants,
 * trigraphs, operators and constants. Also contains global token
 * list and functions to add to this list. Used by lexical
 * analyzer.
 */
#include "token.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <ctype.h>

#ifndef PREPROCESSOR
#    include "backend.h"
#    include "decl.h"
#    include "cc1_main.h"
#    include "debug.h"
#else
#    include "archdefs.h"
#    include "cpp_main.h"
#    include "macros.h"
#    include "preprocess.h"
#endif

#include "error.h"
#include "defs.h"
#include "zalloc.h"
#include "numlimits.h"
#include "standards.h"
#include "misc.h"
#include "type.h"
#include "typemap.h"

#ifndef PREPROCESSOR
#    include "features.h"
#endif
#include "n_libc.h"

struct token	*toklist;
size_t		lex_chars_read;
char		*lex_line_ptr;
char		*lex_tok_ptr;
char		*lex_file_map;
char		*lex_file_map_end;


struct ty_string		*str_const = NULL;
static struct ty_string		*str_const_tail = NULL;

struct ty_float			*float_const = NULL;

struct ty_llong			*llong_const = NULL;

struct token *
alloc_token(void) {
	struct token	*ret = n_xmalloc(sizeof *ret);
	static struct token	nulltok;


#if 0
static unsigned long size = 0;
size += sizeof *ret;
static int foo;
++foo
	;

if (foo % 100 == 0) {
	printf("%lu\r", size);
	fflush(stdout);
}
#endif


#if USE_TOK_SEQNO 
	static int seq;
++seq;
#endif

	*ret = nulltok;
#if USE_TOK_SEQNO 
	if (/*seq == 2657*/ seq == -2775) abort();
	ret->seqno = seq;
#endif

	return ret;
}

struct token *
dup_token(struct token *tok) {
	struct token	*ret = alloc_token();
	*ret = *tok;
	return ret;
}

void
free_tokens(struct token *from, struct token *to, int type) {
	if (from == NULL) {
		return;
	}

	do {
		struct token	*next = from->next;
		
		if (type == FREE_DECL) {
			if (from->type == TOK_PAREN_OPEN
				|| from->type == TOK_PAREN_CLOSE
				|| from->type == TOK_ARRAY_OPEN
				|| from->type == TOK_ARRAY_CLOSE
				|| from->type == TOK_SEMICOLON
				|| IS_KEYWORD(from->type)) {
				free(from);
			} else if (from->type == TOK_OPERATOR
				&& *(int *)from->data == TOK_OP_ASSIGN) {
				break;
			}	
		} else {
			abort();
		}	
		from = next;
	} while (from != NULL && from != to);
}


static int
append_ty_string(
	struct ty_string **head, 
	struct ty_string **tail,
	struct ty_string **str) {
	struct ty_string	*tmp;

	for (tmp = str_const; tmp != NULL; tmp = tmp->next) {
		if (tmp->size == (*str)->size
			&& tmp->is_wide_char == (*str)->is_wide_char) {
			/*
			 * 07/20/08: This used strcmp() instead of
			 * memcmp(), thus not allowing for embedded
			 * \0 chars!
			 *
			 * 08/08/09: Don't mix wide char with ordinary
			 * string constants
			 */
			if (memcmp(tmp->str, (*str)->str, tmp->size) == 0) {
				*str = tmp;
				return -1;
			}
		}
	}
	if (*head == NULL) {
		*head = *tail = *str;
	} else {
		(*tail)->next = *str;
		*tail = (*tail)->next;
	}
#ifndef PREPROCESSOR 
	(*str)->count = ty_string_count();
#endif

	(*tail)->next = NULL;
	return 0;
}


static int	store_char_index		= 0;

/*
 * Routine used by several functions in this module - stores
 * character ch in *p, resizing it if necessary. Must be called
 * with a first argument of NULL before it can be used in order
 * to initialize static data. Exits if no memory is available.
 *
 * XXX Not reentrant
 * XXX Should use nwstr library instead, which may also speed up
 * detecing keywords in the source
 */
int 
store_char(char **p, int ch) {
	static int	chunksiz	= 32;
	static int	alloc		= 0;

	if (p == NULL) {
		 /* Initialize static data */
		 chunksiz = 8; /* was 8 */
		 alloc = 0;
		 store_char_index= 0;
		 return 0;
	}

	if (store_char_index >= alloc) {
		alloc += chunksiz;
		chunksiz *= 2;
		if ((*p = n_xrealloc(*p, alloc)) == NULL) {
			perror("realloc");
			exit(EXIT_FAILURE);
		}
	}	
	(*p)[(unsigned)store_char_index] = (unsigned char)ch;
	return ++store_char_index;
}

int
store_string(char **p, char *str) {
	int	rc = 0;
	
	for (; *str != 0; ++str) {
		rc = store_char(p, *str);
	}
	store_char(p, 0);
	return rc;
}	


/*
 * Function parses the escape sequence pointed to by s and returns
 * the result. The type argument indicates whether the function is
 * called on a string or character constant. -1 is returned on
 * failure (bad format.)
 *
 * XXX this is not aware of cross-compilation... So in the event that
 * we're going to support a system with 16- or 32-bit bytes (not
 * infinitely unlikely), it will have to be adapted as necessary.
 * This should also affect the preprocessor as far as character
 * constants are concerned
 */
static int 
get_escape_sequence(const char *s, int type, int *badseq) {
	int	rc = -1;

	*badseq = 0;
	if (isdigit((unsigned char)*s)) {
		/* Octal constant */
		if (s[0] == '0' && isdigit((unsigned char)s[1]) == 0) {
			/* Null constant '\0' */
			return 0;
		}
		if (s[0] == '0') ++s;
		if (sscanf(s, type == TOK_STRING_LITERAL ? "%3o" : "%o",
					(unsigned *)&rc) != 1) {
			lexerror("Invalid octal constant.");
			*badseq = 1;
			return -1;
		}
	} else if (tolower((unsigned char)*s) == 'x') {
		/* Is hexadecimal constant */
		if (sscanf(s+1, type == TOK_STRING_LITERAL ? "%2x" : "%x",
					(unsigned *)&rc) != 1) {
			lexerror("Invalid hexadecimal constant.");
			*badseq = 1;
			return -1;
		}
	} else {
		/* Must be other escape sequence */
		static struct {
			char	ch;
			char	seq;
		} esc[] = {
			{ 'n', '\n' },
			{ 'r', '\r' },
			{ 't', '\t' },
			{ 'v', '\v' },
			{ 'b', '\b' },
			{ 'a', '\a' },
			{ 'f', '\f' },
			{ '\\', '\\' },
			{ '\'', '\''},
			{ '\"', '\"'},
			{ '\?', '\?'},
			{ 0, 0		}
		};
		int	i;
		for (i = 0; esc[i].ch != 0; ++i) {
			if (s[0] == esc[i].ch) {
				rc = esc[i].seq;
				if (s[1] != 0 && type == TOK_CHAR_LITERAL) {
					if (s[1] != '\'') {
						lexwarning("Multiple characters "
							"in character constant"
							" - ignoring excess"
							"characters.");
					}
				}
				break;
			}
		}
		if (esc[i].ch == 0) {
			if (s[0] == '%') {
				lexwarning("`\\%%' is not a valid escape "
					"sequence in ISO C");
			} else {
				lexerror("Invalid escape sequence.");
			}
		}
	}

	if (type == TOK_CHAR_LITERAL
		&& (rc > UCHAR_MAX || rc < CHAR_MIN)) {
		lexwarning("Literal out of character range.");
	}
	rc = (char)rc;
	return rc;
}

/*
 * Reads trigraph from stream f, if present, parses it and returns the
 * result. This function must be called AFTER a ``?'' character has
 * been read from f. The function has three possible types of a return
 * value:
 * 1) -1 - This means a non-question-mark character was read. f remains
 *        the same as it was upon entry of the function
 * 2) 0 - This means a question mark character was read, but the
 *        next character did not complete a valid trigraph. f points to
 *        that non-question-mark character
 * 3) everything else - This is the character to which the trigraph maps
 *                      f points after the trigraph
 *
 * XXX we shouldn't disappoint the millions of digraphs users either lol
 */
int 
#ifndef PREPROCESSOR
get_trigraph(struct input_file *f) {
#else
get_trigraph(struct input_file *f) {
#endif
	static struct {
		char	sigch; /* significant character */
		char	trans; /* translation result of trigraph */
	} trig_map[] = {
		{ '=', '#' },
		{ ')', ']' },
		{ '!', '|' },
		{ '(', '[' },
		{ '\'', '^' },
		{ '>', '}' },
		{ '/', '\\' },
		{ '<', '{' },
		{ '-', '~' },
		{ 0, 0 }
	};
	int	ch;
	int	i;

	if ((ch = FGETC(f)) == EOF) {
		lexerror("Unexpected end of file");
		return 0;
	}
	if (ch != '?') {
		/* This has nothing to do with trigraphs at all... */
		UNGETC(ch, f);
		return -1;
	}

	/* If the next char matches, we have a trigraph */
	if ((ch = FGETC(f)) == EOF) {
		lexerror("Unexpected end of file");
		return 0;
	}
	for (i = 0; trig_map[i].sigch != 0; ++i) {
		if (trig_map[i].sigch == ch) {
			/* Is a trigraph! */
			lexwarning("Interpreting `??%c' as trigraph",
				trig_map[i].sigch);	
			return trig_map[i].trans;
		}
	}

	/* Not a trigraph */
	UNGETC(ch, f);
	return 0;
}

/*
 * Reads character literal from stream f, parses it and returns
 * the result. This function must be called AFTER the opening '
 * has already been read from f. -1 is returned on failure (bad
 * format / no closing ')
 */
int 
#ifndef PREPROCESSOR
get_char_literal(struct input_file *inf, int *err) {
#else
get_char_literal(struct input_file *inf, int *err, char **text) {
#endif
	int		ch;
	int		trig;
	int		last = 0;
	int		rc;
	char	*p = NULL;

	*err = 0;
	/* Initialize store_char()'s private data */
	store_char(NULL, 0);
	while ((ch = FGETC(inf)) != EOF) {
		/*
		 * Loop until closing ' encountered. It's important to check 
		 * that no \' escape sequence is considered to be a closing '.
		 *
		 * UPDATE: checking for last = \ did not suffice and hid a
		 * really obscure bug. If a literal reads '\\', then obviously
		 * last should not make the loop continue
		 */
		if (ch == '?') {
			/* Might be trigraph */
			if ((trig = get_trigraph(inf)) == -1) {
				/*
				 * This is an illegal multi-char literal only if
				 * the current byte at f is not a ' char!
				 */
				store_char(&p, ch);
				last = ch;
			} else if (trig == 0) {
				/* Illegal, but let code later on catch it */
				store_char(&p, ch);
				store_char(&p, '?');
				last = '?';
			} else {
				/* Legal trigraph */
				store_char(&p, trig);
				last = trig;
			}
			continue;
		} else if (ch != '\'') {
			store_char(&p, ch);
			last = ch;
			continue;
		} else if (last == '\\') {
			/*
			 * 06/30/07: Seems p[1] was being read uninitialized!
			 * Hence the iterations check
			 */
			if (store_char_index < 2
				|| !(p[0] == '\\' && p[1] == '\\')) {
				store_char(&p, ch);
				last = ch;
				continue;
			}
		}

		/* Literal ends here, parse and return it now */

		if (p == NULL) {
			lexerror("Empty character literal");
			*err = 1;
			return -1;
		}

		store_char(&p, 0);

#ifdef PREPROCESSOR
		*text = p;
#endif

		if (p[0] == '\\') {
			int	badseq;

			/* Is escape sequence */
			rc = get_escape_sequence(p + 1, TOK_CHAR_LITERAL, &badseq); 
#ifdef DEBUG
			printf("Read character literal %d\n", rc);
#endif
			return rc;
		} else {
			/* Ordinary character */
			if (p[1] != 0) {
				/*
				 * Multi-character character literals are
				 * uncommon but legal in C (and illegal in
				 * C++.) Since their values are
				 * implementation-defined, it is OK to
				 * truncate them
				 */
				lexwarning("Multiple characters in character "
					"constant - ignoring excess "
					"characters.");
			}
#ifdef DEBUG
			printf("Read character literal ``%c''\n", p[0]);
#endif
			return p[0];
		}
	}
	lexerror("Unexpected end of file - expected ' to close "
		"character constant.");
	*err = 1;
	return -1;
}


/*
 * Returns 1 if ``code'' is part of the numeric system specified by
 * ``type'' ('x' indicates hexadecimal, everything else octal) in
 * ascii, else 0
 */
static int
is_part_of_constant(int code, int type) {
	if (isdigit((unsigned char)code)) {
		if (type != 'x') {
			/* Must be octal */
			if (code > '7') {
				return 0;
			}
		}	
		return 1;
	}
	if (type == 'x') {
		/* Hexadecimal */
		code = toupper((unsigned char)code);
		if (code != 0 && strchr("ABCDEF", code) != NULL) {
			return 1;
		}
	}
	return 0;
}


/*
 * Reads string literal from stream f and returns a dynamically
 * allocated string as result. Must be called AFTER the opening
 * " has been read from f. A null pointer is returned on failure
 * (no closing ")
 */
struct ty_string *
#ifndef PREPROCESSOR
get_string_literal(struct input_file *f, int is_wide_char) {
#else
get_string_literal(struct input_file *f) {
#endif
	char	*p			= NULL;
	int	ch;
	int	trig;
#ifdef PREPROCESSOR
	int	is_wide_char = 0; /* dummy */
#endif


	/* Initialize store_char()'s private data */
	store_char(NULL, 0);
#ifdef PREPROCESSOR
	store_char(&p, '"');
#endif
	while ((ch = FGETC(f)) != EOF) {
		if (ch == '?') {
			/* Might be trigraph */
			if ((trig = get_trigraph(f)) == -1) {
				store_char(&p, ch);
				continue;
			} else if (trig == 0) {
				store_char(&p, ch);
				store_char(&p, '?');
				continue;
			} else {
				/* Is trigraph! Let code below handle it */
				ch = trig;
			}
		}

		if (ch == '"') {
			/* Literal ends here, nul-terminate and return it */
			int			size;
			struct ty_string	*ret;

#ifdef PREPROCESSOR
			store_char(&p, '"');
#endif
			size = store_char(&p, 0); 
#ifdef DEBUG
			printf("Read string literal `%s'\n", p);
#endif
			ret = alloc_ty_string();
			ret->size = size;
			ret->str = p;
			ret->is_wide_char = is_wide_char;

			return ret;
		}
#ifdef PREPROCESSOR
		store_char(&p, ch);
#endif
		if (ch == '\n' || ch == '\r') {
			lexerror("Newline in string literal - use '\\n' instead");
		} else if (ch == '\\') {
			/* Escape sequence */

#ifndef PREPROCESSOR
			char	buf[4];
			int	badseq;
#endif

			/*
			 * Unfortunately, some escape sequence parsing must be
			 * done here already, because it wouldn't otherwise be
			 * known how many characters can be read (octal /
			 * hexadecimal constants take two, newlines, formfeeds,
			 * etc only one)
			 */
			if ((ch = FGETC(f)) == EOF) {
				break;
			}
#ifdef PREPROCESSOR
			store_char(&p, ch);
#else
			buf[0] = (unsigned char)ch;
			if (tolower((unsigned char)buf[0]) != 'x'
				&& !isdigit(buf[0])) {	
				buf[1] = 0;
				ch = get_escape_sequence(buf,
					TOK_STRING_LITERAL, &badseq);
			} else {
				/*
				 * Read numeric components of escape sequence.
				 * We must be careful to exclude non-digit
				 * characters from processing
				 */
				if ((ch = FGETC(f)) == EOF) {
					break;
				}
				if (is_part_of_constant(ch, buf[0])) {
					buf[1] = (unsigned char)ch;
					if ((ch = FGETC(f)) == EOF) {
						break;
					}
					if (is_part_of_constant(ch, buf[0])) {
						buf[2] = (unsigned char)ch;
						buf[3] = 0;
					} else {
						UNGETC(ch, f);
						buf[2] = 0;
					}
				} else {
					UNGETC(ch, f);
					if (buf[0] == 'x') {
						lexwarning("Empty hexadecimal "
							" escape sequence");
						continue;
					} else {
						buf[1] = 0;
					}	
				}
				ch = get_escape_sequence(buf,
					TOK_STRING_LITERAL, &badseq);
			}
			if (ch == -1 && badseq) {
				store_char(&p, '\x1');
			} else {
				store_char(&p, ch);
			}
#endif
			continue;
		}
#ifndef PREPROCESSOR
		store_char(&p, ch);
#endif
	}

	lexerror("Unexpected end of file - expected \" to close string literal.");
	if (p) free(p);
	return NULL;
}

/*
 * Returns the numeric code corresponding to ascii operator ``op''
 * on success, -1 if the operator is unknown
 */
static int 
do_get_operator(char *op, char **ascii) {
	int	i;
	int	c = *op;

	if ((i = LOOKUP_OP(c)) == 0) {
		return -1;
	}

	for (; operators[i].name[0] == c; ++i) {
		if (strcmp(operators[i].name, op) == 0) {
			if (operators[i].is_ambig != 0) {
				*ascii = operators[i].name;
				return operators[i].is_ambig;
			} else {
				*ascii = operators[i].name;
				return operators[i].value;
			}
		}
	}
		
	return -1;
}

/*
 * Read operator, starting from ch, from stream f. The result is
 * merely an approximation to the real semantics of the operator
 * in cases where multiple interpretations are possible in
 * different contexts (e.g. ``&'' as address-of operator vs. ``&''
 * as bitwise and.) The reported value for one of those ambiguous
 * operators must be adjusted at the hierarchial source analysis.
 * In particular, ambiguities occur with these operators:
 *
 * +	- unary plus    |  addition
 * -    - unary minus   |  substraction
 * *    - multiply      |  dereference       (pointer declaration)
 * &    - bitwise and   |  address-of
 * ++   - pre-increment |  post-increment
 * --   - pre-decrement |  post-decrement
 * :    - cond. oper.                        (bitfield member | label)
 */
int	
#ifndef PREPROCESSOR
get_operator(int ch, struct input_file *f, char **ascii) {
#else
get_operator(int ch, struct input_file *f, char **ascii) {
#endif
	char	buf[8];
	int		i = 1;
	int		tmp;
	int		latest_valid = -1;
	buf[0] = ch;
	buf[1] = 0;
	latest_valid = do_get_operator(buf, ascii);

	/*
	 * The strategy is to read new characters until the resulting
	 * string is NOT a valid operator anymore (or an identifier /
	 * whitespace is encountered.) The effect is that the previous
	 * operator (latest_valid) must have been valid, because we
	 * otherwise would have never reached this point. This is very
	 * handy for operators that, e.g., turn from ``<'' to ``<<''
	 * to ``<<=''.
	 */
	while ((ch = FGETC(f)) != EOF) {
		if (isalnum((unsigned char)ch)
			|| (ch != 0 && strchr("_$ \t\n", ch) != NULL)) {
			/* Identifier or whitespace reached */
			UNGETC(ch, f);
#ifdef DEBUG
			buf[i] = 0;
			printf("Read operator %s\n", buf);
#endif
			return latest_valid;
		}

		buf[i] = ch;
		buf[i + 1] = 0;
		if ((tmp = do_get_operator(buf, ascii)) == -1) {
			/* Not valid anymore - return last index */
			UNGETC(ch, f);
#ifdef DEBUG
			buf[i] = 0;
			printf("Read operator %s\n", buf);
#endif
			return latest_valid;
		}
		latest_valid = tmp;
		++i;
	}

	lexerror("Unexpected end of file.");
	return -1;
}


/*
 * Debugging function to print the value pointed to by ``ptr'',
 * interpreting it based on the type code ``type''. If the
 * ``verbose'' argument is nonzero, it will also print exactly
 * what type is being dealt with
 */
void 
rv_setrc_print(void *ptr, int type, int verbose) {
	(void)ptr; (void)type; (void)verbose;
#ifdef DEBUG
	if (type == TY_INT) { 
		printf("%s %d\n",
			verbose?"Read integer":"", *(int *)ptr);
	} else if (type == TY_UINT) {
		printf("%s %u\n",
			verbose?"Read unsigned integer":"", *(unsigned *)ptr);
	} else if (type == TY_LONG) {
		printf("%s %ld\n",
			verbose?"Read long":"", *(long *)ptr);
	} else if (type == TY_ULONG) {
		printf("%s %lu\n",
			verbose?"Read unsigned long":"", *(unsigned long *)ptr);
	} else if (type == TY_FLOAT) {
		printf("%s %f\n",
			verbose?"Read float":"", *(float *)ptr);
	} else if (type == TY_DOUBLE) {
		printf("%s %f\n",
			verbose?"Read double":"", *(double *)ptr);
	} else if (type == TY_LDOUBLE) {
		printf("%s %Lf\n",
			verbose?"Read long double":"", *(long double *)ptr);
	}
#elif USE_UCPP
	/* XXX not cross-compilation clean */
	if (type == TY_FLOAT) {
		struct ty_float	*tf = ptr;
		printf("%f", *(float *)tf->num->value);
	} else if (type == TY_DOUBLE) {
		struct ty_float	*tf = ptr;
		printf("%f", *(double *)tf->num->value);
	} else if (type == TY_LDOUBLE) {
		struct ty_float	*tf = ptr;
		printf("%Lf", *(long double *)tf->num->value);
	} else {
		cross_print_value_by_type(stdout, ptr, type, 'd');
	}
#endif

}


/*
 * XXX this is a quick kludge for 64bit PowerPC long/long long constants.
 * Whenever we encounter a long constant that is not part of a constant
 * expression, we create the constant in the data segment and use a
 * displacement with the TOC
 */
#ifndef PREPROCESSOR

void
put_ppc_llong(struct num *n) {
	struct ty_llong		*tll;
	static unsigned long	llcount;

	tll = alloc_ty_llong();
	tll->count = llcount++;
	tll->num = n;
#if XLATE_IMMEDIATELY
	if (emit->llong_constants != NULL) {
		tll->next = NULL;
		emit->llong_constants(tll);
	}	
#endif
	tll->next = llong_const;
	llong_const = tll;
}

#endif /* #ifndef PREPROCESSOR */

static int
hexdigit_to_val(int ch) {
	int	digit;

	if (isdigit(ch)) {
		digit = ch - '0';
	} else {
		static const char	*hexdigits = "abcdef";

		/* Must be a - f */
		digit = 10 + (strchr(hexdigits, tolower(ch))
				- hexdigits);
	}
	return digit;
}

static int
parse_hexfloat_const(struct num *ret, char *textval, char *bin_exp) {
	char		*p;
	char		*after;
	unsigned	before_val = 0;
	int		factor = 1;
	int		expval;
	int		negative_exp = 0;
	int		digit;
	int		divisor;
	void		*res;
	long double	ld;

	textval += 2; /* skip 0x part */
	if ((p = strchr(textval, '.')) == NULL) {
		return -1;
	}		

	*p = 0;
	after = p+1;
	for (--p;; --p) {
		if (p == textval - 1) {
			break;
		}
		digit = hexdigit_to_val(*p);

		before_val += digit * factor;
		factor <<= 4;
	}

	ld = before_val;
	divisor = 16;
	for (p = after; *p != 0; ++p) {
		digit = hexdigit_to_val(*p);
		ld += (double)digit / divisor;
		divisor <<= 4;
	}

	++bin_exp; /* skip p */
	if (*bin_exp == '-') {
		negative_exp = 1;
		++bin_exp;
	} else if (*bin_exp == '+') {
		++bin_exp;
	}
	expval = atoi(bin_exp);
	if (expval) {
		if (negative_exp) {
			while (expval--) {
				ld /= 2;
			}
		} else {
			while (expval--) {
				ld *= 2;
			}
		}
	}

	/*
	 * Now we have the result as a HOST long double. This requires
	 * us to convert it to the requested target type (by default
	 * double, with f postfix float, and with L postfix long double).
	 * This may require us to use double as source format because
	 * there is no mapping between host long double and target type.
	 */
	res = zalloc_buf(Z_CEXPR_BUF);  /*n_xmalloc(16);*/ /* XXX */
	/*if (ret->type == TY_LDOUBLE) {
		unimpl();
	} else*/ {
		double	d = (double)ld;

		/* XXX again this ignores endianness, this stuff is
		 * completely hopeless :-( */  
		memcpy(res, &d, sizeof d);
		cross_conv_host_to_target(res, ret->type,
			TY_DOUBLE);	
	}
	ret->value = res;

	return 0;
}	



static struct num *
complete_num_literal(char *p, char *bin_exp, int digits_read, int octal_flag, int hexa_flag, int fp_flag, int float_flag, int hex_float, int bin_exp_idx,  int long_flag, int unsigned_flag) {
	int		real_type;
	struct num	*rc;

	if (digits_read == 0 && !octal_flag && !fp_flag) {
		lexerror("Constant with no digits");
	} else if (fp_flag == 0) {
		/* 
		 * Perform range check, adjust type of
		 * constant as needed
		 */
		real_type = range_check(p, hexa_flag,
			octal_flag, unsigned_flag, long_flag,
			digits_read);
		if (real_type == -1) {
			free(p);
			return NULL;
		} else if (real_type != 0)  {
			/* 0 means unchanged */
			if (IS_LLONG(real_type)
				&& long_flag != 2) {
				/*
				 * The number wasn't requested
				 * to be of type ``long long'',
				 * but has to be! This may not
				 * be intended
				 */
				lexwarning("Number `%s' is too "
					"large for `unsigned "
					"long'", p);
			}	
			if (real_type == TY_ULONG
				|| real_type == TY_ULLONG
				|| real_type == TY_UINT) {
				unsigned_flag = 1;
			}
			if (real_type == TY_LONG
				|| real_type == TY_ULONG) {
				long_flag = 1;
			} else if (real_type == TY_LLONG
				|| real_type == TY_ULLONG) {
				long_flag = 2;
			}
		} else {
			/*
			 * The user-specified type is
			 * sufficient
			 */
			if (unsigned_flag) {
				if (long_flag == 1) {
					real_type = TY_ULONG;
				} else if (long_flag == 2) {
					real_type = TY_ULLONG;
				} else {
					real_type = TY_UINT;
				}
			} else {
				/* signed */
				if (long_flag == 1) {
					real_type = TY_LONG;
				} else if (long_flag == 2) {
					real_type = TY_LLONG;
				} else {
					real_type = TY_INT;
				}
			}
		}	
	} else {
		/* Floating point */
		if (float_flag) {
			real_type = TY_FLOAT;
		} else if (long_flag) {
			real_type = TY_LDOUBLE;
		} else {
			real_type = TY_DOUBLE;
		}	
	}	

	if (hex_float) {
		if (bin_exp_idx == 0) {
			lexerror("Hexadecimal floating point "
				"constant misses exponent");
			return NULL;
		}
					
		rc = n_xmalloc(sizeof *rc);
#ifndef PREPROCESSOR
		rc->value = n_xmalloc(backend->get_sizeof_type(
			make_basic_type(real_type), NULL));
#else
		rc->value = n_xmalloc(cross_get_sizeof_type(
			make_basic_type(real_type)));
#endif
		bin_exp[bin_exp_idx] = 0;
		rc->type = real_type;
#ifndef PREPROCESSOR
		if (parse_hexfloat_const(rc, p, bin_exp) == 0) {
			put_float_const_list(rc);
		}
#endif
	} else {	
		rc = cross_scan_value(p, real_type,
			hexa_flag, octal_flag, fp_flag);
	}
#ifdef DEBUG
	printf("(%d digits)\n", digits_read);
#endif
	if (fp_flag /*&& backend->need_floatconst*/) {
#if 0
		struct ty_float		*fc;
		static unsigned long	count;

		fc = n_xmalloc(sizeof *fc);
		fc->count = count++;
		fc->num = rc;
		fc->next = float_const;
		float_const = fc;
#endif
#ifndef PREPROCESSOR
	} else if (long_flag
		&& backend->abi == ABI_POWER64) {
		/* 64bit native long long/long! */
		put_ppc_llong(rc);
#endif
	}

#ifdef PREPROCESSOR
	rc->ascii = p;
#endif
	if (IS_LLONG(real_type)) {
		static int warned;
		if (!warned) {
			if (stdflag == ISTD_C89) {
				lexwarning("ISO C90 has "
				"no `long long' constants "
				"(suppressing further "
				"warnings of this kind)");
				warned = 1;
			}
		}
	}

	return rc;
}



/*
 * Reads a numeric literal from stream f and returns a pointer to
 * a dynamically allocated ``struct num'' containing its value and
 * type. The first character that has already been read from the
 * literal (and which actually indicated that we are dealing with
 * one) is passed as ``firstch''. On failure (bad format or memory
 * allocation problems) a null pointer is returned. 
 * This stuff is believed to be able to handle all types of numeric
 * constants that exist in C
 */ 
struct num *
#ifndef PREPROCESSOR
get_num_literal(int firstch, struct input_file *f) {
#else
get_num_literal(int firstch, struct input_file *f) {
#endif
	int			ch;
	int			real_type		= 0;
	int			octal_flag		= 0;
	int			hexa_flag		= 0;
	int			long_flag		= 0;
	int			unsigned_flag		= 0;
	int			float_flag		= 0;
	int			fp_flag			= 0;
	int			digits_read		= 0;
	int			last_dig		= 0;
	int			hex_float		= 0;
	char			*p			= NULL;
	struct num		*rc;
	char			bin_exp[128];
	int			bin_exp_idx = 0;

	/* Initialize store_char() */
	store_char(NULL, 0);

	store_char(&p, firstch);
	if (firstch == '0') {
		if ((ch = FGETC(f)) != EOF) {
			if (tolower((unsigned char)ch) == 'x') {
				/* hexadecimal (0x1) */
				hexa_flag = 1;
				store_char(&p, ch);
			} else if (ch == '.') {
				/* floating point (0.1) */
				fp_flag = 1;
				store_char(&p, ch);
			} else if (isdigit((unsigned char)ch)) {
				/* octal (01) */
				octal_flag = 1;
				store_char(&p, ch);
				if (ch != '0') { /* 0 is insignificant */
					++digits_read;
				} else {
					last_dig = ch;
				}
			} else {
				/* unknown */
				UNGETC(ch, f);
				++digits_read;
			}
		}
	} else if (firstch == '.') {
		/* Number like .5 (equivalent to 0.5) */
		fp_flag = 1;
	} else {
		/* Is significant digit */
		++digits_read;
		last_dig = firstch;
	}


	while ((ch = FGETC(f)) != EOF) {
		switch (ch) {
		case 'l':
		case 'L':
			++long_flag;
#ifdef PREPROCESSOR
			store_char(&p, ch);
#endif
			if (long_flag > 2) {
				lexerror("Unknown constant (was expecting "
					"long long)");
				return NULL;
			}
			break;
		case 'u':
		case 'U':
#ifdef PREPROCESSOR
			store_char(&p, ch);
#endif
			++unsigned_flag;
			if (unsigned_flag > 1) {
				lexwarning("Multiple unsigned designators "
					"in constant");
			}
			break;
		case 'f':
		case 'F':
			if (hexa_flag && (!hex_float || bin_exp_idx == 0)) {
				store_char(&p, 'f');
				++digits_read;
			} else {
				float_flag = ++fp_flag;
#ifdef PREPROCESSOR
				store_char(&p, ch);
#endif
				if (float_flag > 2) {
					lexwarning(
						"Multiple floating point "
						"designators in constant"); 
				}
			}
			break;
		case '.':
			fp_flag = 1;
			if (hexa_flag){
				hex_float = 1;
			}	
			store_char(&p, '.');
			break;
		default:
			if (isdigit((unsigned char)ch)) {
				/*
				 * Verify the numeric part has not yet been
				 * termianted
				 */
				if (long_flag || unsigned_flag || float_flag) {
					lexerror("Unexpected digit.");
					if (p) free(p);
					return NULL;
				}
				if (hex_float && bin_exp_idx > 0) {
					bin_exp[bin_exp_idx++] = ch;
#ifdef PREPROCESSOR
					store_char(&p, ch);
#endif
				} else {
					store_char(&p, ch);
					++digits_read;
					if (last_dig == 0 && ch != '0') {
						last_dig = ch;
					}
				}
				continue;
			}

			/* Not digit - Does the constant end here? */
			if (tolower((unsigned char)ch) == 'e') {
				if (hexa_flag == 0) {
					/*
					 * Must be scientific notation
					 */
					if (fp_flag == 0) {
#if 0
						error("Using scientific "
							"notation on "
							"integral value.");
						if (p) free(p);
						return NULL;
#endif
						fp_flag = 1;
					}
					store_char(&p, ch);
					if ((ch = FGETC(f)) == '+'
						|| ch == '-') {
						store_char(&p, ch);
					} else {
						UNGETC(ch, f);
					}
					continue;
				} else {
					++digits_read;
				}
				store_char(&p, ch);
				continue;
			}

			/* Hexadecimal digit? */
			if (hexa_flag == 1) {
				int	t;

				t = tolower((unsigned char)ch);
				if (strchr("abcd", t) != NULL
					&& bin_exp_idx == 0) {
					/* e & f are covered by other cases */
					store_char(&p, t);
					++digits_read;
					continue;
					/*break; ??? */
				} else if (hex_float) {
					if (t == 'p') {
						if (bin_exp_idx > 0) {
							lexerror("Constant already "
								"has a binary "
								"exponent");
							if (p) free(p);
							return NULL;
						} else {
							bin_exp[bin_exp_idx++]
								= t;
						}
#ifdef PREPROCESSOR
						store_char(&p, ch);
#endif
						continue;
					} else if (t == '-' || t == '+') {
						if (t == '-') {
							bin_exp[bin_exp_idx++]
								= '-';
						}
#ifdef PREPROCESSOR
						store_char(&p, ch);
#endif
						continue;
					}
				}
			}

			/* Ok, done with it */
			store_char(&p, 0);
			UNGETC(ch, f);


			return complete_num_literal(p, bin_exp, digits_read, octal_flag, hexa_flag, fp_flag, float_flag, hex_float, bin_exp_idx, long_flag, unsigned_flag);

#if 0
			if (digits_read == 0 && !octal_flag && !fp_flag) {
				lexerror("Constant with no digits");
			} else if (fp_flag == 0) {
				/* 
				 * Perform range check, adjust type of
				 * constant as needed
				 */
				real_type = range_check(p, hexa_flag,
					octal_flag, unsigned_flag, long_flag,
					digits_read);
				if (real_type == -1) {
					free(p);
					return NULL;
				} else if (real_type != 0)  {
					/* 0 means unchanged */
					if (IS_LLONG(real_type)
						&& long_flag != 2) {
						/*
						 * The number wasn't requested
						 * to be of type ``long long'',
						 * but has to be! This may not
						 * be intended
						 */
						lexwarning("Number `%s' is too "
							"large for `unsigned "
							"long'", p);
					}	
					if (real_type == TY_ULONG
						|| real_type == TY_ULLONG
						|| real_type == TY_UINT) {
						unsigned_flag = 1;
					}
					if (real_type == TY_LONG
						|| real_type == TY_ULONG) {
						long_flag = 1;
					} else if (real_type == TY_LLONG
						|| real_type == TY_ULLONG) {
						long_flag = 2;
					}
				} else {
					/*
					 * The user-specified type is
					 * sufficient
					 */
					if (unsigned_flag) {
						if (long_flag == 1) {
							real_type = TY_ULONG;
						} else if (long_flag == 2) {
							real_type = TY_ULLONG;
						} else {
							real_type = TY_UINT;
						}
					} else {
						/* signed */
						if (long_flag == 1) {
							real_type = TY_LONG;
						} else if (long_flag == 2) {
							real_type = TY_LLONG;
						} else {
							real_type = TY_INT;
						}
					}
				}	
			} else {
				/* Floating point */
				if (float_flag) {
					real_type = TY_FLOAT;
				} else if (long_flag) {
					real_type = TY_LDOUBLE;
				} else {
					real_type = TY_DOUBLE;
				}	
			}	

			if (hex_float) {
				if (bin_exp_idx == 0) {
					lexerror("Hexadecimal floating point "
						"constant misses exponent");
					return NULL;
				}
					
				rc = n_xmalloc(sizeof *rc);
#ifndef PREPROCESSOR
				rc->value = n_xmalloc(backend->get_sizeof_type(
					make_basic_type(real_type), NULL));
#else
				rc->value = n_xmalloc(cross_get_sizeof_type(
					make_basic_type(real_type)));
#endif
				bin_exp[bin_exp_idx] = 0;
				rc->type = real_type;
#ifndef PREPROCESSOR
				if (parse_hexfloat_const(rc, p, bin_exp) == 0) {
					put_float_const_list(rc);
				}
#endif
			} else {	
				rc = cross_scan_value(p, real_type,
					hexa_flag, octal_flag, fp_flag);
			}
#ifdef DEBUG
			printf("(%d digits)\n", digits_read);
#endif
			if (fp_flag /*&& backend->need_floatconst*/) {
#if 0
				struct ty_float		*fc;
				static unsigned long	count;

				fc = n_xmalloc(sizeof *fc);
				fc->count = count++;
				fc->num = rc;
				fc->next = float_const;
				float_const = fc;
#endif
#ifndef PREPROCESSOR
			} else if (long_flag
				&& backend->abi == ABI_POWER64) {
				/* 64bit native long long/long! */
				put_ppc_llong(rc);
#endif
			}

#ifdef PREPROCESSOR
			rc->ascii = p;
#endif
			if (IS_LLONG(real_type)) {
				static int warned;
				if (!warned) {
					if (stdflag == ISTD_C89) {
						lexwarning("ISO C90 has "
						"no `long long' constants "
						"(suppressing further "
						"warnings of this kind)");
						warned = 1;
					}
				}
			}

			return rc;
#endif
		}
	}

	store_char(&p, 0);
	return complete_num_literal(p, bin_exp, digits_read, octal_flag, hexa_flag, fp_flag, float_flag, hex_float, bin_exp_idx, long_flag, unsigned_flag);
}

/*
 * Reads an identifier from stream f and returns a pointer to a 
 * dynamically allocated string containing the result. It will
 * append all characters until a character is encountered that
 * is neither alpha-numeric, nor ``_'', nor ``$''. On failure
 * (end of file), a null pointer is returned
 */
char *
#ifndef PREPROCESSOR
get_identifier(int ch, struct input_file *f) {
#else
get_identifier(int ch, struct input_file *f, int *slen, int *hash_key) {
#endif
	char	*p = NULL;
#ifdef PREPROCESSOR
	int	key = 0;
	int	len = 1;
#endif

	/* Initialize static store_char() data */
	store_char(NULL, 0);

#ifdef PREPROCESSOR
	key = ch;
#endif

	store_char(&p, ch);
	while ((ch = FGETC(f)) != EOF) {
		if (isalnum((unsigned char)ch) || ch == '_' || ch == '$') {
			store_char(&p, ch);
#ifdef PREPROCESSOR
			key = (33 * key + ch) & N_HASHLIST_MOD;
			++len;
#endif
			if (ch == '$') {
				lexerror("`$' characters are not allowed in "
					"identifiers");
			}
		} else {
			/*
			 * Operator, white space or other token encountered -
			 * return
			 */
			store_char(&p, 0);
			UNGETC(ch, f);
#ifdef DEBUG
			printf("Read identifier ``%s''\n", p);
#endif
#ifdef PREPROCESSOR
			*hash_key = key;
			*slen = len;
#endif
			return p;
		}
	}


	store_char(&p, 0);
#ifdef DEBUG
	printf("Read identifier ``%s''\n", p);
#endif
#ifdef PREPROCESSOR
	*hash_key = key;
	*slen = len;
#endif
	return p;
}


static char 	*curfile;
/*static int	curfileid;*/

/*
 * Set path of file currently processed. This will be used by
 * store_token(), which saves in each token which file it is
 * contained by
 * XXX This is sorta bogus??
 */
void
token_setfile(char *file) {
	curfile = file;
}

#if 0
void
token_setfileid(int id) {
	curfileid = id;
}
#endif


static char *
tok_to_ascii(int type, void *data) {
	char	*ret = "???";

	if (type == TOK_IDENTIFIER) {
		if (data != NULL) ret = data;
		else ret = "identifier";
	} else if (type == TOK_STRING_LITERAL) {
		if (data != NULL) {
			struct ty_string	*str;
			str = data;
			ret = str->str;
		} else {
			ret = "string constant";
		}
	} else if (type == TOK_OPERATOR) {
		if (data != NULL) {
			ret = lookup_operator(*(int *)data);
			if (ret == NULL) {
				(void) fprintf(stderr,
					"FATAL - unknown operator - %d\n",
					*(int *)data);
				abort();
			}
		} else {
			ret = "operator";
		}
	} else if (type == TOK_PAREN_OPEN) {
		ret = "(";
	} else if (type == TOK_PAREN_CLOSE) {
		ret = ")";
	} else if (type == TOK_COMP_OPEN) {
		ret = "{";
	} else if (type == TOK_COMP_CLOSE) {
		ret = "}";
	} else if (type == TOK_ARRAY_OPEN) {
		ret = "[";
	} else if (type == TOK_ARRAY_CLOSE) {
		ret = "]";
	} else if (type == TOK_SEMICOLON) {
		ret = ";";
	} else if (type == TOK_ELLIPSIS) {
		ret=  "...";
#ifdef PREPROCESSOR
	} else if (type == TOK_WS) {
		ret = data;
#endif
	} else if (IS_CONSTANT(type)) {
#if 0 /* for debugging */
		if (IS_INT(type)) {
			char	buf[128];
			sprintf(buf, "(%d)", *(int *)data);
			ret = n_xstrdup(buf);
		} else 
#endif
		{
			ret = "(constant)";
		}
	} else if (IS_OPERATOR(type)) {
		/* This will only be used by expect_token() */
	} else {
#ifdef DEBUG
		char	lame[256];
		sprintf(lame, "<unknown>(code = %d)", type);
		ret = strdup(lame);
#else
		ret = "???";
#endif
	}
	return ret;
}


static void
check_ident(struct token *t, const char *ident) {
	(void) t; (void) ident;
#if 0
	char	*p = NULL;

	if (cur_inc != NULL
		&& cur_inc_is_std) {
		/* 
		 * Identifier occurs in standard
		 * include - part of implementation!
		 */
		return;
	}
	if (strncmp(ident, "str", 3) == 0) {
		p = "begins with `str'";
	} else if (strncmp(ident, "mem", 3) == 0) {
		p = "begins with `mem'";
	} else if (strncmp(ident, "is", 2) == 0
		&& isalpha(ident[3])) {
		p = "begins with `is' followed by letter";
	} else if (ident[0] == '_') {
		if (isupper((unsigned char)ident[1])
			|| ident[1] == '_') {
			p = "begins with underscore followed "
				"by uppercase letter or underscore";
		} else {				
			if (curscope == &global_scope) {
				p = "begins with underscore at "
				"global/file scope or in the tag namespace";
			}
		}	
	}
	if (p != NULL) {
		warningfl(t, "Identifier `%s' invades "
			"implementation namespace (%s)", ident, p);
	}	
#endif
}



/*
 * Appends the token specified by the data-type-linenum triple to
 * the token list pointed to by ``dest''. Exits with an error
 * message if no memory can be allocated. ``data'' MUST point to
 * dynamically allocated memory if you intend use destroy_toklist()
 * token_setfile() must be called to validate the file state data
 * before store_token() can be used
 * Adjacent string literals are concatenated
 */
struct token *
store_token(struct token **dest,
#ifdef PREPROCESSOR
	struct token **dest_tail,
#endif
	void *data, int type, int linenum, char *ascii) {
	struct token		*t;
	struct token		*ret = NULL;
	static struct token	*cur;

#if 0
printf("                                  line %d ... \r ", linenum);	
#endif

	if (*dest == NULL) {
#if 0
		*dest = n_xmalloc(sizeof **dest);
#endif
		*dest = alloc_token();
#ifdef PREPROCESSOR
		*dest_tail = *dest;
#endif
		t = *dest;
		t->prev = NULL;
		t->next = NULL;
		cur = t;

		ret = t;

		if (type == TOK_STRING_LITERAL) {
			struct ty_string	*newstr = data;
			struct type		*ty;

			ty = make_array_type(newstr->size, newstr->is_wide_char);
			newstr->ty = ty;
		}
	} else {
#ifdef PREPROCESSOR
		(*dest_tail)->next = alloc_token();
		(*dest_tail)->next->prev = *dest_tail;
		t = (*dest_tail)->next;
		*dest_tail = t;
		ret = t;
#else
		t = cur;
		ret = t;
		if (type == TOK_STRING_LITERAL) {
			/*
			 * Let's check whether we have to concatenate adjacent
			 * string literals
			 */
			struct type		*ty;
			struct ty_string	*newstr = data;

#ifndef PREPROCESSOR
			if (t && t->type == TOK_STRING_LITERAL) {
				/* Yes! */
				/*
				 * XXX this will make "foo" "bar"
				 * appear as "foobar" in error message
				 * output as soon as we display the
				 * line containing the error. This may
				 * be worth investigating
				 */
				struct ty_string	*oldstr = t->data;
				size_t	newsiz =
					oldstr->size + newstr->size - 1;


				oldstr->str = n_xrealloc(oldstr->str, newsiz);
				memcpy(oldstr->str + oldstr->size - 1,
						newstr->str,
						newstr->size);
				oldstr->size = newsiz;

				t->ascii = oldstr->str;
				oldstr->ty->tlist->arrarg_const = oldstr->size;
#if REMOVE_ARRARG
				oldstr->ty->tlist->have_array_size = 1;
#endif

				/* Okay, we are done here already */
				return ret;
			}
#endif /* #ifndef PREPROCESSOR */

			ty = make_array_type(newstr->size, newstr->is_wide_char);
			newstr->ty = ty;
		} else if (cur && cur->type == TOK_STRING_LITERAL) {
			/*
			 * Only now can we store the string constant
			 * because there are no adjacent constants left
			 * to merge ...
			 */
			struct ty_string	*ts = cur->data;
			if (append_ty_string(&str_const, &str_const_tail, &ts)
				== 0) {
				/* Not duplicate string - is new */
#if XLATE_IMMEDIATELY
				emit->strings(ts);
#endif
			}
			cur->data = ts;
		}

#if 0
		t->next = n_xmalloc(sizeof *t->next);
#endif
		t->next = alloc_token();
		t->next->prev = t;
		t = t->next;
		t->next = NULL;
		cur = t;

		ret = t;
#endif /* #ifndef PREPROCESSOR */
	}

	/* Save token in ascii */
	if (ascii != NULL) {
		/* Caller supplies text */
		t->ascii = ascii;
	} else {
		/* We have to make stuff up ourselves */
		t->ascii = tok_to_ascii(type, data);
	}

	t->file = curfile; 
/*	t->fileid = curfileid;*/

#ifndef PREPROCESSOR
	if (data == NULL) {
		/* We're done - terminate list */
		t->type = 0;
		t->data = NULL;
		t->line = linenum;
		return ret;
	}
#endif

	t->type = type;
	t->line = linenum;

	if (float_const != NULL &&
		(type == TY_FLOAT
		|| type == TY_DOUBLE
		|| type == TY_LDOUBLE)) {
		/* XXX ugly way to pass the data :( */
		t->data = float_const;
	} else if (llong_const != NULL
		&& (IS_LLONG(type) || IS_LONG(type))) {
		/*t->data = llong_const;*/
		t->data = data;
		t->data2 = llong_const;
	} else {	
		t->data = data;
	}	

	t->line_ptr = lex_line_ptr;
	t->tok_ptr = lex_tok_ptr;

#ifndef PREPROCESSOR
	if (type == TOK_IDENTIFIER) {
		/* Let's try and see whether this identifier is a keyword */
		struct keyword	*kw;
		
#if 0
		if ((i = lookup_key(data)) != -1) {
#endif
		if ((kw = lookup_key(data)) != NULL) {	
#if 0
			t->type = keywords[i].value;
			t->data = t->ascii = keywords[i].name;
			free(data);
#endif
#ifdef DEBUG
			printf("Identifier is keyword?!)\n");
#endif
			if ( /*keywords[i].*/  kw->value == TOK_KEY_EXTENSION) {
				/*
				 * Keyword used to decorate GNU C extensions -
				 * just ignore it
				 */
				if (t->prev != NULL) {
					cur = t->prev;
					cur->next = NULL;
				} else {
					*dest = NULL;
				}
					
				free(data);
				free(t);
			} else if ((stdflag == ISTD_C89 || stdflag == ISTD_GNU89)
				&& /*keywords[i].*/ kw->std != C89
				&& ((char *)data)[0] != '_') {
				lexwarning("`%s' is a C99 keyword", data);
#if 0
				/*
				 * 05/13/09: When was this NONSENSE introduced?
				 * The very point of this check is that we
				 * DON'T want to treat it as keyword. Is this
				 * for compatibility?!
				 */
				t->type = /*keywords[i].*/  kw->value;
#endif
				t->data = t->ascii = /*keywords[i].*/ kw->name;
				free(data);
			} else {
				t->type = /*keywords[i].*/  kw->value;
				t->data = t->ascii = /*keywords[i].*/ kw->name;
				free(data);
			}	
		} else {
#ifndef PREPROCESSOR
			if (strncmp(data, "__builtin_", strlen("__builtin_")) == 0
				&& builtin_to_be_renamed(data)) {
				/*
				 * 02/14/09: gcc puts half of libc into builtins, e.g.
				 * all (or most) of string.h and math.h. To bypass
				 * this nonsense for userland apps (e.g. OSX headers
				 * use lots of nonsense builtins, like __builtin_bzero()),
				 * we now rename them to libc calls like bzero() to avoid
				 * have to implement them
				 */
				t->data = n_xstrdup(data + strlen("__builtin_"));
				t->ascii = t->data;
				t->flags |= TOK_FLAG_WAS_BUILTIN;
				free(data);
			} else {
				/* 
				 * Check whether ident invades the implementation
				 * namespace
				 */
				if (/*strictansi*/1) {
					check_ident(t, data);
				}	
			}
#endif
		}
	}
#endif
	return ret;
}


#ifdef TEST_STORE_TOKEN
/*
 * Inserts token specified by data-type-lineno triple before token
 * list member pointed to by dest. ``data'' MUST point to dynamically
 * allocated memory if you intend to use destroy_toklist()
 * XXX this is not used anywhere
 */
void 
insert_token(struct token *dest, void *data, int type, int lineno) {
	struct token	*t = n_xmalloc(sizeof *t);
	t->data = data;
	t->type = type;
	t->line = lineno;

	t->next = dest;
	t->prev = dest->prev;
	dest->prev->next = t;
}
#endif

/*
 * Advances token list pointed to by tok to next node if possible.
 * Returns 1 if the end of the list was reached unexpectedly, else 0
 */
int 
next_token(struct token **tok) {
	struct token	*t = *tok;

	if (t == NULL || t->next == NULL) {
#ifndef PREPROCESSOR
		errorfl(t, "Unexpected end of file!");
#endif
		return 1;
	}
	*tok = t->next;
	return 0;
}

/*
 * May not be called to read something that may be the last token
 * in a translation unit!
 * expect_token(&t, TOK_SEMICOLON) = BAD!! because expect_token()
 * fails if there's no token after the token we're looking for
 */
int
expect_token(struct token **tok, int value, int skiptok) {
	struct token	*t;
	int		notfound = 0;

	if ((t = *tok) == NULL) return -1; 
	if (next_token(&t) != 0) return -1; 

	if (t->type == TOK_OPERATOR) {
		if (*(int *)t->data != value) {
			notfound = 1;
		}
	} else if (t->type != value) {
		notfound = 1;
	}

	if (notfound) {
		char	*ascii = tok_to_ascii(value, NULL);
		errorfl(t, "Unexpected token `%s' - expected `%s'",
			t->ascii, ascii);
	}

	if (skiptok) {
		if (next_token(&t) != 0) return -1;
	}

	*tok = t;
	return 0;
}

#ifdef TEST_STORE_TOKEN

/*
 * Deletes all nodes of the token list pointed to by ``tok'', as well
 * as their data members, if they're nonzero. (It can't be stressed
 * enough that for this very reason only dynamically allocated data
 * should be put into the token list nodes.)
 * XXX this is not needed anymore
 */
void 
destroy_toklist(struct token **tok) {
	struct token	*t;
	struct token	*tmp;

	for (t = *tok; t != NULL;) {
		if (t->data != NULL) free(t->data);
		tmp = t;
		t = t->next;
		free(tmp);
	}
	*tok = NULL;
}

#endif


#ifdef TEST_STORE_TOKEN

/*
 * - Construct list with nodes ``one'', ``two'', ``three'', ``four''
 * - Traverse forwards, print all nodes 
 * - Traverse backwards, print all nodes
 * - Insert ``hehe'' as second node in list
 */

int 
main(void) {
	int				i;
	struct token	*tmp;
	struct token	*list = NULL;
	char	*data[] = {
		"one", "two", "three", "four", NULL
	};
	for (i = 0; i < 5; ++i) {
		store_token(&list, data[i]?strdup(data[i]):NULL,
			TOK_COMP_OPEN, i, NULL);
	}

	for (tmp = list; tmp->next != NULL; tmp = tmp->next) {
		printf("%s\n", (char *)tmp->data);
	}

	puts("----");
	/* Skip terminator */
	tmp = tmp->prev;
	for (; tmp != NULL; tmp = tmp->prev) {
		printf("%s\n", (char *)tmp->data);
	}
	insert_token(list->next, strdup("hehehe"), TOK_COMP_OPEN, 0);

	puts("----");
	for (tmp = list; tmp->next != NULL; tmp = tmp->next) {
		printf("%s\n", (char *)tmp->data);
	}
	destroy_toklist(&list);

	return 0;
}

#endif

#ifndef PREPROCESSOR

struct token *
make_bitfield_mask(struct type *ltype, int for_reading, int *shiftbits, struct token **inv_tok0) {
	long long	mask = 0LL;
	long long	inv_mask;
	struct token	*tok;
	struct token	*tok_with_shiftbits;
	struct token	*inv_tok;
	int		i;

	(void) for_reading;

	for (i = 0; i < ltype->tbit->numbits; ++i) {
		mask |= (1LL << i);
	}
	
	tok = alloc_token();

	tok_with_shiftbits = alloc_token();

	if (inv_tok0 != NULL) {
		inv_tok = alloc_token();
	}

	if (ltype->code == TY_ENUM) {
		/*
	 	 * 07/21/08: Like gcc, nwcc allows enum bitfields. If that's
		 * the case here, set type to int because enum constants aren't
	 	 * recognized in the emitters
	 	 */
		tok->type = TY_INT;
	} else {
		tok->type = ltype->code;
	}


	tok->data = zalloc_buf(Z_CEXPR_BUF); /*n_xmalloc(16);*/ /* XXX */

	cross_to_type_from_host_long_long(tok->data, ltype->code, ( mask /*<< ltype->tbit->shiftbits*/ ) );

	tok_with_shiftbits->type = tok->type;
	tok_with_shiftbits->data = zalloc_buf(Z_CEXPR_BUF); /*n_xmalloc(16);*/ /* XXX */
	cross_to_type_from_host_long_long(tok_with_shiftbits->data,
			ltype->code, ( mask << ltype->tbit->shiftbits ) );
	ltype->tbit->bitmask_tok_with_shiftbits = tok_with_shiftbits; /* XXXX interface sucks! */


	if (inv_tok0 != NULL) {
		/*
		 * Create inverted bitfield mask.
		 *
		 * This mask is used to set the bitfield in the storage
		 * unit to 0 while keeping the other bits unchanged. So
		 * we have to include the shift bits to determine at which
		 * position the bitfield is located!
		 *
		 * Thus
		 *
		 *     unit = (unit & ~bf_mask) | new_value;
		 *                    ^^^^^^^^ inv mask
		 *
		 * ... will set the bitfield in a storage unit to a new 
		 * value.
		 */
		inv_mask = ~(mask << ltype->tbit->shiftbits);
		inv_tok->type = tok->type;
		inv_tok->data = zalloc_buf(Z_CEXPR_BUF); /*n_xmalloc(16);*/ /* XXX */
		cross_to_type_from_host_long_long(inv_tok->data, ltype->code, inv_mask);

		*inv_tok0 = inv_tok;
		ppcify_constant(inv_tok);
	}

	if (shiftbits != NULL) {
		/*. ... */
	}

	/* 12/03/08: This was missing */
	ppcify_constant(tok);

	/* 12/11/08: Whoops, this was missing too */
	ppcify_constant(tok_with_shiftbits);
	return tok;
}

#else /* Is preprocessor */

void
append_token_copy(struct token **d, struct token **d_tail, struct token *t) {
	struct token	*temp = alloc_token();

/*	t = n_xmemdup(t, sizeof *t);*/
	*temp = *t;
	t = temp;

	if (*d != NULL) {
		(*d_tail)->next = t;
		t->prev = *d_tail;
		*d_tail = t;
	} else {
		*d = *d_tail = t;
		t->prev = NULL;
	}

	t->next = NULL;
}


void
free_token_list(struct token *t) {
        return; /* 05/22/09: Why is this??? */
	while (t != NULL) {
		/* XXX free data members */
		struct token    *tmp = t->next;
		free(t);
		t = tmp;
	}
}

void
output_token_list(FILE *out, struct token *t) {
	for (; t != NULL; t = t->next) {
		x_fprintf(out, "%s", t->ascii);
	}
}

char *
toklist_to_string(struct token *toklist) {
	struct token    *t;
	char            *p = NULL;
	size_t          len = 0;
	size_t          tlen;

	for (t = toklist; t != NULL; t = t->next) {
		tlen = strlen(t->ascii);
		p = n_xrealloc(p, len+tlen+1);
		sprintf(p+len, "%s", t->ascii);
		len += tlen;
	}
	return p;
}


#endif

