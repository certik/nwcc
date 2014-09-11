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
 */
#ifndef MISC_H
#define MISC_H

#include <stdio.h>

FILE	*exec_cmd(int use_popen, char *prog, char *format, ...);
int	ascii_abi_to_value(const char *name, int arch);
void	get_host_arch(int *arch, int *abi, int *sys);
void	get_default_abi(int *abi, int arch, int sys);
void	get_target_arch_by_name(int *arch, const char *name);
void	set_target_arch_and_abi_and_sys(int *arch, int *abi, int *sys,
		const char *target_str, const char *abi_str, const char *sys_str);
char	*arch_to_ascii(int val);
char	*arch_to_option(int val);
char	*sys_to_option(int val);
char	*abi_to_option(int val);
void	check_arch_sys_consistency(int arch, int sys);

/*
 * General-purpose hash function.
 * DANGER: Table size must be a power of 2!
 */
int	generic_hash(const char *name, int tabsize);

#define ASM_NASM        1
#define ASM_YASM        2
#define ASM_GAS         3
#define ASM_SGI_AS      4
#define ASM_NWASM	5

#endif

