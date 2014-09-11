#include "typemap.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "type.h"
#include "expr.h"
#include "error.h"
#include "misc.h"

#ifndef PREPROCESSOR
#    include "backend.h"
#    include "decl.h"
#    include "features.h"
#else
#    include "archdefs.h"
#endif

#include "defs.h"
#include "n_libc.h"

/*
 * This file contains all type information for all supported architectures,
 * and the meat of constant expression evaluation.
 *
 * First a few words about the purpose of all of this stuff. The whole
 * point of recording and using all the type information is to enable cross
 * compilation. A constant expression has to be evaluated at compile time.
 * A non-cross-compiling compiler can handle any numbers using native types.
 * If a program contains the constant expression ``123l + 456l'',  the
 * compiler can sscanf("%ld"...) these numbers into two ``long'' variables
 * and then do ``long result = src + dest;'' with them.
 *
 * But if we intend to compile code for an architecture that does not match
 * the architecture on which the compiler runs, this may not work if the
 * types involved differ. Thus we first have to check whether the properties 
 * of a type are the same on both host and target platforms. If they are, we
 * can use the old approach. Otherwise we have to use a different and
 * possibly much more difficult way to carry out the operation.
 *
 * There are three possible cases here:
 *
 *    - A type (say ``long'') has the same size on both host and target
 * platforms. This means we can do sscanf(%ld) and ``long result = src+dest''
 * as usual
 *
 *    - A type (say ``long'') has a different size (e.g. 32bit on the target
 * platform and 64bit on the host platform), but a different host type, say
 * ``int'', has the same properties as the target type. Thus we can use it
 * to emulate the target type using sscanf(%d) and ``int result = src+dest''
 *
 *    - A target types properties do not match the properties of any host
 * type. This is the worst case, and requires full software emulation. This
 * is currently unimplemented.
 *
 * Note that this code assumes a ``sane'' host and target architecture. In
 * particular:
 *
 *    - both hosts use two's complement representation
 *    - integer and floating point types have no padding
 *    - all data pointers are the same size, and there's no harvard arch
 *    - there is only little and big endianness
 *    - the same (ieee) floating point representation is used
 *    - etc...
 *
 * Some of these will break with the introduction of support for embedded
 * architectures
 */

#define TL(x) x, sizeof x - 1
#define ASCII_8BIT	TL(ascii_8_dec), TL(ascii_8_hex), TL(ascii_8_oct)
#define ASCII_8UBIT	TL(ascii_8_dec_unsigned), TL(ascii_8_hex_unsigned), \
				TL(ascii_8_oct_unsigned)
#define ASCII_16BIT	TL(ascii_16_dec), TL(ascii_16_hex), TL(ascii_16_oct)
#define ASCII_16UBIT	TL(ascii_16_dec_unsigned), TL(ascii_16_hex_unsigned), \
				TL(ascii_16_oct_unsigned)
#define ASCII_32BIT	TL(ascii_32_dec), TL(ascii_32_hex), TL(ascii_32_oct)
#define ASCII_32UBIT	TL(ascii_32_dec_unsigned), TL(ascii_32_hex_unsigned), \
				TL(ascii_32_oct_unsigned)

#define ASCII_64BIT	TL(ascii_64_dec), TL(ascii_64_hex), TL(ascii_64_oct)
#define ASCII_64UBIT	TL(ascii_64_dec_unsigned), TL(ascii_64_hex_unsigned), \
				TL(ascii_64_oct_unsigned)
#define ASCII_NONE	0,0,0,0,0,0


static char	ascii_8_dec[]		= "127";
static char	ascii_8_dec_unsigned[]	= "255";
static char	ascii_16_dec[]		= "32767";
static char	ascii_16_dec_unsigned[]	= "65535";
static char	ascii_32_dec[]		= "2147483647";
static char	ascii_32_dec_unsigned[]	= "4294967295";
static char	ascii_64_dec[]		= "9223372036854775807";
static char	ascii_64_dec_unsigned[]	= "18446744073709551615";
static char	ascii_8_hex[]		= "7f";
static char	ascii_8_hex_unsigned[]	= "ff";
static char	ascii_16_hex[]		= "7fff";
static char	ascii_16_hex_unsigned[]	= "ffff";
static char	ascii_32_hex[]		= "7fffffff";
static char	ascii_32_hex_unsigned[]	= "ffffffff";
static char	ascii_64_hex[]		= "7fffffffffffffff";
static char	ascii_64_hex_unsigned[]	= "ffffffffffffffff";
static char	ascii_8_oct[]		= "177";
static char	ascii_8_oct_unsigned[]	= "377";
static char	ascii_16_oct[]		= "77777";
static char	ascii_16_oct_unsigned[]	= "177777";
static char	ascii_32_oct[]		= "17777777777";
static char	ascii_32_oct_unsigned[]	= "37777777777";
static char	ascii_64_oct[]		= "777777777777777777777";
static char	ascii_64_oct_unsigned[]	= "1777777777777777777777";

struct target_properties	*target_info;
static struct arch_properties	host_arch_properties;
static int			char_signedness;


/*
 * XXX x86 alignment stuff is messed up... gcc uses 4 bytes for
 * long long, long double and double, but 8 also makes sense.
 * Furthermore, __alignof(double), etc is not consistent with
 * __alignof(a structure containing double), etc. And on top
 * of that there's -malign-double/-mnoalign-double
 */

/*
 * XXX x86 and AMD64 ``long double'' treatment really sucks. The
 * x87 FPU offers 80 bits, i.e. 10 bytes of precision. But on
 * x86, most compilers make the type 12 bytes, that is, they
 * insert 2 padding bytes to make it rounder. Problem: nwcc has
 * ``traditionally'' used 10 bytes, and this cannot be changed
 * to 12 without breaking self-compilation on at least FreeBSD.
 * The reasons for this are unknown and (presumably) obscure;
 * Therefore I'll look into this later.
 *
 * Another problem: AMD64 still uses x87/80bits for long double,
 * but makes the entire thing 16 bytes (and always passes it on
 * the stack and requires 16 byte alignment, etc.)
 *
 * This is a goddamn terrible irritating mess of the numbers 10,
 * 12 and 16 flying all over the code, which may steal the last
 * bit of my remaining sanity :-/
 */
