#include "libnwcc.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

/* Macros for bitwise access.. NOTE: These are little-endian */
#define GET_BIT(ar, idx) \
	!!((ar)[(idx) / CHAR_BIT] & (1 << (idx) % CHAR_BIT))

#define SET_BIT(ar, idx) \
	((ar)[(idx) / CHAR_BIT] |= (1 << (idx) % CHAR_BIT))

#define CLR_BIT(ar, idx) \
	((ar)[(idx) / CHAR_BIT] &= ~(1 << (idx) % CHAR_BIT))

static void
do_reverse(unsigned char *data, int data_size) {
	/* Convert from little to big endian */
	unsigned char	*start = data;
	unsigned char	*end = data +
		 (data_size / 8 - 1);
	do {
		unsigned char	tmp = *start;
		*start = *end;
		*end = tmp;
	} while (++start < --end);
}

/*
 * This function must be called on big endian hosts to
 * convert data to little endian format before processing,
 * and afterwards to convert the result back to big endian.
 * This is just a quick temporary kludge to get this code
 * ``up and running'' on PowerPC. Performance is TERRIBLE
 */
static void
correct_endianness(unsigned char *data, int data_size) {
	/* XXX cross-compilation */
	int	x = 123;

	if (((unsigned char *)&x)[sizeof(int) - 1] == 123) {
		do_reverse(data, data_size);
	}
}

#if 0
static void
dump_llong(unsigned char *p, int data_size) {
        unsigned char   *uc;

        for (uc = p + /*7*/ (data_size / 8) - 1; uc >= p; --uc) {
                int     i;

                for (i = 7; i >= 0; --i) {
                        putchar('0' + GET_BIT(uc, i));
                }
        }
        putchar('\n');
}
#endif


static int 
skip_zeros(unsigned char *num, int data_size) {
	int		i;
	int		j = CHAR_BIT - 1;
	unsigned char	mask;

	for (i = /*7*/data_size / 8 - 1; i >= 0; --i) {
		if (num[i] != 0) {
			break;
		}
	}
	if (i == -1) {
		return -1;
	}
	for (mask = 1 << (CHAR_BIT - 1); (num[i] & mask) == 0; mask >>= 1)
		--j;
	return (i * CHAR_BIT) + j; 
	
}

void
__nwcc_sub(/*unsigned char *res,*/ unsigned char *dest, unsigned char *src,
	int maxbit, int wrong_endianness, int data_size) {

	int		i;
	int		carry = 0;
	unsigned char	res[8];

	if (wrong_endianness) {
		correct_endianness(dest, data_size);
		correct_endianness(src, data_size);
	}

	memset(res, 0, 8);
	for (i = 0; i < maxbit || (carry && i < /*64*/data_size); ++i) {
		int	dbitval = GET_BIT(dest, i);
		int	sbitval = GET_BIT(src, i);
		int	resbit;

		if (carry) {
			if (sbitval) {
				sbitval = 0;
			} else {
				sbitval = 1;
				carry = 0;
			}
		}
		
		resbit = dbitval - sbitval;
		if (resbit < 0) {
			carry = 1;
		}
		if (resbit) {
			SET_BIT(res, i);
		}	
	}
	memcpy(dest, res, 8);
	if (wrong_endianness) {
		correct_endianness(dest, data_size);
	}
}	

void
__nwcc_add(unsigned char *dest, unsigned char *src,
	int data_size, int wrong_endianness) {

	int		carry = 0;
	int		i;
	unsigned char	res[8];

	if (wrong_endianness) {
		correct_endianness(dest, data_size);
		correct_endianness(src, data_size);
	}

	memset(res, 0, 8);

	for (i = 0; i < /*64*/data_size; ++i) { 
		int	dbitval = GET_BIT(dest, i);
		int	sbitval = GET_BIT(src, i);
		int	resbit;

		if (dbitval && sbitval) {
			if (carry) {
				resbit = 1;
			} else {
				resbit = 0;
			}
			carry = 1;
		} else if (carry) {
			if (dbitval || sbitval) {
				resbit = 0;
			} else {
				resbit = 1;
				carry = 0;
			}
		} else {
			resbit = dbitval + sbitval;
			carry = 0;
		}

		if (resbit) {
			SET_BIT(res, i);
		}	
	}
	memcpy(dest, res, 8);
	if (wrong_endianness) {
		correct_endianness(dest, data_size);
	}
}

