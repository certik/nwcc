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
 * Functions to execute cpp/NASM/ncc
 */
#include "cc1_main.h"
#include "exectools.h"
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "defs.h"
#include "analyze.h"
#include "error.h"
#include "lex.h"
#include "misc.h"
#include "functions.h"
#include "scope.h"
#include "backend.h"
#include "typemap.h"
#include "zalloc.h"
#include "debug.h"
#include "symlist.h"
#include "sysdeps.h"
#include "config.h"
#include "n_libc.h"
#include "fcatalog.h"
#include "standards.h"

#if USE_ZONE_ALLOCATOR
/* Some includes for zalloc_init() */
#  include "control.h"
#  include "expr.h"
#  include "functions.h"
#  include "icode.h"
#  include "reg.h"
#  include "subexpr.h"
#endif


int	stackprotectflag;
int	ansiflag;
int	pedanticflag;
int	verboseflag;
int	stupidtraceflag;
int	gnuheadersflag = 1;
int	gflag;
int	Eflag;
int	Oflag = /*-1*/ 0; /* XXX set to 0 */
int	assembler; /* XXX unused by nwcc1, necessary for exectools linking */
int	abiflag;
int	archflag;
int	sysflag; /* 01/31/09: Specify system using -sys */
int	picflag;
int	timeflag;
char	*input_file;
char	*asmflag;
char	*asmname;
char	*gnuc_version;
char	*cpp;

char	*custom_cpp_args;

/*
 * 12/25/08: For PPC - Full or minimal TOC
 */
int	mintocflag;
int	fulltocflag;

/*
 * 03/02/09: -funsigned-char and -fsigned-char
 */
int	funsignedchar_flag;
int	fsignedchar_flag;
/*
 * 05/17/09: Added support for common variables
 */
int	fnocommon_flag;
int	use_common_variables;

/*
 * 05/18/09: Added -notgnu
 */
int	notgnu_flag;
int	color_flag;

int	dump_macros_flag;

int	write_fcat_flag;
int	save_bad_translation_unit_flag;


static char *
check_preprocessor(const char *path, const char *name, int *using_nwcpp) {
	if (strcmp(name, "nwcpp") == 0) {
		*using_nwcpp = 1;
	} else if (strcmp(name, "cpp") == 0) {
		; /* OK - cpp  (XXX: check system?) */
	} else if (strcmp(name, "gcc") == 0) {
		/* OK - gcc - append -E */
		char	*temp;

		temp = n_xmalloc(strlen(path)
			+ sizeof " -E");
		sprintf(temp, "%s -E", path);
		path = temp;
	} else {
		(void) fprintf(stderr, "Unrecognized "
			"preprocessor `%s' - must be "
			"gcc, cpp or nwcpp\n",
			name);
		return NULL;
	}
	return (char *)path;
}

#define REM_EXIT(n1, n2) do { \
	if (is_tmpfile) { \
		if (!save_bad_translation_unit_flag) { \
			(void) remove(n1); \
		} \
	} \
	(void) remove(n2); \
	return EXIT_FAILURE; \
} while (0)