struct type_properties	x86_sysv_type_properties[] = {
	{ TY_CHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* char */
	{ TY_SCHAR, TOK_KEY_SIGNED, 1, 1, 8, ASCII_8BIT }, /* signed char */
	{ TY_UCHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* unsigned char */
	{ TY_SHORT, TOK_KEY_SIGNED, 2, 8, 16, ASCII_16BIT }, /* short */
	{ TY_USHORT, TOK_KEY_UNSIGNED, 2, 2, 16, ASCII_16UBIT }, /* unsigned short */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* int */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* enum */
	{ TY_UINT, TOK_KEY_UNSIGNED, 4, 4, 32, ASCII_32UBIT }, /* unsigned int */
	{ TY_LONG, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* long */
	{ TY_ULONG, TOK_KEY_UNSIGNED, 4, 4, 32, ASCII_32UBIT }, /* unsigned long */
	{ TY_LLONG, TOK_KEY_SIGNED, 8, 4, 64, ASCII_64BIT }, /* long long */
	{ TY_ULLONG, TOK_KEY_UNSIGNED, 8, 4, 64, ASCII_64UBIT },  /* unsigned long long */
	{ TY_FLOAT, 0, 4, 4, 32, ASCII_NONE },  /* float */
	{ TY_DOUBLE, 0, 8, 4, 64, ASCII_NONE },  /* double */
	{ TY_LDOUBLE, 0, 12, 4, 80, ASCII_NONE },  /* long double */
	{ TY_BOOL, 0, 1, 1, 8, ASCII_NONE }
};
struct type_properties	x86_osx_type_properties[] = {
	{ TY_CHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* char */
	{ TY_SCHAR, TOK_KEY_SIGNED, 1, 1, 8, ASCII_8BIT }, /* signed char */
	{ TY_UCHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* unsigned char */
	{ TY_SHORT, TOK_KEY_SIGNED, 2, 8, 16, ASCII_16BIT }, /* short */
	{ TY_USHORT, TOK_KEY_UNSIGNED, 2, 2, 16, ASCII_16UBIT }, /* unsigned short */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* int */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* enum */
	{ TY_UINT, TOK_KEY_UNSIGNED, 4, 4, 32, ASCII_32UBIT }, /* unsigned int */
	{ TY_LONG, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* long */
	{ TY_ULONG, TOK_KEY_UNSIGNED, 4, 4, 32, ASCII_32UBIT }, /* unsigned long */
	{ TY_LLONG, TOK_KEY_SIGNED, 8, 4, 64, ASCII_64BIT }, /* long long */
	{ TY_ULLONG, TOK_KEY_UNSIGNED, 8, 4, 64, ASCII_64UBIT },  /* unsigned long long */
	{ TY_FLOAT, 0, 4, 4, 32, ASCII_NONE },  /* float */
	{ TY_DOUBLE, 0, 8, 4, 64, ASCII_NONE },  /* double */
	{ TY_LDOUBLE, 0, 16, 4, 80, ASCII_NONE },  /* long double */
	{ TY_BOOL, 0, 1, 1, 8, ASCII_NONE }
};
struct arch_properties x86_arch_properties = {
	ARCH_X86,
	0,
	ENDIAN_LITTLE, /* endianness */
	4, /* data pointer size */
	4, /* function pointer size */
	0 /* long can store unsigned int */	
};
struct type_properties	amd64_type_properties[] = {
	{ TY_CHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* char */
	{ TY_SCHAR, TOK_KEY_SIGNED, 1, 1, 8, ASCII_8BIT }, /* signed char */
	{ TY_UCHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* unsigned char */
	{ TY_SHORT, TOK_KEY_SIGNED, 2, 2, 16, ASCII_16BIT }, /* short */
	{ TY_USHORT, TOK_KEY_UNSIGNED, 2, 2, 16, ASCII_16UBIT }, /* unsigned short */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* int */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* enum */
	{ TY_UINT, TOK_KEY_UNSIGNED, 4, 4, 32, ASCII_32UBIT }, /* unsigned int */
	{ TY_LONG, TOK_KEY_SIGNED, 8, 8, 64, ASCII_64BIT }, /* long */
	{ TY_ULONG, TOK_KEY_UNSIGNED, 8, 8, 64, ASCII_64UBIT }, /* unsigned long */
	{ TY_LLONG, TOK_KEY_SIGNED, 8, 8, 64, ASCII_64BIT }, /* long long */
	{ TY_ULLONG, TOK_KEY_UNSIGNED, 8, 8, 64, ASCII_64UBIT },  /* unsigned long long */
	{ TY_FLOAT, 0, 4, 4, 32, ASCII_NONE },  /* float */
	{ TY_DOUBLE, 0, 8, 8, 64, ASCII_NONE },  /* double */
	{ TY_LDOUBLE, 0, 16, 16, 80, ASCII_NONE },  /* long double */
	{ TY_BOOL, 0, 1, 1, 8, ASCII_NONE }
};
struct arch_properties amd64_arch_properties = {
	ARCH_AMD64,
	0,
	ENDIAN_LITTLE, /* endianness */
	8, /* data pointer size */
	8, /* function pointer size */
	1 /* long can store unsigned int */	
};
struct type_properties	ppc_type_properties[] = {
	{ TY_CHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* char */
	{ TY_SCHAR, TOK_KEY_SIGNED, 1, 1, 8, ASCII_8BIT }, /* signed char */
	{ TY_UCHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* unsigned char */
	{ TY_SHORT, TOK_KEY_SIGNED, 2, 2, 16, ASCII_16BIT }, /* short */
	{ TY_USHORT, TOK_KEY_UNSIGNED, 2, 2, 16, ASCII_16UBIT }, /* unsigned short */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* int */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* enum */
	{ TY_UINT, TOK_KEY_UNSIGNED, 4, 4, 32, ASCII_32UBIT }, /* unsigned int */
	{ TY_LONG, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* long */
	{ TY_ULONG, TOK_KEY_UNSIGNED, 4, 4, 32, ASCII_32UBIT }, /* unsigned long */
	{ TY_LLONG, TOK_KEY_SIGNED, 8, 8, 64, ASCII_64BIT }, /* long long */
	{ TY_ULLONG, TOK_KEY_UNSIGNED, 8, 8, 64, ASCII_64UBIT },  /* unsigned long long */
	{ TY_FLOAT, 0, 4, 4, 32, ASCII_NONE },  /* float */
	{ TY_DOUBLE, 0, 8, 8, 64, ASCII_NONE },  /* double */
	{ TY_LDOUBLE, 0, 8, 8, 64, ASCII_NONE },  /* long double */
	{ TY_BOOL, 0, 1, 1, 8, ASCII_NONE }
};	
struct arch_properties ppc_arch_properties = {
	ARCH_POWER,
	ABI_POWER32,
	ENDIAN_BIG, /* endianness */
	4, /* data pointer size */
	4, /* function pointer size */
	0 /* long can store unsigned int */	
};
struct type_properties	ppc64_aix_type_properties[] = {
	{ TY_CHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* char */
	{ TY_SCHAR, TOK_KEY_SIGNED, 1, 1, 8, ASCII_8BIT }, /* signed char */
	{ TY_UCHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* unsigned char */
	{ TY_SHORT, TOK_KEY_SIGNED, 2, 2, 16, ASCII_16BIT }, /* short */
	{ TY_USHORT, TOK_KEY_UNSIGNED, 2, 2, 16, ASCII_16UBIT }, /* unsigned short */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* int */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* enum */
	{ TY_UINT, TOK_KEY_UNSIGNED, 4, 4, 32, ASCII_32UBIT }, /* unsigned int */
	{ TY_LONG, TOK_KEY_SIGNED, 8, 8, 64, ASCII_64BIT }, /* long */
	{ TY_ULONG, TOK_KEY_UNSIGNED, 8, 8, 64, ASCII_64UBIT }, /* unsigned long */
	{ TY_LLONG, TOK_KEY_SIGNED, 8, 8, 64, ASCII_64BIT }, /* long long */
	{ TY_ULLONG, TOK_KEY_UNSIGNED, 8, 8, 64, ASCII_64UBIT },  /* unsigned long long */
	{ TY_FLOAT, 0, 4, 4, 32, ASCII_NONE },  /* float */
	{ TY_DOUBLE, 0, 8, 8, 64, ASCII_NONE },  /* double */
	{ TY_LDOUBLE, 0, 8, 8, 64, ASCII_NONE },  /* long double */
	{ TY_BOOL, 0, 1, 1, 8, ASCII_NONE }
};
/* 11/13/08: Linux ABI (the only difference here is 128bit long double) */
struct type_properties	ppc64_linux_type_properties[] = {
	{ TY_CHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* char */
	{ TY_SCHAR, TOK_KEY_SIGNED, 1, 1, 8, ASCII_8BIT }, /* signed char */
	{ TY_UCHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* unsigned char */
	{ TY_SHORT, TOK_KEY_SIGNED, 2, 2, 16, ASCII_16BIT }, /* short */
	{ TY_USHORT, TOK_KEY_UNSIGNED, 2, 2, 16, ASCII_16UBIT }, /* unsigned short */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* int */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* enum */
	{ TY_UINT, TOK_KEY_UNSIGNED, 4, 4, 32, ASCII_32UBIT }, /* unsigned int */
	{ TY_LONG, TOK_KEY_SIGNED, 8, 8, 64, ASCII_64BIT }, /* long */
	{ TY_ULONG, TOK_KEY_UNSIGNED, 8, 8, 64, ASCII_64UBIT }, /* unsigned long */
	{ TY_LLONG, TOK_KEY_SIGNED, 8, 8, 64, ASCII_64BIT }, /* long long */
	{ TY_ULLONG, TOK_KEY_UNSIGNED, 8, 8, 64, ASCII_64UBIT },  /* unsigned long long */
	{ TY_FLOAT, 0, 4, 4, 32, ASCII_NONE },  /* float */
	{ TY_DOUBLE, 0, 8, 8, 64, ASCII_NONE },  /* double */
	{ TY_LDOUBLE, 0, 16, 16, 128, ASCII_NONE },  /* long double */
	{ TY_BOOL, 0, 1, 1, 8, ASCII_NONE }
};
struct arch_properties ppc64_arch_properties = {
	ARCH_POWER,
	ABI_POWER64,
	ENDIAN_BIG, /* endianness */
	8, /* data pointer size */
	8, /* function pointer size */
	1 /* long can store unsigned int */	
};
struct type_properties mipsn32_type_properties[] = {
	{ TY_CHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* char */
	{ TY_SCHAR, TOK_KEY_SIGNED, 1, 1, 8, ASCII_8BIT }, /* signed char */
	{ TY_UCHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* unsigned char */
	{ TY_SHORT, TOK_KEY_SIGNED, 2, 2, 16, ASCII_16BIT }, /* short */
	{ TY_USHORT, TOK_KEY_UNSIGNED, 2, 2, 16, ASCII_16UBIT }, /* unsigned short */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* int */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* enum */
	{ TY_UINT, TOK_KEY_UNSIGNED, 4, 4, 32, ASCII_32UBIT }, /* unsigned int */
	{ TY_LONG, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* long */
	{ TY_ULONG, TOK_KEY_UNSIGNED, 4, 4, 32, ASCII_32UBIT }, /* unsigned long */
	{ TY_LLONG, TOK_KEY_SIGNED, 8, 8, 64, ASCII_64BIT }, /* long long */
	{ TY_ULLONG, TOK_KEY_UNSIGNED, 8, 8, 64, ASCII_64UBIT },  /* unsigned long long */
	{ TY_FLOAT, 0, 4, 4, 32, ASCII_NONE },  /* float */
	{ TY_DOUBLE, 0, 8, 8, 64, ASCII_NONE },  /* double */
	{ TY_LDOUBLE, 0, 8, 8, 64, ASCII_NONE },  /* long double */
	{ TY_BOOL, 0, 1, 1, 8, ASCII_NONE }
};	
struct arch_properties mipsn32_arch_properties = {
	ARCH_MIPS,
	ABI_MIPS_N32,
	ENDIAN_BIG, /* endianness */
	4, /* data pointer size */
	4, /* function pointer size */
	0 /* long can store unsigned int */	
};
struct type_properties	mipsn64_type_properties[] = {
	{ TY_CHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* char */
	{ TY_SCHAR, TOK_KEY_SIGNED, 1, 1, 8, ASCII_8BIT }, /* signed char */
	{ TY_UCHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* unsigned char */
	{ TY_SHORT, TOK_KEY_SIGNED, 2, 2, 16, ASCII_16BIT }, /* short */
	{ TY_USHORT, TOK_KEY_UNSIGNED, 2, 2, 16, ASCII_16UBIT }, /* unsigned short */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* int */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* enum */
	{ TY_UINT, TOK_KEY_UNSIGNED, 4, 4, 32, ASCII_32UBIT }, /* unsigned int */
	{ TY_LONG, TOK_KEY_SIGNED, 8, 8, 64, ASCII_64BIT }, /* long */
	{ TY_ULONG, TOK_KEY_UNSIGNED, 8, 8, 64, ASCII_64UBIT }, /* unsigned long */
	{ TY_LLONG, TOK_KEY_SIGNED, 8, 8, 64, ASCII_64BIT }, /* long long */
	{ TY_ULLONG, TOK_KEY_UNSIGNED, 8, 8, 64, ASCII_64UBIT },  /* unsigned long long */
	{ TY_FLOAT, 0, 4, 4, 32, ASCII_NONE },  /* float */
	{ TY_DOUBLE, 0, 8, 8, 64, ASCII_NONE },  /* double */
	{ TY_LDOUBLE, 0, 8, 8, 64, ASCII_NONE },  /* long double */
	{ TY_BOOL, 0, 1, 1, 8, ASCII_NONE }
};
struct type_properties	sparc32_type_properties[] = {
	{ TY_CHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* char */
	{ TY_SCHAR, TOK_KEY_SIGNED, 1, 1, 8, ASCII_8BIT }, /* signed char */
	{ TY_UCHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* unsigned char */
	{ TY_SHORT, TOK_KEY_SIGNED, 2, 2, 16, ASCII_16BIT }, /* short */
	{ TY_USHORT, TOK_KEY_UNSIGNED, 2, 2, 16, ASCII_16UBIT }, /* unsigned short */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* int */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* enum */
	{ TY_UINT, TOK_KEY_UNSIGNED, 4, 4, 32, ASCII_32UBIT }, /* unsigned int */
	{ TY_LONG, TOK_KEY_SIGNED, 4, 4, 32, ASCII_64BIT }, /* long */
	{ TY_ULONG, TOK_KEY_UNSIGNED, 4, 4, 32, ASCII_64UBIT }, /* unsigned long */
	{ TY_LLONG, TOK_KEY_SIGNED, 8, 8, 64, ASCII_64BIT }, /* long long */
	{ TY_ULLONG, TOK_KEY_UNSIGNED, 8, 8, 64, ASCII_64UBIT },  /* unsigned long long */
	{ TY_FLOAT, 0, 4, 4, 32, ASCII_NONE },  /* float */
	{ TY_DOUBLE, 0, 8, 8, 64, ASCII_NONE },  /* double */
	{ TY_LDOUBLE, 0, 16, 16, 128, ASCII_NONE },  /* long double */
	{ TY_BOOL, 0, 1, 1, 8, ASCII_NONE }
};
struct type_properties	sparc64_type_properties[] = {
	{ TY_CHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* char */
	{ TY_SCHAR, TOK_KEY_SIGNED, 1, 1, 8, ASCII_8BIT }, /* signed char */
	{ TY_UCHAR, TOK_KEY_UNSIGNED, 1, 1, 8, ASCII_8UBIT }, /* unsigned char */
	{ TY_SHORT, TOK_KEY_SIGNED, 2, 2, 16, ASCII_16BIT }, /* short */
	{ TY_USHORT, TOK_KEY_UNSIGNED, 2, 2, 16, ASCII_16UBIT }, /* unsigned short */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* int */
	{ TY_INT, TOK_KEY_SIGNED, 4, 4, 32, ASCII_32BIT }, /* enum */
	{ TY_UINT, TOK_KEY_UNSIGNED, 4, 4, 32, ASCII_32UBIT }, /* unsigned int */
	{ TY_LONG, TOK_KEY_SIGNED, 8, 8, 64, ASCII_64BIT }, /* long */
	{ TY_ULONG, TOK_KEY_UNSIGNED, 8, 8, 64, ASCII_64UBIT }, /* unsigned long */
	{ TY_LLONG, TOK_KEY_SIGNED, 8, 8, 64, ASCII_64BIT }, /* long long */
	{ TY_ULLONG, TOK_KEY_UNSIGNED, 8, 8, 64, ASCII_64UBIT },  /* unsigned long long */
	{ TY_FLOAT, 0, 4, 4, 32, ASCII_NONE },  /* float */
	{ TY_DOUBLE, 0, 8, 8, 64, ASCII_NONE },  /* double */
	{ TY_LDOUBLE, 0, 16, 16, 128, ASCII_NONE },  /* long double */
	{ TY_BOOL, 0, 1, 1, 8, ASCII_NONE }
};

struct arch_properties mipsn64_arch_properties = {
	ARCH_MIPS,
	ABI_MIPS_N64,
	ENDIAN_BIG, /* endianness */
	8, /* data pointer size */
	8, /* function pointer size */
	1 /* long can store unsigned int */	
};
struct arch_properties sparc32_arch_properties = {
	ARCH_SPARC,
	ABI_SPARC32,
	ENDIAN_BIG, /* endianness */
	4, /* data pointer size */
	4, /* function pointer size */
	0 /* long can store unsigned int */	
};
struct arch_properties sparc64_arch_properties = {
	ARCH_SPARC,
	ABI_SPARC64,
	ENDIAN_BIG, /* endianness */
	8, /* data pointer size */
	8, /* function pointer size */
	1 /* long can store unsigned int */	
};

#define PROP_IDX_X86_SYSV	0
#define PROP_IDX_X86_OSX	1
#define PROP_IDX_AMD64		2
#define PROP_IDX_PPC		3
#define PROP_IDX_PPC64_AIX	4
#define PROP_IDX_PPC64_LINUX	5
#define PROP_IDX_MIPSO32	6
#define PROP_IDX_MIPSN32	7
#define PROP_IDX_MIPSN64	8
#define PROP_IDX_SPARC32	9
#define PROP_IDX_SPARC64	10

struct target_properties	target_properties[] = {
	{ x86_sysv_type_properties, &x86_arch_properties },
	{ x86_osx_type_properties, &x86_arch_properties },
	{ amd64_type_properties, &amd64_arch_properties },
	{ ppc_type_properties, &ppc_arch_properties },
	{ ppc64_aix_type_properties, &ppc64_arch_properties }, 
	{ ppc64_linux_type_properties, &ppc64_arch_properties }, 
	{ mipsn32_type_properties, &mipsn32_arch_properties }, /* XXX o32!! */
	{ mipsn32_type_properties, &mipsn32_arch_properties },
	{ mipsn64_type_properties, &mipsn64_arch_properties },
	{ sparc32_type_properties, &sparc32_arch_properties },
	{ sparc64_type_properties, &sparc64_arch_properties }
};

#define TYMAP_IDX_CHAR		0
#define TYMAP_IDX_SCHAR		1
#define TYMAP_IDX_UCHAR		2
#define TYMAP_IDX_SHORT		3
#define TYMAP_IDX_USHORT	4
#define TYMAP_IDX_INT		5
/*
 * XXX enum is currently treated like an int. It should always come
 * after signed int because that way a property match search will
 * use INT first. This hopefully minimizes confusion
 */
#define TYMAP_IDX_ENUM		6
#define TYMAP_IDX_UINT		7
#define TYMAP_IDX_LONG		8
#define TYMAP_IDX_ULONG		9
#define TYMAP_IDX_LLONG		10
#define TYMAP_IDX_ULLONG	11
#define TYMAP_IDX_FLOAT		12
#define TYMAP_IDX_DOUBLE	13
#define TYMAP_IDX_LDOUBLE	14
/* XXX whoa I guess we need _Bool in constant expressions too... for now
 * only supported with cross_get_sizeof_type()
 */
#define TYMAP_IDX_BOOL		15


/*
 * All interfaces only use void pointers to keep them consistent
 * and thus to enable the caller to ignore all type information
 */
struct type_mapping {
	struct type_properties	properties;
	int	code;
	void	(*from_char)(void *, void *);
	void	(*from_schar)(void *, void *);
	void	(*from_uchar)(void *, void *);
	void	(*from_short)(void *, void *);
	void	(*from_ushort)(void *, void *);
	void	(*from_int)(void *, void *);
	void	(*from_uint)(void *, void *);
	void	(*from_long)(void *, void *);
	void	(*from_ulong)(void *, void *);
	void	(*from_llong)(void *, void *);
	void	(*from_ullong)(void *, void *);
	void	(*from_float)(void *, void *);
	void	(*from_double)(void *, void *);
	void	(*from_ldouble)(void *, void *);
	void	(*negate)(struct token *, void *, void *, void *); /* unary - */
	void	(*bnegate)(struct token *, void *, void *, void *); /* ~ */
	void	(*lnegate)(struct token *, void *, void *, void *); /* ! */
	void	(*add)(struct token *, void *, void *, void *);
	void	(*sub)(struct token *, void *, void *, void *);
	void	(*mul)(struct token *, void *, void *, void *);
	void	(*div)(struct token *, void *, void *, void *);
	void	(*mod)(struct token *, void *, void *, void *);
	void	(*band)(struct token *, void *, void *, void *);
	void	(*bor)(struct token *, void *, void *, void *);
	void	(*xor)(struct token *, void *, void *, void *);
	/*
	 * For bshl/bshr the source must always be of type int
	 */
	void	(*bshl)(struct token *, void *, void *, void *);
	void	(*bshr)(struct token *, void *, void *, void *);

	/*
	 * All comparison operators yield a result of type int
	 */
	void	(*greater)(struct token *, void *, void *, void *);
	void	(*smaller)(struct token *, void *, void *, void *);
	void	(*greater_eq)(struct token *, void *, void *, void *);
	void	(*smaller_eq)(struct token *, void *, void *, void *);
	void	(*equal)(struct token *, void *, void *, void *);
	void	(*not_equal)(struct token *, void *, void *, void *);
} *type_map[TYMAP_IDX_LDOUBLE + 1]; 



static void char_from_char(void *dest, void *src) {
	*(char *)dest = (char) *(char *)src; }
static void char_from_schar(void *dest, void *src) {
	*(char *)dest = (char) *(signed char *)src; }
static void char_from_uchar(void *dest, void *src) {
	*(char *)dest = (char) *(unsigned char *)src; }
static void char_from_short(void *dest, void *src) {
	*(char *)dest = (char) *(short *)src; }
static void char_from_ushort(void *dest, void *src) {
	*(char *)dest = (char) *(unsigned short *)src; }
static void char_from_int(void *dest, void *src) {
	*(char *)dest = (char) *(int *)src; }
static void char_from_uint(void *dest, void *src) {
	*(char *)dest = (char) *(unsigned int *)src; }
static void char_from_long(void *dest, void *src) {
	*(char *)dest = (char) *(long *)src; }
static void char_from_ulong(void *dest, void *src) {
	*(char *)dest = (char) *(unsigned long *)src; }
static void char_from_llong(void *dest, void *src) {
	*(char *)dest = (char) *(long long *)src; }
static void char_from_ullong(void *dest, void *src) {
	*(char *)dest = (char) *(unsigned long long *)src; }
static void char_from_float(void *dest, void *src) {
	*(char *)dest = (char) *(float *)src; }
static void char_from_double(void *dest, void *src) {
	*(char *)dest = (char) *(double *)src; }
static void char_from_ldouble(void *dest, void *src) {
	*(char *)dest = (char) *(long double *)src; }


static void schar_from_char(void *dest, void *src) {
	*(signed char *)dest = (signed char) *(char *)src; }
static void schar_from_schar(void *dest, void *src) {
	*(signed char *)dest = (signed char) *(signed char *)src; }
static void schar_from_uchar(void *dest, void *src) {
	*(signed char *)dest = (signed char) *(unsigned char *)src; }
static void schar_from_short(void *dest, void *src) {
	*(signed char *)dest = (signed char) *(short *)src; }
static void schar_from_ushort(void *dest, void *src) {
	*(signed char *)dest = (signed char) *(unsigned short *)src; }
static void schar_from_int(void *dest, void *src) {
	*(signed char *)dest = (signed char) *(int *)src; }
static void schar_from_uint(void *dest, void *src) {
	*(signed char *)dest = (signed char) *(unsigned int *)src; }
static void schar_from_long(void *dest, void *src) {
	*(signed char *)dest = (signed char) *(long *)src; }
static void schar_from_ulong(void *dest, void *src) {
	*(signed char *)dest = (signed char) *(unsigned long *)src; }
static void schar_from_llong(void *dest, void *src) {
	*(signed char *)dest = (signed char) *(long long *)src; }
static void schar_from_ullong(void *dest, void *src) {
	*(signed char *)dest = (signed char) *(unsigned long long *)src; }
static void schar_from_float(void *dest, void *src) {
	*(signed char *)dest = (signed char) *(float *)src; }
static void schar_from_double(void *dest, void *src) {
	*(signed char *)dest = (signed char) *(double *)src; }
static void schar_from_ldouble(void *dest, void *src) {
	*(signed char *)dest = (signed char) *(long double *)src; }

static void uchar_from_char(void *dest, void *src) {
	*(unsigned char *)dest = (unsigned char) *(char *)src; }
static void uchar_from_schar(void *dest, void *src) {
	*(unsigned char *)dest = (unsigned char) *(signed char *)src; }
static void uchar_from_uchar(void *dest, void *src) {
	*(unsigned char *)dest = (unsigned char) *(unsigned char *)src; }
static void uchar_from_short(void *dest, void *src) {
	*(unsigned char *)dest = (unsigned char) *(short *)src; }
static void uchar_from_ushort(void *dest, void *src) {
	*(unsigned char *)dest = (unsigned char) *(unsigned short *)src; }
static void uchar_from_int(void *dest, void *src) {
	*(unsigned char *)dest = (unsigned char) *(int *)src; }
static void uchar_from_uint(void *dest, void *src) {
	*(unsigned char *)dest = (unsigned char) *(unsigned int *)src; }
static void uchar_from_long(void *dest, void *src) {
	*(unsigned char *)dest = (unsigned char) *(long *)src; }
static void uchar_from_ulong(void *dest, void *src) {
	*(unsigned char *)dest = (unsigned char) *(unsigned long *)src; }
static void uchar_from_llong(void *dest, void *src) {
	*(unsigned char *)dest = (unsigned char) *(long long *)src; }
static void uchar_from_ullong(void *dest, void *src) {
	*(unsigned char *)dest = (unsigned char) *(unsigned long long *)src; }
static void uchar_from_float(void *dest, void *src) {
	*(unsigned char *)dest = (unsigned char) *(float *)src; }
static void uchar_from_double(void *dest, void *src) {
	*(unsigned char *)dest = (unsigned char) *(double *)src; }
static void uchar_from_ldouble(void *dest, void *src) {
	*(unsigned char *)dest = (unsigned char) *(long double *)src; }

static void short_from_char(void *dest, void *src) {
	*(short *)dest = (short) *(char *)src; }
static void short_from_schar(void *dest, void *src) {
	*(short *)dest = (short) *(signed char *)src; }
static void short_from_uchar(void *dest, void *src) {
	*(short *)dest = (short) *(unsigned char *)src; }
static void short_from_short(void *dest, void *src) {
	*(short *)dest = (short) *(short *)src; }
static void short_from_ushort(void *dest, void *src) {
	*(short *)dest = (short) *(unsigned short *)src; }
static void short_from_int(void *dest, void *src) {
	*(short *)dest = (short) *(int *)src; }
static void short_from_uint(void *dest, void *src) {
	*(short *)dest = (short) *(unsigned int *)src; }
static void short_from_long(void *dest, void *src) {
	*(short *)dest = (short) *(long *)src; }
static void short_from_ulong(void *dest, void *src) {
	*(short *)dest = (short) *(unsigned long *)src; }
static void short_from_llong(void *dest, void *src) {
	*(short *)dest = (short) *(long long *)src; }
static void short_from_ullong(void *dest, void *src) {
	*(short *)dest = (short) *(unsigned long long *)src; }
static void short_from_float(void *dest, void *src) {
	*(short *)dest = (short) *(float *)src; }
static void short_from_double(void *dest, void *src) {
	*(short *)dest = (short) *(double *)src; }
static void short_from_ldouble(void *dest, void *src) {
	*(short *)dest = (short) *(long double *)src; }

static void ushort_from_char(void *dest, void *src) {
	*(unsigned short *)dest = (unsigned short) *(char *)src; }
static void ushort_from_schar(void *dest, void *src) {
	*(unsigned short *)dest = (unsigned short) *(signed char *)src; }
static void ushort_from_uchar(void *dest, void *src) {
	*(unsigned short *)dest = (unsigned short) *(unsigned char *)src; }
static void ushort_from_short(void *dest, void *src) {
	*(unsigned short *)dest = (unsigned short) *(short *)src; }
static void ushort_from_ushort(void *dest, void *src) {
	*(unsigned short *)dest = (unsigned short) *(unsigned short *)src; }
static void ushort_from_int(void *dest, void *src) {
	*(unsigned short *)dest = (unsigned short) *(int *)src; }
static void ushort_from_uint(void *dest, void *src) {
	*(unsigned short *)dest = (unsigned short) *(unsigned int *)src; }
static void ushort_from_long(void *dest, void *src) {
	*(unsigned short *)dest = (unsigned short) *(long *)src; }
static void ushort_from_ulong(void *dest, void *src) {
	*(unsigned short *)dest = (unsigned short) *(unsigned long *)src; }
static void ushort_from_llong(void *dest, void *src) {
	*(unsigned short *)dest = (unsigned short) *(long long *)src; }
static void ushort_from_ullong(void *dest, void *src) {
	*(unsigned short *)dest = (unsigned short) *(unsigned long long *)src; }
static void ushort_from_float(void *dest, void *src) {
	*(unsigned short *)dest = (unsigned short) *(float *)src; }
static void ushort_from_double(void *dest, void *src) {
	*(unsigned short *)dest = (unsigned short) *(double *)src; }
static void ushort_from_ldouble(void *dest, void *src) {
	*(unsigned short *)dest = (unsigned short) *(long double *)src; }
	
/*
 * XXX return default_char_signed? int_from_schar(...): int_from_uchar
 */
static void int_from_char(void *dest, void *src) {
	*(int *)dest = (int) *(char *)src; }
static void int_from_schar(void *dest, void *src) {
	*(int *)dest = (int) *(signed char *)src; }
static void int_from_uchar(void *dest, void *src) {
	*(int *)dest = (int) *(unsigned char *)src; }
static void int_from_short(void *dest, void *src) {
	*(int *)dest = (int) *(short *)src; }
static void int_from_ushort(void *dest, void *src) {
	*(int *)dest = (int) *(unsigned short *)src; }
static void int_from_int(void *dest, void *src) {
	*(int *)dest = (int) *(int *)src; }
static void int_from_uint(void *dest, void *src) {
	*(int *)dest = (int) *(unsigned int *)src; }
static void int_from_long(void *dest, void *src) {
	*(int *)dest = (int) *(long *)src; }
static void int_from_ulong(void *dest, void *src) {
	*(int *)dest = (int) *(unsigned long *)src; }
static void int_from_llong(void *dest, void *src) {
	*(int *)dest = (int) *(long long *)src; }
static void int_from_ullong(void *dest, void *src) {
	*(int *)dest = (int) *(unsigned long long *)src; }
static void int_from_float(void *dest, void *src) {
	*(int *)dest = (int) *(float *)src; }
static void int_from_double(void *dest, void *src) {
	*(int *)dest = (int) *(double *)src; }
static void int_from_ldouble(void *dest, void *src) {
	*(int *)dest = (int) *(long double *)src; }
static void negate_signed_int(struct token *tok, void *res, void *src,
	void *unused) {
	(void) tok;
	*(int *)res = -*(int *)src; (void)unused; }
static void bnegate_signed_int(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(int *)res = ~*(int *)src; (void)unused; }
static void lnegate_signed_int(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(int *)res = !*(int *)src; (void)unused; }
static void add_signed_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(int *)dst + *(int *)src; }
static void sub_signed_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(int *)dst - *(int *)src; }
static void mul_signed_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(int *)dst * *(int *)src; }
static void div_signed_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	if (*(int *)src == 0) {
		warningfl(tok, "Division by zero");
		*(int *)res = 0;
		return;
	}
	*(int *)res = *(int *)dst / *(int *)src; }
static void mod_signed_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	if (*(int *)src == 0) {
		warningfl(tok, "Division by zero");
		*(int *)res = 0;
		return;
	}
	*(int *)res = *(int *)dst % *(int *)src; }
static void band_signed_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(int *)dst & *(int *)src; }
static void bor_signed_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(int *)dst | *(int *)src; }
static void xor_signed_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(int *)dst ^ *(int *)src; }
static void bshl_signed_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(int *)dst << *(int *)src; }
static void bshr_signed_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(int *)dst >> *(int *)src; }
static void greater_signed_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(int *)dst > *(int *)src; }
static void smaller_signed_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(int *)dst < *(int *)src; }
static void greater_eq_signed_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(int *)dst >= *(int *)src; }
static void smaller_eq_signed_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(int *)dst <= *(int *)src; }
static void equal_signed_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(int *)dst == *(int *)src; }
static void not_equal_signed_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(int *)dst != *(int *)src; }

/* XXX assumes enum = int */	
static void enum_from_char(void *dest, void *src) {
	int_from_char(dest, src); }
static void enum_from_schar(void *dest, void *src) {
	int_from_schar(dest, src); }
static void enum_from_uchar(void *dest, void *src) {
	int_from_uchar(dest, src); }
static void enum_from_short(void *dest, void *src) {
	int_from_short(dest, src); }
static void enum_from_ushort(void *dest, void *src) {
	int_from_ushort(dest, src); }
static void enum_from_int(void *dest, void *src) {
	int_from_int(dest, src); }
static void enum_from_uint(void *dest, void *src) {
	int_from_uint(dest, src); }
static void enum_from_long(void *dest, void *src) {
	int_from_long(dest, src); }
static void enum_from_ulong(void *dest, void *src) {
	int_from_ulong(dest, src); }
static void enum_from_llong(void *dest, void *src) {
	int_from_llong(dest, src); }
static void enum_from_ullong(void *dest, void *src) {
	int_from_ullong(dest, src); }
static void enum_from_float(void *dest, void *src) {
	int_from_float(dest, src); }
static void enum_from_double(void *dest, void *src) {
	int_from_double(dest, src); }
static void enum_from_ldouble(void *dest, void *src) {
	int_from_ldouble(dest, src); }

static void uint_from_char(void *dest, void *src) {
	*(unsigned int *)dest = (unsigned int) *(char *)src; }
static void uint_from_schar(void *dest, void *src) {
	*(unsigned int *)dest = (unsigned int) *(signed char *)src; }
static void uint_from_uchar(void *dest, void *src) {
	*(unsigned int *)dest = (unsigned int) *(unsigned char *)src; }
static void uint_from_short(void *dest, void *src) {
	*(unsigned int *)dest = (unsigned int) *(short *)src; }
static void uint_from_ushort(void *dest, void *src) {
	*(unsigned int *)dest = (unsigned int) *(unsigned short *)src; }
static void uint_from_int(void *dest, void *src) {
	*(unsigned int *)dest = (unsigned int) *(int *)src; }
static void uint_from_uint(void *dest, void *src) {
	*(unsigned int *)dest = (unsigned int) *(unsigned int *)src; }
static void uint_from_long(void *dest, void *src) {
	*(unsigned int *)dest = (unsigned int) *(long *)src; }
static void uint_from_ulong(void *dest, void *src) {
	*(unsigned int *)dest = (unsigned int) *(unsigned long *)src; }
static void uint_from_llong(void *dest, void *src) {
	*(unsigned int *)dest = (unsigned int) *(long long *)src; }
static void uint_from_ullong(void *dest, void *src) {
	*(unsigned int *)dest = (unsigned int) *(unsigned long long *)src; }
static void uint_from_float(void *dest, void *src) {
	*(unsigned int *)dest = (unsigned int) *(float *)src; }
static void uint_from_double(void *dest, void *src) {
	*(unsigned int *)dest = (unsigned int) *(double *)src; }
static void uint_from_ldouble(void *dest, void *src) {
	*(unsigned int *)dest = (unsigned int) *(long double *)src; }
static void negate_unsigned_int(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(unsigned int *)res = -*(unsigned int *)src; (void)unused; }
static void bnegate_unsigned_int(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(unsigned int *)res = ~*(unsigned int *)src; (void)unused; }
static void lnegate_unsigned_int(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(unsigned int *)res = !*(unsigned int *)src; (void)unused; }
static void add_unsigned_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned *)res = *(unsigned *)dst + *(unsigned *)src; }
static void sub_unsigned_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned *)res = *(unsigned *)dst - *(unsigned *)src; }
static void mul_unsigned_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned *)res = *(unsigned *)dst * *(unsigned *)src; }
static void div_unsigned_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	if (*(unsigned *)src == 0) {
		warningfl(tok, "Division by zero");
		*(unsigned *)res = 0;
		return;
	}
	*(unsigned *)res = *(unsigned *)dst / *(unsigned *)src; }
