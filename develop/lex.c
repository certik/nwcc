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
 * Lexical analyzer
 */
#include "lex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "lex_ucpp.h"
#include "token.h"
#include "defs.h"
#include "backend.h" /* 07/25/09: For backend->get_wchar_t() */
#include "error.h"
#include "numlimits.h"
#include "misc.h"
#include "debug.h"
#include "zalloc.h"
#include "type.h"
#include "fcatalog.h"
#include "cc1_main.h"
#include "dwarf.h"
#include "n_libc.h"

const char	*cur_inc;
int		cur_inc_is_std;
int		lineno;

static int	lex_traditional_cpp(struct input_file *in);

#if 0
static void
check_std_inc(const char *inc) {
	const char	*p;
	static char	*std_headers[] = {
		"assert.h", "ctype.h", 
		"float.h", "limits.h",
		"math.h", "stdarg.h", "stddef.h",
		"stdio.h" "stdlib.h", "string.h",
		NULL
	};
	int		i;
	
	cur_inc = NULL;
	cur_inc_is_std = 0;
	if ((p = strrchr(inc, '/')) == NULL) {
		return;
	}	
	inc = p+1;
	if (strncmp(inc, "/usr/include", p - inc - 1) != 0) {
		return;
	}	
	for (i = 0; std_headers[i] != NULL; ++i) {
		if (strcmp(std_headers[i], inc) == 0) {
			cur_inc = inc;
			cur_inc_is_std = 1;
			break;
		}	
	}
}	
#endif


void
set_input_file_fd(struct input_file *file, FILE *fd) {
	file->fd = fd;
}

void
set_input_file_buffer(struct input_file *file, const char *buffer) {
	if (buffer == NULL) {
		file->buf = file->cur_buf_ptr = file->buf_end = NULL;
	} else {
		file->buf = (char *)buffer;
		file->cur_buf_ptr = file->buf;
		file->buf_end = file->buf + strlen(file->buf);
	}
}


struct input_file *
create_input_file(FILE *fd) {
	struct input_file		*ret = n_xmalloc(sizeof *ret);
	static struct input_file	nullfile;
	*ret = nullfile;

	set_input_file_fd(ret, fd);
	set_input_file_buffer(ret, NULL);

	ret->fd = fd;
	return ret;
}





int
get_next_char(struct input_file *file) {
        int     ch;

	if (file->fd != NULL) {
		/* Reading from file */
		ch = getc(file->fd);

		if (!doing_fcatalog) {
			if (ch == '\n') {
				lex_line_ptr = lex_file_map + lex_chars_read;
		                err_setlineptr(lex_line_ptr);
       			}
		}
	} else {
		/* Reading from buffer */
		if (file->cur_buf_ptr == file->buf_end) {
			return EOF;
		} else {
			ch = *file->cur_buf_ptr;
			++file->cur_buf_ptr;
		}
	}
        return ch;
}

void
unget_char(int ch, struct input_file *file) {
	if (file->fd != NULL) {
		/* Reading from file */
		ungetc(ch, file->fd);
	} else {
		/* Reading from buffer */
		if (file->cur_buf_ptr == file->buf) {
			/* Already at beginning (XXX Warning/error needed?) */
			;
		} else {
			*file->cur_buf_ptr = ch;
			--file->cur_buf_ptr;
		}
	}
}



static char *
FGETS(char *buf, size_t bufsiz, struct input_file *in) {
	int	ch;

	while ((ch = FGETC(in)) != EOF) {
		if (bufsiz > 1) {
			if ((*buf++ = ch) == '\n') {
				break;
			}
		} else {
			break;
		}
		--bufsiz;
	}
	*buf = 0;
	return buf;
}

#if 0
static int	nextch;

#define get_next_char(fd) \
	((nextch = getc(fd)) == '\n'? \
	 	lex_line_ptr = lex_file_map + lex_chars_read, \
		err_setlineptr(lex_line_ptr), nextch: nextch)
#endif


