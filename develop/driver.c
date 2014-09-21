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
 * Driver for cpp, nasm, ncc and ld
 */
#include "driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>
#include "exectools.h"
#include "backend.h"
#include "misc.h"
#include "sysdeps.h"
#include "debug.h"
#include "cc_main.h"
#include "defs.h"
#include "n_libc.h"

/*
 * Store file path referenced by ``name'' in dynamically allocated variable
 * *output_names, resize it if necessary (*output_index contains the
 * number of files in use, it is incremented at each call. The variable keeping
 * track of allocated members in *output_names is static.)
 */
static int
store_file(char ***output_names, int **output_del, int needdel,
char *name, int *output_index) {
	
	static int	alloc = 0;
	static int	chunk_size =  8;
	int		tmpi = *output_index;

	if (tmpi >= alloc) {
		alloc += chunk_size;

		/* Quadratically increase number of allocated slots */
		chunk_size *= chunk_size;

		*output_names =
			n_xrealloc(*output_names, alloc * sizeof(char *));
		*output_del =
			n_xrealloc(*output_del, alloc * sizeof **output_del);
			
	}
	(*output_names)[tmpi] = n_xstrdup(name);
	(*output_del)[tmpi] = needdel;
	++(*output_index);
	return 0;
}

extern int Sflag; /* XXX */

