#include "cfgfile.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <pwd.h>
#include <unistd.h>
#include "defs.h"
#include "n_libc.h"

static char * 
next_opt_str(char **p0) {
	char	*p = *p0;
	char	*start;

	while (isspace((unsigned char)*p)) {
		++p;
	}
	if (*p == 0) {
		return NULL;
	}
	start = p;
	do {
		++p;
	} while (*p != 0 && !isspace((unsigned char)*p));	
	if (*p != 0) {
		*p = 0;
		*p0 = p+1;
	} else {
		*p0 = p;
	}	
	return start;
}

struct cfg_option {
	char	*name;
	char	**argv;
	int	argc;
	struct opt_info	*info;
	struct cfg_option *next;
};

static struct cfg_option *
lookup_option(struct cfg_option *list, const char *name) {
	for (; list != NULL; list = list->next) {
		if (list->name && strcmp(list->name, name) == 0) {
			return list;
		}
	}
	return NULL;
}

#if 0
static void
print_option_list(struct cfg_option *opt) {
	int	i;

	for (; opt != NULL; opt = opt->next) {
		if (opt->name == NULL) {
			continue;
		}	
		printf("option %s:\n", opt->name);
		for (i = 0; i < opt->argc; ++i) {
			printf("\t%s\n", opt->argv[i]);
		}
	}	
}	
#endif

static void
free_option(struct cfg_option *opt) {
	free(opt->argv);
	free(opt->name);
	free(opt);
}	

static void
free_option_list(struct cfg_option *list) {
	while (list != NULL) {
		struct cfg_option	*tmp = list->next;
		int			i;

		for (i = 0; i < list->argc; ++i) {
			free(list->argv[i]);
		}	

		free(list->argv);
		free(list->name);
		list = tmp;
	}	
}	

#define OPT_OPTIONS	1
#define OPT_ARCH	2
#define OPT_ABI		3
#define OPT_ASM		4
#define OPT_GNUC	5
#define OPT_CPP		6

static struct opt_info {
	char	*name;
	int	code;
	int	minargs;
	int	maxargs;
	int	mergable;
	char	*env_override;
} known_options[] = {
	{ "options", OPT_OPTIONS, 1, 0, 1, NULL },
	{ "arch", OPT_ARCH, 1, 1, 0, "NWCC_ARCH" },
	{ "abi", OPT_ABI, 1, 1, 0, "NWCC_ABI" },
	{ "asm", OPT_ASM, 1, 1, 0, "NWCC_ASM" },
	{ "gnuc", OPT_GNUC, 1, 1, 0, "NWCC_GNU_VERSION" },
	{ "cpp", OPT_CPP, 1, 1, 0, "NWCC_CPP" },
	{ NULL, 0, 0, 0, 0, NULL }
};

