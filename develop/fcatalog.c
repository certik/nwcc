#include "fcatalog.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "n_libc.h"
#include "token.h"
#include "attribute.h"
#include "error.h"
#include "lex.h"
#include "decl.h"
#include "reg.h"
#include "scope.h"
#include "type.h"

/*
 * 07/26/09: This file handles the new catalog of known standard C library
 * functions, which can be used to obtain type information about undeclared
 * functions.
 *
 * The file ``fcatalog'' contains the function lists for the known
 * industry standards (actually, only C89 as of July 2009; C99 and UNIX
 * should be added later).
 *
 * By executing
 *
 *    ./nwcc -write-fcat
 *
 * ... nwcc takes the ``fcatalog'' file and writes an indexed
 * ``fcatalog.idx'' file, from which lookup can take place during
 * subsequent nwcc invocations.
 *
 * The purpose of the indexed file is to speed up things a bit by reading
 * in only those declarations which are actually referenced by the compiled
 * program. There is a hash table to look up the entry of interest faster.
 *
 * XXX This may be a ``premature optimization''! Currently we could just
 * parse the entire file at every startup, like an implicit include file.
 * However, the file may grow to thousands of functions that should
 * probably not have to be parsed all the time, hence the desire to use
 * a preprocessed format of some sort.
 */

int	doing_fcatalog;


struct fcat_hash_entry {
	char			*name;
	char			offset[16];
	struct fcat_hash_entry	*next;
};

struct fcat_data_entry {
	char			*text;
	struct fcat_data_entry	*next;
};

static unsigned long fcat_data_offset;




#define H_TABSIZE	128

static struct fcat_hash_entry	*hash_table_head[H_TABSIZE];
static struct fcat_hash_entry	*hash_table_tail[H_TABSIZE];
unsigned long			hash_table_slot_length[H_TABSIZE];

static struct fcat_data_entry	*data_list_head;
static struct fcat_data_entry	*data_list_tail;

static int
fcat_hash_name(const char *name) {
	int	key = 0;

	for (; *name != 0; ++name) {
		key = (*name + 33 * key) & (H_TABSIZE - 1);
	}
	return key & (H_TABSIZE - 1);
}

