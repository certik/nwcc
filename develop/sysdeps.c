#include "archdefs.h"
#include "sysdeps.h"

/*
 * 07/25/09: Finally a file to keep host architecture/system macro
 * decisions in one place instead of using them in lots of files
 *
 * This should later also handle things like linker and assembler
 * flags
 */

int
sysdep_get_host_system(void) {
#ifdef __linux__
	return OS_LINUX;
#elif defined __DragonFly__
	return OS_DRAGONFLYBSD;
#elif defined __FreeBSD__ 
	return OS_FREEBSD;
#elif defined __NetBSD__
	return OS_NETBSD;
#elif defined __MirBSD__
	return OS_MIRBSD;
#elif defined __OpenBSD__
	return OS_OPENBSD;
#elif defined _AIX
	return OS_AIX;
#elif defined __sgi
	return OS_IRIX;
#elif defined __sun
	return OS_SOLARIS;
#elif defined __APPLE__
	return OS_OSX;
#else
	return -1;
#endif
}

int
sysdep_get_host_arch(void) {
#ifdef __i386__
	return ARCH_X86;
#elif defined __mips__ || defined __sgi
	return ARCH_MIPS;
#elif defined _ARCH_PPC || defined _AIX /* XXX */
	return ARCH_POWER;
#elif defined __amd64__ || defined __x86_64__
	return ARCH_AMD64;
#elif defined __sun || defined __sparc /* XXX Can this collide with x86 Solaris? */
	return ARCH_SPARC;
#else
	return -1;
#endif
}

