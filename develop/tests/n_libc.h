/*
 * Copyright (C) 2004, 2005  Nils R. Weller 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef N_LIBC_H
#define N_LIBC_H

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
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
void	*debug_malloc_pages(size_t nbytes);
void	unimpl(void);

#if 0
#define DEBUG_REALLOC
#define DEBUG_FREE
#define DEBUG_MALLOC
#endif

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

