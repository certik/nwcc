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
 * Error and warning reporting functions
 */
#include "error.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/wait.h>
#include "token.h"
#include "debug.h"
#include "cc1_main.h"
#include "n_libc.h"

static char	*file = NULL;
static char	*lineptr = NULL;
static int	*line = NULL;
static FILE	*output;

int errors;
int warnings;


void
err_setfile(char *f) {
	file = f;
}

void 
err_setline(int *l) {
	line = l;
}

void
err_setlineptr(char *lp) {
	lineptr = lp;
}

static int	is_inited = 0;
static void 
init_output(void) {
#ifdef DEBUG
	output = stdout;
#else
	output = stderr;
#endif
	is_inited = 1;
}


#define T_COL_RED	31	
#define T_COL_BLUE	34
#define T_COL_MAGENTA	35 /* cyan 36 */

static void
set_text_color(int value) {
	fprintf(output, "\033[1;%dm", value);
}

void
reset_text_color(void) {
	pid_t	pid;

	if ((pid = fork()) == (pid_t)-1) {
		perror("fork");
	} else if (pid == 0) {
		execlp("tput", "tput", "sgr0", (char *)NULL);
	} else {
		wait(NULL);
	}
}


/*
 * XXX instead of passing struct token.ascii for the length, consider
 * setting the length in store_token(), since ascii representation is
 * not always equivalent
 */
void
print_source_line(const char *linep, const char *tokp, const char *tok, int as)
{
	if (linep != NULL) {
		const char	*p;
		size_t	len;
		size_t	i;
		int	allbytes = 0;
		
		if (as) {
			allbytes = strlen(tok);
		}	

		for (p = linep; p < lex_file_map_end; ++p) {
			fputc(*p, output);
			if (as) --allbytes;
			if (*p == '\n') {
				if (!as) {
					break;
				} else {
					/*
					 * Printing string constant - 
					 * do all lines
					 */
					if (allbytes <= 0) {
						return;
					}
				}	
			}
		}
		if (as) {
			return;
		}	

		for (p = linep; p < tokp - 1; ++p) {
			if (*p == '\t') {
				fputc('\t', output);
			} else {
				fputc(' ', output);
			}
		}
		if (tok != NULL) {
			for (i = 0, len = strlen(tok); i < len; ++i) {
				fputc('^', output);
			}
			fprintf(output, " here\n");
		}	
	}
}

void 
lexerror(char *fmt, ...) {
	va_list		v;
	va_start(v, fmt);
	if (is_inited == 0) {
		init_output();
	}
	if (color_flag) {
		set_text_color(T_COL_RED);
	}
	fprintf(output, "%s:%d: Error: ", file? file : "<unknown>",
					line? *line : -1);
	vfprintf(output, fmt, v);
	fputc('\n', output);


	print_source_line(lineptr, lex_file_map + lex_chars_read, " ", 0); 
		
	va_end(v);
	++errors;
	/* exit(EXIT_FAILURE); */
}


void 
errorfl(struct token *f, const char *fmt, ...) {
	va_list		v;

	if (color_flag) {
		set_text_color(T_COL_RED);
	}
	va_start(v, fmt);
	if (is_inited == 0) {
		init_output();
	}
	if (f != NULL) {
		file = f->file;
		line = &f->line;
		fprintf(output, "%s:%d: Error: ", file? file : "<unknown>",
						*line);
		vfprintf(output, fmt, v);
		fputc('\n', output);
		print_source_line(f->line_ptr, f->tok_ptr, f->ascii, 0);
	} else {
		fprintf(output, "Error: ");
		vfprintf(output, fmt, v);
		fputc('\n', output);
	}
	++errors; /* XXX */
	va_end(v);
	/* exit(EXIT_FAILURE); */
}



/*
 * XXX line_ptr unused! if we use this, we need line_offset
 * parameter as well
 */
struct token *
errorfl_mk_tok(const char *f, int l, char *line_ptr) {
	static struct token	tok;
	(void) line_ptr;
	tok.file = (char *)f;
	tok.line = l;
	tok.line_ptr = NULL;
	return &tok;
}

static int
suppress_warning(struct token *tok) {
	char	*f;

	if (tok != NULL) {
		f = tok->file;
	} else {
		f = file;
	}

	if (strncmp(f, "/usr/include/", strlen("/usr/include")) == 0) {
		/*
		 * 12/25/08: Suppress warnings about system headers (not
		 * much we can do about those anyway) unless requested
		 */
		if (!verboseflag) {
			return 1;
		}
	}
	return 0;
}
	


void 
lexwarning(const char *fmt, ...) {
	va_list		v;

	va_start(v, fmt);
	if (is_inited == 0) {
		init_output();
	}

	if (color_flag) {
		set_text_color(T_COL_BLUE); /* XXX mandatory */
	}
	if (suppress_warning(NULL)) {
		return;
	}

	fprintf(output, "%s:%d: Warning: ", file ? file : "<unknown>",
					line ? *line : -1);
	vfprintf(output, fmt, v);
	fputc('\n', output);
	va_end(v);
	++warnings;
	return;
}

static void 
vwarningfl(struct token *tok, const char *fmt, int tcol, va_list v) {
/*	va_list		v;*/
	char		*f;
	int		l;

/*	va_start(v, fmt);*/
	if (is_inited == 0) {
		init_output();
	}

	if (color_flag) {
		set_text_color(tcol);
	}

	if (suppress_warning(tok)) {
		return;
	}

	if (tok != NULL) {
		file = f = tok->file;
		l = tok->line;
		line = &l;

		if (file != NULL) {
			fprintf(output, "%s:%d: ", file, *line);
		}	

		fprintf(output, "Warning: ");
		vfprintf(output, fmt, v);
		fputc('\n', output);
		print_source_line(tok->line_ptr, tok->tok_ptr, tok->ascii, 0);
	} else {	
		fprintf(output, "Warning: ");
		vfprintf(output, fmt, v);
		fputc('\n', output);
	}	
/*	va_end(v);*/
	++warnings;

	return;
}

void
warningfl(struct token *tok, const char *fmt, ...) {
	va_list	va;

	va_start(va, fmt);
	vwarningfl(tok, fmt, T_COL_BLUE, va);
	va_end(va);
}

/*
 * 05/19/09: Distinguish between mandatory and extended warnings.
 * For now we print them with different colors if colored output is
 * requested, but we may also wish to abort with -Werror when
 * encountering mandatory warnings
 *
 * XXX this should distinguish between C89 and C99
 */
void
m_warningfl(struct token *tok, const char *fmt, ...) {
	va_list	va;

	va_start(va, fmt);
	vwarningfl(tok, fmt, T_COL_MAGENTA, va);
	va_end(va);
}

