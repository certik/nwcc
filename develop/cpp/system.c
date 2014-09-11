#include "system.h"
#include "archdefs.h"
#include "preprocess.h"
#include "n_libc.h"
#include "standards.h"
#include "cpp_main.h"
#include "macros.h"

static void	set_linux_macros(struct predef_macro **head, struct predef_macro **tail,
			int archflag, int abiflag);
static void	set_freebsd_macros(struct predef_macro **head, struct predef_macro **tail,
			int archflag, int abiflag);
static void	set_openbsd_macros(struct predef_macro **head, struct predef_macro **tail,
			int archflag, int abiflag);
static void	set_netbsd_macros(struct predef_macro **head, struct predef_macro **tail,
			int archflag, int abiflag);
static void	set_aix_macros(struct predef_macro **head, struct predef_macro **tail,
			int archflag, int abiflag);
static void	set_irix_macros(struct predef_macro **head, struct predef_macro **tail,
			int archflag, int abiflag);
static void	set_solaris_macros(struct predef_macro **head, struct predef_macro **tail,
			int archflag, int abiflag);
static void	set_osx_macros(struct predef_macro **head, struct predef_macro **tail,
			int archflag, int abiflag);

static void	set_arch_macros(struct predef_macro **head, struct predef_macro **tail,
			int archflag, int abiflag, int sysflag);

void
set_type_macros(struct predef_macro **head, struct predef_macro **tail,
	int archflag, int abiflag, int sysflag) {

	struct predef_macro	*tmp_pre;


	(void) archflag; (void) abiflag; (void) sysflag;
	
	tmp_pre = tmp_pre_macro("__SIZE_TYPE__", "unsigned long"); /* XXX */
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("__PTRDIFF_TYPE__", "signed long"); /* XXX */
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("__UINTMAX_TYPE__", "unsigned long long"); /* XXX */
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("__INTMAX_TYPE__", "long long"); /* XXX */
	APPEND_LIST(*head, *tail, tmp_pre);

	/*
	 * 05/23/09: We have to provide type limits like in limits.h, but
	 * using different names (which don't invade the namespace). These
	 * are also defined by gcc and used extensively at least by glibc
	 *
	 * Note that only MAX limit are provided, not MIN! And only signed!
	 */
	tmp_pre = tmp_pre_macro("__LONG_LONG_MAX__", "9223372036854775807LL"); 
	APPEND_LIST(*head, *tail, tmp_pre);

	if (is_64bit) {
		tmp_pre = tmp_pre_macro("__LONG_MAX__", "9223372036854775807L"); 
	} else {
		tmp_pre = tmp_pre_macro("__LONG_MAX__", "2147483647L");
	}
	APPEND_LIST(*head, *tail, tmp_pre);

	tmp_pre = tmp_pre_macro("__INT_MAX__", "2147483647");
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("__SHRT_MAX__", "32767");
	APPEND_LIST(*head, *tail, tmp_pre);

	if (fsignedchar_flag) {
		tmp_pre = tmp_pre_macro("__SCHAR_MAX__", "127");
		APPEND_LIST(*head, *tail, tmp_pre);
	} else {
		tmp_pre = tmp_pre_macro("__SCHAR_MAX__", "255"); 
		APPEND_LIST(*head, *tail, tmp_pre);
	}
	tmp_pre = tmp_pre_macro("__CHAR_BIT__", "8");
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("__FLT_MIN__", "1.17549435e-38F");
	APPEND_LIST(*head, *tail, tmp_pre);
}

