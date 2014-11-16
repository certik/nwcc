/*
 * Copyright (c) 2005 - 2010, Nils R. Weller
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include "defs.h"
#include "n_libc.h"
#include "reg.h"

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
abort();
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

	ret = n_xmalloc(len);
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
	int	rc;

	va_start(va, fmt);
	rc = vfprintf(fd, fmt, va);
	va_end(va);
	if (rc < 0) {
		perror("vfprintf");
		exit(EXIT_FAILURE);
	}
}

void
(x_fflush)(FILE *fd) {
	if (fflush(fd) == EOF) {
		perror("fflush");
		exit(EXIT_FAILURE);
	}
}	


void
(x_fputc)(int ch, FILE *fd) {
	if (fputc(ch, fd) == EOF) {
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
	ret = mmap(0, npages * psize, PROT_READ|PROT_WRITE,
		MAP_ANON|MAP_SHARED, -1, 0);
	if (ret == MAP_FAILED) {
		perror("mmap");
		exit(EXIT_FAILURE);
	}
#else
#if 0
	puts("debug_malloc_pages() does not work on this system");
	exit(1);
#endif
	return NULL;
#endif
	return ret;
}

void
debug_make_unwritable(void *p, size_t size) {
	long	psize = sysconf(_SC_PAGESIZE);
	size_t	npages = size / psize;

	if (npages * psize < size) {
		++npages;
	}
	mprotect(p, npages*psize, PROT_READ);
}

void
debug_make_writable(void *p, size_t size) {
	long	psize = sysconf(_SC_PAGESIZE);
	size_t	npages = size / psize;

	if (npages * psize < size) {
		++npages;
	}
	mprotect(p, npages*psize, PROT_READ|PROT_WRITE);
}

void
(dounimpl)(const char *f, int line) {
	printf("In function `%s', line %d:\n", f, line);
	puts("WHOOPS! This code path should not have been executed");
	puts("because it is unimplemented. Sorry about that. Rest");
	puts("assured that I'll work on this soon.");
	puts("");
	puts("How about a game of snake in the meantime? [y/n] ");
	if (isatty(1) && tolower(getchar()) == 'y') {
		struct stat	sbuf;
		char		*path;

		if (stat(INSTALLDIR "/nwcc/bin/snake", &sbuf) == 0) {
			path = INSTALLDIR "/nwcc/bin/snake";
		} else {
			path = "./snake";
		}	
		(void) execl(path, "snake", (char *)NULL);
		perror(path);
		exit(EXIT_FAILURE);
	}	
	puts("Calling abort() ...");
	abort();
}	

void
(dobuggypath)(const char *f, int line) {
	printf("In function `%s', line %d:\n", f, line);
	puts("BUG:  This code path is invalid (deactived) and should");
	puts("      not have been executed! This means that the");
	puts("      code changes which rendered this part of the");
	puts("      program obsolete are not entirely correct for");
	puts("      all cases.");
	puts("");
	puts(" Calling abort()....");
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


int	n_optind = -1;
char	*n_optarg;

#define NEXT_ARG() do { \
	if (++curind >= argc ||  \
		(argv[curind][0] == '-' \
		 	&& argv[curind][1] != 0)) { \
		(void) fprintf(stderr, "No argument given for " \
			       "option `%s'\n", argv[curind-1]); \
		return '?'; \
	} \
	n_optind = curind; \
} while (0) 

/*
 * This mostly resembles getopt() but doesn't stop processing when there is a
 * non-option argument, as BSD implementations do, and supports long arguments.
 *
 * XXX this should probably mess with argv to make n_optind meaningful when
 * normal arguments are mixed with options, as does GNU getopt()
 */
