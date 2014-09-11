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

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "n_libc.h"

static size_t	n_xmalloc_guard;

void
n_xmalloc_set_guard(size_t nbytes) {
	n_xmalloc_guard = nbytes;
}

void *
(n_xmalloc)(size_t nbytes) {
	void	*ret;

	if ((ret = malloc(nbytes+n_xmalloc_guard)) == NULL) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	return ret;
}

void *
(n_xrealloc)(void *block, size_t nbytes) {
	void	*ret;

	if ((ret = realloc(block, nbytes)) == NULL) {
		perror("realloc");
		exit(EXIT_FAILURE);
	}
	return ret;
}

char *
(n_strdup)(const char *msg) {
	char	*ret;
	size_t	len = strlen(msg) + 1;

	if ((ret = malloc(len)) == NULL) {
		return NULL;
	}
	memcpy(ret, msg, len);
	return ret;
}

char *
(n_xstrdup)(const char *msg) {
	char	*ret;

	if ((ret = n_strdup(msg)) == NULL) {
		perror("n_strdup");
		exit(EXIT_FAILURE);
	}
	return ret;
}


void *
(n_memdup)(const void *data, size_t len) {
	void	*ret;

	if ((ret = malloc(len)) == NULL) {
		return NULL;
	}
	memcpy(ret, data, len);
	return ret;
}

void *
(n_xmemdup)(const void *data, size_t len) {
	void	*ret;

	if ((ret = n_memdup(data, len)) == NULL) {
		perror("n_memdup");
		exit(EXIT_FAILURE);
	}
	return ret;
}

void
(x_fprintf)(FILE *fd, const char *fmt, ...) {
	va_list	va;
	int		rc;

	va_start(va, fmt);
	rc = vfprintf(fd, fmt, va);
	va_end(va);
	if (rc == EOF || fflush(fd) == EOF) {
		perror("vfprintf");
		exit(EXIT_FAILURE);
	}
}

void
(x_fputc)(int ch, FILE *fd) {
	if (fputc(ch, fd) == EOF
		|| fflush(fd) == EOF) {
		perror("x_fputc");
		exit(EXIT_FAILURE);
	}
}


/*
 * If some data is modified unexpectedly by someone unknown, just
 * do
 * ptr = debug_malloc_pages(bufsiz);
 * memcpy(ptr, buf, bufsiz);
 * mprotect(ptr, bufsiz, PROT_READ);
 * buf = ptr;
 * ... then a bus error or segfault will be caused when someone
 * attempts to change buf, and gdb gives us a backtrace
 */
void *
debug_malloc_pages(size_t nbytes) {
	long	psize = sysconf(_SC_PAGESIZE);
	size_t	npages = nbytes / psize;
	void	*ret;
	
	if (npages * psize < nbytes) {
		++npages;
	}

#ifdef MAP_ANON
	printf("npages*psize=%lu\n", (unsigned long)npages * psize);
	ret = mmap(0, npages * psize, PROT_READ|PROT_WRITE,
		MAP_ANON|MAP_SHARED, -1, 0);
	if (ret == MAP_FAILED) {
		perror("mmap");
		exit(EXIT_FAILURE);
	}
#else
	puts("debug_malloc_pages() does not work on this system");
	exit(1);
#endif
	return ret;
}

void
unimpl(void) {
	puts("WHOOPS! This code branch should not have been executed");
	puts("because it is unimplemented. Sorry about that. Rest");
	puts("assured that I'll work on this soon.");
	puts("Calling abort() ...");
	abort();
}	


/* XXX remove */
void
make_room(char **p, size_t *size, size_t nbytes) {
	if (nbytes > *size) {
		if (*size == 0) *size = 128;
		else *size *= 2;
		*p = n_xrealloc(*p, *size);
	}
}

int
main(void) {
}