void
__nwcc_shift_right(unsigned char *dest, unsigned src,
	int data_size, int wrong_endianness) {
	int		kill_bytes = src / 8;
	int		nbytes = data_size / 8;
	int		shift_bits;
	unsigned char	*p;
	unsigned char	*destp;

	if (wrong_endianness) {
		correct_endianness(dest, data_size);
	}
	if (kill_bytes) {
		destp = dest;
		for (p = dest+kill_bytes; p < dest+nbytes; ++p) {
			*destp++ = *p;
		}
		while (destp < dest+nbytes) {
			*destp++ = 0;
		}
	}
	if ((shift_bits = src - (kill_bytes * 8)) > 0) {
		int	saved_bits = 0;

		for (p = dest+nbytes-1; p >= dest; --p) {
			int	new_saved_bits = *p << (8-shift_bits);

			*p = (*p >> shift_bits) | saved_bits;
			saved_bits = new_saved_bits; 
		}
	}
	if (wrong_endianness) {
		correct_endianness(dest, data_size);
	}
}

void
__nwcc_shift_left(unsigned char *dest, unsigned src,
	int data_size, int wrong_endianness) {

	int		kill_bytes = src / 8;
	int		nbytes = data_size / 8;
	int		shift_bits;
	unsigned char	*p;
	unsigned char	*destp;

	if (wrong_endianness) {
		correct_endianness(dest, data_size);
	}
	if (kill_bytes) {
		destp = dest+nbytes-1;
		for (p = dest+nbytes-1-kill_bytes; p >= dest; --p) {
			*destp-- = *p;
		}
		while (destp >= dest) {
			*destp-- = 0;
		}
	}

	if ((shift_bits = src - (kill_bytes * 8)) > 0) {
		int	saved_bits = 0;

		for (p = dest; p < dest+nbytes; ++p) {
			int	new_saved_bits = *p >> (8-shift_bits);

			*p = (*p << shift_bits) | saved_bits;
			saved_bits = new_saved_bits; 
		}
	}
	if (wrong_endianness) {
		correct_endianness(dest, data_size);
	}
}

	

static int
no_fit(
	unsigned char *dest, unsigned char *src,
	int chunkstart, int destbit, int srcbit) {

	int	ndestbits;

	
#if 0
printf("no_fit()   dest %d, src %d\n", destbit, srcbit);
{
unsigned char *tmp;
for (tmp = dest; tmp < dest+8; ++tmp) {
	printf("%02x ", *tmp);
}
putchar(10);
}


{
unsigned char *tmp;
for (tmp = src; tmp < src+8; ++tmp) {
	printf("%02x ", *tmp);
}
putchar(10);
}
#endif



	while (GET_BIT(dest, destbit) == 0) {
		--destbit;
	}	
	ndestbits = destbit - chunkstart;

	if (ndestbits < srcbit) {
		/* Less dest bits selected */
		return 1;
	} else if (ndestbits > srcbit) {
		/* Definitely a fit */
		return 0;
	} else {
		/* Compare all bits */
		int	i;
		int	j;

		for (i = destbit, j = srcbit; i >= chunkstart; --i, --j) {
			int	dbitval = GET_BIT(dest, i);
			int	sbitval = GET_BIT(src, j);

			if (dbitval < sbitval) {
				return 1;
			} else if (dbitval > sbitval) {
				return 0;
			}
		}
	}	
	return 0;
}