int
lex_nwcc(struct input_file *in) {
	/*
	 * Perform general initializations that are always needed regardless of
	 * the preprocessor we're using
	 */
	if (/*options.showline*/ /*1*/ !doing_fcatalog) {
		int	fd = fileno(in->fd);
		struct stat	s;
		if (fstat(fd, &s) == -1) {
			perror("fstat");
			exit(EXIT_FAILURE);
		}
		lex_file_map = mmap(0, s.st_size, PROT_READ, MAP_SHARED, fd, 0);
		if (lex_file_map == MAP_FAILED) {
			perror("mmap");
			exit(EXIT_FAILURE);
		}
		lex_file_map_end = lex_file_map + s.st_size;
		lex_line_ptr = lex_file_map;
#ifdef MADV_SEQUENTIAL
		(void) madvise(lex_file_map, s.st_size, MADV_SEQUENTIAL);
#endif
	}

	/* Initialize error message module */
	err_setfile("unknown");
	token_setfile("unknown");
	err_setline(&lineno);


#if 0
	/*
	 * 02/26/10: First, setting errors/warnings is not needed because
	 * these are static variables implicitly initialized to 0. Second,
	 * it is harmful if we call lex() multiple times (which happens
	 * with the function catalog now)
	 */
	errors = 0;
	warnings = 0;
#endif

	/*
	 * Initialize the digit limits of integral constants so
	 * get_num_literal() can warn about overflow.
	 */
	init_max_digits();

	/*
	 * We now invoke the lexical analyzer
	 */
	if (using_ucpp) {
		return lex_ucpp(in);
	} else {
		return lex_traditional_cpp(in);
	}
}

