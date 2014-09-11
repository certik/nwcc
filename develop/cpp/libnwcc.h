#ifndef LIBNWCC_H
#define LIBNWCC_H

/*
 * Duplicated libnwcc symbols caused some problems when
 * compiling nwcc with itself; In order to prevent these,
 * the libnwcc.o linked with nwcc1 must be compiled with
 * EXTERNAL_USE undefined, so as to avoid nameclashes.
 * The ``library'' version extlibnwcc.o must be compiled
 * with -DEXTERNAL_USE.
 */
#ifndef EXTERNAL_USE
#define __nwcc_negate		nwcc_negate
#define __nwcc_ulldiv		nwcc_ulldiv
#define __nwcc_lldiv		nwcc_lldiv
#define __nwcc_ullmul		nwcc_ullmul
#define __nwcc_llmul		nwcc_llmul
#define __nwcc_add		nwcc_add
#define __nwcc_shift_left	nwcc_shift_left
#define __nwcc_shift_right	nwcc_shift_right
#define __nwcc_stack_corrupt	nwcc_stack_corrupt
#define __nwcc_sub		nwcc_sub
#define __nwcc_conv_to_ldouble	nwcc_conv_to_ldouble
#define __nwcc_conv_from_ldouble	nwcc_conv_from_ldouble
#define llong_to_hex __llong_to_hex
#endif

void
__nwcc_negate(unsigned char *, int);

void
__nwcc_ulldiv(unsigned char *, unsigned char *, int, int);

void
__nwcc_lldiv(unsigned char *, unsigned char *, int, int);

void
__nwcc_ullmul(unsigned char *, unsigned char *, int);

void
__nwcc_llmul(unsigned char *, unsigned char *, int);

void
__nwcc_conv_to_ldouble(unsigned char *dest, unsigned char *src);

void
__nwcc_conv_from_ldouble(unsigned char *dest, unsigned char *src);

void
llong_to_hex(char *out, unsigned char *in, int is_bigend);

#endif