static void mod_unsigned_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	if (*(unsigned *)src == 0) {
		warningfl(tok, "Division by zero");
		*(unsigned *)res = 0;
		return;
	}
	*(unsigned *)res = *(unsigned *)dst % *(unsigned *)src; }
static void band_unsigned_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned *)res = *(unsigned *)dst & *(unsigned *)src; }
static void bor_unsigned_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned *)res = *(unsigned *)dst | *(unsigned *)src; }
static void xor_unsigned_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned *)res = *(unsigned *)dst ^ *(unsigned *)src; }
static void bshl_unsigned_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned *)res = *(unsigned *)dst << *(int *)src; }
static void bshr_unsigned_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned *)res = *(unsigned *)dst >> *(int *)src; }
static void greater_unsigned_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned *)dst > *(unsigned *)src; }
static void smaller_unsigned_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned *)dst < *(unsigned *)src; }
static void greater_eq_unsigned_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned *)dst >= *(unsigned *)src; }
static void smaller_eq_unsigned_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned *)dst <= *(unsigned *)src; }
static void equal_unsigned_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned *)dst == *(unsigned *)src; }
static void not_equal_unsigned_int(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned *)dst != *(unsigned *)src; }

static void long_from_char(void *dest, void *src) {
	*(long int *)dest = (long int) *(char *)src; }
static void long_from_schar(void *dest, void *src) {
	*(long int *)dest = (long int) *(signed char *)src; }
static void long_from_uchar(void *dest, void *src) {
	*(long int *)dest = (long int) *(unsigned char *)src; }
static void long_from_short(void *dest, void *src) {
	*(long int *)dest = (long int) *(short *)src; }
static void long_from_ushort(void *dest, void *src) {
	*(long int *)dest = (long int) *(unsigned short *)src; }
static void long_from_int(void *dest, void *src) {
	*(long int *)dest = (long int) *(int *)src; }
static void long_from_uint(void *dest, void *src) {
	*(long int *)dest = (long int) *(unsigned int *)src; }
static void long_from_long(void *dest, void *src) {
	*(long int *)dest = (long int) *(long *)src; }
static void long_from_ulong(void *dest, void *src) {
	*(long int *)dest = (long int) *(unsigned long *)src; }
static void long_from_llong(void *dest, void *src) {
	*(long int *)dest = (long int) *(long long *)src; }
static void long_from_ullong(void *dest, void *src) {
	*(long int *)dest = (long int) *(unsigned long long *)src; }
static void long_from_float(void *dest, void *src) {
	*(long int *)dest = (long int) *(float *)src; }
static void long_from_double(void *dest, void *src) {
	*(long int *)dest = (long int) *(double *)src; }
static void long_from_ldouble(void *dest, void *src) {
	*(long int *)dest = (long int) *(long double *)src; }
static void negate_signed_long(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(long *)res = -*(long *)src; (void)unused; }
static void bnegate_signed_long(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(long *)res = ~*(long *)src; (void)unused; }
static void lnegate_signed_long(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(long *)res = !*(long *)src; (void)unused; }
static void add_signed_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long *)res = *(long *)dst + *(long *)src; }
static void sub_signed_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long *)res = *(long *)dst - *(long *)src; }
static void mul_signed_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long *)res = *(long *)dst * *(long *)src; }
static void div_signed_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	if (*(long *)src == 0) {
		warningfl(tok, "Division by zero");
		*(long *)res = 0;
		return;
	}
	*(long *)res = *(long *)dst / *(long *)src; }
static void mod_signed_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	if (*(long *)src == 0) {
		warningfl(tok, "Division by zero");
		*(long *)res = 0;
		return;
	}
	*(long *)res = *(long *)dst % *(long *)src; }
static void band_signed_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long *)res = *(long *)dst & *(long *)src; }
static void bor_signed_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long *)res = *(long *)dst | *(long *)src; }
static void xor_signed_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long *)res = *(long *)dst ^ *(long *)src; }
static void bshl_signed_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long *)res = *(long *)dst << *(int *)src; }
static void bshr_signed_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long *)res = *(long *)dst >> *(int *)src; }
static void greater_signed_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long *)dst > *(long *)src; }
static void smaller_signed_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long *)dst < *(long *)src; }
static void greater_eq_signed_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long *)dst >= *(long *)src; }
static void smaller_eq_signed_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long *)dst <= *(long *)src; }
static void equal_signed_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long *)dst == *(long *)src; }
static void not_equal_signed_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long *)dst != *(long *)src; }


static void ulong_from_char(void *dest, void *src) {
	*(unsigned long *)dest = (unsigned long) *(char *)src; }
static void ulong_from_schar(void *dest, void *src) {
	*(unsigned long *)dest = (unsigned long) *(signed char *)src; }
static void ulong_from_uchar(void *dest, void *src) {
	*(unsigned long *)dest = (unsigned long) *(unsigned char *)src; }
static void ulong_from_short(void *dest, void *src) {
	*(unsigned long *)dest = (unsigned long) *(short *)src; }
static void ulong_from_ushort(void *dest, void *src) {
	*(unsigned long *)dest = (unsigned long) *(unsigned short *)src; }
static void ulong_from_int(void *dest, void *src) {
	*(unsigned long *)dest = (unsigned long) *(int *)src; }
static void ulong_from_uint(void *dest, void *src) {
	*(unsigned long *)dest = (unsigned long) *(unsigned int *)src; }
static void ulong_from_long(void *dest, void *src) {
	*(unsigned long *)dest = (unsigned long) *(long *)src; }
static void ulong_from_ulong(void *dest, void *src) {
	*(unsigned long *)dest = (unsigned long) *(unsigned long *)src; }
static void ulong_from_llong(void *dest, void *src) {
	*(unsigned long *)dest = (unsigned long) *(long long *)src; }
static void ulong_from_ullong(void *dest, void *src) {
	*(unsigned long *)dest = (unsigned long) *(unsigned long long *)src; }
static void ulong_from_float(void *dest, void *src) {
	*(unsigned long *)dest = (unsigned long) *(float *)src; }
static void ulong_from_double(void *dest, void *src) {
	*(unsigned long *)dest = (unsigned long) *(double *)src; }
static void ulong_from_ldouble(void *dest, void *src) {
	*(unsigned long *)dest = (unsigned long) *(long double *)src; }
static void negate_unsigned_long(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(unsigned long *)res = -*(unsigned long *)src; (void)unused; }
static void bnegate_unsigned_long(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(unsigned long *)res = ~*(unsigned long *)src; (void)unused; }
static void lnegate_unsigned_long(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(unsigned long *)res = !*(unsigned long *)src; (void)unused; }
static void add_unsigned_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned long *)res = *(unsigned long *)dst + *(unsigned long *)src; }
static void sub_unsigned_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned long *)res = *(unsigned long *)dst - *(unsigned long *)src; }
static void mul_unsigned_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned long *)res = *(unsigned long *)dst * *(unsigned long *)src; }
static void div_unsigned_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	if (*(unsigned long *)src == 0) {
		warningfl(tok, "Division by zero");
		*(unsigned long *)res = 0;
		return;
	}
	*(unsigned long *)res = *(unsigned long *)dst / *(unsigned long *)src; }
static void mod_unsigned_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	if (*(unsigned long *)src == 0) {
		warningfl(tok, "Division by zero");
		*(unsigned long *)res = 0;
		return;
	}
	*(unsigned long *)res = *(unsigned long *)dst % *(unsigned long *)src; }
static void band_unsigned_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned long *)res = *(unsigned long *)dst & *(unsigned long *)src; }
static void bor_unsigned_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned long *)res = *(unsigned long *)dst | *(unsigned long *)src; }
static void xor_unsigned_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned long *)res = *(unsigned long *)dst ^ *(unsigned long *)src; }
static void bshl_unsigned_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned long *)res = *(unsigned long *)dst << *(int *)src; }
static void bshr_unsigned_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned long *)res = *(unsigned long *)dst >> *(int *)src; }
static void greater_unsigned_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned long *)dst > *(unsigned long *)src; }
static void smaller_unsigned_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned long *)dst < *(unsigned long *)src; }
static void greater_eq_unsigned_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned long *)dst >= *(unsigned long *)src; }
static void smaller_eq_unsigned_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned long *)dst <= *(unsigned long *)src; }
static void equal_unsigned_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned long *)dst == *(unsigned long *)src; }
static void not_equal_unsigned_long(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned long *)dst != *(unsigned long *)src; }

static void llong_from_char(void *dest, void *src) {
	*(long long int *)dest = (long long int) *(char *)src; }
static void llong_from_schar(void *dest, void *src) {
	*(long long int *)dest = (long long int) *(signed char *)src; }
static void llong_from_uchar(void *dest, void *src) {
	*(long long int *)dest = (long long int) *(unsigned char *)src; }
static void llong_from_short(void *dest, void *src) {
	*(long long int *)dest = (long long int) *(short *)src; }
static void llong_from_ushort(void *dest, void *src) {
	*(long long int *)dest = (long long int) *(unsigned short *)src; }
static void llong_from_int(void *dest, void *src) {
	*(long long int *)dest = (long long int) *(int *)src; }
static void llong_from_uint(void *dest, void *src) {
	*(long long int *)dest = (long long int) *(unsigned int *)src; }
static void llong_from_long(void *dest, void *src) {
	*(long long int *)dest = (long long int) *(long *)src; }
static void llong_from_ulong(void *dest, void *src) {
	*(long long int *)dest = (long long int) *(unsigned long *)src; }
static void llong_from_llong(void *dest, void *src) {
	*(long long int *)dest = (long long int) *(long long *)src; }
static void llong_from_ullong(void *dest, void *src) {
	*(long long int *)dest = (long long int) *(unsigned long long *)src; }
static void llong_from_float(void *dest, void *src) {
	*(long long int *)dest = (long long int) *(float *)src; }
static void llong_from_double(void *dest, void *src) {
	*(long long int *)dest = (long long int) *(double *)src; }
static void llong_from_ldouble(void *dest, void *src) {
	*(long long int *)dest = (long long int) *(long double *)src; }
static void negate_signed_llong(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(long long *)res = -*(long long *)src; (void)unused; }
static void bnegate_signed_llong(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(long long *)res = ~*(long long *)src; (void)unused; }
static void lnegate_signed_llong(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(long long *)res = !*(long long *)src; (void)unused; }
static void add_signed_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long long *)res = *(long long *)dst + *(long long *)src; }
static void sub_signed_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long long *)res = *(long long *)dst - *(long long *)src; }
static void mul_signed_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long long *)res = *(long long *)dst * *(long long *)src; }
static void div_signed_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	if (*(long long *)src == 0) {
		warningfl(tok, "Division by zero");
		*(long long *)res = 0;
		return;
	}
	*(long long *)res = *(long long *)dst / *(long long *)src; }
static void mod_signed_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	if (*(long long *)src == 0) {
		warningfl(tok, "Division by zero");
		*(long long *)res = 0;
		return;
	}
	*(long long *)res = *(long long *)dst % *(long long *)src; }