int
nw_get_arg(int argc, char **argv, struct ga_option *options,int nopts,int*idx) {
	static int	curind;
	char		*p;
	char		*p2 = NULL;
	int		i;

	*idx = 0;

	if (n_optind == -1) {
		/*n_optind = 1; XXX ?!?! */
		/*curind = 0;*/
		curind = -1;
	}	

#if 0
	do {
		if (++curind >= argc) {
			return -1;
		}
		p = argv[curind];
	} while (*p != '-'); 	
#endif
	if (++curind >= argc) {
		return -1;
	}	
	p = argv[curind];
	n_optind = curind;
	if (*p != '-') {
		n_optarg = argv[curind];
		return '!';
	}	

	if (strcmp(p+1, "-") == 0) {
		/* Done */
		n_optind = curind = argc;
		return -1;
	} else if (p[1] == 0) {
		return '-';
	} else if (p[2] == 0) {
		/* Single char option */
		++p;
do_single:		
		for (i = 0; i < nopts; ++i) {
			if (options[i].ch == *p) {
				if (!options[i].takesarg && p[1] != 0) {
					if (*p == 'W'
						|| *p == 'f'
						|| *p == 'm') {
						/* XXX gcc compatibility... */
						return '!';
					}

					(void) fprintf(stderr,
					"Option %c does not take an argument\n",
					*p);
					return '?';
				}	
				break;
			}
		}
	} else {
		int	is_long = 0;

		if (*++p == '-') {
			/* --long-option */
			++p;
			is_long = 1;
		}
		p2 = strchr(p, '=');
		
		if (strncmp(p, "Wp,", 3) == 0
			|| strncmp(p, "Wl,", 3) == 0
			|| strncmp(p, "Wa,", 3) == 0) {
			/*
			 * 03/03/09: Some more gcc nonsense special cases;
			 *
			 *    -Wl,foo
			 *
			 * instead of
			 *
			 *    -Wl=foo
			 */
			p2 = strchr(p, ',');
		}

		for (i = 0; i < nopts; ++i) {
			if (options[i].name != NULL) {
				if (p2 != NULL) {
					/* -opt=val */
					/*
					 * 03/03/09: Whoops, this already
					 * considered partial matches to
					 * be complete! E.g. -Wa,foo vs
					 * -Wall
					 */
					if (strncmp(p, options[i].name,	p2 - p) == 0
						&& options[i].name[p2 - p] == 0) {
						break;
					}	
				} else {	
					if (strcmp(p, options[i].name) == 0) {
						break;
					}
				}	
			}
		}
		if (i == nopts && !is_long) {
			/*
			 * This may be a single-char option plus argument, as
			 * in;
			 * -lfoo    equals   -l foo 
			 */
			goto do_single;
		}
	}
	if (i == nopts) {
		(void) fprintf(stderr, "Unknown option -- %s\n", argv[curind]);
		return '?';
	}
	if (options[i].takesarg) {
		if (options[i].name != NULL) {
			if (strcmp(options[i].name, "Xlinker") == 0) {
				/*
				 * 03/03/09: YIKES another special case for gcc
				 *
				 *   -Xlinker -option
				 *
				 *  passes -option as optarg to -Xlinker
				 */
	/*			NEXT_ARG();*/
				if (++curind >= argc /*|| 
					(argv[curind][0] == '-'
					 	&& argv[curind][1] != 0)*/) { 
					(void) fprintf(stderr, "No argument given for " 
						       "option `%s'\n", argv[curind-1]); 
					return '?'; 
				} 
				n_optind = curind; 
				n_optarg = argv[curind];
			} else {
				if (p2 == NULL) {
					/* Argument must be next string */
					NEXT_ARG();
					n_optarg = argv[curind];
				} else {
					n_optarg = p2+1;
				}	
			}
		} else {
			if (p[1] != 0) {
				n_optarg = p+1;
			} else {
				NEXT_ARG();
				n_optarg = argv[curind];
			}	
		}	
	} else {
		n_optind = curind;
	}	
	
	*idx = i; /* XXXX !!!!!!!!!!!!!!!!!!!! should set to -1 :-( */
	if (options[i].name == NULL) {
		return options[i].ch;
	} else {
		return '?';
	}	
}

/*
 * gdb on IRIX usually doesn't seem to work with abort()-generated
 * core dumps :-( I do not know why :-(
 * :-( :-(
 */
void
irix_abort(void) {
	fflush(NULL);
	*(char *)0 = 0;
}

