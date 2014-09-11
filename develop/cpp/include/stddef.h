#ifndef _NWCPP_STDDEF_H
#define _NWCPP_STDDEF_H

typedef unsigned long	size_t;
typedef signed long	ptrdiff_t;
typedef unsigned long	wchar_t;

#if 0
#ifdef __linux__
/* hmm this is what cpp does instead of sys/types */
#include <bits/types.h>
#endif
#endif

#if 0
#include <bits/types.h>
#include <stdint.h>

typedef int	intptr_t;
#endif

#ifndef NULL
#  define NULL ((void *)0)
#endif

#define offsetof(type, member) \
	(size_t)( (char *)&((type *)0)->member - (char *)((type *)0) )

#endif