static void band_signed_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long long *)res = *(long long *)dst & *(long long *)src; }
static void bor_signed_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long long *)res = *(long long *)dst | *(long long *)src; }
static void xor_signed_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long long *)res = *(long long *)dst ^ *(long long *)src; }
static void bshl_signed_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long long *)res = *(long long *)dst << *(int *)src; }
static void bshr_signed_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long long *)res = *(long long *)dst >> *(int *)src; }
static void greater_signed_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long long *)dst > *(long long *)src; }
static void smaller_signed_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long long *)dst < *(long long *)src; }
static void greater_eq_signed_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long long *)dst >= *(long long *)src; }
static void smaller_eq_signed_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long long *)dst <= *(long long *)src; }
static void equal_signed_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long long *)dst == *(long long *)src; }
static void not_equal_signed_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long long *)dst != *(long long *)src; }


static void ullong_from_char(void *dest, void *src) {
	*(unsigned long long *)dest = (unsigned long long) *(char *)src; }
static void ullong_from_schar(void *dest, void *src) {
	*(unsigned long long *)dest = (unsigned long long) *(signed char *)src; }
static void ullong_from_uchar(void *dest, void *src) {
	*(unsigned long long *)dest = (unsigned long long) *(unsigned char *)src; }
static void ullong_from_short(void *dest, void *src) {
	*(unsigned long long *)dest = (unsigned long long) *(short *)src; }
static void ullong_from_ushort(void *dest, void *src) {
	*(unsigned long long *)dest = (unsigned long long) *(unsigned short *)src; }
static void ullong_from_int(void *dest, void *src) {
	*(unsigned long long *)dest = (unsigned long long) *(int *)src; }
static void ullong_from_uint(void *dest, void *src) {
	*(unsigned long long *)dest = (unsigned long long) *(unsigned int *)src; }
static void ullong_from_long(void *dest, void *src) {
	*(unsigned long long *)dest = (unsigned long long) *(long *)src; }
static void ullong_from_ulong(void *dest, void *src) {
	*(unsigned long long *)dest = (unsigned long long) *(unsigned long *)src; }
static void ullong_from_llong(void *dest, void *src) {
	*(unsigned long long *)dest = (unsigned long long) *(long long *)src; }
static void ullong_from_ullong(void *dest, void *src) {
	*(unsigned long long *)dest = (unsigned long long) *(unsigned long long *)src; }
static void ullong_from_float(void *dest, void *src) {
	*(unsigned long long *)dest = (unsigned long long) *(float *)src; }
static void ullong_from_double(void *dest, void *src) {
	*(unsigned long long *)dest = (unsigned long long) *(double *)src; }
static void ullong_from_ldouble(void *dest, void *src) {
	*(unsigned long long *)dest = (unsigned long long) *(long double *)src; }
static void negate_unsigned_llong(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(unsigned long long *)res = -*(unsigned long long *)src; (void)unused; }
static void bnegate_unsigned_llong(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(unsigned long long *)res = ~*(unsigned long long *)src; (void)unused; }
static void lnegate_unsigned_llong(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(unsigned long *)res = !*(unsigned long *)src; (void)unused; }
static void add_unsigned_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned long long *)res = *(unsigned long long *)dst
		+ *(unsigned long long *)src;
}
static void sub_unsigned_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned long long *)res = *(unsigned long long *)dst
		- *(unsigned long long *)src; }
static void mul_unsigned_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned long long *)res = *(unsigned long long *)dst
		* *(unsigned long long *)src; }
static void div_unsigned_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	if (*(unsigned long long *)src == 0) {
		warningfl(tok, "Division by zero");
		*(unsigned long long *)res = 0;
		return;
	}
	*(unsigned long long *)res = *(unsigned long long *)dst
		/ *(unsigned long long *)src; }
static void mod_unsigned_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	if (*(unsigned long long *)src == 0) {
		warningfl(tok, "Division by zero");
		*(unsigned long long *)res = 0;
		return;
	}
	*(unsigned long long *)res = *(unsigned long long *)dst
		% *(unsigned long long *)src; }
static void band_unsigned_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned long long *)res = *(unsigned long long *)dst
		& *(unsigned long long *)src; }
static void bor_unsigned_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned long long *)res = *(unsigned long long *)dst
		| *(unsigned long long *)src; }
static void xor_unsigned_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned long long *)res = *(unsigned long long *)dst
		^ *(unsigned long long *)src; }
static void bshl_unsigned_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned long long *)res = *(unsigned long long *)dst << *(int *)src; }
static void bshr_unsigned_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(unsigned long long *)res = *(unsigned long long *)dst >> *(int *)src; }
static void greater_unsigned_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned long long *)dst
		> *(unsigned long long *)src; }
static void smaller_unsigned_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned long long *)dst
		< *(unsigned long long *)src; }
static void greater_eq_unsigned_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned long long *)dst
		>= *(unsigned long long *)src; }
static void smaller_eq_unsigned_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned long long *)dst
		<= *(unsigned long long *)src; }
static void equal_unsigned_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned long long *)dst
		== *(unsigned long long *)src; }
static void not_equal_unsigned_llong(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(unsigned long long *)dst
		!= *(unsigned long long *)src; }

static void float_from_char(void *dest, void *src) {
	*(float *)dest = (float) *(char *)src; }
static void float_from_schar(void *dest, void *src) {
	*(float *)dest = (float) *(signed char *)src; }
static void float_from_uchar(void *dest, void *src) {
	*(float *)dest = (float) *(unsigned char *)src; }
static void float_from_short(void *dest, void *src) {
	*(float *)dest = (float) *(short *)src; }
static void float_from_ushort(void *dest, void *src) {
	*(float *)dest = (float) *(unsigned short *)src; }
static void float_from_int(void *dest, void *src) {
	*(float *)dest = (float) *(int *)src; }
static void float_from_uint(void *dest, void *src) {
	*(float *)dest = (float) *(unsigned int *)src; }
static void float_from_long(void *dest, void *src) {
	*(float *)dest = (float) *(long *)src; }
static void float_from_ulong(void *dest, void *src) {
	*(float *)dest = (float) *(unsigned long *)src; }
static void float_from_llong(void *dest, void *src) {
	*(float *)dest = (float) *(long long *)src; }
static void float_from_ullong(void *dest, void *src) {
	*(float *)dest = (float) *(unsigned long long *)src; }
static void float_from_float(void *dest, void *src) {
	*(float *)dest = (float) *(float *)src; }
static void float_from_double(void *dest, void *src) {
	*(float *)dest = (float) *(double *)src; }
static void float_from_ldouble(void *dest, void *src) {
	*(float *)dest = (float) *(long double *)src; }
static void negate_float(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(float *)res = -*(float *)src; (void)unused; }
	/* XXX nwcc doesn't support float foo; !foo; yet :-/ */
static void lnegate_float(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(float *)res = *(float *)src == 0.0f; (void)unused; }
static void add_float(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(float *)res = *(float *)dst + *(float *)src; }
static void sub_float(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(float *)res = *(float *)dst - *(float *)src; }
static void mul_float(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(float *)res = *(float *)dst * *(float *)src; }
static void div_float(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	if (*(float *)src == 0.0f) {
		warningfl(tok, "Division by zero");
		*(float *)res = 0;
		return;
	}
	*(float *)res = *(float *)dst / *(float *)src; }
static void greater_float(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(float *)dst > *(float *)src; }
static void smaller_float(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(float *)dst < *(float *)src; }
static void greater_eq_float(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(float *)dst >= *(float *)src; }
static void smaller_eq_float(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(float *)dst <= *(float *)src; }
static void equal_float(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(float *)dst == *(float *)src; }
static void not_equal_float(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(float *)dst != *(float *)src; }

static void double_from_char(void *dest, void *src) {
	*(double *)dest = (double) *(char *)src; }
static void double_from_schar(void *dest, void *src) {
	*(double *)dest = (double) *(signed char *)src; }
static void double_from_uchar(void *dest, void *src) {
	*(double *)dest = (double) *(unsigned char *)src; }
static void double_from_short(void *dest, void *src) {
	*(double *)dest = (double) *(short *)src; }
static void double_from_ushort(void *dest, void *src) {
	*(double *)dest = (double) *(unsigned short *)src; }
static void double_from_int(void *dest, void *src) {
	*(double *)dest = (double) *(int *)src; }
static void double_from_uint(void *dest, void *src) {
	*(double *)dest = (double) *(unsigned int *)src; }
static void double_from_long(void *dest, void *src) {
	*(double *)dest = (double) *(long *)src; }
static void double_from_ulong(void *dest, void *src) {
	*(double *)dest = (double) *(unsigned long *)src; }
static void double_from_llong(void *dest, void *src) {
	*(double *)dest = (double) *(long long *)src; }
static void double_from_ullong(void *dest, void *src) {
	*(double *)dest = (double) *(unsigned long long *)src; }
static void double_from_float(void *dest, void *src) {
	*(double *)dest = (double) *(float *)src; }
static void double_from_double(void *dest, void *src) {
	*(double *)dest = (double) *(double *)src; }
static void double_from_ldouble(void *dest, void *src) {
	*(double *)dest = (double) *(long double *)src; }
static void negate_double(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(double *)res = -*(double *)src; (void)unused; }
static void lnegate_double(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(double *)res = *(double *)src == 0.0; (void)unused; }
static void add_double(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(double *)res = *(double *)dst + *(double *)src; }
static void sub_double(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(double *)res = *(double *)dst - *(double *)src; }
static void mul_double(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(double *)res = *(double *)dst * *(double *)src; }
static void div_double(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	if (*(double *)src == 0.0) {
		warningfl(tok, "Division by zero");
		*(double *)res = 0;
		return;
	}
	*(double *)res = *(double *)dst / *(double *)src; }
static void greater_double(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(double *)dst > *(double *)src; }
static void smaller_double(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(double *)dst < *(double *)src; }
static void greater_eq_double(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(double *)dst >= *(double *)src; }
static void smaller_eq_double(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(double *)dst <= *(double *)src; }
static void equal_double(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(double *)dst == *(double *)src; }
static void not_equal_double(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(double *)dst != *(double *)src; }

static void ldouble_from_char(void *dest, void *src) {
	*(long double *)dest = (long double) *(char *)src; }
static void ldouble_from_schar(void *dest, void *src) {
	*(long double *)dest = (long double) *(signed char *)src; }
static void ldouble_from_uchar(void *dest, void *src) {
	*(long double *)dest = (long double) *(unsigned char *)src; }
static void ldouble_from_short(void *dest, void *src) {
	*(long double *)dest = (long double) *(short *)src; }
static void ldouble_from_ushort(void *dest, void *src) {
	*(long double *)dest = (long double) *(unsigned short *)src; }
static void ldouble_from_int(void *dest, void *src) {
	*(long double *)dest = (long double) *(int *)src; }
static void ldouble_from_uint(void *dest, void *src) {
	*(long double *)dest = (long double) *(unsigned int *)src; }
static void ldouble_from_long(void *dest, void *src) {
	*(long double *)dest = (long double) *(long *)src; }
static void ldouble_from_ulong(void *dest, void *src) {
	*(long double *)dest = (long double) *(unsigned long *)src; }
static void ldouble_from_llong(void *dest, void *src) {
	*(long double *)dest = (long double) *(long long *)src; }
static void ldouble_from_ullong(void *dest, void *src) {
	*(long double *)dest = (long double) *(unsigned long long *)src; }
static void ldouble_from_float(void *dest, void *src) {
	*(long double *)dest = (long double) *(float *)src; }
static void ldouble_from_double(void *dest, void *src) {
	*(long double *)dest = (long double) *(double *)src; }
static void ldouble_from_ldouble(void *dest, void *src) {
	*(long double *)dest = (long double) *(long double *)src; }
static void negate_ldouble(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(long double *)res = -*(long double *)src; (void)unused; }
static void lnegate_ldouble(struct token *tok, void *res, void *src, void *unused) {
	(void) tok;
	*(int *)res = *(long double *)src == 0.0L;
	(void)unused; }
static void add_ldouble(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long double *)res = *(long double *)dst + *(long double *)src; }
static void sub_ldouble(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long double *)res = *(long double *)dst - *(long double *)src; }
static void mul_ldouble(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(long double *)res = *(long double *)dst * *(long double *)src; }
static void div_ldouble(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	if (*(long double *)src == 0.0L) {
		warningfl(tok, "Division by zero");
		*(long double *)res = 0;
		return;
	}
	*(long double *)res = *(long double *)dst / *(long double *)src; }
static void greater_ldouble(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long double *)dst > *(long double *)src; }
static void smaller_ldouble(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long double *)dst < *(long double *)src; }
static void greater_eq_ldouble(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long double *)dst >= *(long double *)src; }
static void smaller_eq_ldouble(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long double *)dst <= *(long double *)src; }
static void equal_ldouble(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long double *)dst == *(long double *)src; }
static void not_equal_ldouble(struct token *tok, void *res, void *dst, void *src) {
	(void) tok;
	*(int *)res = *(long double *)dst != *(long double *)src; }