static int
search_dir(const char *path, const char *name) {
	DIR				*d;
	struct dirent	*dir;

	if ((d = opendir(path)) == NULL) {
		return -1;
	}
	while ((dir = readdir(d)) != NULL) {
		if (strcmp(dir->d_name, name) == 0) {
			/* Found command */
			closedir(d);
			return 0;
		}
	}
	closedir(d);
	return -1;
}


int
find_cmd(const char *name, char *output, size_t outsiz) {
	char	*path;
	char	*saved_path;
	char	*p;

	if ((path = getenv("PATH")) == NULL) {
		(void) fprintf(stderr,
			"find_cmd: No PATH environment variable set!\n");
		return -1;
	}
	saved_path = path = n_xstrdup(path);
	for (;;) {
		p = strchr(path, ':');
		if (p != NULL) *p = 0;
		if (search_dir(path, name) == 0) {
			/* Found */
			if (output != NULL) {
				if ((strlen(path) + strlen(name) + 2)
					> outsiz) {
					(void) fprintf(stderr,
					"find_cmd: Path too long for buffer\n");
					free(saved_path);
					return -1;
				} else {
					sprintf(output, "%s/%s", path, name);
				}
			}
			free(saved_path);
			return 0;
		}
		if (p == NULL) break;
		else path = p + 1;
	}
	free(saved_path);
	return -1;
}

FILE *
get_tmp_file(char *temp, char *output, char *postfix) {
	FILE	*ret;
	int		fd;
	size_t	i;

	i = 0;
	do {
		sprintf(output, "%s%lu.%s", temp, (unsigned long)i++, postfix);
		fd = open(output, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
	} while (fd == -1 && errno == EEXIST);

	if (fd == -1) {
		perror(output);
		return NULL;
	}

	if ((ret = fdopen(fd, "w+")) == NULL) {
		perror("fdopen");
		close(fd);
		remove(output);
		return NULL;
	}
	return ret;
}

void
start_timer(struct timeval *tv) {
	gettimeofday(tv, NULL);
}

int
#ifdef TEST_TIMER
stop_timer_(struct timeval *tv, struct timeval *tv2) {
	struct timeval	now = *tv2;
#else
stop_timer(struct timeval *tv) {
	struct timeval	now;
#endif
	int		ret;

#if ! TEST_TIMER
	gettimeofday(&now, NULL);
#endif
	if (now.tv_usec >= tv->tv_usec) {
		/*
		 * 4.774 7.889
		 *
		 * -> sec2 - sec = 3
		 *    usec2 - usec = 115
		 */
		ret = (now.tv_sec - tv->tv_sec) * 1000000;
		ret += (now.tv_usec - tv->tv_usec);
	} else {
		/*
		 * 1.242 2.001
		 *
		 * -> sec2 - sec - 1 = 0
		 *    usec2 + 1000 - usec = 759
		 */
		ret = (now.tv_sec - tv->tv_sec - 1) * 1000000;
		ret += ((now.tv_usec + 1000000) - tv->tv_usec);
	}
	return ret;
}


#ifdef TEST_GET_ARG

int
main(int argc, char **argv) {
	int	ch;
	int	idx;
	struct 	ga_option opts[] = {
		{ 'x', NULL, 0 },
		{ 'y', NULL, 1 },
		{ 0, "zm", 1 },
		{ 0, "zorg", 0 },
		{ 0, "hehe", 1 }
	};
	int	nopts = sizeof opts / sizeof opts[0];

	while ((ch = nw_get_arg(argc, argv, opts, nopts, &idx)) != -1) {
		switch (ch) {
		case 'x':
			printf("read x!\n");
			break;
		case 'y':
			printf("read y! arg=%s\n", n_optarg);
			break;
		case '!':
			printf("read normal argument `%s'\n", n_optarg);
			break;
		case '?':
			if (idx) {
				printf("read option %s!\n", opts[idx].name);
				if (opts[idx].takesarg) {
					printf("arg=%s\n", n_optarg);
				}
			} else {	
				puts("hm");
			}	
		}
	}
	argc -= n_optind;
	argv += n_optind;
	while (argc--) {
		printf("%s\n", *argv++);
	}	
		
	return 0;
}	

#endif