static struct cfg_option *
do_read_config_file(const char *path, struct cfg_option *other) {
	FILE				*fd;
	char				buf[1024];
	char				*p;
	char				*p2;
	char				*option;
	int				lineno = 0;
	int				i;
	struct cfg_option		*newcfg;
	struct cfg_option		*tmpcfg;
	struct cfg_option		*ret = NULL;
	struct cfg_option		*ret_tail = NULL;
	static struct cfg_option	nullcfg;

	if ((fd = fopen(path, "r")) == NULL) {
		return NULL;
	}

	while (fgets(buf, sizeof buf, fd) != NULL) {
		if ((p = strchr(buf, '\n')) != NULL) {
			*p = 0;
		} else {
			int	ch;

			(void) fprintf(stderr,
				"%s:%d: Line too long - truncating\n",
				path, lineno);
			do {
				ch = fgetc(fd);
			} while (ch != '\n' && ch != EOF);	
		}
		++lineno;
		p = buf;
		while (isspace((unsigned char)*p)) {
			++p;
		}	
		if (*p == '#' || *p == 0) {
			/* Comment or empty line */
			continue;
		}
		option = p;
		while (isalpha((unsigned char)*p)) {
			++p;
		}
		if (!*p) {
			(void) fprintf(stderr, "%s:%d: Malformed "
				"line `%s'\n",
				path, lineno, buf);
			continue;
		}
		if (strncmp(option, "undef", 5) == 0
			&& !isalpha((unsigned char)option[5])) {
			/* Undefining existing option */
			*p++ = 0;
			if ((p2 = next_opt_str(&p)) != NULL) {
				if ((newcfg = lookup_option(other, p2)) ||
					(newcfg = lookup_option(ret, p2))) {
					free(newcfg->name);
					newcfg->name = NULL;
				}
			}
		} else {
			char	*startp = p;
			int	exists = 0;

			/* Defining new option */
			newcfg = n_xmalloc(sizeof *newcfg);
			*newcfg = nullcfg;

			while (isspace((unsigned char)*p)) {
				++p;
			}
			if (*p++ != '=') {
				/* Incomplete or malformed line - ignore */
				(void) fprintf(stderr, "%s:%d: Malformed "
					"line `%s'\n",
					path, lineno, buf);
				continue;
			}
			*startp = 0;
			if (other == NULL ||
				((tmpcfg=lookup_option(other,option)) == NULL
				&& (tmpcfg = lookup_option(ret,option))
				==NULL)) {
				/*
				 * Doesn't already exist
				 */
				newcfg->name = n_xstrdup(option);
			} else {
				/*
				 * We're overriding an existing definition
				 * from a previous configuration file -
				 * Are we dealing with an option that can
				 * be merged, or one that only allows a
				 * single definition?
				 */
				newcfg->name = tmpcfg->name;
				tmpcfg->name = NULL;
				if (tmpcfg->info->mergable) {
					exists = 1;
					newcfg->argv = tmpcfg->argv;
					newcfg->argc = tmpcfg->argc;
					tmpcfg->argv = NULL;
				}	
			}

			while ((p2 = next_opt_str(&p)) != NULL) {
				int	save_opt = 1;

				if (exists) {
					for (i = 0; i < newcfg->argc; ++i) {
						if (strcmp(newcfg->argv[i],
							p2) == 0) {
							/* Already in list */
							save_opt = 0;
							break;
						}
					}
				}
				if (save_opt) {
					++newcfg->argc;
					newcfg->argv = n_xrealloc(newcfg->argv,
						newcfg->argc *
						sizeof *newcfg->argv);
					newcfg->argv[newcfg->argc - 1] =
						n_xstrdup(p2);
				}
			}
			
			/*
			 * Now establish the option's identity and save it
			 * in the list
			 */
			for (i = 0; known_options[i].name != NULL; ++i) {
				if (strcmp(newcfg->name,
					known_options[i].name) == 0) {
					break;
				}
			}
			if (known_options[i].name == NULL) {
				/* Unknown option */
				(void) fprintf(stderr, "%s:%d: Unknown "
					"option `%s'\n",
					path, lineno, newcfg->name);
				free_option(newcfg);
				continue;
			}
			newcfg->info = &known_options[i];
			if (newcfg->info->maxargs != 0
				&& newcfg->argc > newcfg->info->maxargs) {
				(void) fprintf(stderr, "%s:%d: Too many "
					"arguments for `%s'\n",
					path, lineno, newcfg->name);
				free_option(newcfg);
				continue;
			} else if (newcfg->info->minargs != 0
				&& newcfg->argc < newcfg->info->minargs) {
				(void) fprintf(stderr, "%s:%d: Not "
					"enough arguments for `%s'\n",
					path, lineno, newcfg->name);
				free_option(newcfg);
				continue;
			}

			if (ret == NULL) {
				ret = ret_tail = newcfg;
			} else {
				ret_tail->next = newcfg;
				ret_tail = newcfg;
			}
		}
	}
	(void) fclose(fd);
	if (ret != NULL && other) {
		free_option_list(other);
	}

	return ret;
}


