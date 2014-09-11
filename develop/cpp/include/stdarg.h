#ifndef _NWCPP_STDARG_H
#define _NWCPP_STDARG_H

typedef __builtin_va_list	__gnuc_va_list; /* for glibc */
typedef __builtin_va_list	va_list;

#define va_start __builtin_va_start
#define va_arg __builtin_va_arg
#define va_end __builtin_va_end

#endif