static void
get_value_range(unsigned char *res, unsigned char *src, int start, int end) {
	int	i;
	int	j;

	memset(res, 0, 8);
	for (i = start, j = 0; i <= end; ++i, ++j) {
		int	val = GET_BIT(src, i);

		if (val) {
			SET_BIT(res, j);
		} else {
			CLR_BIT(res, j);
		}	
	}	
}	


static void
prepend_bit(unsigned char *dest, int newbit) {
	int	i;

	for (i = 7; i >= 0; --i) {
		dest[i] <<= 1;
		if (i > 0) {
			int	prev;

			prev = GET_BIT(dest, (i - 1) * 8 + 7);
			if (prev) {
				SET_BIT(dest, i * 8);
			} else {
				CLR_BIT(dest, i * 8);
			}	
		} else {
			if (newbit) {
				SET_BIT(dest, 0);
			} else {
				CLR_BIT(dest, 0);
			}
		}
	}
}	

/*
 * Division of two unsigned 64bit values. Though this is hardcoded for 64bit
 * right now, it is in principle completely independent of the size of its
 * input and should be able to deal with arbitrarily sized integers.
 * The result is stored in dest. If want_remainder is nonzero, the result is
 * the remainder (useful for modulo.)
 *
 * This was hard to write and is probably less efficient than it could be.
 */
void
__nwcc_ulldiv(
	unsigned char *dest,
	unsigned char *src,
	int want_remainder,
	int data_size) {

	int		destbit;
	int		srcbit;
	int		curbit;
	int		initial;
	int		nbits;
	int		resbits = 0;
	unsigned char	destbuf[8];
	unsigned char	result[8];

	correct_endianness(src, data_size);
	correct_endianness(dest, data_size);

	memset(result, 0, sizeof result);
	if ((destbit = skip_zeros(dest, data_size)) == -1) {
		/* 0 / something is 0 */
		memcpy(dest, result, sizeof result);
		return;
	} else if ((srcbit = skip_zeros(src, data_size)) == -1) {
		puts("DIVISION BY ZERO ERROR");
		abort();
	}

#if 0
	if (destbit < 32 && srcbit < 32) {
		/* Wow, no trickery required */
		return;
	}
#endif

	/*
	 * Do the school method:
	 * 123 / 11 = 11
	 * 11
	 * --
	 *  13
	 *  11
	 * ...
	 */
	curbit = destbit;
	memset(destbuf, 0, sizeof destbuf);

	nbits = 0, ++curbit;
	
	for (initial = 1;; initial = 0) {

		for (;;) {
			/* Get one more bit */
			if (--curbit == -1) {
				/* Doesn't fit */
				break;
			} else {
				int	newbit = GET_BIT(dest, curbit);

				prepend_bit(destbuf, newbit);
				/*
				 * 02/20/09: There was an overflow for
				 * very large values in no_fit(). But
				 * we can't break if nbits >= data_size
				 * because that misses the final
				 * iteration
				 */
				if (nbits < data_size - 1) {
					++nbits;
				} else {
					;
				}

				if (!initial) {
					if (no_fit(destbuf, src, 0,
						nbits, srcbit)) {
#if 0
						CLR_BIT(result, 63-resbits);
#endif
						CLR_BIT(result, data_size-1-resbits);
						++resbits;
					} else {
						/*
						 * Divisor now fits into
						 * dividend
						 */
						break;
					}	
				} else if (!no_fit(destbuf, src, 0,
					nbits, srcbit)) {
					/*
					 * Divisor fits into initial sub-
					 * dividend
					 */
					break;
				}
			}
		}

		if (curbit > -1) {
			/*unsigned char	tmp[8];*/
			
			__nwcc_sub(/*tmp,*/ destbuf, src, destbit, 0, data_size);

			/* Add a 1 to the result */
#if 0
			SET_BIT(result, 63 - resbits); 
#endif	
			SET_BIT(result, data_size - 1 - resbits);
			++resbits;
			/*memcpy(destbuf, tmp, 8);*/
			if ((nbits = skip_zeros(destbuf, data_size)) == -1) {
				nbits = 0;
			}	
		} else {
			if (want_remainder) {
				memcpy(dest, destbuf, 8);
				correct_endianness(dest, data_size);
				return;
			}	
			break;
		}
	}

	/*
	 * Now the result is stored at the end of the result buffer, so we have
	 * to move it to the start as necessary
	 */
#if 0
	get_value_range(dest, result, 64 - resbits, 63);
#endif
	get_value_range(dest, result, data_size - resbits, data_size - 1);
	correct_endianness(dest, data_size);
}