static int
lex_traditional_cpp(struct input_file *in) {
	int		ch;
	int		tmpi;
	int		compound	= 0;
	int		array		= 0;
	int		parentheses	= 0;
	int		atline		= 0;
	int		*dummyptr	= n_xmalloc(sizeof *dummyptr);
	int		err;
	char		buf[512];
	char		buf2[256];
	char		*tmpc;
	char		*curfile = NULL;
#if 0
	int		curfileid = 0;
#endif
	int		is_wide_char = 0;


	while ((ch = FGETC(in)) != EOF) {
		if (!doing_fcatalog) {
			lex_tok_ptr = lex_file_map + lex_chars_read;
		} else {
			lex_tok_ptr = NULL;
		}

		switch (ch) {
		case '#':
			if (write_fcat_flag) {
				/* Must be comment */
				while ((ch = FGETC(in)) != EOF && ch != '\n')
					;
				++lineno;
				continue;
			}

			/* Must be processor file indicator or #pragma */

			if ((ch = FGETC(in)) == EOF) {
				continue;
			}
			if (ch == 'p') {
				/* Must be pragma */
				UNGETC(ch, in);
				do {
					ch = FGETC(in);
				} while (ch != EOF && ch != '\n');
#if 0
				(void) fgets(buf, sizeof buf, in); 
#endif
				continue;
			}

			/* Must be preprocessor file name */
#if 0 
			if (fgets(buf, sizeof buf, in) == NULL) {
#endif
			if (FGETS(buf, sizeof buf, in) == NULL) {
				lexerror("Unexpected end of file - "
					"expected preprocessor output.");
				return 1;
			}


			if (ch == 'i' && strncmp(buf, "dent", 4) == 0) {
				/*
				 * 08/12/08: Accept but ignore (even correctness
				 * of) #ident directives
				 */
				continue;
			}
			if (sscanf(buf, " %d \"%256[^\"]\" %*d", &atline, buf2)
				!= 2) {
#ifndef __sgi
				lexerror("Bad preprocessor directive format.");
#else
				continue;
#endif
			}

			/*
			 * Note that curfile is still referred to by tokens -
			 * don't free
			 */
			curfile = strdup(buf2);
			if (gflag) {
				unimpl();
				/*curfileid = dwarf_put_file(curfile);*/
			}
#if 0
			XXX
			check_std_inc(curfile);
#endif			
			if (*curfile
				&& curfile[strlen(curfile) - 1] == 'c') {
				cur_inc = NULL;
				cur_inc_is_std = 0;
			}	

			/* Processing new file */
			err_setfile(curfile);
			token_setfile(curfile);

#if 0
			token_setfileid(curfileid);
#endif
			lineno = atline;

			break;

		case ' ':
		case '\f':
		case '\t':
		case '\r':
			continue;
			break;
		case '\n':
			++lineno;
			continue;
			break;
		case '\'':
			err = 0;
			tmpi = get_char_literal(in, &err); /* XXX cross-comp */
			if (!err) {
				int	char_type;

				/*
				 * Character literals are really treated
				 * like integer constants
				 */
				/*int	*tmpip = malloc(sizeof(int));*/
				int	*tmpip = zalloc_buf(Z_CEXPR_BUF); /*n_xmalloc(16);*/ /* XXX */
				if (tmpip == NULL) {
					perror("malloc");
					exit(EXIT_FAILURE);
				}
				*tmpip = tmpi;

				if (is_wide_char) {
					char_type = backend->get_wchar_t()->code;

					/*
					 * The assignment above assumes int,
					 * i.e. 32bit on all supported
					 * platforms
					 */
					assert(backend->get_sizeof_type(
						backend->get_wchar_t(), NULL) == 4);
				} else {
					char_type = TY_INT;
				}
				store_token(&toklist, tmpip, char_type, lineno, NULL);
			}
			is_wide_char = 0;
			break;
		case '"': {
			struct ty_string	*tmpstr;

			tmpstr = get_string_literal(in, is_wide_char);
			if (tmpstr != NULL) {
				store_token(&toklist, tmpstr,
					TOK_STRING_LITERAL, lineno, NULL); 
			}
			is_wide_char = 0;
			break;
			}
		case '(':
		case ')':
			if (ch == '(') {
				++parentheses;
			} else {
				if (parentheses == 0) {
					lexerror("No matching opening "
						"parentheses.");
				}
				--parentheses;
			}
			store_token(&toklist, dummyptr,
				ch == '(' ? TOK_PAREN_OPEN :
					TOK_PAREN_CLOSE, lineno, NULL);
			break;
		case '{':
		case '}':
			if (ch == '{') {
				++compound;
			} else {
				if (compound == 0) {
					lexerror("No matching opening brace.");
				}
				--compound;
			}
			store_token(&toklist, dummyptr,
				ch == '{'? TOK_COMP_OPEN: TOK_COMP_CLOSE,
				lineno, NULL);
			break;
		case '[':
		case ']':
			if (ch == '[') {
				++array;
			} else {
				if (array == 0) {
					lexerror("Not a valid subscript.");
					++array;
				}
				--array;
			}
			store_token(&toklist, dummyptr,
				ch == '[' ? TOK_ARRAY_OPEN : TOK_ARRAY_CLOSE,
				lineno, NULL);
			break;
		case ';':
			store_token(&toklist, dummyptr, TOK_SEMICOLON, lineno, NULL);
			break;
		case '.':
			/*
			 * This might be either a structure / union
			 * indirection operator or a floating point
			 * value like .5 (equivalent to 0.5). If the
			 * latter is the case, call get_num_literal(),
			 * else fall through
			 */
			if ((tmpi = FGETC(in)) == EOF) {
				lexerror("Unexpected end of file.");
				return 1;
			}
			UNGETC(tmpi, in);
			if (isdigit((unsigned char)tmpi)) {
				struct num	*n = get_num_literal(ch, in);

#if 0
				if (standard == C89 && IS_LLONG(n->type)) {
					error("`long long' constants are not "
						"available in C89 (don't use"
						"-ansi or -std=c89");	
				}	
#endif
				if (n != NULL) {
					store_token(&toklist, n->value,
						n->type, lineno, NULL);
				}
				break;
			}
			/* FALLTHRU */
		default:
			if (ch == '?') {
				int	trig;
				/* Might be trigraph */
				if ((trig = get_trigraph(in)) == -1) {
					/*
					 * Not a trigraph - LOOKUP_OP()
					 * will catch the ``?''
					 */
					;
				} else if (trig == 0) {
					/*
					 * The source file contained a ``??''
					 * that isn't isn't part of a trigraph -
				 	 * this is a syntax error, since it 
					 * cannot be the conditional operator
				  	 */
					lexerror("Syntax error at ``?\?''");
				} else {
					/* Valid trigraph! */
					UNGETC(trig, in);
					continue;
				}
			}
					
			if (LOOKUP_OP(ch)) {
				int	*ptri = malloc(sizeof(int));
				char	*ascii = NULL;
				int	is_ellipsis = 0;

				if (ch == '.') {
					int	ch2 = FGETC(in);
					if (ch2 != '.') {
						UNGETC(ch2, in);
					} else {
						int	ch3 = FGETC(in);
						if (ch3 != '.') {
							/*
							 * We've already read
							 * two dots, so there
							 * is no return. Two
							 * dots can never be
							 * valid anyway
							 */
							lexerror("Syntax error at `..'");
						} else {
							is_ellipsis = 1;
						}
					}
				}

				if (is_ellipsis) {
					*ptri = 0;
					store_token(&toklist, ptri,
						TOK_ELLIPSIS, lineno, ascii);
				} else {	
					if (ptri == NULL) {
						perror("malloc");
						exit(EXIT_FAILURE);
					}
					tmpi = get_operator(ch, in, &ascii);
					if (tmpi != -1) {
						*ptri = tmpi;
						store_token(&toklist, ptri,
							TOK_OPERATOR, lineno, ascii);
					} else {
						lexerror("INVALID OPERATOR!!!");
					}
				}
			} else if (isdigit((unsigned char)ch)) {
				struct num	*n = get_num_literal(ch, in);
				if (n != NULL) {
					store_token(&toklist, n->value,
						n->type, lineno, NULL);
				} else {
					lexerror("Couldn't read numeric literal");
				}
			} else if (isalpha((unsigned char)ch) || ch == '_') {
				if (ch == 'L') {
					int	tmpch;

					tmpch = FGETC(in);
					if (tmpch != EOF) {
						UNGETC(tmpch, in);
						if (tmpch == '\'' || tmpch == '"') {
							/*
							 * Long constant - treat like
							 * ordinary one
							 *
							 * 07/24/09: Now we do
							 * distinguish!
							 */
							is_wide_char = 1;
							continue;
						}
					}
				}
					
				tmpc = get_identifier(ch, in);
				if (tmpc != NULL) {
					store_token(&toklist, tmpc,
						TOK_IDENTIFIER, lineno, NULL);
				}
			} else {
				lexerror("Unknown token - %c (code %d)\n", ch, ch);
			}
		}
	}
#if 0
	store_token(&toklist, NULL, 0, lineno);
#endif
	print_token_list(toklist);
	return errors;
}


void 
print_token_list(struct token *list) {
	(void)list;
#if defined DEBUG || USE_UCPP
	puts("-------------------------------------------------------------");
	for (; list /*->data*/ != NULL; list = list->next) {
		if (list->type == TOK_OPERATOR) {
			int	i;
			for (i = 0; operators[i].name != NULL; ++i) {
				if (*(int *)list->data == operators[i].value
					|| *(int *)list->data
					== operators[i].is_ambig) {
					printf("%s", operators[i].name);
					break;
				}
			}
			if (operators[i].name == NULL) {
				(void) fprintf(stderr, "FATAL -- Unknown "
					"operator %d\n",
					*(int *)list->data);
			}
		} else if (IS_CONSTANT(list->type)) {
			rv_setrc_print(list->data, list->type, 0);
		} else if (IS_KEYWORD(list->type)) {
			printf(" %s ", (char *)list->data);
		} else if (list->type == TOK_IDENTIFIER) {
			printf(" %s ", (char *)list->data);
		} else if (list->type == TOK_STRING_LITERAL) {
			struct ty_string	*ts = list->data;
			printf("\"%s\"", ts->str);
		} else if (list->type == TOK_PAREN_OPEN) {
			printf("(");
		} else if (list->type == TOK_PAREN_CLOSE){ 
			printf(")");
		} else if (list->type == TOK_ARRAY_OPEN) {
			printf("[");
		} else if (list->type == TOK_ARRAY_CLOSE) {
			printf("]");
		} else if (list->type == TOK_COMP_OPEN) {
			printf("{\n");
		} else if (list->type == TOK_COMP_CLOSE) {
			printf("}\n");
		} else if (list->type == TOK_SEMICOLON) {
			printf(";\n");
		} else {
			printf("Unknown - code %d\n", list->type);
		}
	}
	puts("-------------------------------------------------------------");
#endif
}

