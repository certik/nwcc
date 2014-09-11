/*
 * Copyright (c) 2003 - 2006, Nils R. Weller
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
 * Preprocessor driver
 */
#include "preprocess.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "token.h"
#include "defs.h"
#include "error.h"
#include "expr.h"
#include "numlimits.h"
#include "type.h"
#include "n_libc.h"
#include "macros.h"


#ifdef DEBUG
static void print_token_list(struct token *list);
#endif

static int
complete_directive(
	FILE *out,
	struct include_file *incf,
	struct pp_directive *dir,
	struct token **toklist,
	int *has_data);


struct token *
do_macro_subst(struct input_file *in, FILE *out,
	struct token *toklist,
	struct token **tailp,
	int dontoutput);


const char		*cur_inc;
int			cur_inc_is_std;
int			g_recording_tokens;
char			g_textbuf[2048];
struct include_dir	*include_dirs;

static int		pre_directive	= 1;
int			lineno		= 1;
char			*curfile = NULL;
 
static int
try_mmap(struct input_file *infile, const char *input, int silent) {
	int 		fd;
	struct stat	s;
	int		saved_errno;

#if 0 
errno = 0;
return -1;
#endif
	fd = open(input, O_RDONLY);
	if (fd == -1) {
		saved_errno = errno;
		if (!silent) {
			perror(input);
		}
		errno = saved_errno;
		return -1;
	} else if (fstat(fd, &s) == -1 || !S_ISREG(s.st_mode)) {
		saved_errno = errno;
		(void) close(fd);
		errno = saved_errno;
		return -1;
	}
	infile->filesize = s.st_size;
	infile->filemap = mmap(0, s.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (infile->filemap == MAP_FAILED) {
		saved_errno = errno;
		if (!silent) {
			perror("mmap");
		}
		errno = saved_errno;
		return -1;
	}
	(void) close(fd);
	infile->filemapend = infile->filemap + infile->filesize;
	infile->filep = infile->filemap;
	return 0;
}

int
open_input_file(struct input_file *inf, const char *input, int silent) {
	inf->unread_idx = 0;

	if (try_mmap(inf, input, silent) == 0) {
		inf->fd = NULL;
		return 0;
	}
	inf->filemap = NULL;
	if (errno == ENOENT) {
		/* File doesn't exist, give up */
		inf->fd = NULL;
		return -1;
	}
		
	if ((inf->fd = fopen(input, "r")) == NULL) {
		if (!silent) {
			perror(input);
		}
		return -1;
	}
	return 0;
}

int
file_is_open(struct input_file *inf) {
	return inf->fd != NULL || inf->filemap != NULL;
}

void
close_input_file(struct input_file *inf) {
	if (inf->fd != NULL) {
		(void) fclose(inf->fd);
	} else {
		(void) munmap(inf->filemap, inf->filesize);
	}
}	

static int
do_get_next_char(struct input_file *inf) {
	if (inf->unread_idx > 0) {
		int	ch = inf->unread_chars[--inf->unread_idx];
		return ch;
	}
	if (inf->fd != NULL) {
		return getc(inf->fd);
	} else {
		if (inf->filep == inf->filemapend) {
			return EOF;
		}
		/*
		 * 05/23/09: Holy cow, this assignment was doing
		 * sign-extension behind our back... So unprintable
		 * 0xff chars were getting mixed up with EOF. Of
		 * course we want getc()-like semantics instead
		 * where the character is converted to unsigned
		 * char
		 */
		return (unsigned char)*inf->filep++;
	}
}

int
get_next_char(struct input_file *inf) {
	int	ch = do_get_next_char(inf);

	static int cnt;
	++cnt;

//if (ch == '(' || ch == ')')
if (ch != EOF)
{
//	printf("   %d:          GOT   %c = %d\n",cnt, ch, ch);
}

        if (ch == '\n') {
                lex_line_ptr = lex_file_map + lex_chars_read;
                err_setlineptr(lex_line_ptr);
		++lineno;
	} else if (ch == '\\') {
		if ((ch = do_get_next_char(inf)) == '\n') {
			/*
			 * Line continued  - XXX wow this doesn't handle
			 * multiple adjacent \\\n constructs
			 */
			++lex_chars_read;
			++lineno;
			if ((ch = do_get_next_char(inf)) == '\n') {	
				++lex_chars_read;
				++lineno;
			}	
			return ch;
		} else {
			--lex_chars_read;
			unget_char(ch, inf);
			return '\\';
		}
	}	

        return ch;
}

int
unget_char(int ch, struct input_file *inf) {
	static int seq;
	++seq;

	if (inf->unread_idx + 1>
		(int)(sizeof inf->unread_chars / sizeof inf->unread_chars[0])) {
		(void) fprintf(stderr, "BUG: unget_char() with too many chars\n");
		abort();
	}
	inf->unread_chars[inf->unread_idx++] = ch;
	if (ch == '\n') {
		--lineno;
	}
	return 0;
}	

static int 
get_string(struct input_file *inf, char *buf, size_t bufsiz,
	char *buf_verbatim,
	int *lastch, int *len, int *key0) {
	char	*p;
	int	ch;
	int	key = 0;

	if (buf_verbatim != NULL) {
		*buf_verbatim++ = '#';
	}

	/* Get directive */
	while ((ch = FGETC(inf)) != EOF && isspace(ch)) {
		if (buf_verbatim != NULL) {
			*buf_verbatim++ = ch;
		}
		if (ch == '\n') {
			UNGETC(ch, inf);
			*buf = 0;
			return 0;
		}
	}	

	for (p = buf;;) {
		if (ch == EOF) {
			break;
		} else if (!isalnum(ch) && ch != '_' && ch != '$') {
			UNGETC(ch, inf);
			break;
		} else if (p == buf + bufsiz - 2) {
			return -1;
		}
		*p++ = ch;
		if (buf_verbatim != NULL) {
			*buf_verbatim++ = ch;
		}
		key = (key * 33 + ch) & N_HASHLIST_MOD;
		ch = FGETC(inf);
	}	
	*p = 0;
	if (buf_verbatim != NULL) {
		*buf_verbatim = 0;
	}
	*lastch = ch;

	if (len != NULL) {
		*len = p - buf;
		*key0 = key;
	}
	return 0;
}	

#define CMD_DEFINE	1
#define CMD_UNDEF	2
#define CMD_IF		3
#define CMD_ELSE	4
#define CMD_ELIF	5
#define CMD_ENDIF	6
#define CMD_ERROR	7
#define CMD_WARNING	8
#define CMD_LINE	9
#define CMD_INCLUDE	10
#define CMD_IFDEF	11
#define CMD_IFNDEF	12
#define CMD_INCLUDE_NEXT	13 /* GNU C... */
#define CMD_IDENT 	14	/* common extension */
#define CMD_PRAGMA 	15
#define CMD_PASSTHRU	16


static struct include_dir	*lastdir; /* for #include_next */


struct pp_directive {
	const char		*name;
	int			code;
	int			takes_arg;
	int			determined;
	int			significant;
	void			*data;
	int			len;
	int			key;
	struct pp_directive	*next;
	struct pp_directive	*prev;
};

static void
set_compiler_line(FILE *out, int line, const char *file) {
	int	old_g_ignore_text = g_ignore_text;

	g_ignore_text = 0;
	x_fprintf(out, "# %d \"%s\"\n", line, file); 
	g_ignore_text = old_g_ignore_text;
}	

static int
check_comment(struct input_file *inf) {
	int	ch;

	if ((ch = FGETC(inf)) == EOF) {
		return -1;
	} else if (ch == '*') {
		int	sline = lineno;

		/* C style comment */
		for (;;) {
			ch = FGETC(inf);
			if (ch == EOF) {
				lineno = sline;
				lexerror("Unterminated comment "
					"(started in line %d)", sline);
				return -1;
			} else if (ch == '\n') {
				/*x_fputc('\n', out);*/
			} else if (ch == '*') {
				if ((ch = FGETC(inf)) == '/') {
					/* comment complete */
					break;
				}
			}
		}	
		return 0;
	} else if (ch == '/' /* && standard != C89 */) {
		/* C99/C++ style comment */
		while ((ch = FGETC(inf)) != '\n' && ch != EOF)
			;
		if (ch != EOF) {
			UNGETC('\n', inf);
		}
		return 0;
	} else {
		return 1;
	}
}	

#define EAT_LINE(in, ch) \
	do ch = FGETC(in); while (ch != EOF && ch != '\n')

	
static void
check_garbage(struct input_file *inf, int ch, const char *dir) {
	if (ch == '\n') {
		(void) FGETC(inf);
		return;
	}
	
	for (;;) {
		int	rc;

		if (ch == '\n' || ch == EOF) {
			return;
		} else if (isspace(ch)) {
			do {
				if ((ch = FGETC(inf)) == '\n') {
					return;
				}
			} while (ch != EOF && isspace(ch));
		} else if (ch == '/') {
			if ((rc = check_comment(inf)) == -1) {
				return;
			} else if (rc == 1) {
				break;
			}	
			ch = FGETC(inf);
		} else {
			break;
		}	
	}
	if (ch != EOF) {
		lexwarning("Ignoring junk after preprocessor directive `%s'", dir);
		EAT_LINE(inf, ch);
	}	
}

void
dump_toklist(struct token *t) {
	fprintf(stderr, "----HERE GOES THE TOKLIST--------");
	for (; t; t = t->next) {
		fprintf(stderr, " LOL %d = %s      (%p)\n",
			 t->type, t->ascii, t);
	}
}	


static unsigned long	cur_directive_start;

/*
 * do_directive() is called by preprocess() when a line begins with a
 * # as first non-whitespace char. For the #undef and #endif directives,
 * proecssing can be completed here already. For others, it is necessary
 * to record subsequent tokens on the same line and then to evaluate them.
 * To do so we just return to preprocess(), which will record the tokens and
 * call complete_directive() when the line ends
 */
static int 
do_directive(struct input_file *inf, struct pp_directive *dir) {
	char		buf[128];
	char		buf_verbatim[256];
	int		ch;
	int		i;
	int		len = 0;
	int		key = -1;
	static const struct pp_directive	directives[] = { 
		{ "define", CMD_DEFINE, 1, 0, 0, NULL, 0,0,0,0 },
		{ "undef", CMD_UNDEF, 1, 0, 0, NULL, 0,0,0,0 },
		{ "include", CMD_INCLUDE, 1, 0, 0, NULL, 0,0,0,0 },
		{ "include_next", CMD_INCLUDE_NEXT, 1, 0, 0, NULL, 0,0,0,0 },
		{ "if", CMD_IF, 1, 0, 0, NULL, 0,0,0,0 },
		{ "else", CMD_ELSE, 0, 0, 0, NULL, 0,0,0,0 },
		{ "elif", CMD_ELIF, 1, 0, 0, NULL, 0,0,0,0 },
		{ "endif", CMD_ENDIF, 0, 0, 0, NULL, 0,0,0,0 },
		{ "ifdef", CMD_IFDEF, 0, 0, 0, NULL, 0,0,0,0 },
		{ "ifndef", CMD_IFNDEF, 0, 0, 0, NULL, 0,0,0,0 },
		{ "warning", CMD_WARNING, 0, 0, 0, NULL, 0,0,0,0 },
		{ "error", CMD_ERROR, 0, 0, 0, NULL, 0,0,0,0 },
		{ "line", CMD_LINE, 1, 0, 0, NULL, 0,0,0,0 },
		{ "ident", CMD_IDENT, 1, 0, 0, NULL, 0,0,0,0 },
		{ "pragma", CMD_PRAGMA, 0, 0, 0, NULL, 0,0,0,0 },
		{ NULL, 0, 0, 0, 0, NULL, 0,0,0,0 }
	};	

	dir->data = NULL;
	cur_directive_start = lex_chars_read;

	if (get_string(inf, buf, sizeof buf,
		buf_verbatim, &ch, NULL, NULL) != 0 
		/*|| !isspace(ch)*/) {
		lexerror("Invalid preprocessor directive");
		EAT_LINE(inf, ch);
		return -1;
	}	

	if (*buf == 0 || isdigit((unsigned char)*buf)) {
		/*
		 * Hmm, GNU cpp just passes # through
		 * 05/21/09: Line number setting directives
		 * such as  # 20 "/usr/include/stdio.h"
		 * are also passed through
		 */
		if (strchr(buf_verbatim, '\n') != NULL) {
			char	*p = strchr(buf_verbatim, 0);

			while ((ch = FGETC(inf)) != EOF) {
				*p++ = ch;
				if (ch == '\n') {
					break;
				}
			}
			*p = 0;
		}
		dir->code = CMD_PASSTHRU;
		dir->data = n_xstrdup(buf_verbatim);
		return 0;
	}

	for (i = 0; directives[i].name != NULL; ++i) {
		if (strcmp(directives[i].name, buf) == 0) {
			break;
		}
	}
	if (directives[i].name == NULL) {
		lexerror("Unknown preprocessor directive `%s'", buf);
		EAT_LINE(inf, ch);
		return -1;
	}
	*dir = directives[i];
	if (dir->takes_arg && ch == '\n') {
		lexerror("No argument given for preprocessor "
			"directive `%s'", buf);
		return -1;
	}
	if (dir->code == CMD_DEFINE) {
		struct macro	*m = alloc_macro();

		if (get_string(inf, buf, sizeof buf, NULL, &ch, &len, &key) != -1) {
			if (g_ignore_text) {
				EAT_LINE(inf, ch);
				return 1;
			}	
			m->name = n_xmemdup(buf, len+1); /*n_xstrdup(buf);*/
			if (ch == EOF || ch == '\n') {
empty:				
				/* #define EMPTY */
				m->empty = 1;
				EAT_LINE(inf, ch);
			} else if (ch == '(') {
				struct macro_arg	*arglist = NULL;
				struct macro_arg	*arglist_tail = NULL;
				struct macro_arg	*ma;
				static struct macro_arg	nullma;

				/* function-like macro */
				(void) FGETC(inf); /* eat ( */
				m->functionlike = 1;

				/* Read parameters */
				for (;;) {
					int	trailing_last = 0;

					if (get_string(inf, buf,
						sizeof buf, NULL, &ch,0,0) == -1) {
						EAT_LINE(inf, ch);
						return -1;
					}
					switch (ch) {
					case ',':
					case ')':
						(void) FGETC(inf);
						break;
					default:
						if (ch == '.') {
							(void) FGETC(inf);
							if (FGETC(inf) != '.'
							|| FGETC(inf) != '.') {
								goto garbage;
							}	
							if (m->trailing_last) {
								lexerror("Macro "
				"already has a trailing last argument");
								EAT_LINE(inf,ch);
								return -1;
							}
							trailing_last = 1;
						} else if (!isspace(ch)) {
garbage:							
							lexerror("Garbage in "
							"macro parameter list"
							" - `%c'", ch);	
							EAT_LINE(inf, ch);
							return -1;
						} else if (ch == '\n') {
							lexerror("Incomplete macro"
							"parameter list");
							EAT_LINE(inf, ch);
							return -1;
						}	
					}
					if (*buf == 0 && ch == ')') {
						break;
					} else if (*buf == 0 && arglist != NULL) {
						lexerror("Empty macro parameter");
						EAT_LINE(inf, ch);
					}	
					ma = n_xmalloc(sizeof *ma);
					*ma = nullma;
					ma->name = n_xstrdup(buf);
					if (arglist == NULL) {
						arglist = arglist_tail = ma;
					} else {
						arglist_tail->next = ma;
						arglist_tail =
							arglist_tail->next;
					}
					if (trailing_last) {
						m->trailing_last = ma;
					}	
					if (ch == ')') {
						/* done */
						break;
					}	
				}
				m->arglist = arglist;
			} else if (!isspace(ch)) {
				/*
				 * 05/23/09: Lack of whitespace does not
				 * warrant an error. This may be e.g. a
				 * comment
				 */
#if 0
				lexerror("Invalid character `%c' after macro "
					"name `%s'", ch, buf);
				return -1;
#endif
				UNGETC(ch, inf);
			} else {
				do {
					if ((ch = FGETC(inf)) == EOF
						|| ch == '\n') {
						if (ch == '\n') {
							UNGETC(ch, inf);
						}	
						goto empty;
					}
				} while (isspace(ch));	
				UNGETC(ch, inf);
			}

			/*
			 * Only empty macros can already be stored
			 * because the macro body is needed to
			 * check identical redefinitions
			 */
			if (m->empty
				&& (m = put_macro(m, len, key)) == NULL) {
				return -1;
			}	
		}
		dir->data = m;
		dir->len = len;
		dir->key = key;
	} else if (dir->code == CMD_UNDEF
		|| dir->code == CMD_IFDEF
		|| dir->code == CMD_IFNDEF) {	
		/* These directives just take an identifier */
		if (get_string(inf, buf, sizeof buf, NULL, &ch, &len, &key) != -1) {
			check_garbage(inf, ch, dir->name);
		}
		if (dir->code == CMD_UNDEF) {
			if (!g_ignore_text) {
				(void) drop_macro(buf, len, key);
			}	
		} else {
			dir->data = n_xmemdup(buf, len+1); /*n_xstrdup(buf);*/
			dir->len = len;
			dir->key = key;
		}	
		return 0;
	} else if (dir->code == CMD_LINE) {
		;
	} else if (dir->code == CMD_INCLUDE
		|| dir->code == CMD_INCLUDE_NEXT) {	
		if (isspace(ch) && ch != '\n') {
			do {
				ch = FGETC(inf);
			} while (isspace(ch) && ch != EOF && ch != '\n');
		}
		if (ch == '<' || ch == '"') {
			char	*p = NULL;
			int	lookingfor = ch == '<'? '>': '"';

			store_char(NULL, 0);
			store_char(&p, ch); 
			while ((ch = FGETC(inf)) != EOF) {
				store_char(&p, ch);
				if (ch == '\n') {
					lexerror("Incomplete #include directive");
					return -1;
				} else if (ch == lookingfor) {
					/* done! */
					store_char(&p, 0);
					break;
				}
			}
			if (ch == EOF) {
				lexerror("Premature end of file");
				return -1;
			}
			dir->data = p;
			dir->len = 0;
			dir->key = -1;
			if ((ch = FGETC(inf)) != EOF && ch != '\n') {
				check_garbage(inf, ch, dir->name);
			}	
		} else if (ch == '\n' || ch == EOF) {
			lexerror("Empty #include directive");
			return -1;
		} else { 
			/* #include pp-tok */
			UNGETC(ch, inf);
		}
		return 0;
	} else if (dir->code == CMD_IF
		|| dir->code == CMD_ELIF) {
		;
	} else if (dir->code == CMD_ERROR
		|| dir->code == CMD_WARNING) {
		if (ch == '\n') {
			/* Done! */
			if (!g_ignore_text) {
				if (dir->code == CMD_ERROR) {
					lexerror("#error");
				} else {
					lexwarning("#warning");
				}	
			}	
			return 0;
		} else if (isspace(ch)) {
			do ch = FGETC(inf); while (isspace(ch) && ch != '\n');
			if (ch == '\n') {
				if (dir->code == CMD_ERROR) {
					lexerror("#error");
				} else {
					lexwarning("#warning");
				}	
				return 0;
			}
			UNGETC(ch, inf);
		}	

		/*
		 * If you think we can just dump ``a line of stuff''
		 * back to the user - think twice. The syntax is
		 * ``#error pp-tokens<opt> newline''
		 * So we need to check token validity, e.g. for string
		 * constants (ucpp doesn't, and accepts #error "foo).
		 */
	} else if (dir->code == CMD_ENDIF
		|| dir->code == CMD_ELSE) {
#if 0
		check_garbage(in, ch, dir->name);
		(void) FGETC(in);
#endif
	} else if (dir->code == CMD_IDENT) {
		char	*p = NULL;

		store_char(NULL, 0);
		if (isspace(ch) && ch != '\n') {
			do {
				ch = FGETC(inf);
			} while (isspace(ch) && ch != EOF && ch != '\n');
		}
		if (ch != '"') {
			puts("invalid #ident directive");
			exit(1);
#if 0
		/* irix doesn't seem to like this :( */
			error("Invalid #ident directive");
			return -1;
#endif
		}
		while ((ch = FGETC(inf)) != EOF) {
			store_char(&p, ch);
			if (ch == '\n') {
				lexerror("Incomplete #ident directive");
				return -1;
			} else if (ch == '"') {
				/* done! */
				store_char(&p, 0);
				break;
			}
		}
		if ((ch = FGETC(inf)) != EOF && ch != '\n') {
			check_garbage(inf, ch, dir->name);
		}	
		return 0;
	} else if (dir->code == CMD_PRAGMA) {
		/* Ignore for now */
		if (ch != '\n') {
			do {
				ch = FGETC(inf);
			} while (ch != EOF && ch != '\n');
		}
	}
	return 0;
}


/*
 * The overwhelming majority of include files uses include guards. But
 * if those files are included more than once, they still may have to
 * be read and processed to find the #endif belonging to the include
 * gaurd. In order to prevent this some programmers tend to use
 * ``redundant include guards''. (I haven't been using such guards
 * since 2003 because I think they suck.) In order to implement this
 * in the preprocessor, we record the position of the first #if/#ifdef/
 * #ifndef and the corresponding #endif for every include file, and
 * whether that covers the entire header. The helps us skip a lot of
 * stuff, particularly in the system headers.
 */
static struct include_file	*current_include;
static struct include_dir	current_working_directory; /* misnomer */

static struct include_file *
lookup_include(struct include_dir *dir, const char *name) {
	struct include_file	*inf;
	size_t			len = strlen(name);

	for (inf = dir->inc_files; inf != NULL; inf = inf->next) {
		if (inf->namelen == len) {
			if (memcmp(inf->name, name, len) == 0) {
				return inf;
			}
		}
	}
	
	return NULL;
}

static void
put_include(struct include_dir *dir, struct include_file *inc) {
	inc->namelen = strlen(inc->name);
	if (dir->inc_files == NULL) {
		dir->inc_files = dir->inc_files_tail = inc;
	} else {
		dir->inc_files_tail->next = inc;
		dir->inc_files_tail = inc;
	}	
}


static int
do_include(FILE *out, char *str, struct token *toklist, int type) {
	char				*p;
	char				*oldname;
	int				rc;
	int				oldline;
	char				*oldfile = curfile;
	struct macro			*mp;
	static int			nesting;
	struct input_file		inf;
	struct include_file		*old_current_include = NULL;
	static struct input_file	nullf;
	struct include_dir		*source_dir = NULL;
	struct include_file		*cached_file;
	struct include_file		*new_cached_file = NULL;
	size_t				old_lex_chars_read;

	if (++nesting > 1000) {
		lexerror("Include file nesting way too deep!");
		return -1;
	}

	inf = nullf;
	if (str == NULL) {
		/* First build include argument from token list */
		toklist = do_macro_subst(NULL, NULL, toklist, NULL, 1); 
		str = toklist_to_string(toklist);
	}
	p = strchr(str, 0);
	if (*str == 0 || --p == str+1) {
		lexerror("Empty include file name");
		return -1;
	} else if ((*p != '"' && *p != '>')
		|| (*p == '"' && *str != '"')
		|| (*p == '>' && *str != '<')) {
		lexerror("#include sytnax is \"file\" or <file>");
		return -1;
	}	
	*p = 0; /* cut " or < */

	if (*str == '"') {
		/*
		 * Try opening file in . first, then fall back to standard
		 * directories (actually absolute paths are ok too.)
		 */
#if 0
		if ((fd = fopen(str+1, "r")) != NULL) {
#endif
		if (open_input_file(&inf, str+1, 1) == 0) {
			/**p = '"';*/
			inf.path = str+1;
			lastdir = NULL;
			source_dir = &current_working_directory;
		}
	}
#if 0
	if (fd == NULL) {
#endif
	if (!file_is_open(&inf)) {	
		/* Try standard includes */
		char			*buf;
		struct include_dir	*id;

		if (type == CMD_INCLUDE_NEXT && lastdir != NULL) {
			if ((id = lastdir->next) == NULL) {
				id = include_dirs;
			}	
		} else {
			id = include_dirs;
		}
		for (;; id = id->next) {
			if (id == NULL) {
				/* End of directory list reached */
				if  (type == CMD_INCLUDE_NEXT) {
					if (lastdir != NULL) {
						/*
						 * We started somewhere in the
						 * middle of the directory
						 * list; wrap around!
						 */
						id = include_dirs;
						lastdir = NULL;
					} else {
						break;
					}
				} else {
					break;
				}
			}	
						
			buf = n_xmalloc(strlen(id->path) +
				sizeof "/" + strlen(str+1));
			sprintf(buf, "%s/%s", id->path, str+1);
#if 0
			if ((fd = fopen(buf, "r")) != NULL) {
#endif
			if (open_input_file(&inf, buf, 1) == 0) {
				lastdir = id;
				inf.path = buf;
				source_dir = id;
				break;
			} else {
				free(buf);
			}	
		}
	}

#if 0
	if ((inf.fd = fd) == NULL) {
#endif
	if (!file_is_open(&inf)) {	
		lexerror("Cannot open include file `%s'", str+1);;
		return -1;
	}

	inf.is_header = 1;
	old_current_include = current_include;
	if ((cached_file = lookup_include(source_dir, str+1)) != NULL) {
		if (cached_file->has_guard) {
			if (cached_file->fully_guarded) {
				if (complete_directive(NULL, cached_file,
					NULL, NULL, NULL) == 0) {
					/*
					 * Include guard condition evaluates
					 * to false - as expected - so the
					 * file need not be read 
					 */
#if 0
					(void) fclose(inf.fd); /* XXX */
#endif
					close_input_file(&inf);
					goto out;
				}	
			}
		}
		/* file is already known - don't record guard */
		current_include = NULL;
	} else {
		/* Processing new file */
		static struct include_file	nullif;

		new_cached_file = n_xmalloc(sizeof *new_cached_file);
		*new_cached_file = nullif;
		new_cached_file->name = str+1;
		current_include = new_cached_file;
	}

#if 0
fprintf(stderr, "processing %s\n", inf.path);
#endif
	/*
	 * __FILE__ and compiler line information must be updated!
	 */
	if ((mp = lookup_macro("__FILE__", 0, -1)) != NULL) {
		oldname = mp->builtin;
		mp->builtin = inf.path;
	}

	old_lex_chars_read = lex_chars_read;
	lex_chars_read = 0;

	/* Inform the compiler that we're doing a new file */
	set_compiler_line(out, 1, inf.path);
	oldline = lineno;
	lineno = 1;

	rc = preprocess(&inf, out); 
#if 0
	(void) fclose(inf.fd);
#endif
	close_input_file(&inf);
	/* free(inf.path); */

	lex_chars_read = old_lex_chars_read;

	/* Restore old line number */
	lineno = oldline;
	curfile = oldfile;
	set_compiler_line(out, lineno, curfile);

	if (mp != NULL) {
		mp->builtin = oldname;
	}
	if (new_cached_file != NULL) {
		put_include(source_dir, new_cached_file);
	}
	current_include = old_current_include;


out:
	--nesting;
	return rc;
}

/*
 * cond_dir_list records the conditional preprocessor directives such as #if,
 * #elif, #ifdef, etc. It is used to match new occasions of such directives
 * with previous ones. The list should be read FIFO stack-like, where the
 * tail points to the top. When a directive is terminated by an #endif, all
 * belonging directives are removed from the tail.
 */ 
static struct pp_directive	*cond_dir_list;
static struct pp_directive	*cond_dir_cur_start;
static struct pp_directive	*cond_dir_list_tail;
int				g_ignore_text = 0;

/* XXX move to evalexpr.c */
int
value_is_nonzero(struct tyval *cv) {
	int	evaluates_true = 0;

	/*
	 * The resulting expression must have integral
	 * type
	 */
#define EVALTRUE(cv, ty) *(ty *)cv->value != 0
	switch (cv->type->code) {
	case TY_INT:
		evaluates_true = EVALTRUE(cv, int);
		break;
	case TY_UINT:
		evaluates_true = EVALTRUE(cv, unsigned int);
		break;
	case TY_LONG:
		evaluates_true = EVALTRUE(cv, long);
		break;
	case TY_ULONG:
		evaluates_true = EVALTRUE(cv, unsigned long);
		break;
	case TY_LLONG:
	case TY_ULLONG: {
		/*
		 * Avoid relying on compiler support for
		 * long long, but assume 64 bits
		 */
		unsigned char	*p =cv->value;
		int		i;

		for (i = 0; i < 8; ++i) { 
			evaluates_true |= !!*p++;
		}
	}	
	default:
		printf("BUG: preprocessor expression has "
			"type %d (not integral!)\n",
			cv->type->code);

	}
	return evaluates_true;
}

/*
 * This function is unfortunately overloaded to serve two distinct purposes:
 *
 * 1) complete a preprocessor directive. If it is an #if/#ifdef/#elif/#endif/etc
 *    directive, g_ignore_text will be modified as necessary, and include guards
 *    are also recorded if current_include is non-null.
 *    All this is done if incf is null (and dir thusly non-null.)
 *
 * 2) check whether the include guard in include file ``incf'' (start_dir)
 *    evaluates to true. Only the result of this evaluation is returned, and no
 *    side effects take place.
 *    This is done if incf is non-null
 *
 * XXX we should have an evaluates_true() instead!
 */
static int
complete_directive(
	FILE *out,
	struct include_file *incf,
	struct pp_directive *dir,
	struct token **toklist,
	int *has_data) {

	struct macro	*mp;
	struct expr	*ex;
	struct token	*t;
	struct token	*last = NULL;
	struct token	*ltoklist;
	int		evaluates_true = 0;
	int		recording_guard = 0;

	if (incf != NULL) {
		dir = incf->start_dir;
		ltoklist = incf->toklist;
		toklist = &ltoklist;
	}

	if (g_ignore_text) {
		if (dir->code == CMD_ERROR
			|| dir->code == CMD_WARNING
			|| dir->code == CMD_LINE
			|| dir->code == CMD_DEFINE
			|| dir->code == CMD_INCLUDE
			|| dir->code == CMD_INCLUDE_NEXT) {
			return 0;
		}
	}

	if (dir->code == CMD_ERROR || dir->code == CMD_WARNING) {
		char	*p = toklist_to_string(*toklist);

		if (dir->code == CMD_WARNING) {
			lexwarning("#warning: %s", p);
		} else {
			lexerror("#error: %s", p);
		}	
		free(p);
		*toklist = NULL;
		return 0;
	} else if (dir->code == CMD_LINE) {
		struct token	*t = *toklist;
		struct token	*t2;
		
		if ((t = skip_ws(t)) == NULL) {
			lexerror("Empty #line directive");
			return -1;
		}
		if (t->type != TY_INT) {
			lexerror("Invalid #line directive x");
			return -1;
		}
		if ((t2 = skip_ws(t->next)) != NULL) {
			if (t2->type != TOK_STRING_LITERAL) {
				lexerror("Invalid #line directive");
				return -1;
			}
			curfile = t2->data+1;
			(void) strtok(curfile, "\"");
		}	
		lineno = *(int *)t->data;
		set_compiler_line(out, lineno, curfile);
		*toklist = NULL;
		return 0;
	} else if (dir->code == CMD_DEFINE) {
		mp = dir->data;
		mp->toklist = *toklist;
		*toklist = NULL;
		if (put_macro(mp, dir->len, dir->key) == NULL) {
			return -1;
		}	
		return 0;
	} else if (dir->code == CMD_INCLUDE
		|| dir->code == CMD_INCLUDE_NEXT) {
		if (!g_ignore_text) {
			int	rc;
		
			rc = do_include(out, NULL, *toklist, dir->code);
			*toklist = NULL;
			return rc;
		} else {
			return 0;
		}	
	}	
		

	/*
	 * At this point, we are only dealing with conditional directives
	 * anymore, i.e. #if/#elif/#ifndef/#endif etc. Those need to be
	 * stored, so a copy must be made
	 */
	if (dir->code != CMD_ENDIF && incf == NULL) {
		dir = n_xmemdup(dir, sizeof *dir);
	}	

	if (incf == NULL) {
		/*
	 	 * A newline has already been read - to ensure a correct __LINE__,
		 * that must be undone
		 */
		--lineno;
	}	

	if (dir->code == CMD_ELIF || dir->code == CMD_ELSE) {
		/* These can only continue an existent directive chain! */
		if (cond_dir_cur_start == NULL) {
			lexerror("Use of #%s directive without preceding "
				"#if/#ifdef/#ifndef", dir->name);
			++lineno;
			return -1;
		} else if (cond_dir_list_tail->code == CMD_ELSE
			&& dir->code == CMD_ELIF) {
			lexerror("#else followed by #elif directive");
			++lineno;
			return -1;
		}
	} else if (dir->code == CMD_IF
		|| dir->code == CMD_IFDEF
		|| dir->code == CMD_IFNDEF) {
		/* These can only introduce a new directive chain! */
		if (incf == NULL) {
			cond_dir_cur_start = dir;
		}	
	}	
	
	switch (dir->code) {
	case CMD_IFDEF:
	case CMD_IFNDEF:	
	case CMD_IF:	
	case CMD_ELIF:
		if (incf == NULL
			&& current_include != NULL
			&& !current_include->has_guard) {
			/* This may be the guard we are looking for */
			if (!*has_data) {
				current_include->fully_guarded = 1;
			}
			recording_guard = 1;
			current_include->startp = dir;
			current_include->start_dir = dir;
			current_include->has_guard = 1;
			if (toklist != NULL) {
				current_include->toklist = *toklist;
			}	
			current_include->start_guard = cur_directive_start;
		}
		if (dir->code == CMD_IFDEF || dir->code == CMD_IFNDEF) {
			if ((mp = lookup_macro(dir->data, dir->len, dir->key)) != NULL) {
				/* Macro exists! */
				if (dir->code == CMD_IFDEF) {
					evaluates_true = 1;
				} else {
					evaluates_true = 0;
				}	
			} else {
				if (dir->code == CMD_IFDEF) {
					evaluates_true = 0;
				} else {
					evaluates_true = 1;
				}
			}
			if (incf == NULL && !recording_guard) {
				free(dir->data);
			}	
		} else if (!g_ignore_text || cond_dir_cur_start->significant) {
			struct token	*toklist_tail = NULL;

			/*
			 * This is an #if or #elif - macro substituion was
			 * disabled while reading tokens - process them
			 * now!
			 */
			*toklist = do_macro_subst(NULL, NULL, *toklist, 
					&toklist_tail, 1);

			/*
		 	 * #if/#elif <constant expression>
		 	 *
			 * First cut all whitespace, then append a newline as
			 * terminator for parse_expr()
			 */
			for (t = *toklist; t != NULL;) {
				struct token	*next = t->next;
	
				if (t->type == TOK_WS) {
					if (t->prev) {
						t->prev->next = t->next;
						if (t->next) {
							t->next->prev = t->prev;
						}	
					} else {
						*toklist = t->next;
						t->next->prev = NULL;
					}
					free(t);
				} else {
					last = t;
				}	
				t = next;
			}	
			if (last == NULL) {
				lexerror("Empty #%s directive", dir->name);
				++lineno;
				return -1;
			} else {
				static struct token	terminator;

				terminator.type = TOK_NEWLINE;
				last->next = &terminator;
			}

			ex = parse_expr(toklist, TOK_NEWLINE, 0, EXPR_CONST, 1);
			if (ex == NULL) {
				++lineno;
				return -1;
			} else if (ex->const_value == NULL) {
				puts("BUG: const_value = NULL?????");
				abort();
			}

			evaluates_true = value_is_nonzero(ex->const_value);
			if (dir->code == CMD_ELIF) {
				if (cond_dir_cur_start->determined) {
					/* Branch to take already determined */
					evaluates_true = 0;
				}
			}
		}
		break;
	case CMD_ELSE:
		if (!cond_dir_cur_start->determined) {
			/* No branch determined yet, so #else wins */
			evaluates_true = 1;
		} else {
			evaluates_true = 0;
		}		
		break;
	case CMD_ENDIF:	
		break;
	default:
		abort();
	}

	if (incf != NULL) {
		return evaluates_true;
	} else if (dir->code != CMD_ENDIF) {
		/*
		 * The result of the evaluation only matters if text
		 * is not already being ignored
		 */
		if (!g_ignore_text) {
			if (!evaluates_true) {
				g_ignore_text = 1;
				dir->significant = 1;
			} else {
				cond_dir_cur_start->determined = 1;
			}	
		} else {
			if (dir->code == CMD_ELSE) {
				if (cond_dir_list_tail->significant
					&& evaluates_true) {
					/* 
					 * Previous directive ends - text is
					 * no longer ignored
					 */
					g_ignore_text = 0;
					cond_dir_cur_start->determined = 1;
				}
			} else if (dir->code == CMD_ELIF) {
				if (cond_dir_list_tail->significant
					&& evaluates_true) {
					g_ignore_text = 0;
					cond_dir_cur_start->determined = 1;
				} else if (cond_dir_list_tail->significant) {
					dir->significant = 1;
				}	
			}
		}
		
		if (cond_dir_list == NULL) {
			cond_dir_list = cond_dir_list_tail = dir;
		} else {	
			cond_dir_list_tail->next = dir;
			dir->prev = cond_dir_list_tail;
			cond_dir_list_tail = dir;
		}
	} else {
		/* Current chain is done! */
		struct pp_directive	*ppd;
		struct pp_directive	*tmp;
		int			code;

		for (ppd = cond_dir_list_tail; ppd != NULL;) {
			if (ppd->significant) {
				/*
				 * An ignored (by ppd's controlling expression)
				 * text passage ends here 
				 */
				g_ignore_text = 0;
			}
			tmp = ppd;
			code = ppd->code;
			ppd = ppd->prev;
			if (code == CMD_IF
				|| code == CMD_IFDEF
				|| code == CMD_IFNDEF) {
				struct pp_directive	*startp = tmp;

				/*
				 * Start of chain reached, we are done. Now we
				 * have to return to dealing with the previous,
				 * outer chain (if any!)
				 */
				if ((cond_dir_list_tail = ppd) == NULL) {
					/* No outer one */
					for (ppd = cond_dir_list /*->next*/; ppd;) {
						tmp = ppd;
						ppd = ppd->next;
						if (current_include != NULL
							&& current_include->startp
							== tmp) {
							continue;
						}	
						free(tmp);
					}	
					cond_dir_list = NULL;
				} else {	
					/* Yes, outer */
					for (tmp = ppd;
						tmp != NULL;
						tmp = tmp->prev) {
						code = tmp->code;
						if (code == CMD_IF
							|| code == CMD_IFDEF
							|| code == CMD_IFNDEF) {
							cond_dir_cur_start =
								tmp;
							break;
						}
					}
					for (ppd = cond_dir_list_tail->next;
						ppd != NULL;) {
						tmp = ppd;
						ppd = ppd->next;
						if (current_include != NULL
							&& current_include->
							startp == tmp) {
							continue;
						}	
						free(tmp);
					}	
					cond_dir_list_tail->next = NULL;
				}
				if (current_include != NULL
					&& current_include->has_guard
					&& current_include->end_guard == 0
					&& startp ==
						current_include->startp) {
					/* Record end of inc guard */
					current_include->end_guard =
						cur_directive_start;
					current_include->end_dir = dir;
					*has_data = 0;
				}
				break;
			}
		}
	}	

	if (toklist) *toklist = NULL;
	++lineno;
	return 0;
}

extern int	collect_parens;

int
preprocess(struct input_file *inf, FILE *out) {
	int			ch;
	int			tmpi;
	int			compound	= 0;
	int			array		= 0;
	int			parentheses	= 0;
	int			prevch		= 0;
	int			first_byte	= 1;
	int			*dummyptr	= n_xmalloc(sizeof *dummyptr);
	int			err;
	struct token		*toklist = NULL;
	struct token		*toklist_tail = NULL;
	struct token		*t;
	struct pp_directive	dir;
	struct macro		*mp = NULL;
	static struct macro	nullm;
	char			*p;
	char			*tmpc;
	int			doing_funclike = 0;
	int			substitute_macros = 1;
	int			doing_pre = 0;
	int			has_data = 0;
	int			maybe_funclike = 0;

	if (/*options.showline*/ inf->fd) {
		int		fd = fileno(inf->fd);
		struct stat	s;
		if (fstat(fd, &s) == -1) {
			perror("fstat");
			exit(EXIT_FAILURE);
		}
		lex_file_map = mmap(0, s.st_size, PROT_READ, MAP_SHARED, fd, 0);
		if (lex_file_map == MAP_FAILED) {
			/* 05/21/09: XXX This seems to error for empty stdin?!!? */
			perror("mmap");
			exit(EXIT_FAILURE);
		}
		lex_file_map_end = lex_file_map + s.st_size;
		lex_line_ptr = lex_file_map;
	} else {
		lex_file_map = inf->filemap;
		lex_file_map_end = inf->filemapend;
		lex_line_ptr = lex_file_map;
	}

	/* Initialize error message module */
	err_setfile(curfile = inf->path);
	token_setfile(curfile);

	if (!inf->is_header && !inf->is_cmdline) {
		/* Processing new .c file */
		lineno = 1;
		set_compiler_line(out, lineno, curfile);
		err_setline(&lineno);

		/*
	 	 * Set predefined macros
		 */
		mp = n_xmalloc(sizeof *mp);
		*mp = nullm;
		mp->name = n_xstrdup("__LINE__");
		mp->builtin = &lineno;
		(void) put_macro(mp, 0, -1);
		mp = n_xmalloc(sizeof *mp);
		*mp = nullm;
		mp->name = n_xstrdup("__FILE__");
		mp->builtin = n_xmalloc(strlen(curfile) + 3);
		sprintf(mp->builtin, "\"%s\"", curfile);
		(void) put_macro(mp, 0, -1);
	}	
	errors = 0;
	warnings = 0;

	/*
	 * Initialize the digit limits of integral constants so
	 * get_num_literal() can warn about overflow.
	 */
	init_max_digits();

	while ((ch = FGETC(inf)) != EOF) {
		lex_tok_ptr = lex_file_map + lex_chars_read;
		if (!isspace(ch) && ch != '#') {
			pre_directive = 0;
			/*
			 * If this is a comment, has_data must remain zero -
			 * check below
			 */
			if (ch != '/') {
				has_data = 1;
			}	
		}	
		if (g_ignore_text
			&& !doing_pre
			&& ch != '\n'
			&& ch != '#'
			&& ch != '/'
			&& ch != '\''
			&& ch != '"') {
			/*
			 * This is a blunt way of avoiding processing  
			 * data in an ignored (e.g. by ``#if 0'')
			 * text passage. Comments, newlines, preprocessor
			 * directives and string/character constants must
			 * still be processed though. I hope I didn't
			 * miss anything here ...
			 */
			continue;
		}	

		if (maybe_funclike && !isspace(ch) && ch != '/' && ch != '(') {
			/*
			 * Function-like macro identifier not followed by
			 * opening parentheses!
			 * (if this is a ``/'' it may be a comment - check
			 * below.)
			 */
			
			output_token_list(out, toklist);
			free_token_list(toklist);
			toklist = NULL;
			maybe_funclike = 0;
			g_recording_tokens = 0;
		}

		switch (ch) {
		case '#':
			if (pre_directive || first_byte) {
				int	done = 0;

				/* Preprocessor directive */
				pre_directive = 0;
				/* XXX predef isn't an input_file */
				if (do_directive(inf, &dir) != 0) {
					pre_directive = 1;
					break;
				}

				/*
				 * Check whether subsequent tokens on same
				 * line (including \-continued ones!) need
				 * to be recorded for expression evaluation
				 * or macro definitions
				 */
				switch (dir.code) {
				case CMD_PASSTHRU:
					fprintf(out, "%s", (char *)dir.data);
					break;
				case CMD_UNDEF:
				case CMD_ENDIF:		
				case CMD_IFDEF:
				case CMD_IFNDEF:
				case CMD_IDENT:
				case CMD_PRAGMA:
					g_recording_tokens = 0;
					doing_pre = 0;
					if (dir.code != CMD_UNDEF
						&& dir.code != CMD_IDENT
						&& dir.code != CMD_PRAGMA) {
						complete_directive(out, NULL,
							&dir, NULL, &has_data);
					}	
					toklist = NULL;
					done = 1;
					break;
				case CMD_DEFINE:
					mp = dir.data;
					if (mp->empty) {
						/* No tokens needed */
						done = 1;
						break;
					}
					doing_pre = 1;
					g_recording_tokens = 1;
					substitute_macros = 0;
					break;
				case CMD_INCLUDE:
				case CMD_INCLUDE_NEXT:	
					if (dir.data != NULL) {
						if (!g_ignore_text) {
							(void) do_include(
							out, dir.data, NULL,
							dir.code);
						}	
						doing_pre = 0;
						substitute_macros = 1;
						g_recording_tokens = 0;
						done = 1;
					} else {
						g_recording_tokens = 1;
						substitute_macros = 0;
						doing_pre = 1;
					}	
					break;	
				case CMD_LINE:	
				case CMD_IF:
				case CMD_ELIF:
					if (dir.code == CMD_LINE) {
						substitute_macros = 1;
					} else {	
						/*
						 * Maco replacement is done
						 * later
						 */
						substitute_macros = 0;
					}	
					g_recording_tokens = 1;
					doing_pre = 1;
					break;
				case CMD_ERROR:
				case CMD_WARNING:
					/* Macro replacement is never done */
					substitute_macros = 0;
					g_recording_tokens = 1;
					doing_pre = 1;
					break;
				default:
					if (dir.code != CMD_ERROR
						&& dir.code != CMD_WARNING) {
						substitute_macros = 0;
					}	
					if (dir.data != NULL) {
						done = 1;
						g_recording_tokens = 0;
					} else {
						doing_pre = 1;
						g_recording_tokens = 1;
					}	
					break;
				}
				if (done) {
					/*UNGETC('\n', in);*/
					pre_directive = 1;
				}
			} else {
				if (!g_recording_tokens) {
					x_fputc('#', out);
				} else {	
					if ((ch = FGETC(inf)) == '#') {
						store_token(&toklist,
							&toklist_tail,
							n_xstrdup("##"),
							TOK_HASHHASH, lineno,
							NULL);
					} else {
						UNGETC(ch, inf);
						store_token(&toklist,
							&toklist_tail,
							n_xstrdup("#"),
							TOK_HASH, lineno,
							NULL);
					}
				}
			}
			break;
		case ' ':
		case '\f':
		case '\t':
		case '\r':
			p = g_textbuf;
			*p++ = ch;
			
			while (isspace(ch = FGETC(inf)) && ch != '\n') {
				*p++ = ch; /* XXX */
			}	
			*p = 0;
			UNGETC(ch, inf);
			if (!g_recording_tokens) {
				if (pre_directive) {
					x_fprintf(out, g_textbuf);
				} else {	
					x_fputc(' ', out);
				}	
			} else {
				/*
				 * 05/24/09: Do not store leading
				 * whitespace tokens for macro
				 * bodies!
				 *
				 * #define foo()       bar
				 *
				 * will always expand to just "bar"
				 */
				if (mp != NULL
					&& doing_pre
					&& dir.code == CMD_DEFINE
					&& toklist == NULL) {
					/* Is macro body whitespace */
					;
				} else {
					store_token(&toklist,
						&toklist_tail,
						n_xstrdup(" "),
						TOK_WS, lineno, NULL);
				}
			}	
			break;
		case '\n':
			if (g_recording_tokens && doing_pre) {
				g_recording_tokens = 0;
				substitute_macros = 1;
				doing_pre = 0;
				/*
				 * 05/24/09: Do not store trailing whitespace
				 * tokens!
				 */
				if (dir.code == CMD_DEFINE
					&& toklist != NULL
					&& toklist_tail->type == TOK_WS) {
					if (toklist == toklist_tail) {
						/* Only token is whitespace */
						toklist = toklist_tail = NULL;
					} else {
						toklist_tail->prev->next = NULL;
						toklist_tail = toklist_tail->prev;
					}
				}
				complete_directive(out, NULL, &dir, &toklist,
					&has_data);
			} else { 
				mp = NULL; /* XXX hm?!? */
			}

			pre_directive = 1;
			x_fputc('\n', out);
			break;
		case '/':
			if ((ch = FGETC(inf)) == '*') {
				int	sline = lineno;
				char	*sfile = curfile;

				/* C style comment */
				for (;;) {
					ch = FGETC(inf);
					if (ch == EOF) {
						err_setfile(sfile);
						lineno = sline;
						lexerror("Unterminated comment "
							"(started in line %d,"
							" file %s)",
							sline, sfile);
						return -1;
#if 0
					/* TODO: warn about nested comments */	
					} else if (ch == '/') {
#endif
					} else if (ch == '\n') {
						x_fputc('\n', out);
						++lineno;
					} else if (ch == '*') {
						if ((ch = FGETC(inf)) == '/') {
							/* comment complete */
							break;
						} else {
							UNGETC(ch, inf);
						}
					}
				}	
				UNGETC(' ', inf);
			} else if (ch == '/' /* && standard != C89 */) {
				/* C99/C++ style comment */
				while ((ch = FGETC(inf)) != '\n' && ch != EOF)
					;
				UNGETC(' ', inf);
				if (ch != EOF) {
					UNGETC('\n', inf);
				}	
			} else {
				has_data = 1;
				/* Not a comment */
				if (maybe_funclike) {
					maybe_funclike = 0;
					g_recording_tokens = 0;
					output_token_list(out, toklist);
					free_token_list(toklist);
					toklist = NULL;
				}	
				UNGETC(ch, inf);
				ch = '/';
				goto do_operator;
			}
			break;
		case '\'':
			err = 0;
			tmpi = get_char_literal(inf, &err, &tmpc);
			if (!err) {
				/*
				 * Character literals are really treated
				 * like integer constants
				 */
				int	*tmpip = malloc(sizeof(int));
				if (tmpip == NULL) {
					perror("malloc");
					exit(EXIT_FAILURE);
				}
				*tmpip = tmpi;
				if (g_recording_tokens) {
					char	*tmpc2;
					
					tmpc2 = n_xmalloc(strlen(tmpc)+3);
					sprintf(tmpc2, "'%s'", tmpc);
					store_token(&toklist,
						&toklist_tail,
						tmpip,
						TY_INT, lineno, tmpc2);
					/* XXX .. */
/*					t->ascii = tmpc2;*/
				} else {
					x_fprintf(out, "'%s'", tmpc);
				}	
			}
			break;
		case '"': {
			struct ty_string	*ts;

			ts = get_string_literal(inf);
			if (ts != NULL) {
				if (g_recording_tokens) {
					store_token(&toklist,
						&toklist_tail,
						ts->str,
						TOK_STRING_LITERAL, lineno,
						ts->str); 
				} else {
					x_fprintf(out, "%s", ts->str);
				}	
			}
			break;
			}
		case '(':
		case ')':
			if (ch == '(') {
				if (maybe_funclike) {
					/* Is function-like macro! */
					doing_funclike = 1;
					maybe_funclike = 0;
					parentheses = 1;
					substitute_macros = 0;
					g_recording_tokens = 1;
					
					/*
					 * There may only be whitespace tokens
					 * between the identifier and the
					 * parentheses - cut those!
					 */
					if (toklist->next) {
						free_token_list(toklist->next);
						toklist->next = NULL;
						toklist_tail = toklist;
					}

					/*
					 * fall through so that ( is appended
					 * below
					 */
				} else {	
					
					/*
					 * Don't increment/decrement paren count
					 * for things like
					 * #define foo (lol
					 */
					if (!doing_pre) ++parentheses;
				}	
			} else {
				if (!doing_pre) --parentheses;
			}
			if (g_recording_tokens) {
				store_token(&toklist, &toklist_tail, dummyptr,
					ch == '(' ? TOK_PAREN_OPEN :
						TOK_PAREN_CLOSE, lineno, NULL);
			} else {
				x_fputc(ch, out);
			}	
			if (doing_funclike && parentheses == 0) {
				/*
				 * XXX take g_recording_tokens into
				 * account :-(
				 */
				toklist = do_macro_subst(inf, out, toklist, 
					&toklist_tail, 0);
				if (toklist == NULL) {
					/* done */
					doing_funclike = 0;
					g_recording_tokens = 0;
					substitute_macros = 1;
				} else {
					parentheses = collect_parens;
				}
			}

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
			if (g_recording_tokens) {
				store_token(&toklist, &toklist_tail, dummyptr,
					ch == '{'? TOK_COMP_OPEN: TOK_COMP_CLOSE,
					lineno, NULL);
			} else {
				x_fputc(ch, out);
			}	
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
			if (g_recording_tokens) {
				store_token(&toklist, &toklist_tail, dummyptr,
					ch == '[' ? TOK_ARRAY_OPEN : TOK_ARRAY_CLOSE,
					lineno, NULL);
			} else {
				x_fputc(ch, out);
			}	
			break;
		case ';':
			if (g_recording_tokens) {
				store_token(&toklist, &toklist_tail ,dummyptr,
					TOK_SEMICOLON, lineno, NULL);
			} else {
				x_fputc(';', out);
			}	
			break;
		case '.':
			/*
			 * This might be either a structure / union
			 * indirection operator or a floating point
			 * value like .5 (equivalent to 0.5). If the
			 * latter is the case, call get_num_literal(),
			 * else fall through
			 */
			if ((tmpi = FGETC(inf)) == EOF) {
				lexerror("Unexpected end of file.");
				return 1;
			}
			UNGETC(tmpi, inf);
			if (isdigit((unsigned char)tmpi)) {
				struct num	*n = get_num_literal(ch, inf);

				if (n != NULL) {
					if (g_recording_tokens) {
						store_token(&toklist,
							&toklist_tail,
							n->value,
							n->type, lineno, n->ascii);
					} else {
					}	
				}
				break;
			}
			/* FALLTHRU */
		default:
			if (ch == '?') {
				int	trig;
				/* Might be trigraph */
				if ((trig = get_trigraph(inf)) == -1) {
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
					UNGETC(trig, inf);
					break;
				}
			}
					
do_operator:				
			if (LOOKUP_OP(ch)) {
				int		*ptri = malloc(sizeof(int));
/*				struct operator	*opp;*/
				char		*opname;

				if (ptri == NULL) {
					perror("malloc");
					exit(EXIT_FAILURE);
				}
				tmpi = get_operator(ch, inf, &opname /*&opp*/);
				if (tmpi == -1) {
					lexerror("INVALID OPERATOR!!!");
					break;
				}
				
				if (g_recording_tokens) {
					*ptri = tmpi;
					store_token(&toklist,
						&toklist_tail,
						ptri,
						TOK_OPERATOR, lineno, NULL);
				} else {
					x_fprintf(out, "%s",
						opname /*opp->name*/); 
				}	
			} else if (isdigit((unsigned char)ch)) {
				struct num	*n = get_num_literal(ch, inf);

				if (n != NULL) {
					if (g_recording_tokens) {
						store_token(&toklist,
							&toklist_tail,
							n->value,
							n->type, lineno, n->ascii);
					} else {
						x_fprintf(out, "%s", n->ascii);
					}	
				} else {
					lexerror("Couldn't read numeric literal");
				}
			} else if (isalpha((unsigned char)ch) || ch == '_') {
				struct macro		*mp;
				struct macro_arg	*ma = NULL;
				int			slen;
				int			hash_key;

				if (ch == 'L') {
					int	tmpch;

					tmpch = FGETC(inf);
					if (tmpch != EOF) {
						UNGETC(tmpch, inf);
						if (tmpch == '\'' || tmpch == '"') {
							/*
							 * Long constant - treat like
							 * ordinary one
							 */
							break;
						}
					}
				}
				tmpc = get_identifier(ch, inf, &slen, &hash_key);
					
				if (tmpc == NULL) {
					break;
				}	

				if (g_recording_tokens
					&& dir.code == CMD_DEFINE
					&& ((struct macro *)dir.data)->
						functionlike) {
					/*
					 * Macro definition tokens are only
					 * subject to macro substitution when
					 * the macro is instantiated, thus;
					 * #define x lol
					 * #define foo() x
					 * #undef x
					 * foo()
					 * ... must yield x rather than lol
					 */
					mp = dir.data;
					for (ma = mp->arglist;
						ma != NULL;
						ma = ma->next) {
						if (strcmp(ma->name, tmpc)
							== 0) {
							break;
						}
					}	
				} else if (substitute_macros
					&& (mp = lookup_macro(tmpc,
						slen, hash_key)) != NULL
					/* && !mp->dontexpand maybe?!?!? */ ) {
					toklist = NULL;
					store_token(&toklist,
						&toklist_tail,
						tmpc,
						TOK_IDENTIFIER, lineno, NULL);

					if (mp->functionlike) {
						/*
						 * We have to wait until a (
						 * comes along
						 */
						maybe_funclike = 1;
						g_recording_tokens = 1;
					} else {
						toklist = do_macro_subst(inf, out,
							toklist, &toklist_tail,
							0);
						if (toklist != NULL) {
							doing_funclike = 1;
							parentheses =
								collect_parens;
						}
					}	
					if (doing_funclike) {
						
						/*
						 * Don't process nested macros
						 * just yet
						 */
						substitute_macros = 0;
						g_recording_tokens = 1;
					}	
					break;
				}	

				if (g_recording_tokens) {
					t = store_token(&toklist,
						&toklist_tail,
						tmpc,
						TOK_IDENTIFIER, lineno, NULL);
					if (ma != NULL) {
						t->maps_to_arg = ma;
					}
					if (t->type == TOK_IDENTIFIER) {
						/*
						 * Store length and hash
						 * key
						 */
						t->slen = slen;
						t->hashkey = hash_key;
					}	
				} else {
					x_fprintf(out, "%s", tmpc);
				}	
			} else {
				printf("LOOKUP_OP(%d) = %d\n",
					ch, LOOKUP_OP(ch));	
				lexerror("Unknown token - %c (code %d)\n", ch, ch);
			}
		}
		first_byte = 0;
		prevch = ch;			

		/*
		 * Check whether the file ends here in order to make last lines
		 * without newline character work
		 */
		if ((ch = FGETC(inf)) == EOF) {
			if (prevch != '\n') {
				UNGETC(ch, inf);
			}
		} else {
			UNGETC(ch, inf);
		}	
	}
	if (has_data) {
		if (current_include != NULL) {
			current_include->fully_guarded = 0;
		}
	}	
#if 0
			puts("nope, not fully guarded");
		} else {
			puts("hahha lol");
		}	
	} else if (current_include && current_include->fully_guarded) {
		printf("%s IS FULLY GUARDED!!!!!!!!!!!!\n",
			current_include->name);
	}	
#endif
#if 0
	store_token(&toklist, NULL, 0, lineno);
#endif
	return errors;
}


#ifdef DEBUG
static void 
print_token_list(struct token *list) {
	(void)list;
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
}
#endif

