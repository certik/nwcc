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
 * nwcc compiler driver
 */
#include "cc_main.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "driver.h"
#include "error.h"
#include "defs.h"
#include "debug.h"
#include "backend.h"
#include "sysdeps.h"
#include "misc.h"
#include "cfgfile.h"
#include "n_libc.h"
#include "reg.h"


struct reg x86_gprs[7];

int		Sflag;
int		Oflag;
int		sflag;
int		Eflag;
int		cflag;
int		oflag;
int		gflag;
int		assembler;
int		stackprotectflag;
int		gnuheadersflag = 1; /* XXX */
int		pedanticflag;
int		verboseflag;
char		*asmflag;
char		*asmname;
char		*gnuc_version;
char		*cppflag;

int		m32flag;
int		m64flag;

int		Oflag;
int		archflag;
int		abiflag;
int		abiflag_default;
int		sysflag; /* 01/31/09: Specify system using -sys */
int		sysflag_default;
int		nostdinc_flag;
int		nostdlib_flag;
int		picflag;
int		sharedflag;
int		stupidtraceflag;
char		*out_file = "a.out";
int		*argmap;

int		timeflag;

int		mintocflag;
int		fulltocflag;


int		funsignedchar_flag;
int		fsignedchar_flag;
int		fnocommon_flag;

char		*custom_cpp_args;
char		*custom_ld_args;
char		*custom_asm_args;
char		*xlinker_args;
char		*soname;

char		*std_flag;

/*
 * 05/18/09: Finally implemented a GNU disabling flag
 */
int		notgnu_flag = -1;
int		color_flag = -1;

int		dump_macros_flag;

int		write_fcat_flag;
int		save_bad_translation_unit_flag;

static void
usage(void) {
	/* XXX add useful stuff here */
	puts("You invoked nwcc incorrectly. Please refer to the README file");
	puts("for supported command line arguments");
	exit(EXIT_FAILURE);
}


static void
print_version(void) {
	printf("nwcc %s\n", NWCC_VERSION);
	printf("Copyright (c) 2003 - 2014 "
		"Nils Robert Weller\n");
	/*
	 * 2014116: If we define the __GNUC__ macro, "nwcc -v" should also contain the text
	 * "gcc version <version>" because some configure scripts (glibc) check for this
	 * (the user needs to set the macro to a suitable three digit version number in this
	 * case)
	 */
	if (notgnu_flag == 0 && getenv("NWCC_DEFINE_GNUC_MACRO") != NULL) {
		printf("Pretending to be gcc version %s\n", getenv("NWCC_DEFINE_GNUC_MACRO"));
	}
}

static void
do_Wl_option(char **cld, char *arg) {
	if (*cld == NULL) {
		*cld = n_xstrdup(arg);
	} else {
		/*
		 * 03/09/09: Whoops, like -Xlinker,
		 * multiple -Wl arguments are also
		 * concatenated
		 */
		*cld = n_xrealloc(*cld,
			strlen(*cld) +
			strlen(arg) +
			sizeof ",");
		strcat(*cld, ",");
		strcat(*cld, arg);
	}
}


static void
add_xlinker_args(char **xlinker_args, const char *n_optarg) {
	size_t	len;
	int	initial = 0;

	if (*xlinker_args != NULL) {
		len = strlen(*xlinker_args);
	} else {
		len = 0;
		initial = 1;
	}

	len += strlen(n_optarg) + 3;
	*xlinker_args = n_xrealloc(*xlinker_args, len);
	if (initial) {
		strcpy(*xlinker_args, n_optarg);
	} else {
		strcat(*xlinker_args, " ");
		strcat(*xlinker_args, n_optarg);
	}
}

static char *
get_libgcc_path(int for_shared) {
	char	*path = NULL;
	char	*p;

#ifdef LIBGCC_PATH
	path = LIBGCC_PATH;
#endif
	if (archflag == ARCH_MIPS) {
		if (abiflag == ABI_MIPS_N32) {
#ifdef LIBGCC_PATH_N32
			path = LIBGCC_PATH_N32;
#endif
		} else {
#ifdef LIBGCC_PATH_N64
			path = LIBGCC_PATH_N64;
#endif
		}
		/*
		 * The archive file doesn't resolve references :-( So always link
		 * libgcc_s.
		 */
		if (for_shared && (p = strrchr(path, '/')) != NULL) {
			char	*temp;
			char	*so;

#if 0
			if (abiflag == ABI_MIPS_N32) {
				so = "libgcc_s_n32.so";
			} else {
				so = "libgcc_s_64.so";
			}
#endif
			so = "libgcc_s.so";

			temp = n_xmalloc(strlen(path) + strlen(so) + sizeof "/");
			strncpy(temp, path, p - path)[p - path] = 0;
			strcat(temp, "/");
			strcat(temp, so); 
			path = temp;
		}
	}
	
	return path;
}