struct type_mapping char_map = {
	{ TY_CHAR, TOK_KEY_UNSIGNED, 0, 0, 0, ASCII_NONE }, /* XXX :/ */
	TY_CHAR,
	char_from_char,
	char_from_schar,
	char_from_uchar,
	char_from_short,
	char_from_ushort,
	char_from_int,
	char_from_uint,
	char_from_long,
	char_from_ulong,
	char_from_llong,
	char_from_ullong,
	char_from_float,
	char_from_double,
	char_from_ldouble,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
struct type_mapping signed_char_map = {
	{ TY_SCHAR, TOK_KEY_SIGNED, 0, 0, 0, ASCII_NONE },
	TY_SCHAR,
	schar_from_char,
	schar_from_schar,
	schar_from_uchar,
	schar_from_short,
	schar_from_ushort,
	schar_from_int,
	schar_from_uint,
	schar_from_long,
	schar_from_ulong,
	schar_from_llong,
	schar_from_ullong,
	schar_from_float,
	schar_from_double,
	schar_from_ldouble,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
struct type_mapping unsigned_char_map = {
	{ TY_UCHAR, TOK_KEY_UNSIGNED, 0, 0, 0, ASCII_NONE },
	TY_UCHAR,
	uchar_from_char,
	uchar_from_schar,
	uchar_from_uchar,
	uchar_from_short,
	uchar_from_ushort,
	uchar_from_int,
	uchar_from_uint,
	uchar_from_long,
	uchar_from_ulong,
	uchar_from_llong,
	uchar_from_ullong,
	uchar_from_float,
	uchar_from_double,
	uchar_from_ldouble,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
struct type_mapping signed_short_map = {
	{ TY_SHORT, TOK_KEY_SIGNED, 0, 0, 0, ASCII_NONE },
	TY_SHORT,
	short_from_char,
	short_from_schar,
	short_from_uchar,
	short_from_short,
	short_from_ushort,
	short_from_int,
	short_from_uint,
	short_from_long,
	short_from_ulong,
	short_from_llong,
	short_from_ullong,
	short_from_float,
	short_from_double,
	short_from_ldouble,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};		
struct type_mapping unsigned_short_map = {
	{ TY_USHORT, TOK_KEY_UNSIGNED, 0, 0, 0, ASCII_NONE },
	TY_USHORT,
	ushort_from_char,
	ushort_from_schar,
	ushort_from_uchar,
	ushort_from_short,
	ushort_from_ushort,
	ushort_from_int,
	ushort_from_uint,
	ushort_from_long,
	ushort_from_ulong,
	ushort_from_llong,
	ushort_from_ullong,
	ushort_from_float,
	ushort_from_double,
	ushort_from_ldouble,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};		
struct type_mapping signed_int_map = {
	{ TY_INT, TOK_KEY_SIGNED, 0, 0, 0, ASCII_NONE },
	TY_INT,
	int_from_char,
	int_from_schar,
	int_from_uchar,
	int_from_short,
	int_from_ushort,
	int_from_int,
	int_from_uint,
	int_from_long,
	int_from_ulong,
	int_from_llong,
	int_from_ullong,
	int_from_float,
	int_from_double,
	int_from_ldouble,
	negate_signed_int,
	bnegate_signed_int,
	lnegate_signed_int,
	add_signed_int,
	sub_signed_int,
	mul_signed_int,
	div_signed_int,
	mod_signed_int,
	band_signed_int,
	bor_signed_int,
	xor_signed_int,
	bshl_signed_int,
	bshr_signed_int,
	greater_signed_int,
	smaller_signed_int,
	greater_eq_signed_int,
	smaller_eq_signed_int,
	equal_signed_int,
	not_equal_signed_int
};
struct type_mapping enum_map = {
	{ TY_INT, TOK_KEY_SIGNED, 0, 0, 0, ASCII_NONE },
	TY_INT,
	enum_from_char,
	enum_from_schar,
	enum_from_uchar,
	enum_from_short,
	enum_from_ushort,
	enum_from_int,
	enum_from_uint,
	enum_from_long,
	enum_from_ulong,
	enum_from_llong,
	enum_from_ullong,
	enum_from_float,
	enum_from_double,
	enum_from_ldouble,
	/* XXX */
	negate_signed_int,
	bnegate_signed_int,
	lnegate_signed_int,
	add_signed_int,
	sub_signed_int,
	mul_signed_int,
	div_signed_int,
	mod_signed_int,
	band_signed_int,
	bor_signed_int,
	xor_signed_int,
	bshl_signed_int,
	bshr_signed_int,
	greater_signed_int,
	smaller_signed_int,
	greater_eq_signed_int,
	smaller_eq_signed_int,
	equal_signed_int,
	not_equal_signed_int
};		
struct type_mapping unsigned_int_map = {
	{ TY_UINT, TOK_KEY_UNSIGNED, 0, 0, 0, ASCII_NONE },
	TY_UINT,
	uint_from_char,
	uint_from_schar,
	uint_from_uchar,
	uint_from_short,
	uint_from_ushort,
	uint_from_int,
	uint_from_uint,
	uint_from_long,
	uint_from_ulong,
	uint_from_llong,
	uint_from_ullong,
	uint_from_float,
	uint_from_double,
	uint_from_ldouble,
	negate_unsigned_int,
	bnegate_unsigned_int,
	lnegate_unsigned_int,
	add_unsigned_int,
	sub_unsigned_int,
	mul_unsigned_int,
	div_unsigned_int,
	mod_unsigned_int,
	band_unsigned_int,
	bor_unsigned_int,
	xor_unsigned_int,
	bshl_unsigned_int,
	bshr_unsigned_int,
	greater_unsigned_int,
	smaller_unsigned_int,
	greater_eq_unsigned_int,
	smaller_eq_unsigned_int,
	equal_unsigned_int,
	not_equal_unsigned_int
};
struct type_mapping signed_long_map = {
	{ TY_LONG, TOK_KEY_SIGNED, 0, 0, 0, ASCII_NONE },
	TY_LONG,
	long_from_char,
	long_from_schar,
	long_from_uchar,
	long_from_short,
	long_from_ushort,
	long_from_int,
	long_from_uint,
	long_from_long,
	long_from_ulong,
	long_from_llong,
	long_from_ullong,
	long_from_float,
	long_from_double,
	long_from_ldouble,
	negate_signed_long,
	bnegate_signed_long,
	lnegate_signed_long,
	add_signed_long,
	sub_signed_long,
	mul_signed_long,
	div_signed_long,
	mod_signed_long,
	band_signed_long,
	bor_signed_long,
	xor_signed_long,
	bshl_signed_long,
	bshr_signed_long,
	greater_signed_long,
	smaller_signed_long,
	greater_eq_signed_long,
	smaller_eq_signed_long,
	equal_signed_long,
	not_equal_signed_long
};
struct type_mapping unsigned_long_map = {
	{ TY_ULONG, TOK_KEY_UNSIGNED, 0, 0, 0, ASCII_NONE },
	TY_ULONG,
	ulong_from_char,
	ulong_from_schar,
	ulong_from_uchar,
	ulong_from_short,
	ulong_from_ushort,
	ulong_from_int,
	ulong_from_uint,
	ulong_from_long,
	ulong_from_ulong,
	ulong_from_llong,
	ulong_from_ullong,
	ulong_from_float,
	ulong_from_double,
	ulong_from_ldouble,
	negate_unsigned_long,
	bnegate_unsigned_long,
	lnegate_unsigned_long,
	add_unsigned_long,
	sub_unsigned_long,
	mul_unsigned_long,
	div_unsigned_long,
	mod_unsigned_long,
	band_unsigned_long,
	bor_unsigned_long,
	xor_unsigned_long,
	bshl_unsigned_long,
	bshr_unsigned_long,
	greater_unsigned_long,
	smaller_unsigned_long,
	greater_eq_unsigned_long,
	smaller_eq_unsigned_long,
	equal_unsigned_long,
	not_equal_unsigned_long
};
struct type_mapping signed_llong_map = {
	{ TY_LLONG, TOK_KEY_SIGNED, 0, 0, 0, ASCII_NONE },
	TY_LLONG,
	llong_from_char,
	llong_from_schar,
	llong_from_uchar,
	llong_from_short,
	llong_from_ushort,
	llong_from_int,
	llong_from_uint,
	llong_from_long,
	llong_from_ulong,
	llong_from_llong,
	llong_from_ullong,
	llong_from_float,
	llong_from_double,
	llong_from_ldouble,
	negate_signed_llong,
	bnegate_signed_llong,
	lnegate_signed_llong,
	add_signed_llong,
	sub_signed_llong,
	mul_signed_llong,
	div_signed_llong,
	mod_signed_llong,
	band_signed_llong,
	bor_signed_llong,
	xor_signed_llong,
	bshl_signed_llong,
	bshr_signed_llong,
	greater_signed_llong,
	smaller_signed_llong,
	greater_eq_signed_llong,
	smaller_eq_signed_llong,
	equal_signed_llong,
	not_equal_signed_llong
};
struct type_mapping unsigned_llong_map = {
	{ TY_ULLONG, TOK_KEY_UNSIGNED, 0, 0, 0, ASCII_NONE },
	TY_ULLONG,
	ullong_from_char,
	ullong_from_schar,
	ullong_from_uchar,
	ullong_from_short,
	ullong_from_ushort,
	ullong_from_int,
	ullong_from_uint,
	ullong_from_long,
	ullong_from_ulong,
	ullong_from_llong,
	ullong_from_ullong,
	ullong_from_float,
	ullong_from_double,
	ullong_from_ldouble,
	negate_unsigned_llong,
	bnegate_unsigned_llong,
	lnegate_unsigned_llong,
	add_unsigned_llong,
	sub_unsigned_llong,
	mul_unsigned_llong,
	div_unsigned_llong,
	mod_unsigned_llong,
	band_unsigned_llong,
	bor_unsigned_llong,
	xor_unsigned_llong,
	bshl_unsigned_llong,
	bshr_unsigned_llong,
	greater_unsigned_llong,
	smaller_unsigned_llong,
	greater_eq_unsigned_llong,
	smaller_eq_unsigned_llong,
	equal_unsigned_llong,
	not_equal_unsigned_llong
};
struct type_mapping float_map = {
	{ TY_FLOAT, 0, 0, 0, 0, ASCII_NONE },
	TY_FLOAT,
	float_from_char,
	float_from_schar,
	float_from_uchar,
	float_from_short,
	float_from_ushort,
	float_from_int,
	float_from_uint,
	float_from_long,
	float_from_ulong,
	float_from_llong,
	float_from_ullong,
	float_from_float,
	float_from_double,
	float_from_ldouble,
	negate_float,
	NULL,
	lnegate_float,
	add_float,
	sub_float,
	mul_float,
	div_float,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	greater_float,
	smaller_float,
	greater_eq_float,
	smaller_eq_float,
	equal_float,
	not_equal_float
};
struct type_mapping double_map = {
	{ TY_DOUBLE, 0, 0, 0, 0, ASCII_NONE },
	TY_DOUBLE,
	double_from_char,
	double_from_schar,
	double_from_uchar,
	double_from_short,
	double_from_ushort,
	double_from_int,
	double_from_uint,
	double_from_long,
	double_from_ulong,
	double_from_llong,
	double_from_ullong,
	double_from_float,
	double_from_double,
	double_from_ldouble,
	negate_double,
	NULL,
	lnegate_double,
	add_double,
	sub_double,
	mul_double,
	div_double,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	greater_double,
	smaller_double,
	greater_eq_double,
	smaller_eq_double,
	equal_double,
	not_equal_double
};
struct type_mapping ldouble_map = {
	{ TY_LDOUBLE, 0, 0, 0, 0, ASCII_NONE },
	TY_LDOUBLE,
	ldouble_from_char,
	ldouble_from_schar,
	ldouble_from_uchar,
	ldouble_from_short,
	ldouble_from_ushort,
	ldouble_from_int,
	ldouble_from_uint,
	ldouble_from_long,
	ldouble_from_ulong,
	ldouble_from_llong,
	ldouble_from_ullong,
	ldouble_from_float,
	ldouble_from_double,
	ldouble_from_ldouble,
	negate_ldouble,
	NULL,
	lnegate_ldouble,
	add_ldouble,
	sub_ldouble,
	mul_ldouble,
	div_ldouble,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	greater_ldouble,
	smaller_ldouble,
	greater_eq_ldouble,
	smaller_eq_ldouble,
	equal_ldouble,
	not_equal_ldouble
};

struct type_mapping	*host_ops[] = {
	&char_map,
	&signed_char_map,
	&unsigned_char_map,
	&signed_short_map,
	&unsigned_short_map,
	&signed_int_map,
	&enum_map,
	&unsigned_int_map,
	&signed_long_map,
	&unsigned_long_map,
	&signed_llong_map,
	&unsigned_llong_map,
	&float_map,
	&double_map,
	&ldouble_map,
	&unsigned_char_map	 /* XXX _Bool!!! */
};	
static int
type_to_index(int code) {
	switch (code) {
	case TY_CHAR:
		return TYMAP_IDX_CHAR;
	case TY_SCHAR:
		return TYMAP_IDX_SCHAR;
	case TY_UCHAR:
		return TYMAP_IDX_UCHAR;
	case TY_SHORT:
		return TYMAP_IDX_SHORT;
	case TY_USHORT:	
		return TYMAP_IDX_USHORT;
	case TY_ENUM:
		return TYMAP_IDX_ENUM;
	case TY_INT:	
		return TYMAP_IDX_INT;
	case TY_UINT:
		return TYMAP_IDX_UINT;
	case TY_LONG:
		return TYMAP_IDX_LONG;
	case TY_ULONG:
		return TYMAP_IDX_ULONG;
	case TY_LLONG:
		return TYMAP_IDX_LLONG;
	case TY_ULLONG:
		return TYMAP_IDX_ULLONG;
	case TY_FLOAT:
		return TYMAP_IDX_FLOAT;
	case TY_DOUBLE:
		return TYMAP_IDX_DOUBLE;
	case TY_LDOUBLE:
		return TYMAP_IDX_LDOUBLE;
	case TY_BOOL:
		return TYMAP_IDX_UCHAR; /* XXX :-( */
	default:
		unimpl();
	}
	return 0; /* NOTREACHED */
}	

extern void	old_do_conv(struct tyval *, int, int save_type);

void
cross_do_conv(struct tyval *changeme, int towhat, int save_type) {
	int	idx_dest;
	int	idx_src;

#if 0
	old_do_conv(changeme, towhat);
	return;
#endif

	if (changeme->type->tlist != NULL) {
		/*
		 * Changing a pointer to a different basic type. On
		 * all presently supported systems, a pointer matches
		 * an ``unsigned long'' in size and representation
		 * XXX not portable
		 */
		idx_src = type_to_index(TY_ULONG);
	} else {
		idx_src = type_to_index(changeme->type->code);
	}	
	idx_dest = type_to_index(towhat);

	if (type_map[idx_dest] != NULL && type_map[idx_src] != NULL) {
		/*
		 * There exist host mappings for both source and
		 * destination target types. Therefore, there must
		 * be a conversion routine between these types
		 */
		switch (type_map[idx_src]->code) {
		case TY_CHAR:
			type_map[idx_dest]->
				from_char(changeme->value, changeme->value);
			break;
		case TY_SCHAR:	
			type_map[idx_dest]->
				from_schar(changeme->value, changeme->value);
			break;
		case TY_UCHAR:
			type_map[idx_dest]->
				from_uchar(changeme->value, changeme->value);
			break;
		case TY_SHORT:
			type_map[idx_dest]->
				from_short(changeme->value, changeme->value);
			break;
		case TY_USHORT:
			type_map[idx_dest]->
				from_ushort(changeme->value, changeme->value);
			break;
		case TY_INT:
			type_map[idx_dest]->
				from_int(changeme->value, changeme->value);
			break;
		case TY_UINT:
			type_map[idx_dest]->
				from_uint(changeme->value, changeme->value);
			break;
		case TY_LONG:
			type_map[idx_dest]->
				from_long(changeme->value, changeme->value);
			break;
		case TY_ULONG:
			type_map[idx_dest]->
				from_ulong(changeme->value, changeme->value);
			break;
		case TY_LLONG:
			type_map[idx_dest]->
				from_llong(changeme->value, changeme->value);
			break;
		case TY_ULLONG:
			type_map[idx_dest]->
				from_ullong(changeme->value, changeme->value);
			break;
		case TY_FLOAT:
			type_map[idx_dest]->
				from_float(changeme->value, changeme->value);
			break;
		case TY_DOUBLE:
			type_map[idx_dest]->
				from_double(changeme->value, changeme->value);
			break;
		case TY_LDOUBLE:
			type_map[idx_dest]->
				from_ldouble(changeme->value, changeme->value);
			break;
		}
	} else {
		unimpl();
	}
	
	/*
	 * 04/01/08: Changing the type at this point, to the type we
	 * just converted to, historically didn't work. There was a
	 * comment here that said the type modification breaks for
	 * unknown reasons, and wasn't present in the original conv
	 * function before typemap was introduced.
	 *
	 * In some cases the result type of a conversion is always
	 * implicitly known and used properly by the caller, and thus
	 * basic type buffers are used which shall not be modified.
	 * For thsoe, the save_type flag is not set.
	 *
	 * In other cases, e.g. a constant expression like
	 *
	 *    (char)0 | (char)1
	 *
	 * ... usual arithmetic conmversions are applied, and it is
	 * then trusted that the type is set correctly. That was
	 * wrong, so now we set the type again and see what other
	 * things break... There may be probems with initializers
	 */
	if (save_type) {
#ifndef PREPROCESSOR
		/*
		 * 05/22/09: XXXXXXXXXXXXXXXXXXXXXXX
		 * This yields a crash, presumably because a 
		 * make_basic_type() type is getting modified,
		 * presumably because we are using an outdated
		 * evalexpr.c
		 *  -> Backport new one!!!!!!!
		 */
		changeme->type->code = towhat;
#endif
	}	
}

int old_convert_tyval(struct tyval *left, struct tyval *right);

int 
/*do_usual_arit_conv*/
cross_convert_tyval(struct tyval *left, struct tyval *right, struct type **result) {
	struct tyval	*to_change;
	struct tyval	*target_type;


#if 0
	return old_convert_tyval(left, right);
#endif

	/*
	 * 07/01/08: New parameter - if result is not a null pointer, it will
	 * be used to store the result type. The other data structures are not
	 * changed!
	 */
	if (result != NULL) {
		*result = NULL;
	}

#ifndef PREPROCESSOR
	if (right == NULL) {
		/* Convert to size_t */
		/* XXX move get_size_t out of backend... */
		struct type	*stt = backend->get_size_t();
		cross_do_conv(left, stt->code, 1);
		left->type = n_xmemdup(make_basic_type(stt->code),
					sizeof(struct type));
		return 0;
	}
#endif

	/*
	 * Promote to int
	 *
	 * 04/01/08: Wow this used to be done after compare_types(), so it
	 * didn't work for char vs char and such
	 *
	 * 07/15/08: Didn't work for pointer types, which were never needed
	 * here, but are now (e.g. to determine the type of
	 *
	 *    0? ptrexpr: non-ptrexpr
	 */
	if (left->type->tlist == NULL && left->type->code < TY_INT) {
		if (result != NULL) {
			*result = make_basic_type(TY_INT);
		} else {
			cross_do_conv(left, TY_INT, 1);
		}
	}
	if (right->type->tlist == NULL && right->type->code < TY_INT) {
		if (result != NULL) {
			*result = make_basic_type(TY_INT);
		} else {
			cross_do_conv(right, TY_INT, 1);
		}
	}	

	if (compare_types(left->type, right->type,
		CMPTY_SIGN|CMPTY_CONST) == 0) {
		return 0;
	}
	if (left->type->tlist != NULL
		|| right->type->tlist != NULL) {
		if (result != NULL) {
			/*
			 * 07/15/08: Pointer vs integer can happen when
			 * evaluating the result type of the conditional
			 * operator
			 */
			if (left->type->tlist == NULL) {
				*result = right->type;
			} else if (right->type->tlist == NULL) {
				*result = left->type;
			}
			return 0;
		} else {
			/*
			 * Caller not interested in result type, so this
			 * cannot be valied, since the function is being
			 * called for some integer operation
			 * XXX OK?
			 */
			return -1;
		}
	}
	
	if (left->type->code > right->type->code) {
		/* Left has higher rank - convert right */
		to_change = right;
		target_type = left;
	} else if (right->type->code > left->type->code) {
		/* Right has higher rank - convert left */
		to_change = left;
		target_type = right;
	} else {
		return 0;
	}	

	if (to_change->type->code == TY_UINT
		&& target_type->type->code == TY_LONG) {
		/*
		 * C89 says: ``Otherwise, if one operand has type long
		 * int and the other type unsigned int, if a long int
		 * can represent all values of an unsigned int, the
		 * operand of type unsigned int is converted to long int''
		 */
		if (target_info->type_info[TYMAP_IDX_LONG].bits
			> target_info->type_info[TYMAP_IDX_UINT].bits) {
			if (result != NULL) {
				*result = make_basic_type(TY_LONG);
			} else {	
				cross_do_conv(to_change, TY_LONG, 1);
			}
		} else {
			/*
			 * ``... if a long int cannot represent all the
			 * values of an unsigned int, both operands are
			 * converted to unsigned long int.''
			 */
			if (result != NULL) {
				*result = make_basic_type(TY_ULONG);
			} else {	
				cross_do_conv(to_change, TY_ULONG, 1);
			}
		}
	} else {
		if (result != NULL) {
			*result = n_xmemdup(target_type->type, 
				sizeof *target_type->type);
		} else {
			cross_do_conv(to_change, target_type->type->code, 1);
		}
	}

	/*
	 * 04/01/08: Wow, this didn't copy but just assign the type
	 * pointer! So after usual arithmetic conversions, both sides
	 * used the same type
	 */
	if (result == NULL) {
		to_change->type = n_xmemdup(target_type->type,
			sizeof *target_type->type);
	}
	return 0;
}

/*
 * 04/13/08: New function to create a constant value token with a target
 * int type
 */
struct token *
cross_make_int_token(long long value) {
	struct token	*ret = alloc_token();

	ret->type = TY_INT;

	/*
	 * XXX this assumes that host and target long long are the same
	 * size!
	 */
	type_map[ type_to_index(TY_INT) ]->
		from_llong(&value, &value);

	/*
	 * Value now holds an int in host represenation, but with target
	 * size properties, presumably in the lower 4 bytes
	 */
	ret->data = n_xmalloc(16); /* XXXXXXX */
	memcpy(ret->data, &value, sizeof value);
	return ret;
}

struct token *
cross_get_bitfield_signext_shiftbits(struct type *ty) {
	struct type_mapping	*mapping;
	int			obj_size;
	
	mapping = type_map[ type_to_index(ty->code) ];
	if (mapping == NULL) {
		unimpl();
	}
	obj_size = mapping->properties.bits;
	return cross_make_int_token(obj_size - ty->tbit->numbits);
}

struct type *
cross_get_bitfield_promoted_type(struct type *ty) {
	struct type_mapping	*mapping;
	int			int_size;
	
	mapping = type_map[ type_to_index(ty->code) ];
	if (mapping == NULL) {
		unimpl();
	}
	int_size = type_map[ type_to_index(TY_INT) ]->properties.bits;

	if (ty->code <= TY_UINT) {
		if (ty->tbit->numbits == int_size
			&& ty->code == TY_UINT) {
			/*
			 * 32 bit unsigned int - does not promote to
			 * int, but remains unsigned int
			 */
			return make_basic_type(TY_UINT);
		} else {
			return make_basic_type(TY_INT);
		}
	} else {
		if (ty->tbit->numbits >= int_size) {
			return make_basic_type(ty->code);
		} else {
			return make_basic_type(TY_INT);
		}
	}
}

struct token *
cross_get_pow2_minus_1(struct token *tok) {
	struct token		*res = NULL;
	static struct tyval	tv;

	tv.type = make_basic_type(tok->type);
	tv.value = tok->data;

	if (tok->type == TY_UINT
		|| tok->type == TY_ULONG
		|| tok->type == TY_ULLONG) {
		unsigned long long	uval;

		uval = cross_to_host_unsigned_long_long(&tv);
		if (uval < 2) {
			return NULL;
		}
		if ((uval & (uval - 1)) == 0) {
			/* Power of 2! */
			res = alloc_token();
			res->type = tok->type;
			res->data = n_xmalloc(16); /* XXX */
			cross_to_type_from_host_long_long(res->data,
				res->type, uval - 1);	
		}
	} else {
		long long		val;

		val = cross_to_host_long_long(&tv);
		if (val < 2) {
			return NULL;
		}
		if ((val & (val - 1)) == 0) {
			/* Power of 2! */
			res = alloc_token();
			res->type = tok->type;
			res->data = n_xmalloc(16); /* XXX */
			cross_to_type_from_host_long_long(res->data,
				res->type, val - 1);	
		}
	}

#ifndef PREPROCESSOR
	if (res != NULL) {
		/* 10/17/08: This was missing! */
		ppcify_constant(res);
	}
#endif

	return res;
}


struct token *
cross_get_pow2_shiftbits(struct token *tok) {
	int			bits;
	struct token		*res = NULL;
	static struct tyval	tv;

	tv.type = make_basic_type(tok->type);
	tv.value = tok->data;

	if (tok->type == TY_UINT
		|| tok->type == TY_ULONG
		|| tok->type == TY_ULLONG) {
		unsigned long long	uval;

		uval = cross_to_host_unsigned_long_long(&tv);
		if (uval < 2) {
			return NULL;
		}
		if ((uval & (uval - 1)) == 0) {
			/* Power of 2! */
			res = alloc_token();
			res->type = tok->type;
			res->data = n_xmalloc(16); /* XXX */

			bits = 0;
			while (uval >>= 1) {
				++bits;
			}

			cross_to_type_from_host_long_long(res->data,
				res->type, bits);	
		}
	} else {
		long long		val;

		val = cross_to_host_long_long(&tv);
		if (val < 2) {
			return NULL;
		}
		if ((val & (val - 1)) == 0) {
			/* Power of 2! */
			res = alloc_token();
			res->type = tok->type;
			res->data = n_xmalloc(16); /* XXX */

			bits = 0;
			while (val >>= 1) {
				++bits;
			}
			cross_to_type_from_host_long_long(res->data,
				res->type, bits);
		}
	}

#ifndef PREPROCESSOR
	if (res != NULL) {
		/* 10/17/08: This was missing! */
		ppcify_constant(res);
	}
#endif
	return res;
}
	
/*
 * 04/12/08: New function to convert tokens.
 *
 * This allows the caller to take a token, e.g. a constant 123u of
 * type unsigned int, and convert it to a different type, e.g. long long.
 * The types are (mapped) target system types.
 */
struct token *
cross_convert_const_token(struct token *tok, int newty) {
	static struct tyval	tv;
	struct token		*ret;
	int			nbytes;

	tv.type = make_basic_type(tok->type);
	tv.value = n_xmalloc(16);

	nbytes = type_map[ type_to_index(tok->type) ]->properties.bytes;
	if (IS_FLOATING(tok->type)) {
		struct ty_float	*tf = tok->data;
		memcpy(tv.value, tf->num->value, nbytes);
	unimpl();	
	} else {	
		memcpy(tv.value, tok->data, nbytes);
	}


	cross_do_conv(&tv, newty, 0);
	ret = alloc_token();
	ret->data = tv.value;
	ret->type = newty;
	/* 10/17/08: This was missing! */
#ifndef PREPROCESSOR
	ppcify_constant(ret);
#endif
	return ret;
}

int
cross_exec_op_for_const_tokens(struct token **result,
	struct token *dest,
	struct token *src,
	int op) {
	static struct token	tok;
	static struct tyval	restv;
	static struct tyval	desttv;
	static struct tyval	srctv;
	struct ty_float		*tf;

	desttv.type = make_basic_type(dest->type);
	if (IS_FLOATING(dest->type)) {
		tf = dest->data;
		desttv.value = tf->num->value;
	} else {
		desttv.value = dest->data;
	}
	srctv.type = make_basic_type(src->type);
	if (IS_FLOATING(dest->type)) {
		tf = dest->data;
		srctv.value = tf->num->value;
	} else {	
		srctv.value = src->data;
	}
	
	restv.value = n_xmalloc(16); /* XXXXXXXXXXX */
	tok.type = TOK_OPERATOR;
	tok.data = &op;
	if (cross_exec_op(&tok, &restv, &desttv, &srctv) != 0) {
		return -1;
	}
	*result = alloc_token();
	(*result)->data = restv.value;
	(*result)->type = restv.type->code;
	/* 10/17/08: This was missing! */
#ifndef PREPROCESSOR
	ppcify_constant(*result);
#endif
	return 0;
}

int
cross_exec_op(struct token *optok,
	struct tyval *res, struct tyval *dest, struct tyval *src) {