void
__nwcc_negate(unsigned char *dest, int data_size) {
	int		i;
	unsigned char	src[8] = { 1 };

	for (i = 0; i < /*64*/data_size; ++i) {
		int	bit = GET_BIT(dest, i);

		if (bit) {
			CLR_BIT(dest, i);
		} else {
			SET_BIT(dest, i);
		}
	}
	__nwcc_add(dest, src, data_size, 0);
}

void
__nwcc_lldiv(
	unsigned char *dest,
	unsigned char *src,
	int want_remainder,
	int data_size) {

	int	neg_dest = 0;
	int	neg_src = 0;
	
	correct_endianness(src, data_size);
	correct_endianness(dest, data_size);
	if (GET_BIT(dest, data_size - 1)) {
		/* Sign bit set! */
		neg_dest = 1;
		__nwcc_negate(dest, data_size);
	}
	if (GET_BIT(src, data_size - 1)) {
		/* Sign bit set! */
		neg_src = 1;
		__nwcc_negate(src, data_size);
	}
	correct_endianness(dest, data_size);
	correct_endianness(src, data_size);

	__nwcc_ulldiv(dest, src, want_remainder, data_size);
	if (neg_dest || neg_src) {
		if (neg_dest != neg_src) {
			/*
			 * One side isn't negative - make result negative
			 * (if both were negative, the result is positive)
			 *
			 * 06/22/08: This was missing: A negative value
			 * modulo a positive one does yield a negative
			 * result, but a positive value modulo negative
			 * does not!
			 */
			if (!want_remainder || !neg_src) {
				correct_endianness(dest, data_size);
				__nwcc_negate(dest, data_size);
				correct_endianness(dest, data_size);
			}
		} else {
			/*
			 * 06/22/08: Negative % negative yields a negative
			 * result!
			 */
			if (want_remainder) {
				correct_endianness(dest, data_size);
				__nwcc_negate(dest, data_size);
				correct_endianness(dest, data_size);
			}
		}
	}
}	


void
__nwcc_ullmul(unsigned char *dest, unsigned char *src, int data_size) {
	int		destbit;
	int		srcbit;
	int		i;
	int		j;
	unsigned char	result[8];
	unsigned char	tmp[8];

	correct_endianness(dest, data_size);
	correct_endianness(src, data_size);

	memset(result, 0, sizeof result);
	if ((destbit = skip_zeros(dest, data_size)) == -1
		|| (srcbit = skip_zeros(src, data_size)) == -1) {
		/* 0 * something = 0 */
		memcpy(dest, result, sizeof result);
		return;
	}

#if 0
	printf("hmm %llu, %llu\n", *(unsigned long long *)dest,
			*(unsigned long long *)src);
printf(" ");
dump_llong((void *)dest, 64);
printf("*");
dump_llong((void *)src, 64);
#endif
	/*
	 * School method -
	 * 1101 * 101
	 *     1101
	 *      0000
	 *       1101
	 *    -------
	 *    1000001
	 */
	for (i = 0; i <= srcbit; ++i) {
		memset(tmp, 0, 8);
		if (GET_BIT(src, i) == 0) {
			continue;
		}
		for (j = 0; j <= destbit; ++j) {
			int	idx = i + j;

			if (idx > data_size - 1) {
				/*
				 * 06/22/08: This was missing: Don't step over
				 * the bounds of the number!
				 */
				break;
			}
			if (GET_BIT(dest, j)) {
				SET_BIT(tmp, idx);
			} else {
				CLR_BIT(tmp, idx);
			}
		}
#if 0
	printf("   line for src bit %d:    ", i);
	dump_llong((void *)tmp, 64);
		printf("     ");
		dump_llong((void *)tmp, 64);
		printf("    +");
		dump_llong((void *)result, 64);
#endif
		__nwcc_add(result, tmp, data_size, 0); 
#if 0
		printf("    =");
		dump_llong((void *)result, 64);
		puts("===");
#endif
	}
	memcpy(dest, result, 8);
	correct_endianness(dest, data_size);
#if 0
	puts("===");
		printf("     ");
		dump_llong((void *)dest, 64);
#endif
}

