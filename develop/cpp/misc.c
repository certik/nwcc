/*
 * Copyright (c) 2003 - 2009, Nils R. Weller
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
 * Various utility functions
 */
#include "misc.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include "token.h"
/*#include "debug.h"*/
#include "n_libc.h"
#include "archdefs.h"

static int 
do_string(char *buf, char *str, size_t size, size_t *len, int dospace) {
	size_t	slen = strlen(str) * 2; /* XXX */
	char	*p;
	char	*bufp = buf + strlen(buf);

	if (slen > (size - *len - (dospace != 0))) {
		(void) fprintf(stderr, "Arguments too long\n");
		return -1;
	}	
#if 0
	strcat(buf, str);
	*len += slen;
#endif
	/*
	 * XXX workaround because of popen() trashing " chars. This is
	 * broken too because it will also trash some command lines,
	 * but those are less likely to occur. Fix exec_cmd()!!!!
	 */
	for (p = str; *p != 0; ++p) {
		if (*p == '"') {
			*len += 2;
			*bufp++ = '\\';
			*bufp++ = '"';
		} else {
			*len += 1;
			*bufp++ = *p;
		}
	}
	/*--*len*/
	*bufp = 0;

	if (dospace) {
		strcat(buf, " ");
		++*len;
	}	
	return 0;
}


/*
 * XXX this SUCKS! popen() should not be used at all because its shell
 * processing will trash command line arguments :-(
 * ./nwcc -DFOO='"bar"' file.c
 * ... the "" quotes are trashed here, thus cpp receives -DFOO=bar
 * Use fork()/pipe()/exec*() manually
 */
FILE *
exec_cmd(int use_popen, char *prog, char *format, ...) {
	va_list		v;
	char		buf[4096] = { 0 };
	char		*p;
	int		err = 0;
	size_t		len = 0;
	char		*arguments[4096];
#define N_ARGS (sizeof arguments / sizeof arguments[0])
	int		args_used = 0;
	
	va_start(v, format);

	if (!use_popen) {
		arguments[args_used++] = prog;
	}

	strncpy(buf, prog, sizeof buf);
	buf[sizeof(buf) - 1] = 0;
	len = strlen(buf);
	while (*format) {
		switch (*format) {
		case '%':
			switch (*++format) {
			case 's':
				p = va_arg(v, char *);
				if (use_popen) {
					if (do_string(buf, p, sizeof buf, &len, 0)
						!= 0) {
						err = 1;
					}	
				} else {
					if ((unsigned)args_used >= N_ARGS - 1) {
						(void) fprintf(stderr,
							"exec_cmd: Too many "
							"arguments\n");
						exit(EXIT_FAILURE);
					}
					while (isspace((unsigned char)*p)) {
						++p;
					}
					if (*p != 0) {
						arguments[args_used++] = p;
					}
				}
				break;
			case '[':
				if (*++format == ']') {
					char	**sv;
					int	i;

					sv = va_arg(v, char **);
					for (i = 0; sv[i] != NULL; ++i) {
						if (use_popen) {
							if (do_string(buf, sv[i],
								sizeof buf, &len, 1) != 0) {
								err = 1;
								break;
							}	
						} else {
							char	*p;

							if ((unsigned)args_used >= N_ARGS - 1) {
								(void) fprintf(stderr,
									"exec_cmd: Too many "
									"arguments\n");
								exit(EXIT_FAILURE);
							}
							p = sv[i];

							while (isspace((unsigned char)*p)) {
								++p;
							}
							if (*p != 0) {
								arguments[args_used++] = p;
							}
						}
					}	
					break;
				}
				/* FALLTHRU */
			default:
				(void) fprintf(stderr,
				"Unknown escape sequence - ignoring.\n");
				if (*format == 0) {
					err = 1;
				}
			}
			break;
		default:
			if (use_popen) {
				if (len < 4095) {
					buf[len] = *format;
					++len;
					buf[len] = 0;
				} else {
					fprintf(stderr, "Arguments too long.\n");
					err = 1;
					break;
				}
			} else {
				/*
				 * 03/01/09: For now we only support %s and %[]
				 * when using execv() instead of popen()
				 */
				if (!isspace((unsigned char)*format)) {
					fprintf(stderr, "exec_cmd: Bad format string\n");
					exit(EXIT_FAILURE);
				}
			}
		}
		if (err == 1) {
			return NULL;
		}
		if (*++format == 0) {
			break;
		}
	}
	va_end(v);

	if (use_popen) {
		buf[len] = 0;
#if 0 
		printf("Executing ``%s''\n", buf);
#endif
		return popen(buf, "r");
	} else {
		int	fds[2];
		pid_t	pid;

		if (pipe(fds) == -1) {
			perror("pipe");
			exit(EXIT_FAILURE);
		}
		arguments[args_used++] = NULL;
		if ((pid = fork()) == (pid_t)-1) {
			perror("fork");
			exit(EXIT_FAILURE);
		} else if (pid == 0) {
			/* Child */
#if 0 
			printf("Executing `%s' with args:\n", prog);
			{
				int	i;
				for (i = 0; arguments[i] != NULL; ++i) {
					printf("    %d: %s\n", i, arguments[i]);
				}
			}
#endif
			(void) close(fds[0]);
			(void) dup2(fds[1], STDOUT_FILENO);
			(void) execvp(prog, arguments);
			perror(prog);
			_exit(EXIT_FAILURE);
		} else {
			(void) close(fds[1]);
			return fdopen(fds[0], "r");
		}
	}
}