	int	idx;
	int	op;

	/* XXX needed? */
	if (optok->type == TOK_OPERATOR) {
		op = *(int *)optok->data;
	} else {
		op = optok->type;
	}	

	switch (op) {
	case TOK_OP_LNEG:
	case TOK_OP_BNEG:
	case TOK_OP_UPLUS:
	case TOK_OP_UMINUS:
		if (dest->type->tlist != NULL) {
			unimpl();
		} else if (dest->type->code < TY_INT) {
			/* A promotion is required */
			cross_do_conv(dest, TY_INT, 1);
			dest->type = make_basic_type(TY_INT);
			dest->type = n_xmemdup(dest->type, sizeof *dest->type);
		}
		break;
	case TOK_OP_PLUS:	
	case TOK_OP_MINUS:
	case TOK_OP_MULTI:
	case TOK_OP_DIVIDE:
	case TOK_OP_MOD:
	case TOK_OP_SMALL:
	case TOK_OP_GREAT:
	case TOK_OP_GREATEQ:
	case TOK_OP_SMALLEQ:
	case TOK_OP_LEQU:
	case TOK_OP_LNEQU:
	case TOK_OP_BAND:
	case TOK_OP_BOR:
	case TOK_OP_BXOR:
		/*
		 * All of these operations require usual arithmetic
		 * conversions
		 */
		if (cross_convert_tyval(dest, src, NULL) != 0) {
			return -1;
		}	
		break;
	case TOK_OP_BSHL:
	case TOK_OP_BSHR:
		/* The right operand becomes an ``int'' by convention */
		cross_do_conv(src, TY_INT, 1);
		break;
	case TOK_OP_LAND:
	case TOK_OP_LOR:	
	case TOK_OP_COND:
		unimpl();
	}	

	idx = type_to_index(dest->type->code);
	if (type_map[idx] != NULL) {
		void	(*fptr)(struct token *, void *, void *, void *) = NULL;

		/*
		 * The target type is identical to the host type, or can
		 * be emulated by means of another host type!
		 */
		switch (op) {
		/* Unary operators */	
		case TOK_OP_LNEG: fptr = type_map[idx]->lnegate; break;
		case TOK_OP_BNEG: fptr = type_map[idx]->bnegate; break;
		case TOK_OP_UPLUS:
			/* Has no effect except promotion */
			break;
		case TOK_OP_UMINUS: fptr = type_map[idx]->negate; break; 
		/* Binary operators */	
		case TOK_OP_PLUS: fptr = type_map[idx]->add; break;
		case TOK_OP_MINUS: fptr = type_map[idx]->sub; break;
		case TOK_OP_MULTI: fptr = type_map[idx]->mul; break;
		case TOK_OP_DIVIDE: fptr = type_map[idx]->div; break;
		case TOK_OP_MOD: fptr = type_map[idx]->mod; break;
		case TOK_OP_SMALL: fptr = type_map[idx]->smaller; break;
		case TOK_OP_GREAT: fptr = type_map[idx]->greater; break;
		case TOK_OP_GREATEQ: fptr = type_map[idx]->greater_eq; break;
		case TOK_OP_SMALLEQ: fptr = type_map[idx]->smaller_eq; break;
		case TOK_OP_LEQU: fptr = type_map[idx]->equal; break;
		case TOK_OP_LNEQU: fptr = type_map[idx]->not_equal; break;
		case TOK_OP_BAND: fptr = type_map[idx]->band; break;
		case TOK_OP_BOR: fptr = type_map[idx]->bor; break;
		case TOK_OP_BXOR: fptr = type_map[idx]->xor; break;
		case TOK_OP_BSHL: fptr = type_map[idx]->bshl; break;
		case TOK_OP_BSHR: fptr = type_map[idx]->bshr; break;
		default:
			printf("?? :( %d\n", op);
			unimpl();
		}	
		if (fptr == NULL && op != TOK_OP_UPLUS) {
			/*
			 * This can only happen if we have an invalid opreation
			 * on a floating point type, such as bitwise XOR
			 */
			errorfl(optok,
				"Invalid use of operator `%s' with floating "
				"point type", optok->ascii);
			return -1;
		}

		res->alloc = 1;
		res->value = n_xmalloc(16); /* XXX */

		if (op == TOK_OP_SMALL
			|| op == TOK_OP_GREAT
			|| op == TOK_OP_GREATEQ
			|| op == TOK_OP_SMALLEQ
			|| op == TOK_OP_LEQU
			|| op == TOK_OP_LNEQU) {
			/* bool in C++! */
			res->type = make_basic_type(TY_INT);
			res->type = n_xmemdup(res->type, sizeof *res->type);
		} else {
			res->type = dest->type;
		}
		if (fptr != NULL) {
			fptr(optok, res->value, dest->value, src? src->value: NULL);
		} else {
			/* Must be unary plus? */
			memcpy(res->value, dest->value,
#ifndef PREPROCESSOR
				backend->get_sizeof_type(res->type, NULL));
#else
				cross_get_sizeof_type(res->type));
#endif
		}	
	} else {
		/*
		 * We have to use a software implementation to emulate the
		 * type
		 */
		unimpl();
	}