void
set_system_macros(struct predef_macro **head, struct predef_macro **tail,
	int archflag, int abiflag, int sysflag) {

	struct predef_macro	*tmp_pre;

	/*
	 * Set C standard marco(s)
	 */
	tmp_pre = tmp_pre_macro("__STDC__", "1");
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("__STDC_HOSTED__", "1");
	APPEND_LIST(*head, *tail, tmp_pre);

	if (stdflag == ISTD_C99 || stdflag == ISTD_GNU99) {
		tmp_pre = tmp_pre_macro("__STDC_VERSION__", "199901L");
		APPEND_LIST(*head, *tail, tmp_pre);
	}


	/*
	 * Set generic system- and architecture-specific macros
	 */
	tmp_pre = tmp_pre_macro("__unix__", "1");
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("__unix", "1");
	APPEND_LIST(*head, *tail, tmp_pre);

	if (!using_strict_iso_c()) {
		tmp_pre = tmp_pre_macro("unix", "1");
		APPEND_LIST(*head, *tail, tmp_pre);
	}

	set_arch_macros(head, tail, archflag, abiflag, sysflag);

	switch (sysflag) {
	case OS_LINUX:
		set_linux_macros(head, tail, archflag, abiflag);
		break;
	case OS_FREEBSD:
		set_freebsd_macros(head, tail, archflag, abiflag);
		break;
	case OS_OPENBSD:
		set_openbsd_macros(head, tail, archflag, abiflag);
		break;
	case OS_NETBSD:
		set_netbsd_macros(head, tail, archflag, abiflag);
		break;
	case OS_AIX:
		set_aix_macros(head, tail, archflag, abiflag);
		break;
	case OS_IRIX:
		set_irix_macros(head, tail, archflag, abiflag);
		break;
	case OS_SOLARIS:
		set_solaris_macros(head, tail, archflag, abiflag);
		break;
	case OS_OSX:
		set_osx_macros(head, tail, archflag, abiflag);
		break;
	default:
		unimpl();
	}
}

static void
set_arch_macros(struct predef_macro **head, struct predef_macro **tail,
	int archflag, int abiflag, int sysflag) {

	struct predef_macro	*tmp_pre;
	char			*name;

	(void) archflag; (void) abiflag; (void) sysflag;
	/*
	 * 05/21/09: Tell our limits.h about the size of long
	 */
	if (archflag == ARCH_X86
		|| (archflag == ARCH_POWER && abiflag == ABI_POWER32)
		|| (archflag == ARCH_MIPS && abiflag == ABI_MIPS_N32)) {
		name = "__LONG_32BIT__";
		is_64bit = 0;
	} else {
		name = "__LONG_64BIT__";
		is_64bit = 1;
	}
	tmp_pre = tmp_pre_macro(name, "1");
	APPEND_LIST(*head, *tail, tmp_pre);

	switch (archflag) {
	case ARCH_X86:
		tmp_pre = tmp_pre_macro("__i386__", "1");
		APPEND_LIST(*head, *tail, tmp_pre);
		tmp_pre = tmp_pre_macro("__i486__", "1");
		APPEND_LIST(*head, *tail, tmp_pre);
		tmp_pre = tmp_pre_macro("__i386", "1");
		APPEND_LIST(*head, *tail, tmp_pre);
		tmp_pre = tmp_pre_macro("__i486", "1");
		APPEND_LIST(*head, *tail, tmp_pre);
		if (!using_strict_iso_c()) {
			tmp_pre = tmp_pre_macro("i386", "1");
			APPEND_LIST(*head, *tail, tmp_pre);
		}
		break;
	case ARCH_AMD64:
		tmp_pre = tmp_pre_macro("__amd64__", "1");
		APPEND_LIST(*head, *tail, tmp_pre);
		tmp_pre = tmp_pre_macro("__amd64", "1");
		APPEND_LIST(*head, *tail, tmp_pre);
		break;
	case ARCH_POWER:
		if (abiflag == ABI_POWER64) {
			tmp_pre = tmp_pre_macro("__64BIT__", "1");
			APPEND_LIST(*head, *tail, tmp_pre);
		}
		break;
	case ARCH_SPARC:
		break;
	case ARCH_MIPS:
		tmp_pre = tmp_pre_macro("__mips64", "1");
		APPEND_LIST(*head, *tail, tmp_pre);
		tmp_pre = tmp_pre_macro("__mips64__", "1");
		APPEND_LIST(*head, *tail, tmp_pre);
		tmp_pre = tmp_pre_macro("__mips", "1");
		APPEND_LIST(*head, *tail, tmp_pre);
		tmp_pre = tmp_pre_macro("__mips__", "1");
		APPEND_LIST(*head, *tail, tmp_pre);
		tmp_pre = tmp_pre_macro("__R4000__", "1");
		APPEND_LIST(*head, *tail, tmp_pre);
		tmp_pre = tmp_pre_macro("__MIPSEB__", "1");
		APPEND_LIST(*head, *tail, tmp_pre);
		tmp_pre = tmp_pre_macro("__MIPSEB", "1");
		APPEND_LIST(*head, *tail, tmp_pre);
		break;
	default:
		break;
	}

}


static void
set_linux_macros(struct predef_macro **head, struct predef_macro **tail,
	int archflag, int abiflag) {

	struct predef_macro	*tmp_pre;

	(void) archflag; (void) abiflag;
	tmp_pre = tmp_pre_macro("__linux__", "1");
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("__linux", "1");
	APPEND_LIST(*head, *tail, tmp_pre);

	if (!using_strict_iso_c()) {
		tmp_pre = tmp_pre_macro("linux", "1");
		APPEND_LIST(*head, *tail, tmp_pre);
	}
}