int
fcat_write_index_file(const char *dest, const char *src) {
	FILE		*srcfd;
	FILE		*destfd;
	struct token	*t;
	int		err = 0;
	struct decl	**dec;
	struct token	*curstd = NULL;
	struct token	*curhead = NULL;
	unsigned long	lines[4096] = { 0 };
	int		i;
	char		buf[1024];
	unsigned long	count;
	fpos_t		offsetpos;

	if ((srcfd = fopen(src, "r")) == NULL) {
		perror(src);
		return -1;
	}

	i = 0;
	count = 0;
	while (fgets(buf, sizeof buf, srcfd) != NULL) {
		lines[i++] = count;
		count += strlen(buf);
	}
	rewind(srcfd);

	if ((destfd = fopen(dest, "w")) == NULL) {
		perror(dest);
		(void) fclose(srcfd);
		return -1;
	}

	if (lex_nwcc(create_input_file(srcfd)) != 0) {
		(void) fprintf(stderr, "ERROR: Cannot lex function catalogue file\n");
		return -1;
	}

	doing_fcatalog = 1;

	for (t = toklist; t != NULL; t = t->next) {
		if (t->type == TOK_IDENTIFIER && curhead == NULL) { /* Not typedef */
			if (curstd == NULL) {
				/* A new standard definition */
				curstd = t;
				if (expect_token(&t, TOK_COMP_OPEN, 0) != 0) {
					err = 1;
					break;
				}
			} else if (curhead == NULL) {
				/* A new header */
				curhead = t;
				if (expect_token(&t, TOK_OP_STRUMEMB, 0) != 0) {
					err = 1;
					break;
				}
				/* XXX assume they all end with .h */
				if (expect_token(&t, TOK_IDENTIFIER, 0) != 0) {
					err = 1;
					break;
				}
				/* XXX assume they all end with .h */
				if (expect_token(&t, TOK_COMP_OPEN, 0) != 0) {
					err = 1;
					break;
				}
			}
		} else if (t->type == TOK_COMP_CLOSE) {
			if (curstd == NULL) {
				(void) fprintf(stderr, "%d: Unexpected closing brace\n",
						t->line);
				err = 1;
				break;
			}
			if (curhead != NULL) {
				curhead = NULL;
			} else {
				curstd = NULL;
			}
		} else {
			struct token		*sav;
			fpos_t			curpos;
			char			*str;
			struct fcat_data_entry	*dent;
			struct fcat_hash_entry	*hent;
			unsigned long		old_fcat_data_offset;
			int			key;

			if (curstd == NULL) {
				(void) fprintf(stderr, "%d: Unexpected "
					"token `%s'\n", t->line, t->ascii);
				err = 1;
				break;
			}

			/*
			 * Must be a declaration
			 */
			sav = t;

			new_scope(SCOPE_CODE);

			dec = parse_decl(&t, DECL_NOINIT);
			if (dec == NULL) {
				(void) fprintf(stderr, "%d: Cannot parse "
				"declaration at '%s'\n", t->line, t->ascii);
				err = 1;
				break;
			}

			/* Read the line containing the declaration */
			fgetpos(srcfd, &curpos);
			fseek(srcfd, lines[sav->line], SEEK_SET);

			if (fgets(buf, sizeof buf, srcfd) != NULL) {
			/*	printf(" on line '%s", buf);*/
			}
			fsetpos(srcfd, &curpos);

			/*
			 * Create a new data entry for this declaration
			 * The format is:
			 * standard,standard2,...;header;decl
			 */
			(void) strtok(buf, "\n");
			str = n_xmalloc(strlen(buf) + strlen(curhead->data) + 128);
			sprintf(str, "%s;%s.h;%s;%s",
				(char *)curstd->data,
				(char *)curhead->data,
				dec[0]->dtype->name,
				buf);

			dent = n_xmalloc(sizeof *dent);
			dent->text = str;

			old_fcat_data_offset = fcat_data_offset;
			fcat_data_offset += strlen(str) + 1;  /* +1 for newline */

			/* Append data entry */
			if (data_list_head == NULL) {
				data_list_head = data_list_tail = dent;
			} else {
				data_list_tail->next = dent;
				data_list_tail = dent;
			}

			/* Create hash entry */
			hent = n_xmalloc(sizeof *hent);
			hent->name = dec[0]->dtype->name;
			sprintf(hent->offset, "%lu", old_fcat_data_offset);

			key = fcat_hash_name(hent->name);

			if (hash_table_head[key] == NULL) {
				hash_table_head[key] = hash_table_tail[key] = hent;
			} else {
				hash_table_tail[key]->next = hent;
				hash_table_tail[key] = hent;
			}

			hash_table_slot_length[key] += strlen(hent->name)
					+ strlen(" \n")
					+ strlen(hent->offset);
		}
	}

	/*
	 * The slot length array now contains the length of each slot; Turn
	 * that into a total offset (i.e. the sum of all preceding elements
	 * is added to the length of each entry)
	 */
	for (i = 0; i < H_TABSIZE; ++i) {
		if (i > 0) {
			hash_table_slot_length[i] += hash_table_slot_length[i - 1];
		}
	}


	/*
	 * Now write all data. First comes the slot length header which
	 * describes the size of the hash table slots.
	 */
	for (i = 0; i < H_TABSIZE; ++i) {
		fprintf(destfd, "%ld", hash_table_slot_length[i]);
		if (i + 1 < H_TABSIZE) {
			fprintf(destfd, ",");
		} else {
			fprintf(destfd, "\n");
		}
	}

	/*
	 * ... followed by the data start index. We first write an
	 * ``empty'' line of spaces, and overwrite it with the real
	 * offset when it is known
	 */
	fgetpos(destfd, &offsetpos);
	fprintf(destfd, "          \n");

	/*
	 * ... followed by the hash slots containing the data pointers
	 */
	for (i = 0; i < H_TABSIZE; ++i) {
		struct fcat_hash_entry	*e;

		for (e = hash_table_head[i]; e != NULL; e = e->next) {
			fprintf(destfd, "%s;%s\n", e->name, e->offset);
		}
	}

	/*
	 * ... followed by the actual data entries
	 */
	{
		struct fcat_data_entry	*d;
		fpos_t			curpos;
		long			bytes_written;

		fgetpos(destfd, &curpos);
		bytes_written = ftell(destfd);

		fsetpos(destfd, &offsetpos);
		fprintf(destfd, "%ld", bytes_written);
		fsetpos(destfd, &curpos);

		for (d = data_list_head; d != NULL; d = d->next) {
			fprintf(destfd, "%s\n", d->text);
		}
	}

	(void) fclose(srcfd);
	(void) fclose(destfd);

	/* XXX return err? */
	(void) err;
	return 0;
}