void
__nwcc_llmul(unsigned char *dest, unsigned char *src, int data_size) {
	int	neg_dest = 0;
	int	neg_src = 0;
	
	correct_endianness(dest, data_size);
	correct_endianness(src, data_size);
	if (GET_BIT(dest, data_size - 1)) {
		/* Sign bit set! */
		neg_dest = 1;
		__nwcc_negate(dest, data_size);
	}
	if (GET_BIT(src, data_size - 1)) {
		/* Sign bit set! */
		neg_src = 1;
		__nwcc_negate(src, data_size);
	}
	correct_endianness(dest, data_size);
	correct_endianness(src, data_size);
	__nwcc_ullmul(dest, src, data_size);
	if (neg_dest || neg_src) {
		if (neg_dest != neg_src) {
			/*
			 * One side isn't negative - make result negative
			 * (if both were negative, the result is positive)
			 */
			correct_endianness(dest, data_size);
			__nwcc_negate(dest, data_size);
			correct_endianness(dest, data_size);
		}
	}	
}


#if 0
void
dump_item(void *p, int size) {
	int		i;
	unsigned char	*ucp = p;

	for (i = 0; i < size; ++i) {
		printf("%02x ", *ucp++);
	}
	putchar('\n');

	ucp = p;
	for (i = 0; i < size; ++i) {
		int	j;
		for (j = 0; j < 8; ++j) {
			putchar('0' + !!(*ucp & (1 << (7 - j))));
		}
		++ucp;
	}
	putchar('\n');
}
#endif


void
__nwcc_conv_to_ldouble(unsigned char *dest, unsigned char *src) {
	int		temp = 123;
	unsigned int	exp;
	unsigned int	mantissa0;
	unsigned int	mantissa1;


#if USE_LIBGCC
	/*
	 * 07/21/09: Since our long double conversion stuff doesn't work, let's
	 * use libgcc for now :-/
	 */
	long double __extenddftf2(double a); 
	*(long double *)dest = __extenddftf2(*(double *)src);
	return;
#endif

#if 0
	printf("src double = %f\n", *(double *)src);
	dump_item(src,8);
#endif
	memset(dest, 0, 16);

	if (*(char *)&temp != 123) { /* Big endian - XXX kludge check */
		/* Set sign bit */
		if (src[0] & 0x80) {
			dest[0] = 0x80;
		}

		/* Get exponent */
		exp = ((src[0] & 0x7f) << 4) | ((src[1] & 0xf0) >> 4);

		/*quiet_nan = !!(src[1] & 0x10);*/

		/* Get mantissa - upper 3 bits of last byte plus 2 full bytes */
		mantissa0 = /*(src[1] & 0xf) << 16) |*/ (src[2] << 8) | src[3]; 
		memcpy(&mantissa1, src+4, sizeof mantissa1);

#if 0 
		printf("input double ........ sign = %x   exp = %x   quiet = %x   mant = %x    %x\n",
			sign, exp, quiet_nan, mantissa0, mantissa1);
#endif

		/* Convert to long double */
		exp <<= 4;   /* extend 11 bits to 15 bits */
		exp |= src[1] & 0xf;  /* 4 highest mantissa bits of double become part of the exponent */

		dest[0] |= (exp >> 8) & 0x7f;  /* Get upper 7 bits */
		dest[1] = exp & 0xff; /* Get lower 8 bits */

		mantissa0 <<= 16;  /* extend from 20 to 32 bits */
		/*
		 * 07/20/09: Note that this depends on the representation! It
		 * will copy the upper 2 bytes.
		 * XXX as per the 20 bits comment above, the shift would
		 * discard the upper 4 bits?!
		 */
		memcpy(dest+2, &mantissa0, sizeof mantissa0);
		memcpy(dest+4, &mantissa1, sizeof mantissa1);
		memset(dest+10, 0, 6);
	} else {
		/* Set sign bit */
		if (src[7] & 0x80) {
			dest[0] = 0x80;
		}		

		/* Get exponent */
		exp = (src[7] & 0x7f) << 4 | ((src[6] & 0xf0) >> 4);

		/* Get mantissa */
		mantissa0 = src[5] << 8 | src[4];
		memcpy(&mantissa1, src, sizeof mantissa1);

		/* Convert to long double */
		exp <<= 4; /* extend 11 bits to 15 bits */
		/*exp |= src[6] & 0xf;*/ /* 4 highest mantissa bits of double become part of the exponent */

		dest[15] = (exp >> 8) & 0x7f; /* Get upper 7 bits */
		dest[14] = (exp & 0xff) >> 4; /* Get lower 8 bits */

		/*mantissa0 <<= 16;*/ /* extend from 20 to 32 bits */
		memcpy(dest+12, &mantissa0, 2 /* sizeof mantissa0*/);
		dest[13] = 0;
		dest[13] |= (src[6] & 0xf) << 4;
		dest[13] |= (src[5] & 0xf0) >> 4;
		memcpy(dest+8, &mantissa1, sizeof mantissa1);
	}
#if 0
	printf("dest ldouble = %f\n", *(long double *)dest);
	dump_item(dest, 16);
#endif
}

