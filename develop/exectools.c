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
#include "exectools.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "misc.h"
#include "token.h"
#include "analyze.h"
#include "defs.h"
#include "error.h"
#include "backend.h"
#include "cc_main.h"
#include "sysdeps.h"
#include "driver.h"
#include "debug.h"
#include "n_libc.h"

extern int gflag; /* XXX */
extern int assembler; /* XXX */

char *
do_asm(char *file, char *asm_flags, int abiflag) {
	static char	output_path[FILENAME_MAX + 1];
	char		buf[256];
	int		rc;
	FILE		*fd;
	char		*p;

	(void) abiflag;
	strncpy(output_path, file, sizeof output_path);
	output_path[sizeof output_path - 1] = 0;
	p = strrchr(output_path, '.');
	*++p = 'o';
	*++p = 0;

	if (sysdep_get_host_arch() == ARCH_X86) {
		/*
		 * XXX that stat() is weak and provisionary. real solution:
		 * fix exec_cmd(). Also, yasm is currently only used if
		 * debug info is requested because nasm still seems a bit
		 * more mature
		 */
		/*assembler = ASM_GAS;*/ /* XXXXXXXXXXXXXXXXXXXXXXXXX */
		if (assembler == ASM_NASM) {
			fd = exec_cmd(1, asmflag, " %s   %s", file, asm_flags);
		} else if (assembler == ASM_NWASM) {
			fd = exec_cmd(1, asmflag, " %s   %s", file, asm_flags);
		} else if (assembler == ASM_YASM) {
			if (!gflag) {
				fd = exec_cmd(1, asmflag, " %s   %s",
					file, asm_flags);
			} else {
				fd = exec_cmd(1, asmflag, " -g dwarf2 %s   %s",
					file, asm_flags);
			}
		} else {
			/* gas */
			fd = exec_cmd(1, asmflag, " %s   %s", file, asm_flags);
		}
	} else if (sysdep_get_host_arch() == ARCH_AMD64) {
		/*
		 * 07/26/12: Allow for cross compilation with assembler
		 * integration in mixed 32/64bit AMD64 systems
		 */
		if (assembler == ASM_GAS) {
			fd = exec_cmd(1, asmflag, " %s %s %s",
				archflag == ARCH_X86? "--32": "--64", file, asm_flags);
		} else {
			/* yasm */
			fd = exec_cmd(1, asmflag, " %s %s %s",
				archflag == ARCH_X86? "-m32": "-m64", file, asm_flags);
		}	
	} else if (sysdep_get_host_arch() == ARCH_MIPS) {
		fd = exec_cmd(1, asmflag,
			" %s   %s", file, asm_flags);
	} else if (sysdep_get_host_arch() == ARCH_POWER) {
		if (!oflag || !cflag) {
			fd = exec_cmd(1, asmflag, " %s %s -o %s",
				 asm_flags, file, output_path);
		} else {
			fd = exec_cmd(1, asmflag, " %s %s", asm_flags, file);
		}
	} else if (sysdep_get_host_arch() == ARCH_SPARC) {
		/* SPARC */
		fd = exec_cmd(1, asmflag, " %s %s ",
			asm_flags, file);	
	} else {
		(void) fprintf(stderr, "FATAL ERROR: Unknown operating system\n");
		remove(file);
		return NULL;
	}

	if (fd == NULL) {
		perror("popen");
		remove(file);
		return NULL; 
	}
	while (fgets(buf, sizeof buf, fd)) {
		fputs(buf, stdout);
	}
	rc = pclose(fd);
	if (WIFEXITED(rc) && WEXITSTATUS(rc) == 0) {
		return output_path;
	} else {
		fprintf(stderr,
			"*** Assembler returned nonzero exit-status.\n");
		return NULL;
	}	
}

