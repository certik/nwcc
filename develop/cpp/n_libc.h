/*
 * Copyright (c) 2005 - 2006, Nils R. Weller
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

#ifndef N_LIBC_H
#define N_LIBC_H

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>

char	*n_strdup(const char *msg);
void	*n_memdup(const void *data, size_t len);
void	n_xmalloc_set_guard(size_t nbytes);
void	*n_xmalloc(size_t nbytes);
void	*n_xrealloc(void *block, size_t nbytes);
char	*n_xstrdup(const char *msg);
void	*n_xmemdup(const void *data, size_t len);
void	make_room(char **p, size_t *size, size_t nbytes);
void	x_fprintf(FILE *fd, const char *fmt, ...);
void	x_fputc(int ch, FILE *fd);
void	x_fflush(FILE *fd);
void	*debug_malloc_pages(size_t nbytes);
FILE	*get_tmp_file(char *temp, char *output, char *postfix);
void	dounimpl(const char *f, int line);


struct ga_option {
	int	ch;
	char	*name;
	int	takesarg;
};

#define APPEND_LIST(list, tail, node) do { \
	if ((tail) == NULL) { \
		(list) = (tail) = (node); \
	} else { \
		(tail)->next = (node); \
		(tail) = (tail)->next; \
	} \
} while (0)	

#define N_OPTIONS(ar) (sizeof (ar) / sizeof (ar[0]))

extern int	n_optind;
extern char	*n_optarg;

int	nw_get_arg(int, char **, struct ga_option *, int nopts, int *idx);
int	find_cmd(const char *name, char *output, size_t outsiz);
void	irix_abort(void);

void	start_timer(struct timeval *);
int	stop_timer(struct timeval *);

#define unimpl() dounimpl(__func__, __LINE__)

#if 0
#define DEBUG_REALLOC
#define DEBUG_FREE
#define DEBUG_MALLOC
#endif

void	dofree(void *);

#define free(x) dofree(x)

#ifdef DEBUG_MALLOC
#define n_xmalloc(x) n_xmalloc((x) + 50)
#endif

#ifdef DEBUG_REALLOC
#define n_xrealloc(x, y) n_xrealloc((x), (y) + 128)
#endif

#ifdef DEBUG_FREE
#undef free
#define free(x) ((void) x)
#endif

#endif