static void
set_freebsd_macros(struct predef_macro **head, struct predef_macro **tail,
	int archflag, int abiflag) {

	struct predef_macro	*tmp_pre;

	(void) archflag; (void) abiflag;
	tmp_pre = tmp_pre_macro("__FreeBSD__", "1");
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("__FreeBSD", "1");
	APPEND_LIST(*head, *tail, tmp_pre);
}

static void
set_openbsd_macros(struct predef_macro **head, struct predef_macro **tail,
	int archflag, int abiflag) {

	struct predef_macro	*tmp_pre;

	(void) archflag; (void) abiflag;
	tmp_pre = tmp_pre_macro("__OpenBSD__", "1");
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("__OpenBSD", "1");
	APPEND_LIST(*head, *tail, tmp_pre);
}

static void
set_netbsd_macros(struct predef_macro **head, struct predef_macro **tail,
	int archflag, int abiflag) {

	struct predef_macro	*tmp_pre;

	(void) archflag; (void) abiflag;
	tmp_pre = tmp_pre_macro("__NetBSD__", "1");
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("__NetBSD", "1");
	APPEND_LIST(*head, *tail, tmp_pre);
}

static void
set_aix_macros(struct predef_macro **head, struct predef_macro **tail,
	int archflag, int abiflag) {

	struct predef_macro	*tmp_pre;

	(void) archflag; (void) abiflag;
	tmp_pre = tmp_pre_macro("_AIX", "1");
	APPEND_LIST(*head, *tail, tmp_pre);

	/* Announce ``long long'' support to headers */
	tmp_pre = tmp_pre_macro("_LONG_LONG", "1");
	APPEND_LIST(*head, *tail, tmp_pre);
}

static void
set_irix_macros(struct predef_macro **head, struct predef_macro **tail,
		int archflag, int abiflag) {

	struct predef_macro	*tmp_pre;

	(void) archflag; (void) abiflag;
	tmp_pre = tmp_pre_macro("__sgi", "1");
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("_LANGUAGE_C", "1");
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("__EXTENSIONS__", "1");
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("_COMPILER_VERSION", "601");
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("__LANGUAGE_C", "1");
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("__LANGUAGE_C__", "1");
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("_MIPS_SZINT", "32");
	APPEND_LIST(*head, *tail, tmp_pre);
	tmp_pre = tmp_pre_macro("_LONGLONG", "1");
	APPEND_LIST(*head, *tail, tmp_pre);

	tmp_pre = tmp_pre_macro("_MIPS_SIM",
		abiflag == ABI_MIPS_N32? "_ABIN32": "_ABIN64");
	APPEND_LIST(*head, *tail, tmp_pre);

	tmp_pre = tmp_pre_macro("_SGI_SOURCE", "1"); /* XXX what about -ansi? */
	APPEND_LIST(*head, *tail, tmp_pre);

	tmp_pre = tmp_pre_macro(abiflag == ABI_MIPS_N32? "_ABIN32": "_ABIN64", "2");
	APPEND_LIST(*head, *tail, tmp_pre);

	if (!using_strict_iso_c()) {
		tmp_pre = tmp_pre_macro("LANGUAGE_C", "1");
		APPEND_LIST(*head, *tail, tmp_pre);
	}
	tmp_pre = tmp_pre_macro("_LANGUAGE_C", "1");
	APPEND_LIST(*head, *tail, tmp_pre);

	tmp_pre = tmp_pre_macro("_MIPS_SZPTR", "32");
	APPEND_LIST(*head, *tail, tmp_pre);

	tmp_pre = tmp_pre_macro("_MIPS_SZLONG",
		abiflag == ABI_MIPS_N32? "32": "64");
	tmp_pre = tmp_pre_macro("_MIPS_SZPTR",
		abiflag == ABI_MIPS_N32? "32": "64");
}

static void
set_solaris_macros(struct predef_macro **head, struct predef_macro **tail,
	int archflag, int abiflag) {
	(void) head; (void) tail;
	(void) archflag; (void) abiflag;
}

static void
set_osx_macros(struct predef_macro **head, struct predef_macro **tail,
	int archflag, int abiflag) {
	(void) head; (void) tail;
	(void) archflag; (void) abiflag;
}

