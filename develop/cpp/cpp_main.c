#include "n_libc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "misc.h"
#include "error.h"
#include "defs.h"
#include "typemap.h"
#include "archdefs.h"
#include "standards.h"
#include "system.h"
#include "preprocess.h"

static struct include_dir	*includes_tail = NULL;

int	archflag;
int	abiflag;
int	sysflag;
int	is_64bit;

int	nostdincflag;
int	funsignedchar_flag;
int	fsignedchar_flag;

char	*std_flag;

int	dump_macros_flag;

static struct include_dir *
make_inc_dir(const char *path) {
	struct include_dir		*ret;
	static struct include_dir	nullinc;

	ret = n_xmalloc(sizeof *ret);
	*ret = nullinc;
	ret->path = (char *)path;
	return ret;
}	

static struct input_file *
tmp_in_file(FILE *fd, char *name) {
	static struct input_file	in;

	in.fd = fd;
	in.path = name;
	in.is_cmdline = 1;
	return &in;
}

static struct predef_macro *
tmp_pre_macro(const char *name, const char *text) {
	static struct predef_macro	pm;
	struct predef_macro		*ret = n_xmalloc(sizeof *ret);

	*ret = pm;
	ret->name = n_xstrdup(name);
	ret->text = n_xstrdup(text);
	return ret;
}

static void
remove_predef_macro(struct predef_macro **macros,
		struct predef_macro **macros_tail,
		struct predef_macro *last_user_macro,
		const char *name) {

	struct predef_macro	*m;
	struct predef_macro	*prev = NULL;
	int			was_tail = 0;

	for (m = *macros; m != NULL; m = m->next) {
		if (last_user_macro != NULL) {
			if (m == last_user_macro) {
				last_user_macro = NULL;
			}
			prev = m;
			continue;
		}
		if (strcmp(m->name, name) == 0) {
			if (*macros == *macros_tail) {
				*macros_tail = NULL;
			}
			if (m == *macros_tail) {
				was_tail = 1;
			}
			if (m == *macros) {
				*macros = (*macros)->next;
			} else {
				prev->next = m->next;
			}
			prev = m->next;
			free(m);
		} else {
			if (m->next == NULL) {
				if (was_tail) {
					*macros_tail = prev;
				}
			}
			prev = m;
		}
	}
}

static void
usage(void) {
	puts("You invoked nwcpp incorrectly. Please refer to the README file");
	puts("for supported command line arguments");
	exit(EXIT_FAILURE);
}


/*
 * XXX The macros passed to the program are totally incomplete. In particular
 * there's usually only one operating system/architecture macro, such that
 * e.g. __amd64__ is defined but __amd64 isn't. This SUCKS!!
 */