static char *
do_cpp(char *file, char **args, int cppind) {
	/* XXX FILENAME_MAX is broken on HP-UX */
	static char	output_path[FILENAME_MAX + 1];
	char		buf[FILENAME_MAX + 1];
	char		tmpbuf[128] = "/var/tmp/cpp";
	char		*arch;
	char		*progname;
	char		*progflag = "-E";
	char		*p;
	char		*gnooh = NULL;
	char		*gnooh2 = NULL;
	int		using_nwcpp = 0;
	int		i;
	int		host_sys;
	FILE		*fd;
	FILE		*fd2;

	host_sys = sysdep_get_host_system();

	if (sysdep_get_host_system() == OS_MIRBSD) {
		progname = "mgcc";
	} else {
		progname = "gcc";
	}

	if (cpp == NULL) {
		cpp = getenv("NWCC_CPP");
		if (cpp == NULL) {
			/*
			 * No user preference; We use gcc -E if available,
			 * otherwise cpp, otherwise nwcpp. gcc comes
			 * before cpp because e.g. GNU cpp on OpenBSD seems
			 * very broken. Also, if we're on a different Unix
			 * system, its cpp may not be compatible with nwcc.
			 * nwcpp last because it's likely to be the most
			 * buggy of all at this point :-(
			 */
			if (find_cmd(progname, NULL, 0) != 0) {
				if (find_cmd("cpp", NULL, 0) == 0) {
					progname = "cpp";
					progflag = "";
				} else {
					progname = INSTALLDIR "/nwcc/bin/nwcpp";
					progflag = "";
					using_nwcpp = 1;
				}
			} else {
				; /* progname is already set to gcc -E */
			}
		}
	}
	if (cpp != NULL) {
		/*
		 * A preprocessor was selected using NWCC_CPP or
		 * -cpp. If it begins with a slash, we take it for
		 * an absolute path, otherwise it is presumably a
		 * binary located in one of the $PATH directories
		 */
		progflag = "";
		if (cpp[0] == '/') {
			struct stat	sbuf;

			if (stat(cpp, &sbuf) == -1) {
				(void) fprintf(stderr, "Fatal error: "
					"Preprocessor `%s' "
					"not accessible\n", cpp);
				return NULL;
			}
			progname = cpp;
			p = strrchr(cpp, '/');
			progname = check_preprocessor(cpp, p+1, &using_nwcpp);
			if (progname == NULL) {
				return NULL;
			}
		} else {
			if (find_cmd(cpp, NULL, 0) == 0) {
				progname = check_preprocessor(cpp, cpp, &using_nwcpp);
				if (progname == NULL) {
					return NULL;
				}
			} else {
				if (strcmp(cpp, "nwcpp") == 0) {
					progname = INSTALLDIR "/nwcc/bin/nwcpp";
					using_nwcpp = 1;
				} else {
					(void) fprintf(stderr, "Fatal error: "
						"Cannot find preprocessor "
						"`%s' in $PATH\n", cpp);
					return NULL;
				}
			}
		}
	}

	/*
	 * 03/02/09: Pass -funsigned-char/-fsigned-char so that it can pass
	 * proper macro definitions to libc's limits.h (for CHAR_MAX/MIN)
	 */
	if (cross_get_char_signedness() == TOK_KEY_UNSIGNED) {
		args[cppind++] = "-funsigned-char";
	} else {
		args[cppind++] = "-fsigned-char";
	}

	if (using_nwcpp) {
		/*
		 * If we're cross-compiling, we have to pass on the
		 * architecture and ABI to nwcpp
		 */
		if (archflag != 0) {
			args[cppind++] = arch_to_option(archflag);
		}
		if (abiflag != 0) {
			args[cppind++] = abi_to_option(abiflag);
		}	
	} else {
		int	host_arch;
		int	host_abi;
		int	host_sys;

		get_host_arch(&host_arch, &host_abi, &host_sys);
		/*
		 * 11/10/08: This passes flags depending on the host
		 * architecture; This is wrong in that we may be
		 * generating for a different target (e.g. we're on
		 * 64bit PPC but generate for 64bit SPARC - In that
		 * case the code below will assume a 32bit PPC ABI.
		 * And it's correct in that a typical preprocessor
		 * will only recognize the host architecture, so
		 * picking wrong flags is better than nothing.
		 *
		 * XXX We should at least pick the most suitable
		 * flags, e.g. 64bit SPARC ABI yields 64bit PPC host
		 * flags
		 */
		if (host_arch == ARCH_MIPS) {
			if (abiflag == ABI_MIPS_N64) {
				args[cppind++] = "-mabi=64";
			} 
		} else if (host_arch == ARCH_POWER) {
			if (abiflag == ABI_POWER64) {
				if (host_sys == OS_AIX) {
					args[cppind++] = "-maix64";
				} else {
					args[cppind++] = "-m64";
				}
			} else {
				if (host_sys != OS_AIX) {
					args[cppind++] = "-m32";
				}	
			}
		}
	}

	if (custom_cpp_args != NULL) {
		char	*p;
		char	*start;

		for (p = start = custom_cpp_args; *p != 0; ++p) {
			if (*p == ',' || p[1] == 0) {
				if (*p == ',') {
					*p = 0;
				}
				/* XXX Check overflow!!!!!! */
				args[cppind++] = start; 
				start = p+1;
			}
		}
	}

	args[cppind] = NULL;

	tunit_name = n_xmalloc(strlen(file) + 1);
	for (p = file, i = 0; *p != 0; ++p) {
		if (isalnum((unsigned char)*p) || *p == '_') {
			tunit_name[i++] = *p;
		}
	}
	tunit_name[i] = 0;

	if ((p = strrchr(tunit_name, '.')) != NULL) {
		*p = 0;
	}
	
	input_file = n_xstrdup(file);
	if (Eflag) {
		fd = stdout;
	} else if (save_bad_translation_unit_flag) {
		sprintf(output_path, "%s.i", input_file);
		fd = fopen(output_path, "w");
		if (fd == NULL) {
			perror(output_path);
			return NULL;
		}
	} else {
		fd = get_tmp_file(tmpbuf, output_path, "cpp");
		if (fd == NULL) {
			return NULL;
		}
	}

	arch = "";

	if (gnuheadersflag && stdflag != ISTD_C89 && !notgnu_flag) {
		if (host_sys == OS_LINUX) {
			/*
			 * 20141114: The __GNUC__ version we pretend to support can
			 * now be specified by using an environment variable
			 */
			static char	version_buffer[] = "-D__GNUC__=X";
			const char 	*envvar = getenv("NWCC_DEFINE_GNUC_MACRO");
			int		version = '3';

			if (envvar != NULL && isdigit((unsigned char)*envvar)) {
				version = *envvar;
			}
			*strchr(version_buffer, 'X') = version;

#ifdef GNUBYDEFAULT
			/*
			 * 07/01/07: Because the major()/minor() macros on my
			 * old SuSE system do not work with a GNU C version of
			 * less than 2, now is probably the time we have to
			 * bump it up. This will probably open a can of worms,
			 * but has to be done some time
			 */

			/*
			 * 07/22/07: That goddamn GNU make tests for version
			 * 2.5... and if that's not present, it #defines
			 * __attribute__ to expand to an empty body. That
			 * breaks stuff like wait() which uses a transparent_
			 * union attribute. So let's set the rest on fire as
			 * well by bumping straight to 3
			 */
#if 0
		gnooh = " -U__GNUC__ -D__GNUC__=3 "; /* was 1 , then 2 !!! */
#endif

			gnooh = "-U__GNUC__"; /* was 1 , then 2 !!! */
			gnooh2 = version_buffer; /* was 1 , then 2 !!! */
#else
			gnooh = version_buffer; /* was 1, then 2 !!! */
			gnooh2 = "";
#endif
			if (using_nwcpp) {
				gnooh = gnooh2 = ""; /* XXX */
			}
			fd2 = exec_cmd(0, progname, "%s %s %s%s%s%[] %s",
				progflag,
				"-D__NWCC__=1",
				arch, gnooh, gnooh2, args, file);
		} else {
			fd2 = exec_cmd(0, progname, "%s %s %s %[] %s",
				progflag,
				"-D__NWCC__=1",
				arch, args, file);
		}
	} else {
		if (host_sys == OS_FREEBSD
			|| host_sys == OS_DRAGONFLYBSD
			|| host_sys == OS_OPENBSD
			|| host_sys == OS_MIRBSD) {
			/*
			 * The FreeBSD headers are worthless without __GNUC__, so
			 * let's invoke cpp with default settings (enables __GNUC__)
			 */
			fd2 = exec_cmd(0, progname, "%s %s %s %s %s %[] %s",
				progflag,
				ansiflag? "-D__aligned\\(x\\)=": "",
				"-D__NWCC__=1",
				(notgnu_flag || ansiflag)? "-U__GNUC__": "",
				arch, args, file);
		} else {
			fd2 = exec_cmd(0, progname, "%s %s %s %s %[] %s",
				progflag,
				"-D__NWCC__=1",
				arch,
				"-U__GNUC__",
				args, file);
		}
	}

	if (fd2 == NULL) {
		perror("popen");
		fclose(fd);
		remove(output_path);
		return NULL;
	}

	while (fgets(buf, sizeof buf, fd2)) {
		fputs(buf, fd);
	}

	{
		int	rc;
		int	bad = 0;

		wait(&rc);
		if (!WIFEXITED(rc)) {
			(void) fprintf(stderr, "*** cpp crashed\n");
			bad = 1;
		} else if (WEXITSTATUS(rc) != 0) {
			(void) fprintf(stderr, "*** cpp returned nonzero exit status.\n");
			bad = 1;
		}
		fclose(fd2);
		if (bad) {
			fclose(fd);
			if (!save_bad_translation_unit_flag) {
				remove(output_path);
			}
			return NULL;
		}
	}

	fclose(fd);
	return output_path;
}