void
__nwcc_conv_from_ldouble(unsigned char *dest, unsigned char *src) {
#if USE_LIBGCC
	/*
	 * 07/21/09: Since our long double conversion stuff doesn't work, let's
	 * use libgcc for now :-/
	 */
	double __trunctfdf2(long double a);
	*(double *)dest = __trunctfdf2(*(long double *)src);
	return;
#endif

	if (1) {
#if 0
		unsigned int	exp;
		unsigned int	quiet_nan;
		unsigned int	mantissa0;
		unsigned int	mantissa1;
		unsigned int	sign;
#endif


	memcpy(dest, src, 8);
	return;




#if 0

		memset(dest, 0, 8);

		/* Set sign bit */
		sign = !!(src[0] & 0x80);

		/* Get exponent */
		exp = ((src[0] & 0x7f) << 8) | src[1];

		/* Get mantissa - upper 3 bits of last byte plus 2 full bytes */
		memcpy(&mantissa0, src+4, sizeof mantissa0); 
		memcpy(&mantissa1, src+8, sizeof mantissa1);

#if 0 
		printf("sign = %x   exp = %x   quiet = %x   mant = %x    %x\n",
			sign, exp, quiet_nan, mantissa0, mantissa1);
#endif

		/* Now convert to double */
		if (sign) {
			dest[0] = 0x80;
		} else {
			dest[0] = 0;
		}


		/* Lower 4 exponent bits become upper 4 mantissa bits */
		dest[1] = exp & 0xf;


		exp >>= 4;
		exp &= 0x7ff;  /* 11 bits */
		dest[0] |= (exp >> 4); /* get upper 7 bits */
		dest[1] |= (exp & 0xf) << 4; /* get lower 4 bits */
		mantissa0 &= 0xfffff; /* 20 bits */
		dest[1] |= mantissa0 >> 16; /* get upper 4 bits */ 
		dest[2] = (mantissa0 >> 8) & 0xff; /* set whole byte */
		dest[3] = (mantissa0 >> 0) & 0xff; /* set whole byte */
		memcpy(dest+4, &mantissa1, sizeof mantissa1); /* mantissa 1 has same size */
	#if 0 
	printf("res double = %f\n", *(double *)dest);
	dump_item(dest, 8);
	#endif
#endif
	}
}