/*
 * Get default ABI for systems on where there are multiple supported
 * ABIs (PPC/MIPS currently.)
 */
void
get_default_abi(int *abi, int arch, int sys) {
	switch (arch) {
	case ARCH_MIPS:	
#ifdef __sgi
		if (sizeof(long)== 8) {
			*abi = ABI_MIPS_N64;
		} else
#endif
		{
			*abi = ABI_MIPS_N32;
		}
		break;
	case ARCH_POWER:
#if defined _AIX || defined _ARCH_PPC
		/* host equals target */
		if (sizeof(long) == 8) {
			*abi = ABI_POWER64;
		} else
#endif
		{
			/*
			 *  01/31/08: Don't use 32bit ABI by default
			 *  for Linux (hasn't been implemented yet)
			 */
			if (sys == OS_AIX) {
				*abi = ABI_POWER32;
			} else {
				*abi = ABI_POWER64;
			}
		}	
		break;
	case ARCH_SPARC:
#if !defined __i386__ && defined __sun
		/* host equals target */
		if (sizeof(long) == 8) {
			*abi = ABI_SPARC64;
		} else
#endif
		{
			/* XXX 32bit isn't supported yet */
			*abi = ABI_SPARC64;
		}
		break;
	default:	
		*abi = 0;
	}
}

void
get_default_sys(int *sys) {
#ifdef __sgi
	*sys = OS_IRIX;
#elif defined _AIX
	*sys = OS_AIX;
#elif defined __linux__
	*sys = OS_LINUX;
#elif defined __FreeBSD__
	*sys = OS_FREEBSD;
#elif defined __OpenBSD__
	*sys = OS_OPENBSD;
#elif defined __NetBSD__
	*sys = OS_NETBSD;
#elif defined __sun
	*sys = OS_SOLARIS;
#elif defined __APPLE__
	*sys = OS_OSX;
#else
	*sys = OS_LINUX;
#endif
}

/*
 * Get host architecture and ABI. These are then used as
 * target arch/ABI if no other target has been explicitly
 * requested
 */
void
get_host_arch(int *arch, int *abi, int *sys) {
	*abi = 0;
#ifdef __i386__
	*arch = ARCH_X86;
#elif defined __sgi
	*arch = ARCH_MIPS;
#elif defined _AIX
	*arch = ARCH_POWER;
#elif defined _ARCH_PPC
	*arch = ARCH_POWER;
#elif defined __amd64__ || defined __x86_64__
	*arch = ARCH_AMD64;
#elif defined __sun || defined __sparc
	*arch = ARCH_SPARC;
#else
	(void) fprintf(stderr, "Unknown host architecture!\n");
	exit(EXIT_FAILURE);
#endif
	get_default_sys(sys);
	get_default_abi(abi, *arch, *sys);
}

void
get_target_arch_by_name(int *arch, const char *name) {
	static struct {
		int		val;
		const char	*name;
	} archs[] = {
		{ ARCH_X86, "x86" },
		{ ARCH_AMD64, "amd64" },
		{ ARCH_POWER, "ppc" },
		{ ARCH_MIPS, "mips" },
		{ ARCH_SPARC, "sparc" },
		{ 0, NULL }
	};
	int	i;

	for (i = 0; archs[i].name != NULL; ++i) {
		if (strcasecmp(archs[i].name, name) == 0) {
			*arch = archs[i].val;
			return;
		}
	}
	(void) fprintf(stderr, "Unknown target architecture `%s'!\n",
		name);
	(void) fprintf(stderr, "Supported values for -arch are:\n");
	for (i = 0; archs[i].name != NULL; ++i) {
		(void) fprintf(stderr, "\t%s\n", archs[i].name);
	}	
	exit(EXIT_FAILURE);
}