int
main(int argc, char **argv) {
	struct ga_option	options[] = {
		{ 'D', NULL, 1 },
		{ 'U', NULL, 1 },
		{ 'I', NULL, 1 },
#ifdef _AIX
		{ 0, "maix64", 0 },
#endif
		{ 0, "arch", 1 },
		{ 0, "abi", 1 },
		{ 0, "mabi", 1 },
		{ 0, "sys", 1 },
		{ 0, "std", 1 },
		{ 0, "ansi", 0 },
		{ 0, "dM", 0 },
		{ 0, "nostdinc", 0 },
		{ 0, "fsigned-char", 0 },
		{ 0, "funsigned-char", 0 }
	};
	int			n_opts = sizeof options / sizeof options[0];
	int			ch;
	int			idx;
	int			rc = 0;
	int			have_gnu = 0;
	char			buf[1024];
	char			*arch_str = NULL;
	char			*abi_str = NULL;
	char			*sys_str = NULL;
	struct stat		sbuf;
	struct include_dir	*tmp_inc;
	struct predef_macro	*predef = NULL;
	struct predef_macro	*predef_tail = NULL;
	struct predef_macro	*tmp_pre;
	struct input_file	*files = NULL;
	struct input_file	*files_tail = NULL;
	struct input_file	*tmp_fil;
	struct predef_macro	*last_user_macro = NULL;
static struct predef_macro	nullpre;	
static struct input_file	nullinput;
	char			*p;
	char			**undefed = NULL;
	size_t			undefed_used = 0;
	size_t			undefed_alloc = 0;
	FILE			*predef_file = NULL;

	while ((ch = nw_get_arg(argc, argv, options, n_opts, &idx)) != -1) {
		switch (ch) {
		case 'D':
			tmp_pre = n_xmalloc(sizeof *tmp_pre);
			*tmp_pre = nullpre;
			tmp_pre->name = n_xstrdup(n_optarg);
			if (strcmp(n_optarg, "__GNUC__") == 0) {
				have_gnu = 1;
			}	
			if ((p = strchr(tmp_pre->name, '=')) != NULL) {
				*p = 0;
				tmp_pre->text = ++p;
			}
			APPEND_LIST(predef, predef_tail, tmp_pre);
			last_user_macro = tmp_pre;
			break;
		case 'U':
			remove_predef_macro(&predef, &predef_tail, NULL, n_optarg);
			if (undefed_used >= undefed_alloc) {
				undefed_alloc *= 2;
				undefed = n_xrealloc(undefed,
						undefed_alloc * sizeof *undefed);
			}
			undefed[undefed_used++] = n_xstrdup(n_optarg);
			break;
		case 'I':
			tmp_inc = make_inc_dir(n_xstrdup(n_optarg)); 
			APPEND_LIST(include_dirs, includes_tail, tmp_inc);
			break;
		case '!':	
			tmp_fil = n_xmalloc(sizeof *tmp_fil);
			*tmp_fil = nullinput;
			tmp_fil->path = n_xstrdup(n_optarg);
#if 0
			if ((tmp_fil->fd = fopen(n_optarg, "r")) == NULL) {
#endif
			if (open_input_file(tmp_fil, n_optarg, 1) != 0) {	
				perror(n_optarg);
				return EXIT_FAILURE;
			}
			APPEND_LIST(files, files_tail, tmp_fil);
			break;
		case '?':
			if (idx) { 
				if (strcmp(options[idx].name, "arch") == 0) {
					arch_str = n_xstrdup(n_optarg);
				} else if (strcmp(options[idx].name, "abi")
					== 0
					|| strcmp(options[idx].name, "mabi")
					== 0) {
					abi_str = n_xstrdup(n_optarg);
				} else if (strcmp(options[idx].name, "sys")
					== 0) {
					sys_str = n_xstrdup(n_optarg);
				} else if (strcmp(options[idx].name, "nostdinc")
					== 0) {	
					nostdincflag = 1;
				} else if (strcmp(options[idx].name, "maix64")
					== 0) {
					abi_str = "aix64";
				} else if (strcmp(options[idx].name, "dM")
					== 0) {
					dump_macros_flag = 1;
				} else if (strcmp(options[idx].name, "fsigned-char")
					== 0) {
					if (funsignedchar_flag != 0) {
						(void) fprintf(stderr, "Ignoring "
							"preceding -funsigned-char "
							"flag\n");
					}
					fsignedchar_flag = 1;
				} else if (strcmp(options[idx].name, "funsigned-char")
					== 0) {
					if (fsignedchar_flag != 0) {
						(void) fprintf(stderr, "Ignoring "
							"preceding -fsigned-char "
							"flag\n");
					}
					funsignedchar_flag = 1;
				} else if (strcmp(options[idx].name, "ansi")
					== 0) {
					stdflag = option_to_std("c89");
                                } else if (strcmp(options[idx].name, "std")
					== 0) {
					if (strlen(n_optarg) > strlen("gnu99")) {
						(void) fprintf(stderr, "Warning: "
							"Unknown standard "
							"requested, using "
							"default\n");
						break;
					}
					if (std_flag != NULL
						&& strcmp(std_flag, n_optarg) != 0) {
						(void) fprintf(stderr, "Warning: "
							"Multiple standards "
							"requested per -ansi "
							"or -std (-ansi means "
							"the same as -std=c89). "
							"Last option wins.\n");
						free(std_flag);
					} else if (std_flag != NULL) {
						free(std_flag);
					}
					stdflag = option_to_std(n_optarg);
				} else {
					usage();
				}	
			} else {
				usage();
			}	
			break;
		default:
			usage();
		}
	}

	if (!have_gnu) {
		/*
		 * __GNUC__=2 or higher requires us to define things like
		 * limits.h constants ourselves on Linux, which is
		 * undesirable at this point. Unfortunately the defective
		 * FreeBSD garbage headers cannot deal with older versions,
		 * so we set the version to 2.95 if we're not on Linux
		 *
		 *
		 */
		if (sysflag == OS_LINUX) { 
			/* XXX hmm set __GNUC_MINOR__ to what!? */
			tmp_pre = tmp_pre_macro("__GNUC__", "1");
			APPEND_LIST(predef, predef_tail, tmp_pre);
		} else {
			tmp_pre = tmp_pre_macro("__GNUC__", "2");
			APPEND_LIST(predef, predef_tail, tmp_pre);
			tmp_pre = tmp_pre_macro("__GNUC_MINOR__", "95");
			APPEND_LIST(predef, predef_tail, tmp_pre);
		}
	}

	set_target_arch_and_abi_and_sys(&archflag, &abiflag, &sysflag, arch_str, abi_str, sys_str);

	if (arch_str != NULL) {
		/* Are we generating for the host? */
		int	hostarch;
		int	hostabi;
		int	hostsys;

		get_host_arch(&hostarch, &hostabi, &hostsys);
		if (archflag == hostarch) {
			arch_str = NULL;
		}
	}



	if (stdflag == ISTD_NONE) {
		stdflag = set_default_std();
	}

	/*
	 * 05/18/09: Better system macro handling, put into system.c now! Takes
	 * sysflag into account
	 */
	set_system_macros(&predef, &predef_tail, archflag, abiflag, sysflag);
	set_type_macros(&predef, &predef_tail, archflag, abiflag, sysflag);

	if (stat("/usr/local/nwcc/include", &sbuf) == 0) {
		p = "/usr/local/nwcc/include";
	} else {
		static char	cwdbuf[2048];

		if (getcwd(cwdbuf, sizeof cwdbuf - sizeof "/include") == NULL) {
			perror("getcwd");
			return EXIT_FAILURE;
		}
		strcat(cwdbuf, "/include");
		if (stat(cwdbuf, &sbuf) == 0) {
			p = cwdbuf;
		} else if ((p = getenv("NWCC_CPP")) != NULL) {
			char	*p2;

			if ((p2 = strrchr(p, '/')) != NULL) {
				*cwdbuf = 0;
#define MIN__VALUE(x, y) \
	((x) > (y)? (y): (x))
				strncat(cwdbuf, p,
					MIN__VALUE(p2 - p,
						(int)(sizeof cwdbuf
							- sizeof "/include")));
				strcat(cwdbuf, "/include");
				p = cwdbuf;
			} else {
				p = NULL;
			}
		}	
	}	

	if (p == NULL) {
		(void) fprintf(stderr, "Warning: Cannot determine private "
			"include file directory\n");
	} else {
		tmp_inc = make_inc_dir(n_xstrdup(p));
		APPEND_LIST(include_dirs, includes_tail, tmp_inc);
	}	

	if (!nostdincflag) {
		/* Set default include directory (comes last, as it should!) */
		tmp_inc = make_inc_dir(n_xstrdup("/usr/include")); 
		APPEND_LIST(include_dirs, includes_tail, tmp_inc);
	}	
	if (funsignedchar_flag) {
		/* 
		 * OpenBSD:__machine_has_unsigned_chars
		 * FreeBSD: __CHAR_UNSIGNED__
		 * Solaris: _CHAR_IS_UNSIGNED / _CHAR_IS_SIGNED
		 * IRIX/OSX/AIX: ....?
		 * XXX complete this
		 * XXX we just use all forms for now regardless of the
		 * target system
		 */
		tmp_pre = tmp_pre_macro("__CHAR_UNSIGNED__", "1");
		APPEND_LIST(predef, predef_tail, tmp_pre);
		tmp_pre = tmp_pre_macro("_CHAR_IS_UNSIGNED", "1");
		APPEND_LIST(predef, predef_tail, tmp_pre);
		tmp_pre = tmp_pre_macro("__machine_has_unsigned_chars", "1");
		APPEND_LIST(predef, predef_tail, tmp_pre);
	} else {
		tmp_pre = tmp_pre_macro("_CHAR_IS_SIGNED", "1");
		APPEND_LIST(predef, predef_tail, tmp_pre);
	}
		


	if (predef != NULL) {
		/*
		 * It is possible - though probably rare - to have complex
		 * definitions on the command line, such as
		 * -Dfoo='do { puts("lol"); } while (0)'
		 * ... so the predefined macros are supplied in a file that
		 * can be processed just like normal macro defintions
		 */
		int	i;

		for (i = 0; i < (int)undefed_used; ++i) {
			remove_predef_macro(&predef, &predef_tail,
				last_user_macro, undefed[i]);
		}

		if (dump_macros_flag) {
			predef_file = stdout;
		} else {
			if ((predef_file = get_tmp_file("", buf, "def")) == NULL) {
				return EXIT_FAILURE;
			}
		}

		while (predef != NULL) {
			struct predef_macro	*pm;

			if (fprintf(predef_file,
				predef->text? "#define %s %s\n"
					: "#define %s\n",
					predef->name, predef->text) < 0) {
				perror("fprintf");
				return EXIT_FAILURE;
			}
			pm = predef;
			predef = predef->next;
			free(pm->name);
			free(pm);
		}
		(void) fclose(predef_file);

		if (dump_macros_flag) {
			return EXIT_SUCCESS;
		}

		if ((predef_file = fopen(buf, "r")) == NULL) {
			perror(buf);
			return EXIT_FAILURE;
		}
		(void) unlink(buf);
	}

	init_oplookup();
	cross_initialize_type_map(archflag, abiflag, sysflag);

	if (files == NULL) {
		static struct input_file	in;
		int	ch;

		in.path = "<stdin>";
		in.fd = get_tmp_file("stdin", buf, "c");
		if (in.fd == NULL) {
			return EXIT_FAILURE;
		}
		while ((ch = getchar()) != EOF) {
			if (fputc(ch, in.fd) < 0) {
				perror("fputc");
				(void) unlink(buf);
				return EXIT_FAILURE;
			}
		}	
		if (fclose(in.fd) == EOF) {
			perror("fclose");
			(void) unlink(buf);
			return EXIT_FAILURE;
		}	
#if 0
		if ((in.fd = fopen(buf, "r")) == NULL) {
#endif
		if (open_input_file(&in, buf, 1) != 0) {	
			perror(buf);
			return EXIT_FAILURE;
		}

		(void) unlink(buf);
		if (predef_file) {
			rc |= preprocess(
				tmp_in_file(predef_file, "predef"), stdout);
		}	
		rc |= preprocess(&in, stdout);
		return errors != 0; /* XXX - rc ? */
	} else {
		for (; files != NULL; files = files->next) {
			if (predef_file) {
				(void) rewind(predef_file);
				rc |= preprocess(
					tmp_in_file(predef_file, "predef"),
					stdout);
			}	
			rc |= preprocess(files, stdout);
		}
	}
	return errors != 0? EXIT_FAILURE: 0; 
}