void
__nwcc_stack_corrupt(void) {
	printf("ERROR: Stack corruption detected, cannot continue program\n");
	printf("       execution. Calling abort()\n");
	abort();
}	

void
llong_to_hex(char *out, unsigned char *in, int is_bigend) {
	char	*p;
	char	buf[5];
	int		i;
	int		j;

	*out++ = '0';
	*out++ = 'x';

	/* 4 binary digits = 1 hex digit. Life's so easy. */
	p = (out + 64 / 4);
	*p-- = 0;

	if (is_bigend) {
		static unsigned char	tmp[8];
		unsigned char			*sp;
		unsigned char			*ep;

		memcpy(tmp, in, 8);
		sp = tmp;
		ep = tmp+7;
		do {
			unsigned char	c = *sp;
			*sp = *ep;
			*ep = c;
		} while (++sp < --ep);
		in = tmp;
	}

	j = 3;
	for (i = 0; i < 64; ++i) {
		buf[j] = '0' + GET_BIT(in, i);
		if (j == 0) {
			buf[4] = 0;
			*p-- = "0123456789abcdef"[strtol(buf, NULL, 2)];
			j = 3;
		} else {
			--j;
		}
	}
}

#ifdef TEST_LIBNWCC

int
main() {
	long long	l1 = 246;
	long long	l2 = 5;
	long long	testdest[128];
	long long	testsrc[128];
	long long	gcc_res;
	int		i;
	int		i1;
	int		i2;
	int		testdest_int[128];
	int		testsrc_int[128];

#if 0
	memset(&testdest[0], 0, 8);
	((char *)&testdest[0])[7] = 1;
	dump_llong((void *)&testdest[0], 64);
	__nwcc_shift_left((void *)&testdest[0], 9, 64, 1);
	dump_llong((void *)&testdest[0], 64);
return 0;
#endif
	srand(time(NULL));
	for (i = 0; i < 128; ++i) {
		/* XXX This stuff is crude ... */
		testdest[i] = rand();
		while (testdest[i] < UINT_MAX) {
			testdest[i] += rand();
		}
		testsrc[i] = rand();
		if (testsrc[i] == 0) ++testsrc[i];
		while (testdest[i] < testsrc[i]) {
			testdest[i] += rand();
		}	
		l1 = testdest[i];
		l2 = testsrc[i];
		gcc_res = l1 / l2;
		__nwcc_ulldiv((void *)&testdest[i], (void *)&testsrc[i], 0, 64);
#if 0
		gcc_res = l1 * l2;
		__nwcc_llmul((void *)&testdest[i], (void *)&testsrc[i], 64);
#endif
#if 0
		gcc_res = l1 + l2;
		__nwcc_add((void *)&testdest[i],
			(void *)&testsrc[i], 64, 1);
#endif
gcc_res >>= 9;

__nwcc_shift_right((void *)&testdest[i], 9 , 64, 1);
 
		if (testdest[i] != gcc_res) {

			puts("BAD RESULT");
		} else {
			printf("worked! %lld %lld (%lld,%lld)\n",
				testdest[i], gcc_res, l1, l2);
		}	
	}
return 0;

	for (i = 0; i < 128; ++i) {
		/* XXX This stuff is crude ... */
		int	gcc_res_int;

		testdest_int[i] = rand();
		testsrc_int[i] = rand();
		while (testdest_int[i] < testsrc_int[i]) {
			testdest_int[i] += rand();
		}
		i1 = testdest_int[i];
		i2 = testsrc_int[i];
		gcc_res_int = i1 / i2;
		__nwcc_ulldiv((void *)&testdest_int[i], (void *)&testsrc_int[i], 0, 32);
		if (testdest_int[i] != gcc_res_int) {
			puts("BAD RESULT");
		} else {
			printf("worked! %d %d (%d,%d)\n",
				testdest_int[i], gcc_res_int, i1, i2);
		}	
	}

	return 0;
}

#endif