static const struct system_info {
	int		val;
	const char	*name;
	int		archs[8];
} systems[] = {
	{ OS_LINUX, "linux", { ARCH_POWER, ARCH_X86, ARCH_AMD64, ARCH_SPARC,0,0,0,0 } },
	{ OS_FREEBSD, "freebsd", { ARCH_X86, ARCH_AMD64,0,0,0,0,0,0 } },
	{ OS_OPENBSD, "openbsd", { ARCH_X86, ARCH_AMD64,0,0,0,0,0,0 } },
	{ OS_NETBSD, "netbsd", { ARCH_X86,0,0,0,0,0,0,0 } },
	{ OS_IRIX, "irix", { ARCH_MIPS, 0,0,0,0,0,0,0 } },
	{ OS_SOLARIS, "solaris", { ARCH_X86, ARCH_SPARC, 0,0,0,0,0 } },
	{ OS_AIX, "aix", { ARCH_POWER, 0,0,0,0,0,0,0 } },
	{ OS_OSX, "osx", { ARCH_X86, ARCH_AMD64,0,0,0,0,0,0 } },
	{ 0, NULL, {0,0,0,0,0,0,0,0} }
};

void
check_arch_sys_consistency(int arch, int sys) {
	int	i;

	for (i = 0; systems[i].name != NULL; ++i) {
		if (sys == systems[i].val) {
			int	j;

			for (j = 0; systems[i].archs[j] != 0; ++j) {
				if (systems[i].archs[j] == arch) {
					return;
				}
			}
			(void) fprintf(stderr, "Invalid system/architecture "
				"combination\n");
			exit(EXIT_FAILURE);
		}
	}
}

void
get_target_sys_by_name(int *sys, const char *name) {
	int	i;

	for (i = 0; systems[i].name != NULL; ++i) {
		if (strcasecmp(systems[i].name, name) == 0) {
			*sys = systems[i].val;
			return;
		}
	}

	(void) fprintf(stderr, "Unknown target system `%s'!\n",
		name);
	(void) fprintf(stderr, "Supported values for -sys are:\n");
	for (i = 0; systems[i].name != NULL; ++i) {
		int	len = strlen(systems[i].name);
		int	j;

		if (systems[i].archs[0] == 0) {
			/*
			 * This system isn't supported on any
			 * architecture yet
			 */
			continue;
		}	
		(void) fprintf(stderr, "\t%s", systems[i].name);
		for (; len < 10; ++len) {
			putchar(' ');
		}	
		printf("(");
		for (j = 0; systems[i].archs[j] != 0; ++j) {
			printf("%s", arch_to_ascii(systems[i].archs[j]));
			if (systems[i].archs[j+1] != 0) {
				printf(", ");
			} else {
				printf(")\n");
			}
		}
	}
	putchar('\n');
	exit(EXIT_FAILURE);
}


/*
 * This is placed in misc.c because backend.c drags in other files and
 * should really only be linked with nwcc1
 */
int
ascii_abi_to_value(const char *name, int arch) {
	static const struct {
		char	*name;
		int	value[8];
		int	arch[8];
#define ONEAR(x) {x,-1,-1,-1,-1,-1,-1,-1}
	} supported[] = {
		{ "n32", ONEAR(ABI_MIPS_N32), ONEAR(ARCH_MIPS) },
		{ "n64", ONEAR(ABI_MIPS_N64), ONEAR(ARCH_MIPS) },
		{ "64", ONEAR(ABI_MIPS_N64), ONEAR(ARCH_MIPS) },
		{ "aix32", ONEAR(ABI_POWER32), ONEAR(ARCH_POWER) },
		{ "aix64", ONEAR(ABI_POWER64), ONEAR(ARCH_POWER) },
		{ "sparc32", ONEAR(ABI_SPARC32), ONEAR(ARCH_SPARC) },
		{ "sparc64", ONEAR(ABI_SPARC64), ONEAR(ARCH_SPARC) },
		{ NULL, ONEAR(0), ONEAR(0) } 
	};
	int	i;

	for (i = 0; supported[i].name != NULL; ++i) {
		if (strcmp(supported[i].name, name) == 0) {
			int	j;

			for (j = 0; supported[i].arch[j] != -1; ++j) {
/*if (arch == ARCH_X86) break;*/
				if (supported[i].arch[j] == arch) {
					break;
				}	
			}
			if (supported[i].arch[j] == -1
				/*&& supported[i].value[0] != ABI_POWER64*/) {
				(void) fprintf(stderr,
				"Option %s is not available for this target "
				"architecture\n", name);
				return -1;
			} else {
				return supported[i].value[j];
			}
		}	
	}
	(void) fprintf(stderr, "Unknown ABI option `%s'\n", name);
	exit(EXIT_FAILURE);
	return -1;
}