int
main(int argc, char *argv[]) {
	int			ch;
	int			i;
	int			ldind = 0;
	int			idx;
	int			nolibnwccflag = 0;
	int			args_used[256] = { 0 };
	int			sd_host_arch;
	int			sd_host_sys;
	size_t			ldpos = 0;
	size_t			ldalloc = 0;
	size_t			cppind = 0;
	char			asm_flags[1024];
	char			*asm_cmdline = NULL;
	char			*ld_flags = NULL;
	char			*ld_args[128];
	char			*cpp_args[256];
	char			*ld_cmdline = NULL;
	char			*target_str = NULL;
	char			*abi_str = NULL;
	char			*sys_str = NULL;
#if USE_LIBGCC
	int			used_libgcc = 0;
#endif
	struct stat		sbuf;
	struct ga_option	options[] = {
		{ 'D', NULL, 1 },
		{ 'U', NULL, 1 },
		{ 'I', NULL, 1 },
		{ 'l', NULL, 1 },
		{ 'L', NULL, 1 },
		{ 'o', NULL, 1 },
		{ 'n', NULL, 1 },
		{ 'R', NULL, 1 },
		{ 'S', NULL, 0 },
		{ 'E', NULL, 0 },
		{ 'c', NULL, 0 },
		{ 'O', NULL, 0 }, /* 07/18/08: This was missing!??!?!? */
		{ 'W', NULL, 0 }, /* ignore */
		{ 'f', NULL, 0 }, /* ignore */
		{ 'g', NULL, 0 }, /* ignore */
		{ 'm', NULL, 0 }, /* ignore */
		{ 's', NULL, 0 }, /* strip */
		{ 'v', NULL, 0 }, /* verbose/version */
		{ 'V', NULL, 0 }, /* verbose/version */
		{ 0, "verboseoffsets", 0 },
		{ 0, "stackprotect", 0 },
		{ 0, "nolibnwcc", 0 },
		{ 0, "pthread", 0 }, /* ignore */
		{ 0, "print-prog-name", 1 },
		{ 0, "nostdinc", 0 },
		{ 0, "nostdlib", 0 },
		{ 0, "rdynamic", 0 }, /* ignore */
		{ 0, "static", 0 }, /* ignore (this hurts, we need it!) */
		{ 0, "shared", 0 },
		{ 0, "stupidtrace", 0 },
		{ 0, "fpic", 0 },
		{ 0, "fPIC", 0 },
#ifdef __sun
		{ 0, "KPIC", 0 },
#endif
		{ 0, "export-dynamic", 0 }, /* ignore */
		{ 0, "arch", 1 },
		{ 0, "gnuc", 1 },
#ifdef __sun
		{ 0, "xarch", 1 },
#endif
		{ 0, "O-1", 0 }, /* disable even VERY simple optimizations */
		{ 0, "O0", 0 }, /* ignore */
		{ 0, "O1", 0 }, /* ignore */
		{ 0, "O2", 0 }, /* ignore */
		{ 0, "O3", 0 }, /* ignore */
		{ 0, "ggdb", 0 }, /* ignore */
		{ 0, "Wall", 0 }, /* ignore */
		{ 0, "pedantic", 0 }, /* ignore */
		{ 0, "verbose", 0 },
		{ 0, "version", 0 },
		{ 0, "pg", 0 }, /* ignore */
		{ 0, "pipe", 0 }, /* ignore */
		{ 0, "ansi", 0 },
		{ 0, "std", 1 },
		{ 0, "dM", 0 },
		{ 0, "mabi", 1 },
		{ 0, "maix64", 0 },
		{ 0, "m32", 0 },
		{ 0, "m64", 0 },
		{ 0, "mminimal-toc", 0 },
		{ 0, "mfull-toc", 0 },
		{ 0, "funsigned-char", 0 },
		{ 0, "fsigned-char", 0 },
		{ 0, "notgnu", 0 },
		{ 0, "gnu", 0 },
		{ 0, "color", 0 },
		{ 0, "uncolor", 0 },
		{ 0, "fno-common", 0 },
		{ 0, "soname", 1 },
		{ 0, "abi", 1 },
		{ 0, "sys", 1 },
		{ 0, "asm", 1 },
		{ 0, "cpp", 1 },
		{ 0, "time", 0 },
		{ 0, "Wa", 1 },
		{ 0, "Wp", 1 },
		{ 0, "Wl", 1 },
		{ 0, "rpath", 1 },
		{ 0, "export-dynamic", 0 },
		{ 0, "Xlinker", 1 },
		{ 0, "lgcc", 0 },
		{ 0, "write-fcat", 0 },
		{ 0, "save-bad-translation-unit", 0 },
		{ 0, "dump-all-arch-sys-abi-combinations", 0 },
		{ 0, "dump-my-sysid", 0 },
		{ 0, "dump-target-id", 0 }
	};
	int		nopts = N_OPTIONS(options);
	int		orig_argc = argc;
	int		dump_target_id_flag = 0;

	merge_argv_with_cfgfile(&argc, &argv, options, nopts);
#if 0
	if ((cfg_file_options = read_config_files()) != NULL) {
		/*
		 * There are config files that need to be integrated into
		 * the command line
		 */
		char	**tmpargv;
		int	tmpargc = argc;
		
		tmpargv = make_new_argv(argv, &tmpargc, cfg_file_options,
				options, nopts);
		if (tmpargv != NULL) {
			argv = tmpargv;
			argc = tmpargc;
		}	
	}
#endif

	if (argc <= (int)(sizeof args_used / sizeof args_used[0])) {
		argmap = args_used;
	} else {
		argmap = n_xmalloc(argc * sizeof *argmap);
		memset(argmap, 0, argc * sizeof *argmap);
	}


	if (getenv("NWCC_DUMPARGS") != NULL) {
		int	i;
		FILE	*fd;

		fd = fopen(".nwcc/log.txt", "w");
		
		if (fd != NULL) {
			for (i = 1; i < argc; ++i) {
				printf("%d:  %s\n", i, argv[i]);
				fprintf(fd, "%d:  %s\n", i, argv[i]);
			}
			fclose(fd);
		}
	}
	if (getenv("NWCC_DEFINE_GNUC_MACRO") != NULL) {
		notgnu_flag = 0;	
	}

	while ((ch = nw_get_arg(argc-1, argv+1, options, nopts, &idx)) != -1) {
		if (ch != '!' && ch != -1) {
			/*
			 * 02/09/08: XXX This is an ugly temporary solution
			 * to mark command line arguments as being used for
			 * options so they are not treated as input files
			 *
			 * Note that in ``-foo fooarg'', only fooarg is
			 * marked, thus the p[0] == '-' check must stay too!
			 */
			argmap[n_optind] = 1;
		}

		switch (ch) {
		case 'n':
			asm_cmdline = n_xstrdup(n_optarg);
			break;
		case 'c':
			cflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case 'v':
		case 'V':
			print_version();
			verboseflag = 1;
			break;
		case 'I':
		case 'D':
		case 'U':	
			if (cppind >= (sizeof cpp_args / sizeof cpp_args[0] - 2)) {
				(void) fprintf(stderr,
					"Too many cpp flags\n");
				return EXIT_FAILURE;
			}
			if (ch == 'I') {
				cpp_args[cppind++] = "-I";
			} else if (ch == 'D') {
				cpp_args[cppind++] = "-D";
			} else if (ch == 'U') {
				cpp_args[cppind++] = "-U";
			}	
			cpp_args[cppind++] = n_optarg;
			break;
		case 'E':
			Eflag = 1;
			break;
		case 'l':
		case 'L':	
			if (ldind == 126) {
				(void) fprintf(stderr,
					"Too many linker flags\n");
				return 1;
			}
			if (ch == 'l') {
				ld_args[ldind++] = "-l";
			} else if (ch == 'L') {
				ld_args[ldind++] = "-L";
			}
			ld_args[ldind++] = n_optarg;
			break;
		case 'o':
			out_file = n_xstrdup(n_optarg);
			oflag = 1;
			break;
		case 'S':
			Sflag = 1;
			break;
		case 'O':
			Oflag = 1;
			break;
		case 'W': /* Ignore -W */
			break;
		case 'g':	
			gflag = 1;
			break;
		case 'R': {
				char	*temp;

				/*
				 * 05/26/09: Allow -R and -rpath, and rewrite
				 * them to -Wl,-R and -WL,-rpath. These options
				 * are passed to nwcc in lots of projects, and
				 * it always seems to happen when libtool is
				 * used (which of course doesn't know nwcc, but
				 * gcc does not have these options either)
				 */
				(void) fprintf(stderr, "Warning: `-R' option "
					"unknown, assuming `-Wl,-R...' was "
					"really intended (maybe incorrect "
					"invocation by libtool)\n");
				temp = n_xmalloc(sizeof "-R" + strlen(n_optarg));
				sprintf(temp, "-R%s", n_optarg);
				do_Wl_option(&custom_ld_args, temp);
				free(temp);
			}
			break;
		case '!':
			break;
		case '?':
			if (idx) {
				if (strcmp(options[idx].name, "stackprotect")
					== 0) {
					stackprotectflag = 1;
				} else if (strcmp(options[idx].name,
					"nolibnwcc") == 0) {
					nolibnwccflag = 1;
				} else if (strcmp(options[idx].name, "Wall")
					== 0) {
					; /* Ignore */
				} else if (options[idx].name[0] == 'O'
					&& (isdigit(options[idx].name[1])
					|| options[idx].name[1] == '-')) {
					if (strcmp(options[idx].name, "O-1")
						== 0) {
						/*
						 * 04/11/08: -1 means even very
						 * standard trivial optimizations
						 * are disabled
						 */
						Oflag = -1;
					} else if (options[idx].name[0] == '0') {
						Oflag = 0;
					} else {
						; /* Ignore */
					}
				} else if (strcmp(options[idx].name, "ggdb")
					== 0) {
					; /* Ignore */
				} else if (strcmp(options[idx].name, "ansi")
					== 0) {
					std_flag = n_xstrdup("c89");
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
					std_flag = n_xstrdup(n_optarg);
				} else if (strcmp(options[idx].name, "pg")
					== 0) {
					; /* Ignore */
				} else if (strcmp(options[idx].name, "pedantic")
					== 0) {
					pedanticflag = 1;
				} else if (strcmp(options[idx].name, "verbose")
					== 0) {
					verboseflag = 1;
				} else if (strcmp(options[idx].name, "pipe")
					== 0) {
					; /* Ignore */
				} else if (strcmp(options[idx].name, "pthread")
					== 0) {
					ld_args[ldind++] =
						"-lpthread";
				} else if (strcmp(options[idx].name, "shared") == 0) {
					sharedflag = 1;
				} else if (strcmp(options[idx].name, "stupidtrace") == 0) {
					stupidtraceflag = 1;
				} else if (strcmp(options[idx].name, "fpic") == 0
					|| strcmp(options[idx].name, "fPIC") == 0
					|| strcmp(options[idx].name, "KPIC") == 0) {
					picflag = 1;
				} else if (strcmp(options[idx].name, "soname") == 0) {
					char	*p;
					p = n_xmalloc(sizeof "-soname=" + strlen(n_optarg));
					sprintf(p, "-soname=%s", n_optarg);
					ld_args[ldind++] = p;
				} else if (strcmp(options[idx].name, "rdynamic") == 0
					|| strcmp(options[idx].name, "export-dynamic") == 0) {
					ld_args[ldind++] = "-export-dynamic";
				} else if (strcmp(options[idx].name,
					"nostdinc") == 0) {
					nostdinc_flag = 1;
				} else if (strcmp(options[idx].name,
					"nostdlib") == 0) {
					nostdlib_flag = 1;
					(void) fprintf(stderr, "Warning: "
						"-nostdlib is ignored\n");
				} else if (strcmp(options[idx].name, "asm")
					== 0) {
					if (asmflag) {
						(void) fprintf(stderr, "-asm "
						"used more than once\n");
						exit(EXIT_FAILURE);
					}	
					asmflag = n_xstrdup(n_optarg);
				} else if (strcmp(options[idx].name, "cpp")
					== 0) {
					if (cppflag) {
						(void) fprintf(stderr, "-cpp "
						"used more than once\n");
						exit(EXIT_FAILURE);
					}
					cppflag = n_xstrdup(n_optarg);
				} else if (strcmp(options[idx].name, "arch")
					== 0) {
					if (target_str != NULL) {
						(void) fprintf(stderr, "-arch "
						"used more than once\n");
						exit(EXIT_FAILURE);
					}	
					target_str = n_xstrdup(n_optarg);
				} else if (strcmp(options[idx].name, "static")
					== 0) {
#if 0
					(void) fprintf(stderr, "Warning: "
						"ignoring -static option\n");
#endif
					ld_args[ldind++] = "-static";
				} else if (strcmp(options[idx].name, "gnuc")
					== 0) {
					if (gnuc_version != NULL) {
						(void) fprintf(stderr, "-gnuc "
						"used more than once\n");
						exit(EXIT_FAILURE);
					} else if (strlen(n_optarg) > 10) {
						(void) fprintf(stderr, "Bogus "
							"argument to -gnuc\n");
						exit(EXIT_FAILURE);
					}	
							
					gnuc_version = n_xstrdup(n_optarg);
				} else if (strcmp(options[idx].name, "xarch")
					== 0) {
					;
				} else if (strcmp(options[idx].name, "dM")
					== 0) {
					dump_macros_flag = 1;
				} else if (strcmp(options[idx].name,
						"print-prog-name") == 0) {
					char	buf[2048];

					if (find_cmd(n_optarg, buf, sizeof buf)
						== -1) {
						puts(n_optarg);
					} else {
						puts(buf);
					}	
					exit(0);
				} else if (strcmp(options[idx].name, "m32") == 0) {
					/* XXX only supported for AMD64 now */
					m32flag = 1;
				} else if (strcmp(options[idx].name, "m64") == 0) {
					/* XXX only supported for AMD64 now */
					m64flag = 1;
				} else if (strcmp(options[idx].name, "maix64")
					== 0) {
					abi_str = n_xstrdup("aix64"); /* XXX */
				} else if (strcmp(options[idx].name, "mminimal-toc") 
					== 0) {
					if (fulltocflag) {
						(void) fprintf(stderr, "Warning: -mminimal-toc and "
							"-mfull-toc specified!\n");
						fulltocflag = 0;
					}
					mintocflag = 1;
				} else if (strcmp(options[idx].name, "mfull-toc")
					== 0) {
					if (mintocflag) {
						(void) fprintf(stderr, "Warning: -mminimal-toc and "
							"-mfull-toc specified!\n");
						mintocflag = 0;
					}
					fulltocflag = 1;
				} else if (strcmp(options[idx].name, "mabi")
					== 0
					|| strcmp(options[idx].name, "abi")
					== 0) {
					if (abi_str != NULL) {
						(void) fprintf(stderr, "-mabi "
							"used more than once\n");
						exit(EXIT_FAILURE);
					}
					abi_str = n_xstrdup(n_optarg);
				} else if (strcmp(options[idx].name, "sys") == 0) {
					if (sys_str != NULL) {
						(void) fprintf(stderr, "-sys "
							"used more than once\n");
						exit(EXIT_FAILURE);
					}
					sys_str = n_xstrdup(n_optarg);
				} else if (strcmp(options[idx].name,
					"notgnu") == 0) {
					notgnu_flag = 1;
				} else if (strcmp(options[idx].name,
					"gnu") == 0) {
					notgnu_flag = 0;
				} else if (strcmp(options[idx].name,
					"color") == 0) {
					color_flag = 1;
				} else if (strcmp(options[idx].name,
					"uncolor") == 0) {
					color_flag = 0;
				} else if (strcmp(options[idx].name, "version")
					== 0) {
					print_version();
					exit(0);
				} else if (strcmp(options[idx].name, "time")
					== 0) {
					timeflag = 1;
				} else if (strcmp(options[idx].name, "funsigned-char") == 0) {
					if (fsignedchar_flag) {
						(void) fprintf(stderr, "Ignoring "
							"preceding -fsigned-char "
							"option\n");
						fsignedchar_flag = 0;
					}
					funsignedchar_flag = 1;
				} else if (strcmp(options[idx].name, "fsigned-char") == 0) {
					if (funsignedchar_flag) {
						(void) fprintf(stderr, "Ignoring "
							"preceding -funsigned-char "
							"option\n");
						funsignedchar_flag = 0;
					}
					fsignedchar_flag = 1;
				} else if (strcmp(options[idx].name, "fno-common") == 0) {
					fnocommon_flag = 1;
				} else if (strcmp(options[idx].name, "Wp") == 0) {
					custom_cpp_args = n_xmalloc(strlen(n_optarg) + sizeof "-Wp,");
					sprintf(custom_cpp_args, "-Wp,%s", n_optarg);
				} else if (strcmp(options[idx].name, "rpath") == 0) {
					char	*temp;

					/*
					 * 05/26/09: Allow -R and -rpath, and rewrite
					 * them to -Wl,-R and -WL,-rpath. These options
					 * are passed to nwcc in lots of projects, and
					 * it always seems to happen when libtool is
					 * used (which of course doesn't know nwcc, but
					 * gcc does not have these options either)
					 */
					(void) fprintf(stderr, "Warning: `-rpath' option "
						"unknown, assuming `-Wl,-rpath...' was "
						"really intended (maybe incorrect "
						"invocation by libtool)\n");
					temp = n_xmalloc(sizeof "-rpath " + strlen(n_optarg));
					sprintf(temp, "-rpath %s", n_optarg);
					do_Wl_option(&custom_ld_args, temp);
					free(temp);
				} else if (strcmp(options[idx].name, "export-dynamic") == 0) {
					/*
					 * 06/21/09: Another one for libtool- Treat
					 * -export-dynamic as -Wel,-export-dynamic
					 */
					do_Wl_option(&custom_ld_args, "-export-dynamic");
				} else if (strcmp(options[idx].name, "Wl") == 0) {
					do_Wl_option(&custom_ld_args, n_optarg);
				} else if (strcmp(options[idx].name, "Wa") == 0) {
					custom_asm_args = n_xstrdup(n_optarg);
				} else if (strcmp(options[idx].name, "Xlinker") == 0
					|| strcmp(options[idx].name, "lgcc") == 0) {

					if (strcmp(options[idx].name, "lgcc") == 0) {

#if USE_LIBGCC
						used_libgcc = 1;
#endif

#ifdef LIBGCC_PATH
#  if USE_LIBGCC
						/*
						 * Wait until ABI settings are known so
						 * we can pick the correct libgcc file
						 * below
						 */
						break;
#  else
						n_optarg = get_libgcc_path(0);
#endif
						/*n_optarg = LIBGCC_PATH;*/
#else
						break;
#endif
					}

					add_xlinker_args(&xlinker_args, n_optarg);
#if 0

					if (xlinker_args != NULL) {
						len = strlen(xlinker_args);
					} else {
						len = 0;
						initial = 1;
					}

					len += strlen(n_optarg) + 3;
					xlinker_args = n_xrealloc(xlinker_args, len);
					if (initial) {
						strcpy(xlinker_args, n_optarg);
					} else {
						strcat(xlinker_args, " ");
						strcat(xlinker_args, n_optarg);
					}
#endif
				} else if (strcmp(options[idx].name, "write-fcat") == 0) {
					write_fcat_flag = 1;
				} else if (strcmp(options[idx].name, "save-bad-translation-unit") == 0) {
					save_bad_translation_unit_flag = 1;
					if (orig_argc == 2) {
						/* Only check whether this feature is available */
						return 0;
					}
				} else if (strcmp(options[idx].name, "dump-all-arch-sys-abi-combinations") == 0) {
					dump_all_arch_sys_abi_combinations();
					exit(0);
				} else if (strcmp(options[idx].name, "dump-my-sysid") == 0) {
					dump_my_sysid();
					exit(0);
				} else if (strcmp(options[idx].name, "dump-target-id") == 0) {
					dump_target_id_flag = 1;
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
	cpp_args[cppind] = NULL;

	if ((m32flag && m64flag)) {
		(void) fprintf(stderr, "Error: Invalid combination of architecture settings\n");
		usage();
	}
		

	{
		int	hostarch;
		int	hostabi;
		int	hostsys;

		get_host_arch(&hostarch, &hostabi, &hostsys);

		/*
		 * 07/26/12: we can now evaluate the -m32/64 otherwise ambiguous flags
		 */
		if (m32flag) {
			if (hostarch == ARCH_AMD64) {
				if (target_str != NULL) {
					(void) fprintf(stderr, "Error: Invalid combination of architecture settings\n");
					usage();
				}
				target_str = "x86"; /* Cross-compile for x86 */
			}
		}
		if (m64flag) {
			if (hostarch == ARCH_AMD64) {
				if (target_str != NULL) {
					(void) fprintf(stderr, "Error: Invalid combination of architecture settings\n");
					usage();
				}
			}
		}

		set_target_arch_and_abi_and_sys(&archflag, &abiflag, &sysflag, target_str, abi_str, sys_str);
		if (target_str != NULL) {
			if ((archflag != hostarch || sysflag != hostsys) && !Sflag) {
				/*
				 * 02/21/09: Allow x86/AMD64 natively on OSX, where they
				 * are similar to ABI settings on IRIX and AIX
				 * XXX we should allow this on Linux and BSD as well
				 * 07/26/12: k.
				 */ 
				if ((archflag == ARCH_AMD64 && hostarch == ARCH_X86)
					|| (archflag == ARCH_X86 && hostarch == ARCH_AMD64)) {
					 ;
				} else {
					if (!dump_target_id_flag) {
						(void) fprintf(stderr, "Warning: Cross-compilation "
							"implies -S (only generates *.asm file.)\n");
					}
					Sflag = 1;
				}
			}
		}
	}

	check_arch_sys_consistency(archflag, sysflag);


	if (dump_target_id_flag) {
		dump_target_id(archflag, sysflag, abiflag);
		exit(0);
	}


	if (sysflag == OS_AIX && mintocflag) {
		(void) fprintf(stderr, "Warning: -mminimal-toc is unimplemented on "
				       "AIX, so it will be ignored\n");
		mintocflag = 0;
		fulltocflag = 1;
	} else if (sysflag == OS_OSX) {
		picflag = 1;
	} else if (archflag == ARCH_MIPS) {
		if (sysflag == OS_LINUX) {
			/*
			 * 07/17/09: Linux/MIPSel needs PIC by default. Not
			 * sure as to whether it works the same on IRIX.
			 */
			picflag = 1;
		}	
	}

#if USE_LIBGCC
	if (!used_libgcc) {
		/*
		 * 07/21/09: We have to use libgcc for software floating point
		 * (disgusting temporary workaround), and it was not already
		 * requested using -lgcc from the user
		 */
		char	*path;


		path = get_libgcc_path(1);

		if (path != NULL) {
			add_xlinker_args(&xlinker_args, path);
		}
	}
#endif

	if (notgnu_flag == -1) {
		/*
		 * 05/18/09: Neither -notgnu nor -gnu was supplied -
		 * pick a sensible default for the target platform.
		 * This can be overridden using configure!
		 */
#if CONF_NOT_GNU
		notgnu_flag = 1;
#elif CONF_IS_GNU
		notgnu_flag = 0;
#else
		if (sysflag == OS_AIX
			|| sysflag == OS_IRIX
			/*|| sysflag == OS_SOLARIS*/
			/*|| sysflag == OS_OSX*/) {
			/*
			 * This is either a UNIX(R) system which does
			 * not know much or anything about GNU C - so
			 * enabling __GNUC__ would be dangerous and
			 * without merit -  or it is OSX, which uses
			 * HORRIBLY GNU-specific headers!
			 *
			 * 07/05/09: Looks like we should really use
			 * GNU mode by default on OSX as well because
			 * the headers don't work AT ALL without it!
			 * Probably completely untested by Apple.
			 *
			 * 07/31/09: Solaris also uses GNU C in the
			 * libc headers now (e.g. for stdarg)
			 */
			notgnu_flag = 1;
		} else {
			/*
			 * Most likely Linux or BSD - assume that we
			 * are better off pretending to be gcc by
			 * default
			 * 08/03/11: It has turned out that there are
			 * lots of applications which do not build in
			 * GNU mode on Linux, while there are
			 * currently no known ones that require GNU
			 * mode, hence we always default to "not GNU"
			 * for Linux as well. On my - admittedly
			 * older - OSX system we definitely need GNU
			 * mode because otherwise LOTS of libc
			 * headers fail. It remains the default on
			 * BSD systems as well
			 */
			if (sysflag == OS_LINUX) {
				notgnu_flag = 1;
			} else {
				notgnu_flag = 0;
			}
		}
#endif
	}

	if (color_flag == -1) {
#if CONF_COLOR_OUTPUT
		color_flag = 1;
#else
		color_flag = 0;
#endif
	}

#if 0
	if (sizeof(long) == 8 && archflag == ARCH_POWER) {
		abiflag = ABI_POWER64;
	}
#endif

	/*
	 * libnwcc is now linked statically because there isn't
	 * a whole lot of code in it anyway
	 */
	if (!nolibnwccflag) {
		int	is_64bit =
			abiflag == ABI_POWER64
			|| abiflag == ABI_MIPS_N64
			|| abiflag == ABI_SPARC64
			|| archflag == ARCH_AMD64; /* XXX ugly - only x86/amd64
						  * are treated as separate
						  * architctures
						  */

		if (sharedflag) {
			/*
			 * 02/15/08: If we're building a shared library, we need the
			 * position-independent version of libnwcc!
			 */
			if (is_64bit) {
				if (stat(INSTALLDIR "/lib/dynlibnwcc64.o", &sbuf) != -1) {
					ld_args[ldind++] = INSTALLDIR "/lib/dynlibnwcc64.o";
				} else if (stat("./dynextlibnwcc64.o", &sbuf) != -1) {
					ld_args[ldind++] = "./dynextlibnwcc64.o";
				} else if (stat(INSTALLDIR "/lib/dynlibnwcc.o", &sbuf) != -1) {	
					ld_args[ldind++] = INSTALLDIR "/lib/dynlibnwcc.o";
				} else if (stat(INSTALLDIR "./dynextlibnwcc.o", &sbuf) != -1) {	
					ld_args[ldind++] = INSTALLDIR "./dynextlibnwcc.o";
				}
			} else {
				if (stat(INSTALLDIR "/lib/dynlibnwcc32.o", &sbuf) != -1) {
					ld_args[ldind++] = INSTALLDIR "/lib/dynlibnwcc32.o";
				} else if (stat("./dynextlibnwcc32.o", &sbuf) != -1) {
					ld_args[ldind++] = "./dynextlibnwcc32.o";
				} else if (stat(INSTALLDIR "/lib/dynlibnwcc.o", &sbuf) != -1) {
					ld_args[ldind++] = INSTALLDIR "/lib/dynlibnwcc.o";
				} else if (stat("./dynextlibnwcc.o", &sbuf) != -1) {
					ld_args[ldind++] = "./dynextlibnwcc.o";
				}
			}
		} else {
			if (is_64bit) {
				if (stat(INSTALLDIR "/lib/libnwcc64.o", &sbuf) != -1) {
					ld_args[ldind++] = INSTALLDIR "/lib/libnwcc64.o";
				} else if (stat("./extlibnwcc64.o", &sbuf) != -1) {
					ld_args[ldind++] = "./extlibnwcc64.o";
				} else if (stat(INSTALLDIR "/lib/libnwcc.o", &sbuf) != -1) {	
					ld_args[ldind++] = INSTALLDIR "/lib/libnwcc.o";
				} else if (stat(INSTALLDIR "./extlibnwcc.o", &sbuf) != -1) {	
					ld_args[ldind++] = INSTALLDIR "./extlibnwcc.o";
				} else if (stat("./extlibnwcc.o", &sbuf) != -1) {	
					ld_args[ldind++] = "./extlibnwcc.o";
				}
			} else {	
				if (stat(INSTALLDIR "/lib/libnwcc32.o", &sbuf) != -1) {
					ld_args[ldind++] = INSTALLDIR "/lib/libnwcc32.o";
				} else if (stat("./extlibnwcc32.o", &sbuf) != -1) {
					ld_args[ldind++] = "./extlibnwcc32.o";
				} else if (stat(INSTALLDIR "/lib/libnwcc.o", &sbuf) != -1) {
					ld_args[ldind++] = INSTALLDIR "/lib/libnwcc.o";
				} else if (stat("./extlibnwcc.o", &sbuf) != -1) {
					ld_args[ldind++] = "./extlibnwcc.o";
				}
			}
		}	
		if (sysdep_get_host_system() == OS_IRIX) {
			/* XXX otherwise symbols of libnwcc are unresolved */
			ld_args[ldind++] = "-lc";
		}
	}

	if (asmflag == NULL) {
		asmflag = getenv("NWCC_ASM");
	}	

	/*
	 * 02/16/08: Now we allow absolute assembler paths and only look a
	 * the last path component to determine which one it is
	 */
	if (asmflag != NULL) {
		if ((asmname = strrchr(asmflag, '/')) != NULL) {
			++asmname;
		} else {
			asmname = asmflag;
		}
	}


	sd_host_arch = sysdep_get_host_arch();
	sd_host_sys = sysdep_get_host_system();

	if (sd_host_arch == ARCH_X86) {

		if (sd_host_sys == OS_SOLARIS) {
			if (asmflag == NULL) {
				asmflag = n_xstrdup("/usr/sfw/bin/gas");
				asmname = "gas";
			}
			assembler = ASM_GAS;
			strcpy(asm_flags, "--traditional-format -Qy ");
		} else if (sd_host_sys == OS_LINUX
			|| sd_host_sys == OS_OPENBSD
			|| sd_host_sys == OS_MIRBSD
			|| sd_host_sys == OS_FREEBSD
			|| sd_host_sys == OS_DRAGONFLYBSD
			|| sd_host_sys == OS_NETBSD) {
			if (asmflag == NULL) {
				/*
				 * 07/28/07: Finally gas is the default assembler!
				 * This is meant to speed up assembling significantly
				 */
				assembler = ASM_GAS  /*NASM*/;
				asmflag = n_xstrdup("as");
				asmname = "gas";
				*asm_flags = 0;
			} else if ( 
				strcmp(asmname, "nasm") == 0
				|| strcmp(asmname, "nwasm") == 0
				|| strcmp(asmname, "yasm") == 0) {
				if (strcmp(asmname, "nwasm") == 0) {
					assembler = ASM_NWASM;
				} else {
					assembler = *asmname == 'n'? ASM_NASM: ASM_YASM;
				}
				strcpy(asm_flags, "-f elf ");
				if (assembler == ASM_NWASM) {
					/*
					 * Disable verbose output
					 */
					strcat(asm_flags, "-q ");
				}	
			} else if (strcmp(asmname, "gas") == 0
					|| strcmp(asmname, "as") == 0) {
				if (strcmp(asmflag, "gas") == 0) {
					asmflag = n_xstrdup("as");
				}
				assembler = ASM_GAS;
				*asm_flags = 0;
			} else {
				(void) fprintf(stderr, "Unknown assembler `%s'\n", asmflag);
			}
		} else if (sd_host_sys == OS_OSX) {
			assembler = ASM_GAS;
			if (asmflag == NULL) {
				asmflag = n_xstrdup("as");
				asmname = "as";
			}
			*asm_flags = 0;

			sprintf(asm_flags, "-arch %s -force_cpusubtype_ALL ",
				archflag == ARCH_X86? "i386": "x86_64");
		} else {
			(void) fprintf(stderr, "FATAL ERROR: Unsupported system\n");
			exit(EXIT_FAILURE);
		}
	} else if (sd_host_arch == ARCH_AMD64) {
		if (asmflag == NULL) {
			/*
			 * 07/28/07: Finally gas is the default assembler!
			 * This is meant to speed up assembling significantly
			 */
			asmflag = n_xstrdup("as");
			asmname = "gas";
			assembler = ASM_GAS  /*NASM*/;
			*asm_flags = 0;
		} else if (strcmp(asmname, "yasm") == 0) {	
			assembler = ASM_YASM; /* XXX */
			strcpy(asm_flags, "-f elf -m amd64");
		} else if (strcmp(asmname, "gas") == 0
			|| strcmp(asmname, "as") == 0)	{
			if (strcmp(asmflag, "gas") == 0) {
			asmflag = n_xstrdup("as");
			asmname = "gas";
			}
			assembler = ASM_GAS;
			*asm_flags = 0;
		} else {
			(void) fprintf(stderr, "Unknown assembler `%s'\n", asmflag);
		}

		if (sd_host_sys == OS_OSX) {
			sprintf(asm_flags, "-arch %s -force_cpusubtype_ALL ",
				archflag == ARCH_X86? "i386": "x86_64");
		}
	} else if (sd_host_arch == ARCH_MIPS) {
		if (sd_host_sys == OS_IRIX) {
			assembler = ASM_SGI_AS; /* XXX */
			if (asmflag == NULL) {
				asmflag = "/usr/lib32/cmplrs/asm";
				asmname = "as";
			}
			if (abiflag == ABI_MIPS_N64) {
				strcpy(asm_flags, "-pic2 -elf -EB -O0 -g0 -G0 -w -mips3 "
					"-64 -t5_ll_sc_bug ");	
			} else {
				strcpy(asm_flags, "-pic2 -elf -EB -O0 -g0 -G0 -w -mips3 "
					"-n32 -t5_ll_sc_bug ");	
			}
		} else if (sd_host_sys == OS_LINUX) {
			assembler = ASM_SGI_AS; /* XXX */
			asmname ="as";
			asmflag = "as";
			if (abiflag == ABI_MIPS_N64) {
				strcpy(asm_flags, "-EL -mabi=64 -mno-shared -KPIC");
			} else {
				strcpy(asm_flags, "-EL -mabi=n32 -mno-shared -KPIC");
			}
		}
	} else if (sd_host_arch == ARCH_SPARC) {
		if (abiflag == ABI_SPARC64) {
			if (sd_host_sys != OS_LINUX) {
				if (asmflag == NULL
					|| strcmp(asmflag, "as") == 0) {
					asmflag = n_xstrdup("/usr/ccs/bin/as");
					asmname = "as";
				}
				strcpy(asm_flags, "-xarch=v9 ");
				if (picflag) {
					strcat(asm_flags, "-K PIC ");
				}
			} else {
				if (asmflag == NULL) {
					asmflag = n_xstrdup("/usr/ccs/bin/as");
					asmname = "as";
				}
				strcpy(asm_flags, "--64 -Av9a -Qy ");
			}
		}
	} else if (sd_host_arch == ARCH_POWER) {
		if (sd_host_sys == OS_AIX) {
#if 0
	strcpy(asm_flags, "-u -mcom ");
#endif
			if (asmflag == NULL) {
				asmflag = n_xstrdup("as");
				asmname = "as";
			}
			if (abiflag == ABI_POWER64) {
				strcpy(asm_flags, "-u -many -a64 -mppc64");
			} else {
				strcpy(asm_flags, "-u -many");
			}
		} else if (sd_host_sys == OS_LINUX) {
			if (asmflag == NULL) {
				asmflag = n_xstrdup("as");
				asmname = "as";
			}
			if (abiflag == ABI_POWER64) {
				strcpy(asm_flags, "-a64 -mppc64 -many");
			} else {
				strcpy(asm_flags, "");
			}
		} else {
			(void) fprintf(stderr, "FATAL ERROR: Unsupported system\n");
			exit(EXIT_FAILURE);
		}
	} else {
		(void) fprintf(stderr, "FATAL ERROR: Unsupported host architecture\n");
		exit(EXIT_FAILURE);
	}

	if (asm_cmdline != NULL) {
		strncat(asm_flags, asm_cmdline,
			sizeof asm_flags-strlen(asm_flags));
	}

	if (custom_asm_args != NULL) {
		char	*p;
		
		for (p =  custom_asm_args; *p != 0; ++p) {
			if (*p == ',') {
				*p = ' ';
			}
		}
		
		p = strchr(asm_flags, 0);
		/* XXX check buffer size */
		sprintf(p, " %s ", custom_asm_args);
	}


	if (asmname == NULL) {
		asmname = asmflag;
	}

	if (cflag && oflag) {
		/* nwcc -c foo.c -o foo.o */
		/* XXX */
		sprintf(asm_flags+strlen(asm_flags), " -o %s", out_file);
	}

	make_room(&ld_flags, &ldalloc, sizeof "-o" + strlen(out_file));
	ldpos += sprintf(ld_flags, "-o %s", out_file);
	for (i = 0; i < ldind; ++i) {
		make_room(&ld_flags, &ldalloc,
				ldpos + strlen(ld_args[i]) + 2);
		ldpos += sprintf(ld_flags+ldpos, " %s", ld_args[i]);
	}
	
	if (asm_cmdline) {
		free(asm_cmdline);
	}
	if (ld_cmdline) {
		free(ld_cmdline);
	}

	if (write_fcat_flag) {
		static char	*dummy[2];
		dummy[0] = "dummy.i";
		dummy[1] = NULL;
		return driver(cpp_args, asm_flags, ld_flags, dummy);
	}
#if 0
	if (write_config_flag) {
		/*
		 * 07/24/12: Write a configuration file with the most important
		 * current settings
		 */
	}
#endif

	return driver(cpp_args, asm_flags, ld_flags, argv+1);
}