static char *garbage;

static void
remove_garbage(void) {
	if (errors) {
		remove(garbage);
	}
}

static int	timing_cpp;

static int
do_ncc(char *cppfile, char *nccfile, int is_tmpfile) {
	static int		inits_done;
	int			fildes;
	FILE			*input;
	FILE			*fd;
	char			*p;
	static struct timeval	tv;
	static int		timing_init;
	static int		timing_lex;
	static int		timing_analysis;
	static int		timing_gen;
	struct stat		sbuf;

	if (timeflag) {
		/* Time initialization stuff */
		start_timer(&tv);
	}
	/* Generate in CWD */
	if ((p = strrchr(nccfile, '/')) != NULL) {
		nccfile = p+1;
	}

	garbage = nccfile;
	atexit(remove_garbage);
	if (inits_done == 0) {
		init_keylookup();
		init_oplookup();
		inits_done = 1;
	}
	toklist = NULL;
	funclist = NULL;

	if (!write_fcat_flag) {
		input = fopen(cppfile, "r");
		if (input == NULL) {
			perror(cppfile);
			return EXIT_FAILURE;
		}
	}
	
	if (dump_macros_flag) {
		char	buf[1024];

		while (fgets(buf, sizeof buf, input) != NULL) {
			printf("%s", buf);
		}
		return 0;
	}

	if (write_fcat_flag) {
		fd = NULL;
	} else {
		(void) unlink(nccfile); /* trash stale .asm file */

		if ((fildes = open(nccfile, O_CREAT | O_EXCL | O_RDWR, S_IRWXU))
			== -1) {
			perror(nccfile);
			if (is_tmpfile) remove(cppfile);
			return EXIT_FAILURE;
		}
		if ((fd = fdopen(fildes, "r+")) == NULL) {
			perror(nccfile);
			REM_EXIT(cppfile, nccfile);
			return EXIT_FAILURE;
		}
	}

	/*
	 * It is important to initialize the backend before doing
	 * lexical analysis because architecture and ABI information
	 * are needed
	 */
	if (init_backend(fd, &global_scope) != 0) {
			REM_EXIT(cppfile, nccfile);
		}

	#if USE_ZONE_ALLOCATOR
		zalloc_create();
		zalloc_init(Z_CONTROL, sizeof(struct control), 1, 0);
		/*
		 * 10/20/09: Disable label memory reclaimation for now. This is
		 * needed since the switch label changes were made, or else the
		 * ctrl->labels (ctrl_to_icode() for TOK_KEY_SWITCH) list will
		 * end up containing a member that links to itself.
		 */
		zalloc_init(Z_LABEL, sizeof(struct label), 1, 1);
		zalloc_init(Z_EXPR, sizeof(struct expr), 1, 0);  /* XXX doesn't work */
		zalloc_init(Z_INITIALIZER, sizeof(struct initializer), 1, 1);
		zalloc_init(Z_STATEMENT, sizeof(struct statement), 1, 1);
		zalloc_init(Z_FUNCTION, sizeof(struct function), 1, 1);
		zalloc_init(Z_ICODE_INSTR, sizeof(struct icode_instr), 1, 0);
		zalloc_init(Z_ICODE_LIST, sizeof(struct icode_list), 1, 0);
		zalloc_init(Z_VREG, sizeof(struct vreg), 1, 0);
		zalloc_init(Z_STACK_BLOCK, sizeof(struct stack_block), 1, 0);
		zalloc_init(Z_S_EXPR, sizeof(struct s_expr), 1, 0);
		zalloc_init(Z_FCALL_DATA, sizeof(struct fcall_data), 1, 0);
/*	zalloc_init(Z_IDENTIFIER, sizeof(struct control), 1);*/
#if FAST_SYMBOL_LOOKUP
	zalloc_init(Z_FASTSYMHASH, sizeof(struct fast_sym_hash_entry), 1, 0);
#endif

	zalloc_init(Z_CEXPR_BUF, 16, 1, 1); /* XXX */

#endif


	if (write_fcat_flag) {
		/*
		 * 07/27/09: Parse function catalog and write index file.
		 * We do this here because there are various parser
		 * initializations which shouldn't be missed
		 */
		if (is_tmpfile) (void) remove(cppfile);
		(void) remove(nccfile);
	
		return fcat_write_index_file("fcatalog.idx", "fcatalog");
	}

	if (stat(INSTALLDIR "/nwcc/lib/fcatalog.idx", &sbuf) == 0) {
		(void) fcat_open_index_file(INSTALLDIR "/nwcc/lib/fcatalog.idx");
	} else {
		(void) fcat_open_index_file("fcatalog.idx");
	}

	if (timeflag) {
		timing_init = stop_timer(&tv);
		start_timer(&tv);
	}

	if (lex(input) != 0) {
		REM_EXIT(cppfile, nccfile);
	}

	if (timeflag) {
		timing_lex = stop_timer(&tv);
		start_timer(&tv);
	}
		

#if XLATE_IMMEDIATELY
	/* Prepare .asm file for code generation */
	backend->gen_prepare_output();
#endif
	
	/* Now compile all code */
	if (analyze(NULL) != 0) {
		REM_EXIT(cppfile, nccfile);
	}
	
#if XLATE_IMMEDIATELY
	if (!errors) {
		/* Finish code generation */
		backend->gen_finish_output();
	}
#endif

	if (timeflag) {
		timing_analysis = stop_timer(&tv);
		start_timer(&tv);
	}

#if ! XLATE_IMMEDIATELY
	/*
	 * All code has been parsed and translated to icode, and can now
	 * be written as a whole .asm file in one step
	 */
	if (errors || backend->generate_program() != 0) {
		;
	}
#endif

	if (timeflag) {
		timing_gen = stop_timer(&tv);
	}

	/* destroy_toklist(&toklist); */
	fclose(input);
	if (is_tmpfile) {
		if (!save_bad_translation_unit_flag) {
			remove(cppfile);
		}
	}

	if (color_flag) {
		reset_text_color();
	}

	(void) fprintf(stderr, "%s - %u error(s), %u warning(s)\n",
		cppfile, (unsigned)errors, (unsigned)warnings);

	if (timeflag) {
		int	timing_total = timing_cpp + timing_init + timing_lex +
				timing_analysis + timing_gen;

#define RESULT(x) x / 1000000.0, (float)x / timing_total * 100
		(void) fprintf(stderr, "=== Timing of nwcc1 ===\n");
		(void) fprintf(stderr, "   Preprocessing:   %f sec  "
			"(%f%% of total)\n", RESULT(timing_cpp));
		(void) fprintf(stderr, "   Initialization:  %f sec  "
			"(%f%% of total)\n", RESULT(timing_init));
		(void) fprintf(stderr, "   Lexing:          %f sec  "
			"(%f%% of total)\n", RESULT(timing_lex));
		(void) fprintf(stderr, "   Parsing+icode:   %f sec  "
			"(%f%% of total)\n", RESULT(timing_analysis));
		(void) fprintf(stderr, "   Emission:        %f sec  "
			"(%f%% of total)\n", RESULT(timing_gen));
	}
	
	if (errors) {
		remove(nccfile);
		return EXIT_FAILURE;
	}
	return 0;
}