	return 0;
}


static int
get_host_endianness(void) {
	int	stuff = 123;

	stuff |= 45 << CHAR_BIT * (sizeof(int) - 1);
	if (*(char *)&stuff == 123
		&& ((char *)&stuff)[sizeof(int) - 1] == 45) {
		return ENDIAN_LITTLE;
	} else if (*(char *)&stuff == 45
		&& ((char *)&stuff)[sizeof(int) - 1] == 123) {
		return ENDIAN_BIG;
	} else {
		if (sizeof(int) == 1) {
			(void) fprintf(stderr,
				"ERROR: sizeof(int) = 1\n");
		} else {
			(void) fprintf(stderr,
				"ERROR: Unknown host endianness!\n");
		}
		exit(EXIT_FAILURE);
	}
	/* NOTREACHED */
	return -1;
}

static int
get_host_char_signedness(void) {
	if (CHAR_MIN != SCHAR_MIN) {
		return TOK_KEY_UNSIGNED;
	} else {
		return TOK_KEY_SIGNED;
	}
}

int
cross_get_char_signedness(void) {
	return char_signedness;
}

void
cross_set_char_signedness(int funsigned_char, int fsigned_char,
		int host_arch, int target_arch) {

	int	sign;

	/*
	 * 03/02/09: Plain char signedness cleanup! First process
	 * the -funsigned-char/-fsigned-char command line options
	 * as overrides.
	 * If those are not specified, and we are NOT cross-
	 * compiling, assume the signedness with which nwcc was
	 * built.
	 * If we ARE cross-compiling, use the predefined default
	 * for the target architecture
	 */
	if (funsigned_char) {
		sign = TOK_KEY_UNSIGNED;
	} else if (fsigned_char) {
		sign = TOK_KEY_SIGNED;
	} else {
		if (host_arch == target_arch) {
			sign = get_host_char_signedness();
		} else {
#if 0 /* Whoops, target_info isn't set yet... So do it in
	cross_initialize_type_map() if sign = 0 */
			sign = target_info->type_info[TYMAP_IDX_CHAR].sign;
#endif
			sign = 0;
		}
	}
	char_signedness = sign;
}


void
debug_print_arch_info(void) {
	int			i;
	struct type_mapping	*mapping;
	
#define E_NDI(val) \
	(val == ENDIAN_BIG? "big": "little")

	printf("Host = `%s', target = `%s'\n",
		arch_to_ascii(-1),
		arch_to_ascii(target_info->arch_info->arch));
	printf("Host endianness:               %s\n", 
		E_NDI(host_arch_properties.endianness));
	printf("Host data pointer size:        %d\n",
		host_arch_properties.data_ptr_size);
	printf("Host function pointer size:    %d\n",
		host_arch_properties.func_ptr_size);
	printf("Target endianness:             %s\n", 
		E_NDI(target_info->arch_info->endianness));
	printf("Target data pointer size:      %d\n",
		target_info->arch_info->data_ptr_size);
	printf("Target function pointer size:  %d\n",
		target_info->arch_info->func_ptr_size);
	

	printf("Target type information:\n");
	for (i = TYMAP_IDX_CHAR; i <= TYMAP_IDX_BOOL; ++i) {
		static struct type t;
		struct type_properties *typ;
		char *p;

		typ = &target_info->type_info[i];
		t.code = typ->code;
		t.sign = typ->sign;
		p = type_to_text(&t);
		printf("\t%s  (sign=%d, bytes=%d, bits=%d)\n",
			p, typ->sign, typ->bytes, typ->bits);
		mapping = type_map[ type_to_index(typ->code) ];
		if (mapping == NULL) {
			printf("\t\t==> [no matching host type]");
		} else {
			static struct type	t2;
			char *p2;
			printf("\t\t==> maps to host type\t");
			t2.code = mapping->code;
			t2.sign = mapping->properties.sign;
			p2 = type_to_text(&t2);
			printf("%s  (sign=%d, bytes=%d, bits=%d)",
				p2, mapping->properties.sign,
				mapping->properties.bytes,
				mapping->properties.bits);
			free(p2);
		}
		putchar('\n');

		free(p);
	}
}

void
cross_initialize_type_map(int targetarch, int targetabi, int targetsys) {
	int	i;
	int	j;

	/*
	 * Obtain target architecture/ABI type information
	 */
	switch (targetarch) {
	case ARCH_X86:
		if (targetsys == OS_OSX) {
			target_info = &target_properties[PROP_IDX_X86_OSX];
		} else {
			target_info = &target_properties[PROP_IDX_X86_SYSV];
		}
		break;
	case ARCH_AMD64:
		target_info = &target_properties[PROP_IDX_AMD64];
		break;
	case ARCH_POWER:
		switch (targetabi) {
		case ABI_POWER32:	
			target_info = &target_properties[PROP_IDX_PPC];
			break;
		case ABI_POWER64:
			if (targetsys == OS_AIX) {
				target_info = &target_properties[PROP_IDX_PPC64_AIX];
			} else {	
				target_info = &target_properties[PROP_IDX_PPC64_LINUX];
			}
			break;
		default:
			abort();
		}
		break;
	case ARCH_MIPS:
		switch (targetabi) {
#if 0
		case ABI_MIPS_O32:	
			target_info = host_properties[PROP_IDX_MIPSO32];
			break;
#endif
		case ABI_MIPS_N32:
			target_info = &target_properties[PROP_IDX_MIPSN32];
			break;
		case ABI_MIPS_N64:	
			target_info = &target_properties[PROP_IDX_MIPSN64];
			break;
		default:
			abort();
		}
		break;
	case ARCH_SPARC:
		switch (targetabi) {
		case ABI_SPARC32:
			target_info = &target_properties[PROP_IDX_SPARC32];
			break;
		case ABI_SPARC64:
			target_info = &target_properties[PROP_IDX_SPARC64];
			break;
		default:
			abort();
		}
		break;
	default:
		unimpl();
	}

	host_arch_properties.data_ptr_size = sizeof(void *);
	host_arch_properties.func_ptr_size = sizeof(void(*)());
	host_arch_properties.endianness = get_host_endianness();

	/*
	 * Determine host type properties. CHAR_BIT and sizeof are used
	 * instead of the ``host_properties[host_arch]'' information
	 * because that enables nwcc to cross-compile from unknown/
	 * unsupported host architectures
	 */
	char_map.properties.bits = 
		signed_char_map.properties.bits =
			unsigned_char_map.properties.bits = CHAR_BIT;

	/*
	 * XXX Assuming char=schar/uchar is clearly bad. We detect the
	 * default setting of the host platform to ensure that at least
	 * native compilation works right. ``char'' seems to be unsigned
	 * on most platforms anyway (if memory serves me.)
	 * It is desirable to record the default setting for all target
	 * platforms with gcc and perhaps the vendor compiler, and also
	 * to handle those -fsigned/unsigned-char thingies.
	 *
	 * 12/22/08: This wrongly compared SCHAR_MAX instead of CHAR_MAX
	 * with UCHAR_MAX. That made no sense whatsoever and always set
	 * the signedness to ``signed''
	 *
	 * 03/02/09: Setting the sign based on the macro check is correct
	 * because this really is the host ``char'' type, not target!
	 * cross_set_char_signedness()
	 */
	if (CHAR_MAX == UCHAR_MAX) {
		char_map.properties.sign = TOK_KEY_UNSIGNED;
	} else {
		char_map.properties.sign = TOK_KEY_SIGNED;
	}
	if (char_signedness == 0) {
		/* 
		 * Now that target_info is set, we can set the char
		 * signedness if not already done conclusively
		 */
		char_signedness = target_info->type_info[TYMAP_IDX_CHAR].sign;
	}


	/*
	 * 03/02/09: Now we more properly set the target ``char''
	 * signedness in cross_set_char_signedness()
	 */
	target_info->type_info[TYMAP_IDX_CHAR].sign = char_signedness;

	signed_short_map.properties.bits = CHAR_BIT * sizeof(signed short);
	unsigned_short_map.properties.bits = CHAR_BIT * sizeof(unsigned short);
	signed_int_map.properties.bits = CHAR_BIT * sizeof(int);
	enum_map.properties.bits = signed_int_map.properties.bits;
	unsigned_int_map.properties.bits = CHAR_BIT * sizeof(unsigned);
	signed_long_map.properties.bits = CHAR_BIT * sizeof(long);
	unsigned_long_map.properties.bits = CHAR_BIT * sizeof(unsigned long);
	signed_llong_map.properties.bits = CHAR_BIT * 8 /*sizeof(long long)*/;
	unsigned_llong_map.properties.bits = CHAR_BIT * 8 /* sizeof(long long) */; 

	/*
	 * XXX stuff below assumes equivalent floating point
	 * (exponent/mantissa) encoding
	 */
	float_map.properties.bits = CHAR_BIT * sizeof(float);
	double_map.properties.bits = CHAR_BIT * sizeof(double);
	ldouble_map.properties.bits = CHAR_BIT * sizeof(long double);
	if (ldouble_map.properties.bits == 96) {
		/* XXX */
		ldouble_map.properties.bits = 80;
	}	

	char_map.properties.bytes =
		signed_char_map.properties.bytes = 
			unsigned_char_map.properties.bytes = 1;
	signed_short_map.properties.bytes = sizeof(short);
	unsigned_short_map.properties.bytes = sizeof(unsigned short);
	signed_int_map.properties.bytes = sizeof(int);
	enum_map.properties.bytes = signed_int_map.properties.bytes;
	unsigned_int_map.properties.bytes = sizeof(unsigned);
	signed_long_map.properties.bytes = sizeof(long);
	unsigned_long_map.properties.bytes = sizeof(unsigned long);
	signed_llong_map.properties.bytes = 8 /*sizeof(long long)*/;
	unsigned_llong_map.properties.bytes = 8 /* sizeof(long long) */; 
	float_map.properties.bytes = sizeof(float);
	double_map.properties.bytes = sizeof(double);
	ldouble_map.properties.bytes = sizeof(long double);
	if (ldouble_map.properties.bytes == 12) {
		/* XXX */
		ldouble_map.properties.bytes = 10;
	}	

	/* Now set all numeric limits for determining literal types */
	for (i = TYMAP_IDX_CHAR; i < TYMAP_IDX_FLOAT; ++i) {
		switch (host_ops[i]->properties.bits) {
		case 8:	
			if (host_ops[i]->properties.sign == TOK_KEY_UNSIGNED) {
				host_ops[i]->properties.max_dec
					= ascii_8_dec_unsigned;
				host_ops[i]->properties.max_hex
					= ascii_8_hex_unsigned;
				host_ops[i]->properties.max_oct
					= ascii_8_oct_unsigned;
			} else {
				host_ops[i]->properties.max_dec = ascii_8_dec;
				host_ops[i]->properties.max_hex = ascii_8_hex;
				host_ops[i]->properties.max_oct = ascii_8_oct;
			}	
			break;
		case 16:
			if (host_ops[i]->properties.sign == TOK_KEY_UNSIGNED) {
				host_ops[i]->properties.max_dec
					= ascii_16_dec_unsigned;
				host_ops[i]->properties.max_hex
					= ascii_16_hex_unsigned;
				host_ops[i]->properties.max_oct
					= ascii_16_oct_unsigned;
			} else {
				host_ops[i]->properties.max_dec
					= ascii_16_dec;
				host_ops[i]->properties.max_hex
					= ascii_16_hex;
				host_ops[i]->properties.max_oct
					= ascii_16_oct;
			}
			break;
		case 32:
			if (host_ops[i]->properties.sign == TOK_KEY_UNSIGNED) {
				host_ops[i]->properties.max_dec
					= ascii_32_dec_unsigned;
				host_ops[i]->properties.max_hex
					= ascii_32_hex_unsigned;
				host_ops[i]->properties.max_oct
					= ascii_32_oct_unsigned;
			} else {
				host_ops[i]->properties.max_dec
					= ascii_32_dec;
				host_ops[i]->properties.max_hex
					= ascii_32_hex;
				host_ops[i]->properties.max_oct
					= ascii_32_oct;
			}
			break;
		case 64:
			if (host_ops[i]->properties.sign == TOK_KEY_UNSIGNED) {
				host_ops[i]->properties.max_dec
					= ascii_64_dec_unsigned;
				host_ops[i]->properties.max_hex
					= ascii_64_hex_unsigned;
				host_ops[i]->properties.max_oct
					= ascii_64_oct_unsigned;
			} else {
				host_ops[i]->properties.max_dec
					= ascii_64_dec;
				host_ops[i]->properties.max_hex
					= ascii_64_hex;
				host_ops[i]->properties.max_oct
					= ascii_64_oct;
			}
			break;
		default:
			unimpl();
		}
	}

	for (i = TYMAP_IDX_CHAR; i <= TYMAP_IDX_BOOL; ++i) {
		/*
		 * Iterate through all host types to find one that
		 * maps well to the target type
		 */
		for (j = 0; j <= TYMAP_IDX_LDOUBLE; ++j) {
			if (target_info->type_info[i].sign == host_ops[j]->properties.sign
				&& target_info->type_info[i].bits == host_ops[j]->properties.bits) {
				/* We found a match! */
				type_map[i] = host_ops[j];
				break;
			}
		}
	}

	/* Assume host ldouble = target ldouble to preserve sanity */
	type_map[TYMAP_IDX_LDOUBLE] = host_ops[TYMAP_IDX_LDOUBLE];

	{
#if 0
		long long x = 1234;
		cross_print_value_chunk(stdout, &x, TY_LLONG,
			TY_UINT, 0);
		cross_print_value_chunk(stdout, &x, TY_LLONG,
			TY_UINT, 1);
		long double x = 1234;
		cross_print_value_chunk(stdout, &x, TY_LDOUBLE,
			TY_UINT, 0, 0);
		cross_print_value_chunk(stdout, &x, TY_LDOUBLE,
			TY_UINT, 0, 1);
		cross_print_value_chunk(stdout, &x, TY_LDOUBLE,
			TY_UINT, TY_USHORT, 2);
		exit(1);
#endif
	}	
#if 0
	debug_print_arch_info();
	exit(1);
#endif
}

size_t
cross_get_sizeof_type(struct type *t) {
	if (t->tlist != NULL) {
#if 0
		/* XXX appalling kludge :-/ */
		return target_info->type_info[TYMAP_IDX_LONG].bytes;
#endif
		if (t->is_func) {
			/* XXX not set for func pointers?! */
			return target_info->arch_info->func_ptr_size;
		} else {
			return target_info->arch_info->data_ptr_size;
		}
	} else {
		if (t->code == TY_STRUCT
			|| t->code == TY_UNION) {
			unimpl();
		} else if (t->code == TY_VOID) {
			/* GNU C */
			return 1;
		}	
		return target_info->type_info[ type_to_index(t->code) ].bytes; 
	}
}

struct type_properties *
cross_get_type_properties(int code) {
	return &target_info->type_info[ type_to_index(code) ];
}

struct arch_properties *
cross_get_host_arch_properties(void) {
	return &host_arch_properties;
}

struct arch_properties *
cross_get_target_arch_properties(void) {
	return target_info->arch_info;
}	

int
cross_have_mapping(int first_type, int second_type) {
	if (type_map[ type_to_index(first_type) ] != NULL
		&& type_map[ type_to_index(second_type) ] != NULL) {
		return 1;
	}
	return 0;
}

void
cross_conv_host_to_target(void *src, int destty, int srcty) {
	if (IS_FLOATING(srcty)) {
		int			srcbits;
		static struct tyval	tmp;
		struct type		*mapty= NULL;

		if (srcty == TY_FLOAT) {
			srcbits = sizeof(float) * CHAR_BIT;
		} else if (srcty == TY_DOUBLE) {
			srcbits = sizeof(double) * CHAR_BIT;
		} else if (srcty == TY_LDOUBLE) {
			srcbits = sizeof(long double) * CHAR_BIT;
		} else {
			srcbits = 0;
			unimpl();
		}

		if (type_map[ type_to_index(TY_FLOAT) ]->
			properties.bits == srcbits) {
			mapty = make_basic_type(TY_FLOAT);
		} else if (type_map[ type_to_index(TY_DOUBLE) ]->
			properties.bits == srcbits) {
			mapty = make_basic_type(TY_DOUBLE);
		} else if (type_map[ type_to_index(TY_LDOUBLE) ]->
			properties.bits == srcbits) {
			mapty = make_basic_type(TY_LDOUBLE);
		} else {
			unimpl();
		}

		tmp.type = mapty;
		tmp.value = src;
		cross_do_conv(&tmp, destty, 0);
	} else {
		unimpl();
	}
}


static void
get_fmt(char fmt[8], int type, int for_scanf,
	int fp_flag, int octal_flag, int hexa_flag) {

	struct type_mapping	*mapping;
	
	mapping = type_map[ type_to_index(type) ];
	if (mapping == NULL) {
		unimpl();
	}	

	if (fp_flag != 0) {
		/* Is floating point value */
		if (mapping != NULL) {
			switch (mapping->code) {
			case TY_FLOAT:
				strcpy(fmt, "%f");
				break;
			case TY_DOUBLE:
				if (!for_scanf) {
					/* printf() uses %f for double too */
					strcpy(fmt, "%f");
				} else {	
					strcpy(fmt, "%lf");
				}	
				break;
			case TY_LDOUBLE:
				strcpy(fmt, "%Lf");
				break;
			default:
				unimpl();
			}
		} else {
			unimpl();
		}
	} else {
		/*
		 * Is integer value. hexa_flag and octal_flag tell us
		 * which number system we are dealing with, but we
		 * have to look at the type mapping to determine
		 * whether the target type can be read as a host int,
		 * long, or long long (or none at all, in which case
		 * we cannot use sscanf())
		 */
		if (!for_scanf) {
			if (hexa_flag) {
				/* Outputing 0x... */
				*fmt++ = '0';
				*fmt++ = 'x';
			} else if (octal_flag) {
				*fmt++ = '0';
			}	
		}

		*fmt++ = '%';
		if (mapping != NULL) {
			switch (mapping->code) {
			case TY_LONG:
			case TY_ULONG:	
				/* Must be %ld or %lx or %lo */
				*fmt++ = 'l';
				break;
			case TY_LLONG:
			case TY_ULLONG:	
				/* Must be %lld or %llx or %llo */
				*fmt++ = 'l';
				*fmt++ = 'l';
				break;
			}
			if (hexa_flag) {
				*fmt++ = 'x';
			} else if (octal_flag) {
				*fmt++ = 'o';
			} else {
				if (mapping->code == TY_UINT
					|| mapping->code == TY_ULONG
					|| mapping->code == TY_ULLONG) {
					*fmt++ = 'u';
				} else {	
					*fmt++ = 'd';
				}	
			}
			*fmt = 0;
		} else {
			unimpl();
		}
	}
}	


struct num *
cross_scan_value(const char *str, int type,
	int hexa_flag, int octal_flag, int fp_flag) {
	
	char			fmt[8];
	size_t			nbytes;
	struct num		*ret = n_xmalloc(sizeof *ret);

	nbytes = 16; /* XXX */
	get_fmt(fmt, type, 1, fp_flag, octal_flag, hexa_flag);

	ret->type = type;
	ret->value = n_xmalloc(nbytes);
	if (sscanf(str, fmt, ret->value) != 1) {
		return NULL;
	}


#ifndef PREPROCESSOR
	/*
	 * Some assembler/architecture combinations, most
	 * notably on x86, require separate definitions of
	 * floating point constants, rather than immediate
	 * encodings
	 */
	if (fp_flag) {
		put_float_const_list(ret);
	}	
#endif
	return ret;
}


#ifndef PREPROCESSOR

struct ty_float *
put_float_const_list(struct num *ret) {
	/*if (backend->need_floatconst  ) {*/
		struct ty_float		*fc;
		static struct ty_float	null;
		static unsigned long	count;

		fc = n_xmalloc(sizeof *fc);
		*fc = null;
		fc->count = count++;
		fc->num = ret;

#if XLATE_IMMEDIATELY
		fc->next = NULL;
		emit->fp_constants(fc);
#endif
		fc->next = float_const;
		if (float_const != NULL) {
			float_const->prev = fc;
		}	
		float_const = fc;
	/* } */	
		return fc;
}

void
remove_float_const_from_list(struct ty_float *tf) {
	if (tf->inactive) {
		return;
	}
	if (tf == float_const) {
		float_const = tf->next;
	} else {
		tf->prev->next = tf->next;
	}
	if (tf->next) {
		tf->next->prev = tf->prev;
	}
	tf->inactive = 1;  /* or delete ??? */
}

#endif /* #ifndef PREPROCESSOR */

static void
do_print_value(FILE *out, void *value, const char *fmt, int code) {	
	/*
	 * 08/19/07: Added bitwise and's to limit value ranges. E.g. when
	 * printing a 2-byte signed short with a value of -1, we used
	 * to wrongly output 0xffffffff instead of 0xffff due to sign-
	 *extension!
	 */
	switch (code) {
	case TY_CHAR:	
		x_fprintf(out, fmt, *(char *)value & UCHAR_MAX);
		break;
	case TY_SCHAR:
		x_fprintf(out, fmt, *(signed char *)value & UCHAR_MAX);
		break;
	case TY_UCHAR:
	case TY_BOOL:	
		x_fprintf(out, fmt, *(unsigned char *)value);
		break;
	case TY_SHORT:
		x_fprintf(out, fmt, *(short *)value & USHRT_MAX);
		break;
	case TY_USHORT:
		x_fprintf(out, fmt, *(unsigned short *)value);
		break;
	case TY_INT:
		x_fprintf(out, fmt, *(int *)value & UINT_MAX);
		break;
	case TY_ENUM:
		x_fprintf(out, fmt, *(int *)value & UINT_MAX);
		break;
	case TY_UINT:
		x_fprintf(out, fmt, *(unsigned int *)value);
		break;
	case TY_LONG:	
		x_fprintf(out, fmt, *(long *)value & ULONG_MAX);
		break;
	case TY_ULONG:
		x_fprintf(out, fmt, *(unsigned long *)value);
		break;
	case TY_LLONG:	
		/* XXX c99 */
		x_fprintf(out, fmt, *(long long *)value & (unsigned long long)-1);
		break;
	case TY_ULLONG:
		x_fprintf(out, fmt, *(unsigned long long *)value);
		break;
	case TY_FLOAT:	
		x_fprintf(out, fmt, *(float *)value);
		break;
	case TY_DOUBLE:
		x_fprintf(out, fmt, *(double *)value);
		break;
	case TY_LDOUBLE:
		x_fprintf(out, fmt, *(long double *)value);
		break;
	default:
		unimpl();
	}	
}

/*
 * Interprets ``value'' as type ``type'' and prints it to out, using the
 * format specified by outfmt ('x', 'o', 'd');
 *
 * Note that this currently just uses fprintf()'s format (with an 0x..
 * and 0.. prepended for hexadecimal and octal values.)
 *
 * ``type'' is the target type, i.e. all type mapping is done
 * automatically, such that a host ``long long'' representing a target
 * 64bit ``long'' in ``value'' will be output using %lld if you pass
 * TY_LONG as type.
 */
void
cross_print_value_by_type(FILE *out, void *value, int type, int outfmt) {
	char			fmt[8];
	int			octal_flag = 0;
	int			hexa_flag = 0;
	int			fp_flag = 0;
	struct type_mapping	*mapping;
	

	if (outfmt == 'x') {
		hexa_flag = 1;
	} else if (outfmt == 'o') {
		octal_flag = 1;
	} else if (outfmt == 'd' || outfmt == 0) {
		;
	} else {
		unimpl();
	}
	fp_flag = IS_FLOATING(type);

	mapping = type_map[ type_to_index(type) ];
	if (mapping == NULL) {
		unimpl();
	}
	get_fmt(fmt, mapping->code, 0, fp_flag, octal_flag, hexa_flag);

	do_print_value(out, value, fmt, mapping->code);
}


/*
 * Interprets ``value'' as type ``type'' and prints a chunk of it to out, as 
 * a hexadecimal number. This is useful to output big numbers (say 64bit) in
 * multiple smaller pieces. ``chunk_type'' is the type as which the chunk 
 * shall be interpreted. E.g. you can interpret a 64bit ``long'' type as two
 * 32bit ``unsigned ints''. ``chunk_type_rem'' is the type as which the
 * remainder shall be interpreted, e.g. to print a 10 bytes ``long double'',
 * you can use two 32bit uints and a 16bit ushort
 *
 * XXX This is inconvenient for systems that do not have many types (Cray
 * vector systems.)
 *
 * chunkno is the number of the current chunk in ``value''. If chunkno*bytes
 * exceeds the size of the item, just the remainder is printed.
 *
 * Example: Printing a 64bit ``long'' value in two 32bit chunks
 *
 * data   |         64bit value           |
 *         
 * bytes  | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
 * 
 * chunks |       0       |       1       |
 *
 * cross_print_value_chunk(out, value, TY_LONG, TY_UINT, 0);
 * cross_print_value_chunk(out, value, TY_LONG, TY_UINT, 1);
 *
 * Note that endianness is taken into account! So if we're printing a 64bit
 * number on a little endian host system for a big endian target, the chunk
 * order of the little endian number representation is reversed, such that
 * the two calls in the example above really access the data in the same
 * way as if the target system executed this code:
 *
 *    long data;
 *    ...
 *    fprintf(out, "%u", ((unsigned int *)&data)[0]);
 *    fprintf(out, "%u", ((unsigned int *)&data)[1]);
 *
 * regardless of host/target endianness.
 */
void
cross_print_value_chunk(FILE *out, void *value, int type,
	int chunk_type, int chunk_type_rem, int chunkno) {

	struct type_mapping	*mapping;
	int			obj_size;
	int			chunk_size;
	int			bytes;
	char			fmt[8];
	unsigned char		*chunkp;
	
	mapping = type_map[ type_to_index(type) ];
	if (mapping == NULL) {
		unimpl();
	}
	obj_size = mapping->properties.bytes;

	/*
	 * 03/04/09: For long double, the actual value object size is
	 * really 10, and the padded size is 12 on x86 and 16 on OSX/
	 * x86 and AMD64
	 */
	if (type == TY_LDOUBLE
#ifndef PREPROCESSOR
		&& (backend->arch == ARCH_X86 || backend->arch == ARCH_AMD64)) {
#else
		&& (target_info->arch_info->arch == ARCH_X86
			|| target_info->arch_info->arch == ARCH_AMD64)) {
#endif
		obj_size = 10;	
	}


	mapping = type_map[ type_to_index(chunk_type) ];
	if (mapping == NULL) {
		unimpl();
	}
	bytes = mapping->properties.bytes;

	if ((chunkno * bytes) + bytes > obj_size) {
		/* Printing remainder */
#if 0
		bytes = mapping->properties.bytes;
#endif
		mapping = type_map[ type_to_index(chunk_type_rem) ];
		if (mapping == NULL) {
			unimpl();
		}

		chunk_type = chunk_type_rem;
		chunk_size = obj_size - chunkno * bytes;
#if 0
		printf("printing remainder of size %d\n", chunk_size);
#endif
	} else {
		chunk_size = bytes;
#if 0 
		printf("printing whole chunk of size %d\n", chunk_size);
#endif
	}
#if 0
printf("== diving %d bytes item into units of size %d\n",
	obj_size, bytes);	
#endif
	
	if (host_arch_properties.endianness
		!= target_info->arch_info->endianness) {
		/* Reverse order - index counts from end to start! */
		chunkp = ((unsigned char *)value + obj_size) - chunkno * bytes;
		chunkp -= chunk_size;
#if 0
		printf("using reversed order!\n");
		printf("grabbing chunk at byte %d\n",
			chunkp - (unsigned char *)value);	
#endif
	} else {
		/* Same host/target endianness */
		chunkp = (unsigned char *)value + chunkno * bytes;
#if 0
		printf("normal order..\n");
		printf("grabbing chunk at byte %d\n",
			chunkp - (unsigned char *)value);	
#endif
	}

	get_fmt(fmt, /*chunk_type*/ mapping->code, 0, 0, 0, 1);
	do_print_value(out, chunkp, fmt, chunk_type);
}

/*
 * XXX this is a temporary kludge... Whenever this function is used,
 * I guess instead the user should be fixed not to required the number
 * in size_t format
 *
 * XXX woah what the hell... does this not cause endianness problems?!
 * at least sometimes?
 */
size_t
cross_to_host_size_t(struct tyval *tv) {
	struct type_mapping	*mapping;

	if (tv->type->tlist != NULL) {
		/*
		 * 08/22/07: This can happen if we have a nonsense cast of
		 * a pointer to an integer, as in
		 *
		 *     (size_t)&((struct foo *)0)->member
		 *
		 * Here despite the cast, we still have a pointer typenode
		 * because that is kept for convenience in the backend. Yet
		 * we know that this type must be represented as a uintptr_t,
		 * otherwise the bogus cast would not have been allowed.
		 *
		 * Yes, this is ugly and the type handling should be done
		 * right instead of these workarounds
		 */
#ifndef PREPROCESSOR
		mapping = type_map[ type_to_index(backend->get_uintptr_t()->code) ];
#else
		mapping = type_map[ type_to_index(TY_ULONG) ]; /* XXX */
#endif
	} else {	
		mapping = type_map[ type_to_index(tv->type->code) ];
	}	
	if (mapping == NULL) {
		unimpl();
	}
	if (sizeof(size_t) == mapping->properties.bytes) {
		return *(size_t *)tv->value;
	} else if (sizeof(unsigned int) == mapping->properties.bytes) {
		return *(unsigned int *)tv->value;
	} else if (sizeof(unsigned long) == mapping->properties.bytes) {
		return *(unsigned long *)tv->value;
	} else if (sizeof(unsigned long long) == mapping->properties.bytes) {
		return *(unsigned long long *)tv->value; /* XXX :-((((( */
	} else if (sizeof(unsigned short) == mapping->properties.bytes) {
		return *(unsigned short *)tv->value;
	} else if (sizeof(unsigned char) == mapping->properties.bytes) {
		return *(unsigned char *)tv->value;
	} else {
		printf("BUG: cross_to_host_size_t(), can't handle code %d\n",
			tv->type->code);	
		unimpl();
	}
	/* NOTREACHED */
	return 0;
}

void
cross_from_host_long(void *dest, long val, int type) {
	struct type_mapping	*mapping;

	mapping = type_map[ type_to_index(type) ];
	if (mapping == NULL) {
		unimpl();
	}
	if (sizeof(long) == mapping->properties.bytes) {
		*(long *)dest = val;
	} else if (sizeof(int) == mapping->properties.bytes) {
		*(int *)dest = val;
	} else if (sizeof(short) == mapping->properties.bytes) {
		*(short *)dest = val;
	} else if (sizeof(signed char) == mapping->properties.bytes) {
		*(signed char *)dest = val;
	} else {
		unimpl();
	}
}

/*
 * 04/13/08: Convert target constant value to host long long. This assumes
 * a host long long is large enough to cover all integral target types
 *
 * This function should probably only be called for signed constant types,
 * and the result is sign-extended
 */
long long
cross_to_host_long_long(struct tyval *tv) {
	struct type_mapping	*mapping;

	if (tv->type->tlist != NULL) {
		/*
		 * 08/22/07: This can happen if we have a nonsense cast of
		 * a pointer to an integer, as in
		 *
		 *     (size_t)&((struct foo *)0)->member
		 *
		 * Here despite the cast, we still have a pointer typenode
		 * because that is kept for convenience in the backend. Yet
		 * we know that this type must be represented as a uintptr_t,
		 * otherwise the bogus cast would not have been allowed.
		 *
		 * Yes, this is ugly and the type handling should be done
		 * right instead of these workarounds
		 */
#ifndef PREPROCESSOR
		mapping = type_map[ type_to_index(backend->get_uintptr_t()->code) ];
#else
		mapping = type_map[ type_to_index(TY_ULONG) ]; /* XXX */
#endif
	} else {	
		mapping = type_map[ type_to_index(tv->type->code) ];
	}	
	if (mapping == NULL) {
		unimpl();
	}
	if (sizeof(long long) == mapping->properties.bytes) {
		return *(long long *)tv->value; /* XXX :-((((( */
	} else if (sizeof(ssize_t) == mapping->properties.bytes) {
		return *(ssize_t *)tv->value;
	} else if (sizeof(int) == mapping->properties.bytes) {
		return *(int *)tv->value;
	} else if (sizeof(long) == mapping->properties.bytes) {
		return *(long *)tv->value;
	} else if (sizeof(short) == mapping->properties.bytes) {
		return *(short *)tv->value;
	} else if (sizeof(char) == mapping->properties.bytes) {
		return *(signed char *)tv->value;
	} else {
		printf("BUG: cross_to_host_long_long(), can't handle code %d\n",
			tv->type->code);	
		unimpl();
	}
	/* NOTREACHED */
	return 0;
}

/*
 * 04/13/08: Convert target constant value to host unsigned long long. This
 * assumes a host long long is large enough to cover all integral target types!
 *
 * This function should probably only be called for unsigned constant types,
 * and the result is zero-extended
 */
unsigned long long
cross_to_host_unsigned_long_long(struct tyval *tv) {
	struct type_mapping	*mapping;

	if (tv->type->tlist != NULL) {
		/*
		 * 08/22/07: This can happen if we have a nonsense cast of
		 * a pointer to an integer, as in
		 *
		 *     (size_t)&((struct foo *)0)->member
		 *
		 * Here despite the cast, we still have a pointer typenode
		 * because that is kept for convenience in the backend. Yet
		 * we know that this type must be represented as a uintptr_t,
		 * otherwise the bogus cast would not have been allowed.
		 *
		 * Yes, this is ugly and the type handling should be done
		 * right instead of these workarounds
		 */
#ifndef PREPROCESSOR
		mapping = type_map[ type_to_index(backend->get_uintptr_t()->code) ];
#else
		mapping = type_map[ type_to_index(TY_ULONG) ]; /* XXX */
#endif
	} else {	
		mapping = type_map[ type_to_index(tv->type->code) ];
	}	
	if (mapping == NULL) {
		unimpl();
	}
	if (sizeof(unsigned long long) == mapping->properties.bytes) {
		return *(unsigned long long *)tv->value; /* XXX :-((((( */
	} else if (sizeof(size_t) == mapping->properties.bytes) {
		return *(size_t *)tv->value;
	} else if (sizeof(unsigned int) == mapping->properties.bytes) {
		return *(unsigned int *)tv->value;
	} else if (sizeof(unsigned long) == mapping->properties.bytes) {
		return *(unsigned long *)tv->value;
	} else {
		printf("BUG: cross_to_host_long_long(), can't handle code %d\n",
			tv->type->code);	
		unimpl();
	}
	/* NOTREACHED */
	return 0;
}

/*
 * 04/13/08: Create target value of specified type from host long long
 * value. This should be ok for both signed and unsigned values
 *
 * XXX check for overflow?
 */
void
cross_to_type_from_host_long_long(void *value, int type, long long src) {
	struct type_mapping	*mapping;

	mapping = type_map[ type_to_index(type) ];
	if (mapping == NULL) {
		unimpl();
	}
	if (sizeof(unsigned long long) == mapping->properties.bytes) {
		*(unsigned long long *)value = (unsigned long long)src;
	} else if (sizeof(size_t) == mapping->properties.bytes) {
		*(size_t *)value = (size_t)src;
	} else if (sizeof(unsigned int) == mapping->properties.bytes) {
		*(unsigned int *)value = (unsigned int)src;
	} else if (sizeof(unsigned long) == mapping->properties.bytes) {
		*(unsigned long *)value = (unsigned long)src;
	} else if (sizeof(unsigned short) == mapping->properties.bytes) {
		*(unsigned short *)value = (unsigned short)src;
	} else if (sizeof(unsigned char) == mapping->properties.bytes) {
		*(unsigned char *)value = (unsigned char)src;
	} else {
		printf("BUG: cross_to_host_long_long(), can't handle code %d\n",
			type);	
		unimpl();
	}
}

struct type *
cross_get_nearest_integer_type(int bytes, int base_bytes, int is_signed) {
	struct type_mapping	*mapping;

	if (bytes == 1) {
		return make_basic_type(is_signed? TY_SCHAR: TY_UCHAR);
	}
	mapping = type_map[ type_to_index(TY_USHORT) ];
	if (mapping->properties.bytes >= bytes) {
		return make_basic_type(is_signed? TY_SHORT: TY_USHORT);
	}
	mapping = type_map[ type_to_index(TY_UINT) ];
	if (mapping->properties.bytes >= bytes) {
		return make_basic_type(is_signed? TY_INT: TY_UINT);
	}
	mapping = type_map[ type_to_index(TY_ULONG) ];
	if (mapping->properties.bytes >= bytes) {
		/*
		 * Don't allow use of unsigned long if the base type
		 * isn't larger than unsigned int (in which case it
		 * must also have been declared as such). In other
		 * words, don't allow use of 64bit storage units
		 * for signed/unsigned int bitfields
		 */
#ifndef PREPROCESSOR
		if (base_bytes > (signed)backend->get_sizeof_type(make_basic_type(TY_UINT), NULL)) {
#else
		if (base_bytes > (int)cross_get_sizeof_type(make_basic_type(TY_UINT))) {
#endif
			return make_basic_type(is_signed? TY_LONG: TY_ULONG);
		} else {
			return NULL;
		}
	}
	/*
	 * 09/29/08: We probably won't want to use long long! On 64bit
	 * archs long already covers this case, on 32bit it's just
	 * wrong
	 *
	 * The exception is large ``long long'' bitfields, such as
	 *    long long foo:48;
	 *
	 * (which are not yet correclty supported anyway)
	 */
#ifndef PREPROCESSOR
	if (base_bytes > (signed)backend->get_sizeof_type(make_basic_type(TY_ULONG), NULL)) {
#else
	if (base_bytes > (int)cross_get_sizeof_type(make_basic_type(TY_ULONG))) {
#endif
		/*
		 * This does seem to be an extended bitfield like
		 *
		 *    long long foo:48
		 */
		mapping = type_map[ type_to_index(TY_ULLONG) ];

		if (mapping->properties.bytes >= bytes) {
			return make_basic_type(is_signed? TY_LLONG: TY_ULLONG);
		}
	}
	return NULL;
}

#ifndef PREPROCESSOR

void
cross_calc_bitfield_shiftbits(struct decl *d) {
	int	total_size;
	int	bf_size;
	int	shiftbits;
	int	start_to_bf;
	int	bf_to_end;
	int	real_bf_to_end;

	total_size = backend->get_sizeof_decl(d, NULL);

	bf_size = d->dtype->tbit->numbits;

	/*
	 * Get distance from start of bitfield storage unit to start of
	 * bitfield
	 */
	start_to_bf = d->dtype->tbit->byte_offset * 8 + d->dtype->tbit->bit_offset; /* XXX CHAR_BIT for arch */

	/*
	 * Distance of bitfield to end of storage unit. This is needed for sign
	 * extension
	 *
	 * real_bf_to_end is the real bit count to the end of the storage unit.
	 * bf_to_end may be the bit count to word size (instead of say char size
	 * for small storage units) for sign extension on RISC
	 */
	real_bf_to_end = (total_size * 8) - (start_to_bf + d->dtype->tbit->numbits);
	if (backend->arch == ARCH_X86 || backend->arch == ARCH_AMD64) {
		bf_to_end = real_bf_to_end;
	} else {
		/*
		 * On RISC architectures, there are no small sub-registers like
		 * there are on x86/AMD64. So in order to sign-extend a 4-bit-
		 * item stored in a single byte, we have to extend to a whole
		 * word (otherwise arithmetic shift right does not extend the
		 * sign like it does in e.g. ``sar $n, %bh'' on x86)
		 */
#ifndef PREPROCESSOR
		if (total_size <= (int)backend->get_sizeof_type(make_basic_type(TY_INT), NULL)) {
#else
		if (total_size <= cross_get_sizeof_type(make_basic_type(TY_INT))) {
#endif
		 	/*
			 * We use ``int'' because for 64bit architectures, the shift
			 * instructions would be wrong for a ``long'' (shift word instead
			 * of shift double word).
			 */
#ifndef PREPROCESSOR
			bf_to_end = (backend->get_sizeof_type(make_basic_type(TY_INT), NULL) * 8)
				- (start_to_bf + d->dtype->tbit->numbits);
#else
			bf_to_end = (cross_get_sizeof_type(make_basic_type(TY_INT)) * 8)
				- (start_to_bf + d->dtype->tbit->numbits);
#endif
		} else {
			bf_to_end = (total_size * 8) - (start_to_bf + d->dtype->tbit->numbits);
		}
	}


#if 0 
	printf("Calculating for `%s':\n", d->dtype->name);
	printf("     Storage unit offset:   %d\n", (int)d->offset);
	printf("     Bitfield at:\n");
	printf("                    byte    %d\n", d->dtype->tbit->byte_offset);
	printf("                    bit     %d\n", d->dtype->tbit->bit_offset);
	printf("     Bits to bf:          %d\n", start_to_bf);
	printf("     Bits from bf to end: %d\n", bf_to_end);
#endif

	/*
	 * Determine number of shift bits needed to encode or decode the
	 * bitfield. Note that we have to handle endianness here! Consider
	 * an example bitfield storage unit of 4 bytes, and a bitfield
	 * that occupies the high 4 bits of the first byte, and the low 4
	 * bits of the second byte of the storage unit:
	 *
	 * [  0   ][  1   ][  2   ][  3   ]
	 *     |  bf  |
	 *            ^---bf to end-------^
	 * ^---^     
	 *  start to bf
	 *
	 * After reading the storage unit on a little endian system, bf will
	 * already be in the lower 2 value bytes, so we'd have to shift
	 * right by 4 to get to the bitfield value.
	 *
	 * On a big endian system, bf will be in the upper 2 value bytes, and
	 * thus needs right shifting by 20.
	 */
	if (1) { /*target_info->arch_info->endianness == ENDIAN_LITTLE) {*/
		/* Little endian */
		shiftbits = start_to_bf;
	} else {
		/*
		 * Big endian. We have to take into account that bits within
		 * bytes do not have a different endianness! So we need to
		 * count two things;
		 *
		 *    - Offset of bitfield in byte (like in little endian) A
		 * bitfield with byte offset 4 requires shifting of 4 bits to
		 * get to the value;
		 *
		 *         0 1 2 3 B B B B  << 4
		 *      =  B B B B 0 0 0 0
		 *
		 *      The offset is also the count!
		 *
		 *
		 *    - Byte offset of bitfield start. If the bitfield begins
		 *  at byte 2 of 4 bytes, then in big endian we want to shift
		 *  the last bit of the bitfield to bit 8 in byte 4
		 */
		shiftbits = d->dtype->tbit->bit_offset  /* bit offset */
			+ (real_bf_to_end - real_bf_to_end % 8);  /* whole bytes */

	}
	d->dtype->tbit->shiftbits = shiftbits;


	if (shiftbits == 0) {
		d->dtype->tbit->shifttok = NULL;
	} else {
		d->dtype->tbit->shifttok = const_from_value(&d->dtype->tbit->shiftbits, NULL);
	}

	d->dtype->tbit->bitmask_tok = make_bitfield_mask(d->dtype, 0, NULL,
		&d->dtype->tbit->bitmask_inv_tok);

	if (d->dtype->sign != TOK_KEY_UNSIGNED) {
		int	extbits;
		int	revbits;

		/*
		 * Need sign-extension, i.e. move highest bitfield bit to
		 * highest bitfield storage unit bit, then move the field
		 * back to the bottom of the unit
		 */
		if (1) { /*target_info->arch_info->endianness == ENDIAN_LITTLE) {*/
			extbits = bf_to_end;
		} else {
			/*
			 * 12/06/08: Different for big endian!
			 */
			extbits = d->dtype->tbit->byte_offset * 8 + (7 - d->dtype->tbit->bit_offset);
		}
		revbits = shiftbits + extbits;

		if (extbits == 0) {
			d->dtype->tbit->shifttok_signext_left = NULL;
		} else {
			d->dtype->tbit->shifttok_signext_left = const_from_value(&extbits, NULL);
		}
		if (revbits == 0) {
			d->dtype->tbit->shifttok_signext_right = NULL;
		} else {
			d->dtype->tbit->shifttok_signext_right = const_from_value(&revbits, NULL);
		}
	} else {
		d->dtype->tbit->shifttok_signext_left = NULL;
		d->dtype->tbit->shifttok_signext_right = NULL;
	}
}

#endif /* #ifndef PREPROCESSOR */