char **
make_new_argv(char **old_argv, int *old_argc, struct cfg_option *opt,
	struct ga_option *options, int nopts) {

	struct cfg_option	*tmpopt;
	char			*buf;
	char			*p;
	char			**ret;
	int			i;
	int			cmd_line_opts;
	int			ch;
	int			idx;
	int			total_config_argc;
	int			*opt_idx;

	/*
	 * First we convert all options to their command line
	 * equivalents, such that e.g.
	 *
	 * target = x86
	 *
	 * becomes
	 *
	 * -arch=x86
	 */
	for (tmpopt = opt; tmpopt != NULL; tmpopt = tmpopt->next) {
		if (tmpopt->name == NULL) {
			continue;
		}

		/*
		 * 03/25/08: Allow environment variables to override
		 * the config file settings!
		 */
		if (tmpopt->info->env_override != NULL) {
			/*
			 * This is currently only done for options which take
			 * a single operand, i.e. argv[0]. It would be nice
			 * to introduce NWCC_OPTIONS, which is also merged
			 * with all options. But not now!!!!
			 */
			char	*p = getenv(tmpopt->info->env_override);

			if (p != NULL) {
				free(tmpopt->argv[0]);
				tmpopt->argv[0] = n_xstrdup(p);
			}
		}

		switch (tmpopt->info->code) {
		case OPT_OPTIONS:
			/*
			 * Interpret all arguments verbatim, unless they
			 * contain a dot, in which case they are assumed
			 * to be file arguments, such as foo.c or bar.o
			 */
			for (i = 0; i < tmpopt->argc; ++i) {
				if ((p = strchr(tmpopt->argv[i], '.')) != NULL) {
					;
				} else {
					buf = n_xmalloc(strlen(tmpopt->argv[i])
						+ 2);
					sprintf(buf, "-%s", tmpopt->argv[i]);
					free(tmpopt->argv[i]);
					tmpopt->argv[i] = buf;
				}
			}
			break;
		case OPT_ARCH:
			buf = n_xmalloc(strlen(tmpopt->argv[0])
				+ sizeof "-arch=");
			sprintf(buf, "-arch=%s", tmpopt->argv[0]);
			free(tmpopt->argv[0]);
			tmpopt->argv[0] = buf;
			break;
		case OPT_ABI:
			buf = n_xmalloc(strlen(tmpopt->argv[0])
				+ sizeof "-abi=");
			sprintf(buf, "-abi=%s", tmpopt->argv[0]);
			free(tmpopt->argv[0]);
			tmpopt->argv[0] = buf;
			break;
		case OPT_ASM:
			buf = n_xmalloc(strlen(tmpopt->argv[0])
				+ sizeof "-asm=");
			sprintf(buf, "-asm=%s", tmpopt->argv[0]);
			free(tmpopt->argv[0]);
			tmpopt->argv[0] = buf;
			break;
		case OPT_GNUC:
			buf = n_xmalloc(strlen(tmpopt->argv[0])
				+ sizeof "-gnuc=");
			sprintf(buf, "-gnuc=%s", tmpopt->argv[0]);
			free(tmpopt->argv[0]);
			tmpopt->argv[0] = buf;
			break;
		case OPT_CPP:
			buf = n_xmalloc(strlen(tmpopt->argv[0])
				+ sizeof "-cpp=");
			sprintf(buf, "-cpp=%s", tmpopt->argv[0]);
			free(tmpopt->argv[0]);
			tmpopt->argv[0] = buf;
			break;
		default:
			abort();
		}
	}

	/*
	 * Now we filter out duplicated options; The policy is to let
	 * options passed on the command line win over those defined
	 * in config files. Thus we first record the option table
	 * indices for all command line arguments, and then remove
	 * any config file options with matching indices
	 */
	opt_idx = n_xmalloc(*old_argc * sizeof *opt_idx);
	i = 0;
	while ((ch = nw_get_arg(*old_argc, old_argv, options, nopts, &idx))
		!= -1) {
		switch (ch) {
		case '!':
			continue;
		case '?':
			if (!idx) { /* XXX -1 !!! */
				continue;
			}	
		}

		if (options[idx].ch == 'l') {
			/*
			 * 03/03/09: XXX Library options are not handled
			 * properly because every -l is treated the same,
			 * so -lm, -lpthread, etc are considered even
			 */
			continue;
		}

		{
			int	j;
			/*
			 * 03/03/09: Filter duplicated actual command
			 * line options like -foo -foo. Otherwise the
			 * code below, which seems to assume they occur
			 * only once, will break
			 * XXX Fix all of this stuff properly
			 */
			for (j = 0; j < i; ++j) {
				if (opt_idx[j] == idx) {
					break;
				}
			}
			if (j != i) {
				continue;
			}
		}
		opt_idx[i++] = idx;
	}
	cmd_line_opts = i;

	total_config_argc = 0;
	for (tmpopt = opt; tmpopt != NULL; tmpopt = tmpopt->next) {
		if (tmpopt->name == NULL) {
			continue;
		}	

		n_optind = -1;

		while ((ch = nw_get_arg(tmpopt->argc, tmpopt->argv,
			options, nopts, &idx)) != -1) {
			switch (ch) {
			case '!':
				continue;
			case '?':
				if (!idx) { /* XXX -1 */
					continue;
				}
			}
			for (i = 0; i < cmd_line_opts; ++i) {
				int	remaining;

				if (opt_idx[i] != idx) {
					continue;
				}	

				/*
				 * Option is duplicated - remove config
				 * file version
				 */
				remaining = tmpopt->argc -
					(n_optind + 1);
				free(tmpopt->argv[n_optind]);
				/*
				 * 03/03/09: remaining can become -1?!?!?!?
				 * 
				 * With
				 *
				 *    ./nwcc y.c -lm
				 *
				 * ... and
				 *
				 *    options = lm
				 *
				 * we first get a double free and then
				 * remaining = -1. XXX What's going on?
				 */
				if (remaining > 0) {
					memmove(tmpopt->argv+n_optind,
						tmpopt->argv+n_optind+1,
							remaining *
							sizeof(char *));
				} else {
					/*
					 * 03/03/09: If remaining = 0,
					 * there can be a double
					 * free() here
					 * XXX why?
					 */
					tmpopt->argv[n_optind] = NULL;
				}

				if (tmpopt->argc == 0) {
					(void) fprintf(stderr, "Warning: "
						"Config file settings "
						"messed up\n");
				} else {
					--tmpopt->argc;
				}
			}
		}
		total_config_argc += tmpopt->argc;
	}
	free(opt_idx);

	/*
	 * Now build an argv with a total of *old_argc + total_config_argc
	 * strings! (plus terminating null pointer)
	 */
	ret = n_xmalloc((*old_argc + total_config_argc + 1) * sizeof(char *));

	memcpy(ret, old_argv, *old_argc * sizeof(char *)); 

	i = 0;
	for (tmpopt = opt; tmpopt != NULL; tmpopt = tmpopt->next) {
		int	j;

		if (tmpopt->name == NULL) {
			continue;
		}	

		for (j = 0; j < tmpopt->argc; ++j) {
			ret[*old_argc + i++] = tmpopt->argv[j];
		}
		tmpopt->argv = NULL;
		tmpopt->argc = 0;
	}
	ret[*old_argc + i] = NULL;
	*old_argc += total_config_argc;

	free_option_list(opt);

#if 0
	printf("new argc = %d\n", *old_argc);
	printf("new argv:\n");
	for (i = 0; i < *old_argc; ++i) {
		printf("  %s\n", ret[i]);
	}	
	printf("   argv[*old_argc] = %p\n", ret[ *old_argc ]);

	print_option_list(opt);
#endif

	n_optind = -1; /* Reset nw_get_arg() */
	return ret;
}

struct cfg_option *
read_config_files(void) {
	char			buf[1024];
	struct cfg_option	*optlist;
	struct cfg_option	*tmp;
	struct passwd		*pw;

	optlist = do_read_config_file(INSTALLDIR "/nwcc/nwcc.conf", NULL);
	if ((pw = getpwuid(getuid())) != NULL
		&& strlen(pw->pw_dir) < (sizeof buf - 32)) {
		sprintf(buf, "%s/.nwcc/nwcc.conf", pw->pw_dir);
		tmp = do_read_config_file(buf, optlist);
		if (tmp != NULL) {
			optlist = tmp;
		}
	}	
	return optlist;
}

void
merge_argv_with_cfgfile(int *argc, char ***argv, struct ga_option *options, int nopts) {
	struct cfg_option	*cfg_file_options;

	if ((cfg_file_options = read_config_files()) != NULL) {
		/*
		 * There are config files that need to be integrated into
		 * the command line
		 */
		char	**tmpargv;
		int	tmpargc = *argc;
		
		tmpargv = make_new_argv(*argv, &tmpargc, cfg_file_options,
				options, nopts);
		if (tmpargv != NULL) {
			*argv = tmpargv;
			*argc = tmpargc;
		}	
	}
}