static void 
segv_handler(int s) {
	char	*p;
	(void) s;
	p = "Segmentation fault\n";
	write(1, p, strlen(p));
	exit(EXIT_FAILURE); /* Dangerous, but necessary for debugging */
}

static void
usage(void) {
	/* XXX add useful stuff here */
	puts("You invoked nwcc1 incorrectly. Please refer to the README file");
	puts("for supported command line arguments");
	exit(EXIT_FAILURE);
}



int
main(int argc, char *argv[]) {
	int			ch;
	char			*p;
	char			*nccfile = NULL;
	char			*tmp;
	char			*target_str = NULL;
	char			*abi_str = NULL;
	char			*sys_str = NULL;
	char			*cpp_args[128];
	int			cppind = 0;
	int			is_tmpfile = 0;
	int			nostdincflag = 0;
	static struct timeval	tv;
	struct ga_option	options[] = {
		{ 'D', NULL, 1 },
		{ 'U', NULL, 1 },
		{ 'I', NULL, 1 },
		{ 'E', NULL, 0 },
		{ 'g', NULL, 0 },
		{ 0, "stackprotect", 0 },
		{ 0, "nostdinc", 0 },
		{ 0, "arch", 1 },
		{ 0, "gnuc", 1 },
		{ 0, "cpp", 1 },
#ifdef __sun
		{ 0, "xarch", 1 },
#endif
		{ 0, "mabi", 1 },
		{ 0, "abi", 1 },
		{ 0, "sys", 1 },
		{ 0, "pedantic", 0 },
		{ 0, "verbose", 0 },
		{ 0, "stupidtrace", 0 },
		{ 0, "std", 1 },
		{ 0, "asm", 1 },
		{ 0, "fpic", 0 },
		{ 0, "fPIC", 0 },
		{ 0, "ansi", 0 },
		{ 0, "time", 0 },
		{ 0, "dM", 0 },
		{ 0, "mminimal-toc", 0 },
		{ 0, "mfull-toc", 0 },
		{ 0, "funsigned-char", 0 },
		{ 0, "fsigned-char", 0 },
		{ 0, "fno-common", 0 },
		{ 0, "notgnu", 0 },
		{ 0, "gnu", 0 },
		{ 0, "color", 0 },
		{ 0, "Wp", 1 },
		{ 0, "O-1", 0 },
		{ 0, "O0", 0 },
		{ 0, "O1", 0 },
		{ 0, "O2", 0 },
		{ 0, "O3", 0 },
		{ 0, "write-fcat", 0 },
		{ 0, "save-bad-translation-unit", 0 }
	};	
	int			nopts = N_OPTIONS(options);
	int			idx;

	(void) segv_handler;
#if 0
	(void) signal(SIGSEGV, segv_handler);
	get_host_arch(&archflag, &abiflag);
#endif
	if (argc <= 1) {
		usage();
	}	

	while ((ch = nw_get_arg(argc-1, argv+1, options, nopts, &idx)) != -1) {
		switch (ch) {
		case 'D':
		case 'U':
		case 'I':
			if (cppind == 126) {
				(void) fprintf(stderr, "Too many stderr "
					     "arguments\n");
				return EXIT_FAILURE;
			}

			/* OpenBSD's cpp chokes on -D arg, needs -Darg ... */
			cpp_args[cppind] = n_xmalloc(strlen(n_optarg) + 3);
			sprintf(cpp_args[cppind++], "-%c%s", ch, n_optarg);
			break;
		case 'g':
			gflag = 1;
			/* XXX */gflag = 0;
			break;
		case 'E':
			Eflag = 1;
			break;
		case '!':
			if (nccfile != NULL) {
				(void) fprintf(stderr, "Error: More than one "
					      "input file specified\n");
				usage();
			}
			nccfile = n_xmalloc(strlen(n_optarg) + 16);
			strcpy(nccfile, n_optarg);
			break;
		case '?':
			if (idx) {
				if (options[idx].name[0] == 'O'
					&& (options[idx].name[1] == '-'
					|| isdigit((unsigned char)options[idx].
						name[1]))) {
					if (strcmp(options[idx].name, "O-1")
						== 0) {
						Oflag = -1;
					} else if (options[idx].name[1] == '0') {
						Oflag = 0;
					} else {
						; /* ignore for now */
					}
				} else if (strcmp(options[idx].name, "stackprotect")
					== 0) {
					stackprotectflag = 1;
				} else if (strcmp(options[idx].name, "ansi")
					== 0) {
					/*ansiflag = standard = C89;*/
					stdflag = option_to_std("c89");
					ansiflag = 1;
				} else if (strcmp(options[idx].name, "pedantic")
					== 0) {
					pedanticflag = 1;
				} else if (strcmp(options[idx].name, "verbose")
					== 0) {
					verboseflag = 1;
				} else if (strcmp(options[idx].name, "stupidtrace")
					== 0) {
					stupidtraceflag = 1;
				} else if (strcmp(options[idx].name, "nostdinc")
					== 0) {
					cpp_args[cppind++] = "-nostdinc";
					nostdincflag = 1;
				} else if (strcmp(options[idx].name, "asm")
					== 0) {
					asmflag = n_xstrdup(n_optarg);
					if ((asmname = strrchr(asmflag, '/')) != NULL) {
						++asmname;
					} else {
						asmname = asmflag;
					}
				} else if (strcmp(options[idx].name, "std")
					== 0) {
					stdflag = option_to_std(n_optarg);
				} else if (strcmp(options[idx].name, "dM")
					== 0) {
					cpp_args[cppind++] = n_xstrdup("-dM");
					dump_macros_flag = 1;
				} else if (strcmp(options[idx].name, "arch")
					== 0) {	
					if (target_str != NULL) {
						(void) fprintf(stderr, "-arch "
						"used more than once\n");
						exit(EXIT_FAILURE);
					}	
					target_str = n_xstrdup(n_optarg);
				} else if (strcmp(options[idx].name, "gnuc")	
					== 0) {	
					if (gnuc_version != NULL) {
						(void) fprintf(stderr, "-gnuc "
						"used more than once\n");
						exit(EXIT_FAILURE);
					}	
					gnuc_version = n_xstrdup(n_optarg);
				} else if (strcmp(options[idx].name, "cpp")
					== 0) {
					if (cpp != NULL) {
						(void) fprintf(stderr, "-cpp "
						"used more than once\n");
						exit(EXIT_FAILURE);
					}	
					cpp = n_xstrdup(n_optarg);
				} else if (strcmp(options[idx].name, "xarch")
					== 0) {
					/* SunCC compatibility */
					;
				} else if (strcmp(options[idx].name, "mfull-toc")
					== 0) {
					fulltocflag = 1;
				} else if (strcmp(options[idx].name, "mminimal-toc")
					== 0) {
					mintocflag = 1;
				} else if (strcmp(options[idx].name, "mabi")
					== 0
					|| strcmp(options[idx].name, "abi")
					== 0) {
					if (abi_str != NULL) {
						(void) fprintf(stderr, "-%s "
						"used more than once\n",
						options[idx].name);
						exit(EXIT_FAILURE);
					}	
					abi_str = n_xstrdup(n_optarg);
#if 0
					abiflag = ascii_abi_to_value(
						n_optarg, archflag);
#endif
				} else if (strcmp(options[idx].name, "sys") == 0) {
					if (sys_str != NULL) {
						(void) fprintf(stderr, "-sys "
							"used more than once");
						exit(EXIT_FAILURE);
					}
					sys_str = n_xstrdup(n_optarg);
				} else if (strcmp(options[idx].name, "fpic")
					== 0
					|| strcmp(options[idx].name, "fPIC")
					== 0) {
					picflag = 1;
				} else if (strcmp(options[idx].name, "fsigned-char")
					== 0) {
					fsignedchar_flag = 1;
				} else if (strcmp(options[idx].name, "funsigned-char")
					== 0) {
					funsignedchar_flag = 1;
				} else if (strcmp(options[idx].name, "fno-common")
					== 0) {
					fnocommon_flag = 1;
				} else if (strcmp(options[idx].name, "notgnu") == 0) {
					notgnu_flag = 1;
				} else if (strcmp(options[idx].name, "gnu") == 0) {
					notgnu_flag = 0;
				} else if (strcmp(options[idx].name, "color") == 0) {
					color_flag = 1;
				} else if (strcmp(options[idx].name, "time")
					== 0) {
					timeflag = 1;
				} else if (strcmp(options[idx].name, "Wp") == 0) {
					custom_cpp_args = n_xstrdup(n_optarg);
				} else if (strcmp(options[idx].name, "write-fcat") == 0) {
					write_fcat_flag = 1;
				} else if (strcmp(options[idx].name, "save-bad-translation-unit") == 0) {
					save_bad_translation_unit_flag = 1;
				} else {
					usage();
				}
			} else {
				usage();
			}
			break;
		default:
			return EXIT_FAILURE;
		}
	}


	if (nccfile == NULL) {
		/* XXX use stdin */
		(void) fprintf(stderr, "No input file specified.\n");
		return EXIT_FAILURE;
	}

	if (stdflag == ISTD_NONE) {
		stdflag = set_default_std();
	}

	if (gnuc_version == NULL) {
		gnuc_version = "2";
	}

	set_target_arch_and_abi_and_sys(&archflag, &abiflag, &sysflag, target_str, abi_str, sys_str);
	
	if (!nostdincflag) {
		if (archflag == ARCH_SPARC && sysdep_get_host_system() == OS_SOLARIS) {
			cpp_args[cppind++] = "-nostdinc";
			cpp_args[cppind++] = "-I/usr/include";
		}
	}

	{
		/* 
		 * 05/13/09: Added this.
		 * XXX Should we ever handle non-GNU preprocessors? (ucpp
		 * may be nice to have)
		 */
		static char	stdbuf[16];
		sprintf(stdbuf, "-std=%s", get_selected_std_name());
		cpp_args[cppind++] = stdbuf;
	}

	if (abiflag == ABI_POWER64) {
#if 0
		cpp_args[cppind++] = "-D__64BIT__=1";
		cpp_args[cppind++] = "-U__LONG_MAX__";
#endif
		/*
		 *  XXX -maix64 doesn't work with GNU cpp on non-AIX
		 * platforms...
		 */
		if (sysdep_get_host_system() == OS_AIX) {
			cpp_args[cppind++] = "-maix64";
		}
#if 0
		if (sizeof(long) != 8) {
			(void) fprintf(stderr, "ERROR: This compiler was not "
				"built with 64bit support\n");
			exit(EXIT_FAILURE);
		}
#endif
	} else if (abiflag == ABI_POWER32) {
#if 0
		if (sizeof(long) == 8) {
			/* Built for 64bit */
			abiflag = ABI_POWER64;
		}
#endif
	} else if (abiflag == ABI_MIPS_N64) {
		if (sysdep_get_host_system() == OS_IRIX) {
			cpp_args[cppind++] = "-mabi=n64";
		} else if (sysdep_get_host_system() == OS_LINUX) {
			cpp_args[cppind++] = "-mabi=64";
		}
	} else if (abiflag == ABI_SPARC64) {
		if (sysdep_get_host_arch() == ARCH_SPARC) {
			cpp_args[cppind++] = "-m64";
		}
	}

	if (sysflag == OS_OSX) {
		if (archflag == ARCH_AMD64) {
			cpp_args[cppind++] = "-m64";
		} else {
			cpp_args[cppind++] = "-m32";
		}
	}

	cpp_args[cppind] = NULL;

	/*
	 * 03/02/09: Before calling cross_initialize_type_map(), determine
	 * the plain ``char'' signedness! This must be done beforehand to
	 * ensure that the map is initialized with proper signedness
	 */
	{
		/*
		 * XXX This is done in do_cpp() too - combine!!!
		 */
		int	host_arch;
		int	host_abi;
		int	host_sys;

		get_host_arch(&host_arch, &host_abi, &host_sys);
		cross_set_char_signedness(funsignedchar_flag, fsignedchar_flag,
				host_arch, archflag);
	}

	cross_initialize_type_map(archflag, abiflag, sysflag);

	if (archflag == ARCH_MIPS && get_target_endianness() == ENDIAN_LITTLE) {
		cross_get_target_arch_properties()->endianness = ENDIAN_LITTLE;
	}

	/*
	 * 05/17/09: Use a more descriptive name (without negation)
	 */
	use_common_variables = !fnocommon_flag;
#if 0
optind = 0;
argv[0] = "new.c";
#endif
	if ((p = strrchr(nccfile, '.')) == NULL
		|| (strcmp(++p, "c") != 0
		&& strcmp(p, "i") != 0)) {
		fprintf(stderr, "%s: Invalid input file name\n", nccfile);
		return EXIT_FAILURE;
	}

	if (timeflag) {
		start_timer(&tv);
	}

	if (strcmp(p, "i") == 0) {
		/* Already preprocessed */
		tmp = n_xstrdup(nccfile);
	} else {
		is_tmpfile = 1;
		if ((tmp = do_cpp(nccfile, cpp_args, cppind)) == NULL) {
			return EXIT_FAILURE;
		}
	}	

	if (timeflag) {
		timing_cpp = stop_timer(&tv);
	}

	if (Eflag) {
		/* Done! */
		return 0;
	}
	strcpy(p, "asm");

	if (mintocflag) {
		mintocflag = 2;
	}

	if (sysflag == OS_OSX) {
		picflag = 1;
	}

	return do_ncc(tmp, nccfile, is_tmpfile);
}