struct type *
fcat_get_dummy_typedef(const char *name) {
	if (strcmp(name, "size_t") == 0) {
		return make_basic_type(TY_ULONG);
	} else if (strcmp(name, "ssize_t") == 0) {
		return make_basic_type(TY_LONG);
	} else if (strcmp(name, "FILE") == 0) {
		return make_basic_type(TY_VOID); /* XXX */
	} else if (strcmp(name, "va_list") == 0) {
		return make_void_ptr_type();
	} else if (strcmp(name, "fpos_t") == 0) {
		return make_basic_type(TY_LONG); /* XXX */
	} else if (strcmp(name, "time_t") == 0) {
		return make_basic_type(TY_LONG); /* XXX */
	} else if (strcmp(name, "clock_t") == 0) {
		return make_basic_type(TY_LONG); /* XXX */
	}
	return NULL;
}


static char	*idx_map;
static long	idx_slot_offsets[H_TABSIZE];
static char	*hash_slot_start;
static char	*data_area_start;

int
fcat_open_index_file(const char *path) {
	int		fd;
	char		*p2;
	char		*curpos;
	unsigned long	idx_data_area_offset;
	int		i;
	struct stat	sbuf;


	if ((fd = open(path, O_RDONLY)) == -1) {
		return -1;
	}
	if (fstat(fd, &sbuf) == -1) {
		perror("fstat");
		close(fd);
		return -1;
	}

	/*
	 * Make a private copy so we are not sensitive to changes to the
	 * underlying file, and make that copy writable so we can temporarily
	 * change things (e.g. 0-terminate partial lines to use them as
	 * strings)
	 */
	idx_map = mmap(0, sbuf.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

	if (idx_map == MAP_FAILED) {
		perror("mmap");
		close(fd);
		return -1;
	}

	close(fd);

	/* Read slot offsets */
	curpos = idx_map;
	i = 0;
	do {
		idx_slot_offsets[i++] = strtol(curpos, &p2, 10);
		curpos = p2+1;
	} while (*p2 != '\n');

	/* Read data area offset */
	idx_data_area_offset = strtol(curpos, &p2, 10);
	if (*p2 != '\n' && *p2 != ' ') {
		(void) fprintf(stderr, "ERROR: Malformed data offset in fcatalog.idx\n");
		return -1;
	}

	curpos = strchr(p2, '\n');

#if 0

	curpos = strchr(curpos+1, '\n')+1;
#endif
	while (!isalpha((unsigned char)*curpos)) {
		++curpos;
	}

	hash_slot_start = curpos;

	data_area_start = idx_map + idx_data_area_offset;
	return 0;
}


struct decl *
fcat_lookup_builtin_decl(const char *name, char **header, int standard) {
	int		key;
	char		*slot_ptr;
	char		*end_ptr;
	long		found_offset = -1;
	static char	headbuf[128];

	(void) standard;
	if (idx_map == NULL) {
		return NULL;
	}

	if (strncmp(name, "__builtin_", sizeof "__builtin_" - 1) == 0) {
		name += sizeof "__builtin_" - 1;
	}

	key = fcat_hash_name(name);

	if (key == 0) {
		/* Is first entry */
		slot_ptr = hash_slot_start;
		end_ptr = slot_ptr + idx_slot_offsets[0];
	} else {
		/* After end of last entry */
		slot_ptr = hash_slot_start + idx_slot_offsets[key - 1];
		end_ptr = slot_ptr + (idx_slot_offsets[key] - idx_slot_offsets[key - 1]);
	}

	while (slot_ptr < end_ptr) {
		char	*offset;

		offset = strchr(slot_ptr, ';');
		if (offset != NULL) {
			*offset = 0;
			if (strcmp(slot_ptr, name) == 0) {
				/* Found */
				found_offset = strtol(offset+1, NULL, 10);
				*offset = ';';
				break;
			}
			*offset = ';';
		}

		slot_ptr = strchr(slot_ptr, '\n');
		if (slot_ptr == NULL) {
			break;
		} else {
			++slot_ptr;
		}
	}

	if (found_offset != -1) {
		char	*data_ptr = data_area_start + found_offset;
		char	std[64];
		char	head[64];
		char	name[64];
		char	decl[128];

		if (sscanf(data_ptr, "%63[^;];%63[^;];%63[^;];%127[^\n]",
				std, head, name, decl) != 4) {
			char	*temp;

			if ((temp = strchr(data_ptr, '\n')) != NULL) {
				*temp = 0;
			}
			(void) fprintf(stderr, "ERROR: Malformed declaration "
				"line `%s'", data_ptr);
			if (temp != NULL) {
				*temp = '\n';
			}
		} else {
			struct token	*old_toklist = toklist;
			struct token	*newtok;
			int		rc;
			char		fname[128];
			FILE		*fd;
			struct decl	**dec;

			toklist = NULL;

			strncpy(headbuf, head, sizeof headbuf - 1);
			headbuf[sizeof headbuf - 1] = 0;
			*header = headbuf;


			fd = get_tmp_file("dummy", fname, "tmp");
			fprintf(fd, "%s\n", decl);
			rewind(fd);


			doing_fcatalog = 1;
			rc = lex_nwcc(create_input_file(fd));

			unlink(fname);

			newtok = toklist;

			toklist = old_toklist;

			if (rc == 0) {
				dec = parse_decl(&newtok, DECL_NOINIT);
				if (dec == NULL) {
					(void) fprintf(stderr, "%d: Cannot parse "
					"declaration at '%s'\n", newtok->line, newtok->ascii);
				}
			}

			doing_fcatalog = 0;

			if (dec != NULL) {
				return dec[0];
			} else {
				return NULL;
			}

		}
	}
	return NULL;
}


static int
get_type_by_fmt(int ch, int ch2, int ch3) {
	switch (ch) {
	case 'c': return TY_INT;
	case 'd': return TY_INT;
	case 'i': return TY_INT;
	case 'u': return TY_UINT;
	case 'x': return TY_UINT;
	case 'o': return TY_UINT;
	case 'l':
		switch (ch2) {
		case 'd': return TY_LONG;
		case 'u': return TY_ULONG;
		case 'f': return TY_DOUBLE;
		case 'l':
			switch (ch3) {
			case 'u': return TY_ULLONG;
			case 'd': return TY_LLONG;
			}
		}
		break;
	case 'f':
		return TY_DOUBLE;
	case 'L':
		switch (ch2) {
		case 'f': return TY_LDOUBLE;
		}
	}
	return 0;
}


void
check_format_string(struct token *tok,
		struct type *fty,
		struct ty_func *fdecl,
		struct vreg **args,
		struct token **from_consts,
		int nargs) {
	
	struct attrib		*a;
	struct ty_string	*ts;
	int			i;
	int			fmtidx;
	int			checkidx;
	int			cur_checkidx;

	a = lookup_attr(fty->attributes, ATTRF_FORMAT);
	assert(a != NULL);

	fmtidx = a->iarg2;
	checkidx = a->iarg3;

	if (fdecl->nargs == -1) {
		/* Function takes no parameters?! */
		return;
	}
	if (fmtidx + 1 > nargs) {
		/* No format string passed?! */
		return;
	}
	if (checkidx + 1 > nargs) {
		/* No arguments to check */
		return;
	}
	cur_checkidx = checkidx;

	if (from_consts[fmtidx] == NULL) {
		/*
		 * Format string isn't a string constant so we can't
		 * check it
		 */
		return;
	}

	ts = from_consts[fmtidx]->data;
	for (i = 0; i < (int)ts->size; ++i) {
		if (ts->str[i] == '%') {
			if (i + 1 == (int)ts->size) {
				warningfl(tok, "Invalid trailing `%%' char "
					"without conversion specifier");
			} else if (ts->str[i + 1] == '%') {
				/* % char */
				++i;
			} else {
				struct type	*passed_ty;
				int		ch = 0;
				char		*p = NULL;

				if (cur_checkidx + 1 > nargs) {
					warningfl(tok, "Format specifier "
						"%d has no corresponding "
						"argument!",
						cur_checkidx - checkidx);
					++cur_checkidx;
					continue;
				}

				passed_ty = args[cur_checkidx]->type;

				/*
				 * We only handle simple obvious cases for
				 * now, i.e. %s vs int argument, %d vs
				 * string argument, etc.
				 */
				if (ts->str[i + 1] == 'l') {
					if (i + 2 == (int)ts->size) {
						warningfl(tok, "Incomplete "
							"conversion specifier "
							"`%%l'");
					} else if (ts->str[i + 2] == 'l') {
						if (i + 3 == (int)ts->size) {
							warningfl(tok, "Incomplete "
								"conversion specifier "
								"`%%ll'");
						} else {
							ch = get_type_by_fmt(
								ts->str[i+1],
								ts->str[i+2],
								ts->str[i+3]);
						}
					} else {
						ch = get_type_by_fmt(
							ts->str[i+1],
							ts->str[i+2],
							0);
					}
				} else {
					ch = get_type_by_fmt(ts->str[i+1], 0, 0);
				}


				switch (ch) {
				case TY_INT:
				case TY_UINT:
				case TY_LONG:
				case TY_ULONG:
				case TY_LLONG:
				case TY_ULLONG:
					if (!is_integral_type(passed_ty)) {
						p = type_to_text(passed_ty);
						warningfl(tok, "Format "
						"specifier %d expects integral "
						"type, but received argument "
						"of type `%s'",
						cur_checkidx-checkidx+1,
						p);
					} else {
						static struct type dummy;
						char			*p2;

						/* OK, both are integral */
						if ( (IS_INT(ch)
							&& IS_LONG(passed_ty->code)) 
							|| (IS_LONG(ch)
							&& IS_INT(passed_ty->code))) {
							static int warned;
							dummy.code = ch;
							p = type_to_text(passed_ty);
							p2 = type_to_text(&dummy);
							/*
							 * Keep the number of
							 * warnings down because
							 * this can happen lots
							 * of times with %d vs
							 * size_t; That will
							 * work on all supported
							 * systems, and a user
							 * who cares will pay
							 * attention to 1 warning
							 * just as well
							 */
							if (!warned) {
								warningfl(tok,
"Format specifier %d expects argument of type `%s', but received `%s'",
cur_checkidx-checkidx+1,
p2, p);
								warned = 1;
							}
							free(p2);
						} else if (IS_LLONG(ch)
							!= IS_LLONG(passed_ty->code)) {
							dummy.code = ch;
							p = type_to_text(passed_ty);
							p2 = type_to_text(&dummy);
warningfl(tok, "Format specifier %d expects argument of type `%s', but received `%s'",
		cur_checkidx-checkidx+1,
		p2, p);
							free(p2);
						}
					}
					break;
				case TY_DOUBLE:
				case TY_LDOUBLE:
					if (!is_floating_type(passed_ty)) {
						p = type_to_text(passed_ty);
						warningfl(tok, "Format "
						"specifier %d expects floating "
						"point type, but received "
						"argument of type `%s'",
						cur_checkidx-checkidx+1,
						p);
					} else if (passed_ty->code != ch) {
						p = type_to_text(passed_ty);
						warningfl(tok, "Format "
						"specifier %d expects type "
						"`%s', but received argument "
						"of type `%s'",
						cur_checkidx-checkidx+1,
						ch == TY_DOUBLE? "double":
							"long double",
						p);
					}
					break;
				default:
					if (ts->str[i+1] == 's') {
						if (passed_ty->tlist == NULL
							|| (passed_ty->tlist->type != TN_ARRAY_OF
							&& passed_ty->tlist->type != TN_POINTER_TO)
							|| passed_ty->tlist->next != NULL
							|| passed_ty->code != TY_CHAR
							|| passed_ty->code != TY_VOID) {
							p = type_to_text(passed_ty);
							warningfl(tok, "Format "
							"specifier %d expects string, "
							"but received argument of type "
							"`%s'",
							cur_checkidx-checkidx+1,
							p);
						}
					}
					break;
				}
				if (p != NULL) {
					free(p);
				}
				++cur_checkidx;
			}
		}
	}
}