/*
 * Set target architecture and ABI. If ``archflag'' and/or ``abiflag''
 * are non-null, those will be used for them. Otherwise, the host
 * architecture is also used as target architecture, and/or the ABI is
 * set to the corresponding default (e.g. n32 on MIPS.)
 */
void
set_target_arch_and_abi_and_sys(int *archflag, int *abiflag, int *sysflag,
	const char *target_str, const char *abi_str, const char *sys_str) {

	int	abiflag_default = 0;
	int	sysflag_default = 0;

	if (target_str != NULL) {
		/*
		 * Command line contained an -arch option, i.e. don't use
		 * default (host) architecture (probably cross-compilation
		 * requested.)
		 */
		get_target_arch_by_name(archflag, target_str);
	} else {
		/* Use default architecture/ABI */
		get_host_arch(archflag, &abiflag_default, &sysflag_default);
	}

	if (sys_str != NULL) {
		get_target_sys_by_name(sysflag, sys_str);	
	} else {
		get_default_sys(sysflag);
	}

	if (abi_str != NULL) {
		/*
		 * Command line contained an -abi/-mabi option, i.e. don't
		 * use default ABI for target architecture
		 */
		*abiflag = ascii_abi_to_value(abi_str,
			*archflag);
		if (*abiflag == -1) {
			exit(EXIT_FAILURE);
		}
	} else {
		/* Use default ABI */
		if (abiflag_default != 0) {
			*abiflag = abiflag_default;
		} else {	
			get_default_abi(abiflag, *archflag, *sysflag); 
		}
	}
}

char *
arch_to_ascii(int val) {
	if (val == -1) {
		/* Get host architecture */
#ifdef __i386__
		val = ARCH_X86;
#elif defined __amd64__ || defined __x86_64__
		val = ARCH_AMD64;
#elif defined __sgi
		val = ARCH_MIPS;
#elif defined _AIX
		val = ARCH_POWER;
#elif defined _ARCH_PPC
		val = ARCH_POWER;
#elif defined __sun || defined __sparc
		val = ARCH_SPARC;
#endif
	}
	switch (val) {
	case ARCH_X86:	
		return "x86";
	case ARCH_AMD64:
		return "amd64";
	case ARCH_MIPS:
		return "mips";
	case ARCH_POWER:
		return "ppc";
	case ARCH_SPARC:
		return "sparc";
	default:
		return "unknown";
	}	
}

char *
arch_to_option(int val) {
	static char	buf[256];

	sprintf(buf, "-arch=%s", arch_to_ascii(val));
	return buf;
}

char *
sys_to_option(int val) {
	static char	buf[128];
	char		*str;

	switch (val) {
	case OS_LINUX:	
		str = "linux";
		break;
	case OS_FREEBSD:
		str = "freebsd";
		break;
	case OS_OPENBSD:
		str = "openbsd";
		break;
	case OS_NETBSD:
		str = "netbsd";
		break;
	case OS_IRIX:
		str = "irix";
		break;
	case OS_AIX:
		str = "aix";
		break;
	case OS_SOLARIS:
		str = "solaris";
		break;
	case OS_OSX:
		str = "osx";
		break;
	}
	sprintf(buf, "-sys=%s", str);
	return buf;
}


char *
abi_to_option(int val) {
	static char	buf[256];
	char		*str;

	switch (val) {
	case ABI_MIPS_N32:
		str = "n32";
		break;
	case ABI_MIPS_N64:
		str = "n64";
		break;
	case ABI_POWER32:
		str = "aix32";
		break;
	case ABI_POWER64:
		str = "aix64";
		break;
	case ABI_SPARC32:
		str = "sparc32";
		break;
	case ABI_SPARC64:
		str = "sparc64";
		break;
	default:
		str = "unknown";
		break;
	}
	sprintf(buf, "-mabi=%s", str);
	return buf;
}

int
generic_hash(const char *name, int tabsize) {
	int	key = 0;

	--tabsize;
	for (; *name != 0; ++name) {
		key = (33 * key + *name) & tabsize;
	}
	return key;
}	