int
driver(char **cpp_flags, char *asm_flags, char *ld_flags, char **files) {
	int			i;
	int			rc;
	int			out_index = 0;
	int			ld_cmd_len;
	int			has_errors = 0;
	int			sd_host_sys;
	char			*ld_cmd;
	char			**output_names = NULL;
	char			buf[256];
	int			*output_del = NULL;
	pid_t			pid;
	FILE			*fd;
	char			ld_std_flags[512];
	char			ld_pre_std_flags[128];
	static struct timeval	tv;

	*ld_pre_std_flags = 0;

	sd_host_sys = sysdep_get_host_system();


	if (archflag == ARCH_X86) {
		if (sd_host_sys == OS_FREEBSD) {	
			sprintf(ld_std_flags,
				"-dynamic-linker /usr/libexec/ld-elf.so.1 "
				"%s /usr/lib/crti.o /usr/lib/crtbegin.o " 
				"-L/usr/lib -lc /usr/lib/crtend.o /usr/lib/crtn.o ",
				sharedflag? "-shared": "/usr/lib/crt1.o");
		} else if (sd_host_sys == OS_DRAGONFLYBSD) {
			/*
			 * Uses same flags as FreeBSD except for ld-elf.so.2 instead
			 * of ld-elf.so.1
			 */
			sprintf(ld_pre_std_flags, sharedflag? "-shared": "/usr/lib/crt1.o");
			sprintf(ld_std_flags,
				"-dynamic-linker /usr/libexec/ld-elf.so.2 "
				"/usr/lib/crti.o /usr/lib/crtbegin.o " 
				"-L/usr/lib -lc /usr/lib/crtend.o /usr/lib/crtn.o ");
		} else if (sd_host_sys == OS_MIRBSD) {
			/*
			 * 07/25/09: XXX Untested!
			 */
			sprintf(ld_pre_std_flags, sharedflag? "-shared /usr/lib/crtbeginS.o":
					"/usr/lib/crt0.o /usr/lib/crtbegin.o");
			sprintf(ld_std_flags, sharedflag?
/*			"-shared "*/
				"/usr/lib/crti.o "
				"/usr/lib/crtendS.o /usr/lib/crtn.o " :
				"-e __start -dynamic-linker /usr/libexec/ld.so "
				"/usr/lib/crti.o " 
				"-lc /usr/lib/crtend.o /usr/lib/crtn.o ");
		} else if (sd_host_sys == OS_OPENBSD) {
			sprintf(ld_pre_std_flags, "/usr/lib/crt0.o /usr/lib/crtbegin.o");
			sprintf(ld_std_flags,
				"-e __start -Bdynamic -dynamic-linker "
				"/usr/libexec/ld.so"
				" -lc /usr/lib/crtend.o ");
		} else if (sd_host_sys == OS_NETBSD) {
			sprintf(ld_pre_std_flags, "/usr/lib/crt0.o /usr/lib/crtbegin.o");
			sprintf(ld_std_flags, "-dc -dp -e __start -dynamic-linker "
				"/usr/libexec/ld.elf_so /usr/lib/crti.o "
				"-lc /usr/lib/crtend.o /usr/lib/crtn.o ");
		} else if (sd_host_sys == OS_LINUX) {
#ifndef LIBDIR_32
#define LIBDIR_32 "/usr/lib/"
#define RTLDDIR_32 "/lib/"
#endif
			sprintf(ld_pre_std_flags, sharedflag? "": LIBDIR_32 "crt1.o");
			sprintf(ld_std_flags,
				/*
				 * 12/26/12: Allow for 32bit cross linking on an AMD64 host
				 */
				"%s -lc %s " LIBDIR_32 "crti.o "
				LIBDIR_32 "crtn.o " LIBDIR_32 "libc.so "
				"--dynamic-linker " RTLDDIR_32 "ld-linux.so.2 ",
				sysdep_get_host_arch() == ARCH_AMD64? "-melf_i386 ": "",
				sharedflag? "-shared": "");  /*"/usr/lib/crt1.o");*/
#undef LIBDIR_32
#undef RTLDDIR_32
		} else if (sd_host_sys == OS_SOLARIS) {
			sprintf(ld_pre_std_flags, sharedflag? "-shared": "/usr/lib/crt1.o");
			sprintf(ld_std_flags,
				"-L/usr/ccs/lib -L/usr/lib -Qy "
				"/usr/lib/crti.o -lc /usr/lib/crtn.o ");
		} else if (sd_host_sys == OS_OSX) {
			sprintf(ld_std_flags,
				"-dynamic -arch i386 -macosx_version_min 10.5.0 "
				"-weak_reference_mismatches non-weak -lcrt1.10.5.o "
				"-lSystem");
		} else {
			/* XXX */
			(void) fprintf(stderr, "This operating system isn't supported"
			      " on x86\n");
		}
	} else if (archflag == ARCH_AMD64) {
		if (sd_host_sys == OS_LINUX) {
			/*
			 * 08/08/07: Explicitly use lib64 instead of lib
			 * dirs, since some distributions seem to put 32bit
			 * files into plain lib dirs
			 *
			 * 07/24/12: Some 64bit distributions have moved the
			 * locations of library directories again. Specifically,
			 * KUbuntu AMD64 not only stores 64bit libs in /usr/lib,
			 * but also puts the CRT libs we're interested in into
			 * a sub directory titled "x86_64-linux-gnu".
			 * We use configure to attempt to make sense of this.
			 * XXX This should probably be stored in the config file
			 * instead.
			 */
#ifndef LIBDIR_64
#define LIBDIR_64 "/usr/lib64/"
#endif
#ifndef RTLDDIR_64
#define RTLDDIR_64 "/lib64/"
#endif

			sprintf(ld_pre_std_flags, sharedflag? "-shared": LIBDIR_64 "crt1.o");
			sprintf(ld_std_flags, "-lc " LIBDIR_64 "crti.o "
				LIBDIR_64 "crtn.o " LIBDIR_64 "libc.so "
				"--dynamic-linker " RTLDDIR_64 "ld-linux-x86-64.so.2 ");
#undef LIBDIR64
#undef RTLDDIR_64
		} else if (sd_host_sys == OS_FREEBSD) {
			sprintf(ld_pre_std_flags, sharedflag? "-shared": "/usr/lib/crt1.o");
			sprintf(ld_std_flags, "/usr/lib/crti.o "
			"/usr/lib/crtbegin.o -L/usr/lib -lc /usr/lib/crtend.o "
			"/usr/lib/crtn.o -dynamic-linker /libexec/ld-elf.so.1 ");
		} else if (sd_host_sys == OS_OPENBSD) {
			sprintf(ld_pre_std_flags, "/usr/lib/crt0.o /usr/lib/crtbegin.o");
			sprintf(ld_std_flags, "-Bdynamic "
				"-dynamic-linker /usr/libexec/ld.so "
				"-lc /usr/lib/crtend.o "
				"-L/usr/lib ");
		} else if (sd_host_sys == OS_OSX) {
			sprintf(ld_std_flags,
				"-dynamic -arch x86_64 -macosx_version_min 10.5.0 "
				"-weak_reference_mismatches non-weak -lcrt1.10.5.o "
				"-lSystem");
		} else {
			/* XXX */
			(void) fprintf(stderr, "This operating system isn't supported"
				      " on AMD64\n");
		}	
	} else if (archflag == ARCH_MIPS) {
		if (sd_host_sys == OS_IRIX) {
			if (abiflag == ABI_MIPS_N64) {
				strcpy(ld_std_flags,
				"-64 /usr/lib64/mips3/crt1.o -L/usr/lib64/mips3 "
				"-L/usr/lib64 -lc /usr/lib64/mips3/crtn.o ");
			} else {
				strcpy(ld_std_flags,
					"/usr/lib32/crt1.o /usr/lib32/crtn.o "
					"/usr/lib32/bpcrt.o -lc ");
			}
		} else if (sd_host_sys == OS_LINUX) {
			if (abiflag == ABI_MIPS_N64) {
				sprintf(ld_pre_std_flags, sharedflag? "-shared": "/usr/lib64/crt1.o");
				strcpy(ld_std_flags,
					"-dynamic-linker /lib64/ld.so.1 -melf64ltsmip "
					"/usr/lib64/crti.o /usr/lib64/crtn.o "
					"-L/usr/lib64 -lc ");
			} else {
				sprintf(ld_pre_std_flags, sharedflag? "-shared": "/usr/lib64/crt1.o");
				strcpy(ld_std_flags,
					"-dynamic-linker /lib64/ld.so.1 -melf64ltsmip "
					"/usr/lib64/crti.o /usr/lib64/crtn.o "
					"-L/usr/lib64 -lc ");
			}
		} else {
			/* XXX */
			(void) fprintf(stderr, "This operating system isn't supported"
			      " on MIPS\n");
		}
	} else if (archflag == ARCH_POWER) {
		if (sd_host_sys == OS_AIX) {
			if (abiflag == ABI_POWER64) {
				strcpy(ld_std_flags,
					"-bpT:0x10000000 -bpD:0x20000000 -btextro -b64 "
					"/lib/crt0_64.o -L/usr/lib -lc ");
			} else {
				strcpy(ld_std_flags,
					"-bpT:0x10000000 -bpD:0x20000000 -btextro /lib/crt0.o -L/usr/lib -lc ");
			}
		} else if (sd_host_sys == OS_LINUX) {
			/*
			 * Because Linux/PPC64 appallingly does not provide crtbegin.o
			 * and crtend.o in standard places, even though they are needed
			 * by crt1.o, which IS provided in standard places, we have to
			 * use a private copy
			 */
			struct stat	sbuf;
			char		*crtbegin = NULL;
			char		*crtend = NULL;

			if (abiflag == ABI_POWER64) {
				if (stat(INSTALLDIR "/nwcc/lib/crtbegin-64.o", &sbuf)==0) {
					crtbegin = INSTALLDIR "/nwcc/lib/crtbegin-64.o";
				} else if (stat("./crtbegin-64.o", &sbuf) == 0) {
					crtbegin = "./crtbegin-64.o";
				}
				if (stat(INSTALLDIR "/nwcc/lib/crtend-64.o", &sbuf)==0) {
					crtend = INSTALLDIR "/nwcc/lib/crtend-64.o";
				} else if (stat("./crtend-64.o", &sbuf) == 0) {
					crtend = "./crtend-64.o";
				}
			} else if (sysflag == OS_LINUX) {
				(void) fprintf(stderr, 
		"ERROR: 32bit mode not implemented for Linux/PPC! Please \n"
		"       contact the author to donate a 32bit account or a\n"
		"       64bit account with 32bit gcc and libraries.\n");
				crtbegin = "";
				crtend = "";

	#if 0
				/* 32bit */
				if (stat(INSTALLDIR "/nwcc/lib/crtbegin-32.o", &sbuf)==0) {
					crtbegin = INSTALLDIR "/nwcc/lib/crtbegin-32.o";
				} else if (stat("./crtbegin-32.o", &sbuf) == 0) {
					crtbegin = "./crtbegin-32.o";
				}
				if (stat(INSTALLDIR "/nwcc/lib/crtend-32.o", &sbuf)==0) {
					crtend = INSTALLDIR "/nwcc/lib/crtend-32.o";
				} else if (stat("./crtend-32.o", &sbuf) == 0) {
					crtend = "./crtend-32.o";
				}
	#endif
			}

			if (!Sflag) {
				if (crtbegin == NULL) {
					(void) fprintf(stderr, "Warning: No crtbegin object found "
						"in " INSTALLDIR "/nwcc/lib or .!\n");
					crtbegin = "";
				}
				if (crtend == NULL) {
					(void) fprintf(stderr, "Warning: No crtend object found "
						"in " INSTALLDIR "/nwcc/lib or .!\n");
					crtend = "";
				}
				if (abiflag == ABI_POWER64) {
					sprintf(ld_pre_std_flags, "/usr/lib64/crt1.o %s",
						crtbegin);
					sprintf(ld_std_flags, "--eh-frame-hdr -Qy -m elf64ppc "
						"-dynamic-linker /lib64/ld64.so.1 "
						"-L/lib64 -L/usr/lib64 "
						"/usr/lib64/crti.o /usr/lib64/crtn.o -lc %s",
						crtend);
				} else {
					unimpl();
				}
			}
		} else {
			/* XXX */
			(void) fprintf(stderr, "This operating system isn't supported"
			      " on PPC\n");
		}
	} else if (archflag == ARCH_SPARC) {
		if (sd_host_sys == OS_LINUX) {
			sprintf(ld_pre_std_flags, "/usr/lib64/crt1.o");
			strcpy(ld_std_flags, "-m elf64_sparc -Y P,/usr/lib64 "
				"-dynamic-linker /lib64/ld-linux.so.2 "
				"-L/lib64 -L/usr/lib64 "
				"/usr/lib64/crti.o /usr/lib64/crtn.o -lc ");
		} else if (sd_host_sys == OS_MIRBSD) {
			/* XXX Untested! */
			sprintf(ld_pre_std_flags, sharedflag? "-shared /usr/lib/crtbeginS.o":
					"/usr/lib/crt0.o /usr/lib/crtbegin.o");
			sprintf(ld_std_flags, sharedflag ?
				"/usr/lib/crti.o "
				"/usr/lib/crtendS.o /usr/lib/crtn.o " :
				"-e __start -dynamic-linker /usr/libexec/ld.so "
				"/usr/lib/crti.o "
				"-lc /usr/lib/crtend.o /usr/lib/crtn.o ");
		} else if (sd_host_sys == OS_SOLARIS) {
			/*
			 * Because Solaris/SPARC does not ship with crt1.o, we
			 * use our own
			 */
			struct stat	sbuf;
			char		*crt1 = NULL;

			if (abiflag == ABI_SPARC64) {
				if (stat(INSTALLDIR "/nwcc/lib/crt1-64.o", &sbuf)==0) {
					crt1 = INSTALLDIR "/nwcc/lib/crt1-64.o";
				} else if (stat("./crt1-64.o", &sbuf) == 0) {
					crt1 = "./crt1-64.o";
				}
			} else {
				/* 32bit */
				if (stat(INSTALLDIR "/nwcc/lib/crt1-32.o", &sbuf)==0) {
					crt1 = INSTALLDIR "/nwcc/lib/crt1-324.o";
				} else if (stat("./crt1-32.o", &sbuf) == 0) {
					crt1 = "./crt1-32.o";
				}
			}
			if (crt1 == NULL) {
				(void) fprintf(stderr, "Warning: No crt1 object found "
					"in " INSTALLDIR "/nwcc/lib or .!\n");
				crt1 = "";
			}

			if (abiflag == ABI_SPARC64) {
				/*
				 * /usr/local/lib/gcc/sparc-sun-solaris2.10/3.4.6/crt1.o
				                                                /sparcv9/crt1.o*/
				sprintf(ld_std_flags,
					"/usr/lib/sparcv9/crti.o /usr/lib/sparcv9/crtn.o %s "
					"-Y \"P,/usr/ccs/lib/sparcv9:/lib/sparcv9"
					":/usr/lib/sparcv9\" %s ",
					sharedflag? "-G -dy -z text": crt1,
					sharedflag? "": "-lc");
			} else {
				sprintf(ld_std_flags,
					"/usr/lib/crti.o /usr/lib/crtn.o %s "
					"-Y \"P,/usr/ccs/lib:/lib:/usr/lib\" -lc ",
					crt1);
			}
		} else {
			/* XXX */
			(void) fprintf(stderr, "This operating system isn't supported"
			      " on SPARC\n");
		}
	} else {
		unimpl();
	}

	for (i = 0; files[i] != NULL; ++i) {
		char	*p = strrchr(files[i], '.');
		char	*p2;
		int	needdel = 1;


		/*
		 * 02/09/08: Check whether this is really a file (as
		 * opposed to a command line option parameter)
		 */
		if (files[i][0] == '-') {
			continue;
		} else if (argmap[i] && !write_fcat_flag) {
			continue;
		}

		/* Ignore files without appropriate extension */
		if (p == NULL) {
			continue;
		}
		++p;

		/*
		 * Determine file type to decide which tools (cpp, nasm, ld)
		 * have to be invoked.
		 */
#if 0
		if (assembler == ASM_GAS
			|| archflag == ARCH_MIPS
			|| archflag == ) {
#endif
			{

			
			/*
			 * gas doesn't do foo.c -> foo.o by default. It calls
			 * the output a.out. *barf*
			 * XXX This is kludged
			 */
			static char	*out_ptr;
			char		*filep;

			if ((filep = strrchr(files[i], '/')) != NULL) {
				++filep;
			} else {
				filep = files[i];
			}	

			if (!cflag || !oflag) {
				if (out_ptr == NULL) {
					out_ptr = strchr(asm_flags, 0);
				}	
				sprintf(out_ptr, " -o %.*s.o",
					(int)(p - filep - 1), filep);
			}
			}
#if 0
		}
#endif

		if (strcmp(p, "c") == 0 || strcmp(p, "i") == 0) {
			p2 = files[i];

#ifdef DEBUG
			printf("Preprocessed successfully as %s\n", p2);
#endif

			/* Compile file ``p2''. */
			if ((pid = fork()) == -1) {
				perror("fork");
				exit(EXIT_FAILURE);
			} else if (pid == 0) {
				char	*nwcc1_args[512]; /* XXX */
				char	*arch = NULL;
				int	j = 0;
				int	k = 0;

				nwcc1_args[j++] = "nwcc1";
				if (stackprotectflag) {
					nwcc1_args[j++] = "-stackprotect";
				}
				if (gnuc_version) {
					static char	gnubuf[128];
					sprintf(gnubuf, "-gnuc=%s",
						gnuc_version);	
					nwcc1_args[j++] = gnubuf;
				}
				if (std_flag != NULL) {
					static char	std[16];
					sprintf(std, "-std=%s", std_flag);
					nwcc1_args[j++] = std;
				}
				if (pedanticflag) {
					nwcc1_args[j++] = "-pedantic";
				}	
				if (verboseflag) {
					nwcc1_args[j++] = "-verbose";
				}
				if (nostdinc_flag) {
					nwcc1_args[j++] = "-nostdinc";
				}
				if (picflag) {
					nwcc1_args[j++] = "-fpic";
				}
				if (fulltocflag || (!mintocflag && !fulltocflag)) {
					nwcc1_args[j++] = "-mfull-toc";
				} else {
					nwcc1_args[j++] = "-mminimal-toc";
				}
				if (stupidtraceflag) {
					nwcc1_args[j++] = "-stupidtrace";
				}


				if (gflag) {
					nwcc1_args[j++] = "-g";
				}
				if (Eflag) {
					nwcc1_args[j++] = "-E";
				}
				if (Oflag) {
					static char	obuf[16];
					sprintf(obuf, "-O%d", Oflag);
					nwcc1_args[j++] = obuf; 
				}
				if (write_fcat_flag) {
					nwcc1_args[j++] = "-write-fcat";
				}
				if (save_bad_translation_unit_flag) {
					nwcc1_args[j++] = "-save-bad-translation-unit";
				}

				/* XXX this should go into misc.c */
				switch (archflag) {
				case ARCH_X86:	
					arch = "-arch=x86";
					break;
				case ARCH_AMD64:
					arch = "-arch=amd64";
					break;
				case ARCH_POWER:
					arch = "-arch=ppc";
					break;
				case ARCH_MIPS:
					if (get_target_endianness() == ENDIAN_LITTLE) {
						arch = "-arch=mipsel";
					} else {
						arch = "-arch=mips";
					}
					break;
				case ARCH_SPARC:
					arch = "-arch=sparc";
					break;
				case ARCH_PA:
				case ARCH_ARM:
				case ARCH_SH:
					unimpl();
				}
				nwcc1_args[j++] = arch;
					
				if (abiflag != abiflag_default) {
					if (abiflag != 0) {
						nwcc1_args[j++] = /*abi*/
							abi_to_option(abiflag);
					}	
				}

				if (sysflag != sysflag_default) {
					if (sysflag != 0) {
						nwcc1_args[j++] = sys_to_option(sysflag);
					}
				}

				if (asmflag) {
					nwcc1_args[j] =
						n_xmalloc(strlen(asmflag)+16);
					sprintf(nwcc1_args[j++],
						"-asm=%s", asmflag);	
				}
				if (cppflag) {
					nwcc1_args[j] =
						n_xmalloc(strlen(cppflag+16));
					sprintf(nwcc1_args[j++],
						"-cpp=%s", cppflag);
				}
				if (timeflag) {
					nwcc1_args[j++] = n_xstrdup("-time");
				}
				if (funsignedchar_flag) {
					nwcc1_args[j++] = n_xstrdup("-funsigned-char");
				}
				if (fsignedchar_flag) {
					nwcc1_args[j++] = n_xstrdup("-fsigned-char");
				}
				if (fnocommon_flag) {
					nwcc1_args[j++] = n_xstrdup("-fno-common");
				}
				if (notgnu_flag) {
					nwcc1_args[j++] = n_xstrdup("-notgnu");
				} else {
					nwcc1_args[j++] = n_xstrdup("-gnu");
				}
				if (color_flag) {
					nwcc1_args[j++] = n_xstrdup("-color");
				}
				if (dump_macros_flag) {
					nwcc1_args[j++] = n_xstrdup("-dM");
				}

				if (custom_cpp_args) {
					nwcc1_args[j++] = custom_cpp_args;
				}

				nwcc1_args[j++] = p2;
				for (; cpp_flags[k] != NULL; ++j, ++k) {
					nwcc1_args[j] = cpp_flags[k];
				}
				nwcc1_args[j] = NULL;
#define DEVEL
#ifdef DEVEL
				execv("./nwcc1", nwcc1_args);
#endif 

				if ((p = getenv("NWCC_CC1")) != NULL) {
					execv(p, nwcc1_args);
					perror(p);
				} else {	
#if 0
					execv("/usr/local/bin/nwcc1",
						nwcc1_args);
					perror("/usr/local/bin/nwcc1");
#endif
					execv(INSTALLDIR "/bin/nwcc1",
						nwcc1_args);
					perror(INSTALLDIR "/bin/nwcc1");
				}
				exit(EXIT_FAILURE);
			} else {
				if (waitpid(pid, &rc, 0) == -1) {
					perror("waitpid");
					exit(EXIT_FAILURE);
				}
				if (dump_macros_flag) {
					/*
					 * 05/19/09: -dM
					 */
					exit(EXIT_SUCCESS);
				}
				if (rc != 0) {
					/* Try other files anyway */
					has_errors = 1;
					continue;
				}
			}

			/*
			 * nwcc just outputs with the same name as the input,
			 * except that the ending is .asm instead of .cpp
			 */
#ifdef DEBUG
			printf("Compiled successfully as %s\n", p2);
#endif

			if (Sflag || Eflag || write_fcat_flag) {
				continue;
			}

			p = p2;
			p2 = n_xmalloc(strlen(p2) + sizeof ".asm");
			strcpy(p2, p);
			p = strrchr(p2, '.');
			strcpy(++p, "asm");

			/* Hopefully p2 is a valid .asm file now... */
			if (timeflag) {
				start_timer(&tv);
			}

			if ((p = strrchr(p2, '/')) != NULL) {
				char	*saved = p+1;
				p2 = do_asm(p+1, asm_flags, abiflag);
				remove(saved);
			} else {
				char	*saved_p2 = p2;
				p2 = do_asm(p2, asm_flags, abiflag);
				remove(saved_p2);
			}	
			if (p2 == NULL) {
				/* Ignore failure, try other files anyway. */
				continue;
			}

			if (timeflag) {
				int	res = stop_timer(&tv);
				(void) fprintf(stderr,
					"=== Timing for assembling ===\n");
				(void) fprintf(stderr,
					"    %f sec\n", res / 1000000.0);
			}

#ifdef DEBUG
			printf("Assembled successfully as %s\n", p2);
#endif
		} else if (strcmp(p, "asm") == 0) {
			p2 = do_asm(files[i], asm_flags, abiflag);
			if (p2 == NULL) {
				/* Ignore failure, try other files anyway. */
				continue;
			}
		} else if (strcmp(p, "o") == 0
			|| strcmp(p, "a") == 0
			|| strcmp(p, "so") == 0) {
			/*
			 * Don't do anything, the name is later just
			 * stored for ld because it is an object,
			 * library archive or shared library file
			 */
			p2 = files[i];
			needdel = 0;
		} else {
			continue;
		}

		/*
		 * File ought to be compiled and assembled fine. Store path for
		 * later ld invocation
		 */
		if (store_file(&output_names, &output_del,
				needdel, p2, &out_index) != 0) {
			fprintf(stderr, "Exiting.\n");
			return 1;
		}
	}

	if (i == 0) {
		fprintf(stderr, "Missing input files.\n");
		return 1;
	}

	if (cflag || Sflag || Eflag || write_fcat_flag) return has_errors;

	if (output_names == NULL) {
		fprintf(stderr, "No valid files to link.\n");
		return 1;
	}

	/* Count length of all files plus white spaces and ld flags */
	ld_cmd_len = 0;
	for (i = 0; i < out_index; ++i) {
		ld_cmd_len += strlen(output_names[i]);
		++ld_cmd_len; /* white space */
	}
	ld_cmd_len += strlen(ld_flags);
	ld_cmd = malloc(sizeof "ld " + ld_cmd_len + 1 + sizeof ld_std_flags
		+ (custom_ld_args != NULL? strlen(custom_ld_args) + 3: 0)
		+ (xlinker_args != NULL? strlen(xlinker_args) + 3: 0));
	if (ld_cmd == NULL) {
		perror("malloc");
		return 1;
	}

	if (archflag == ARCH_SPARC) {
		if (sd_host_sys == OS_LINUX) {
			strcpy(ld_cmd, "ld ");
		} else {
			/* Solaris */
			if (abiflag == ABI_SPARC64) {
				strcpy(ld_cmd, "/usr/ccs/bin/sparcv9/ld ");
			} else {			
				strcpy(ld_cmd, "/usr/ccs/bin/ld ");
			}
		}
	} else {	
		strcpy(ld_cmd, "ld ");
	}


	/*
	 * 07/02/09: CRT modules (and possibly other things) which should be
	 * put at the beginning of the link line
	 */
	strcat(ld_cmd, ld_pre_std_flags);
	strcat(ld_cmd, " ");

	for (i = 0; i < out_index; ++i) {
		strcat(ld_cmd, output_names[i]);
		strcat(ld_cmd, " ");
	}

	/*
	 * 11/23/07: Whoops, guess user-defined flags must come before
	 * standard ones (XXX are there exceptions?) because otherwise
	 * things like dummy library stubs in libc.so are given
	 * precedence over user-supplied libraries such as -lpthread or
	 * -lrt
	 */
	strcat(ld_cmd, ld_flags);
	strcat(ld_cmd, " ");
	strcat(ld_cmd, ld_std_flags);
	if (custom_ld_args != NULL) {
		char	*p;

		/*
		 * Strip commas;
		 *
		 *    -Wl,-foo,-bar,-baz
		 *
		 * turns into
		 *
		 *    -foo -bar -baz
		 */
		for (p = custom_ld_args; *p != 0; ++p) {
			if (*p == ',') {
				*p = ' ';
			}
		}
		strcat(ld_cmd, " ");
		strcat(ld_cmd, custom_ld_args);
	}
	if (xlinker_args != NULL) {
		strcat(ld_cmd, " ");
		strcat(ld_cmd, xlinker_args);
	}


	if (timeflag) {
		start_timer(&tv);
	}

	if (verboseflag) {
		printf("Running: %s\n", ld_cmd);
	}
	fd = popen(ld_cmd, "r");
	if (fd == NULL) {
		perror("popen");
		return 1;
	}
	while (fgets(buf, sizeof buf, fd)) {
		printf("%s", buf);
	}

	if (timeflag) {
		int	res = stop_timer(&tv);
		(void) fprintf(stderr, "=== Timing of linking ===\n");
		(void) fprintf(stderr, "  %f sec\n", res / 1000000.0);
	}

	for (i = 0; i < out_index; ++i) {
		if (output_del[i]) {
			remove(output_names[i]);
		}	
	}
	rc = pclose(fd);
	if (WIFEXITED(rc)) {
		rc = WEXITSTATUS(rc);
		if (rc != 0) {
			has_errors = 1;
		} else {
			if (sflag) {
				/*
				 * Strip. Note that this is only done here;
				 * i.e. when building an executable file.
				 * When doing ``nwcc -c foo.c -s'', the
				 * flag is silently ignored, just like in
				 * gcc
				 */
				if ((fd = exec_cmd(1, "strip", " %s", out_file)) != NULL) {
					rc = pclose(fd);
					if (!WIFEXITED(rc)
						|| WEXITSTATUS(rc) != 0) {
						has_errors = 1;
					}
				} else {
					has_errors = 1;
				}
			}
		}
	} else {
		has_errors = 1;
	}
	return has_errors /*|| ((unsigned)pclose(fd) >> 8)*/ ; /* XXX !!! */
}

