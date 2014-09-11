/*
 * Copyright (c) 2006 - 2010, Nils R. Weller
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
#ifndef ARCHDEFS_H
#define ARCHDEFS_H

#define ARCH_X86 	1
#define ARCH_PA		2
#define ARCH_POWER	3
#define ARCH_MIPS	4
#define ARCH_ITANIUM	5
#define ARCH_SPARC	6
#define ARCH_ALPHA	7
#define ARCH_AMD64	8
#define ARCH_ARM	9
#define ARCH_SH		10

/*
 * XXX This only lists the ABI in cases where more than one is available.
 * On systems like x86 where we use SysV, the ABI flags are not set or
 * used. This needs to be overhauled (and much of misc.c and in the
 * backends really belongs here)
 */
#define ABI_MIPS_N32	1
#define ABI_MIPS_N64	2
#define ABI_POWER64	5
#define ABI_POWER32	6
#define ABI_SPARC32	7
#define ABI_SPARC64	8

#define OS_LINUX	1
/*
 * FreeBSD includes close derivatives like MidnightBSD
 */
#define OS_FREEBSD	2
#define OS_OPENBSD	3
#define OS_NETBSD	4
#define OS_AIX		5
#define OS_IRIX		6
#define OS_SOLARIS	7
#define OS_OSX		8
/*
 * DragonFly and MirBSD cannot be combined with FreeBSD and OpenBSD
 * because they use different linker flags
 */
#define OS_DRAGONFLYBSD	9
#define OS_MIRBSD	10

#endif

