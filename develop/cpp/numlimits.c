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
 *
 * Functions to determine the real type, and possible overflows,
 * of constants
 */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "typemap.h"
#include "token.h"
#include "misc.h"
#include "error.h"
#ifndef PREPROCESSOR
#    include "debug.h"
#endif
#include "n_libc.h"

static struct type_properties	*max_int;
static struct type_properties	*max_uint;
static struct type_properties	*max_long;
static struct type_properties	*max_ulong;
static struct type_properties	*max_llong;
static struct type_properties	*max_ullong;

static char	*max_hex_int;
static char	*max_hex_uint;
static char 	*max_hex_long;
static char 	*max_hex_ulong;
static char	*max_hex_llong;
static char	*max_hex_ullong;

static char	*max_oct_int;
static char	*max_oct_uint;
static char	*max_oct_long;
static char	*max_oct_ulong;
static char	*max_oct_llong;
static char	*max_oct_ullong;

static char	*max_dec_int;
static char	*max_dec_uint;
static char	*max_dec_long;
static char	*max_dec_ulong;
static char	*max_dec_llong;
static char	*max_dec_ullong;

static int	min_hex_digits;
static int	min_oct_digits;
static int	min_dec_digits;

#ifndef LLONG_MAX 
#ifdef __i386__
#   define LLONG_MAX    9223372036854775807LL
#   define LLONG_MIN    (-LLONG_MAX - 1LL)

#   define ULLONG_MAX   18446744073709551615ULL
#endif
#endif


/*
 * Now that we always have the target information, using_llong should
 * always be enabled...probably!
 */
static int using_llong = 1;

/*
 * XXX this is super slow!
 * instead use something like
 * struct val_limit {
 *     char *value;
 *     size_t digits;
 * } limits[x];
 * limits[x].digits = get_digits(val);
 * limits[x].value = n_xmalloc(limits[x].digits + 1);
 * sprintf(limits[x].value, "%...", val);
 * ... tehn
 * #define OVERFLOW(limit, valstr) \
 *    if (valstr->len != limit.value.len) {
 */

void
init_max_digits(void) {
	static int	has_inited;

	if (has_inited == 1) {
		return;
	}
	has_inited = 1;
	max_int = cross_get_type_properties(TY_INT);
	max_uint = cross_get_type_properties(TY_UINT);
	max_long = cross_get_type_properties(TY_LONG);
	max_ulong = cross_get_type_properties(TY_ULONG);
	max_llong = cross_get_type_properties(TY_LLONG);
	max_ullong = cross_get_type_properties(TY_ULLONG);

	min_dec_digits = max_int->max_dec_len;
	max_dec_int = max_int->max_dec;
	max_dec_uint = max_uint->max_dec;
	max_dec_long = max_long->max_dec;
	max_dec_ulong = max_ulong->max_dec;
	max_dec_llong = max_llong->max_dec;
	max_dec_ullong = max_ullong->max_dec;

	min_hex_digits = max_int->max_hex_len;
	max_hex_int = max_int->max_hex;
	max_hex_uint = max_uint->max_hex;
	max_hex_long = max_long->max_hex;
	max_hex_ulong = max_ulong->max_hex;
	max_hex_llong = max_llong->max_hex;
	max_hex_ullong = max_ullong->max_hex;
	
	min_oct_digits = max_int->max_oct_len;
	max_oct_int = max_int->max_oct;
	max_oct_uint = max_uint->max_oct;
	max_oct_long = max_long->max_oct;
	max_oct_ulong = max_ulong->max_oct;
	max_oct_llong = max_llong->max_oct;
	max_oct_ullong = max_ullong->max_oct;
}


/*
 * Returns 1 if the ascii number pointed to by input overflows the
 * ascii number pointed to by limit, else 0
 *
 * XXX this should be using length information before doing the
 * comparison :/
 */
static int
overflow(const char *input, const char *limit) {
	int		is_larger = 0;
	int		sofar_even = 1;

	while (*input && *limit) {
		if (!isdigit((unsigned char)*input)
			&& strchr("abcdefx", tolower((unsigned char)*input))
			 == NULL) {
			break;
		}
		if (*input > *limit && sofar_even == 1) {
			/*
			 * is_larger is significant if both have the same
			 * number of digits
			 */
			is_larger = 1;
		} else if (*input < *limit) {
			sofar_even = 0;
		}
		++input;
		++limit;
	}

	if (*limit) {
		/* Limit not exceeded */
		is_larger = 0;
	} else if (isdigit((unsigned char)*input)
		|| (*input && strchr("abcdef", *input))) {
		/* Limit exceeded */
		is_larger = 1;
	}
	return is_larger;
}

/*
 * The following two functions - check_hex_oct() and
 * check_dec() - determine the type of an integer constant and return
 * the value of that as success, -1 on failure (overflow). Decimal
 * integer constants are by default of type ``int''. If they do not fit
 * into an int, they are promoted to ``long''. If that's not enough
 * either, C99 specifies that ``long long'' shall be used.
 * A similar concept is applied to constants with unsigned and long/
 * long long specifiers or different source representation (hexadecimal,
 * octal constants), as will be seen shortly.
 */
static int
check_hex_oct(char *num, int is_hex, int signfl, int long_flag) {
	/*
	 * Since octal and hexadecimal constants are handled EXACTLY the
	 * same in this respect, the same code can be used. Only a
	 * different working set must be chosen depending on whether we are
	 * dealing with an octal or a hexadecimal constant
	 */
	char	*ws[6];

	if (is_hex) {
		ws[0] = max_hex_int;
		ws[1] = max_hex_uint;
		ws[2] = max_hex_long;
		ws[3] = max_hex_ulong;
		ws[4] = max_hex_llong;
		ws[5] = max_hex_ullong;
	} else {
		ws[0] = max_oct_int;
		ws[1] = max_oct_uint;
		ws[2] = max_oct_long;
		ws[3] = max_oct_ulong;
		ws[4] = max_oct_llong;
		ws[5] = max_oct_ullong;
	}

	/* For syntactic convenience when dealing with ws... */
#define WS_INT	 	0	
#define WS_UINT		1	
#define WS_LONG		2	
#define WS_ULONG	3	
#define WS_LLONG	4
#define WS_ULLONG	5
		
	if (signfl) {
		/* Unsigned */
		if (long_flag) {
			/*
			 * 123lu
			 * unsigned long int
			 * unsigned long long int
			 */

			/*
			 * 06/19/08: Don't check for long if the
			 * constant already has a request for long
			 * long!
			 */
			if (long_flag < 2) {
				if (!overflow(num, ws[WS_ULONG])) {
					return TY_ULONG;
				}	
			}
			if (using_llong) {
				/* C99 */
				if (overflow(num, ws[WS_ULLONG])) {
					return -1;
				} else {
					return TY_ULLONG;
				}
			} else {
				/* C89 */
				return -1;
			}
		} else {
			/*
			 * 123u
			 * unsigned int
			 * unsigned long int
			 * unsigned long long int
			 */
			if (!overflow(num, ws[WS_UINT])) {
				return TY_UINT;
			}	
			if (!overflow(num, ws[WS_ULONG])) {
				return TY_ULONG;
			}
			if (using_llong) {
				/* C99 / GNU C */
				if (overflow(num, ws[WS_ULLONG])) {
					return -1;
				} else {
					return TY_ULLONG;
				}	
			} else {
				/* C89 */
				return -1;
			}
		}
	} else {
		/* Is signed */
		if (long_flag) {
			/*
			 * 123l
			 * long int
			 * unsigned long int
			 * long long int
			 * unsigned long long int
			 */
			
			/*
			 * 06/19/08: Don't check for long if the
			 * constant already has a request for long
			 * long!
			 */
			if (long_flag < 2) {
				if (!overflow(num, ws[WS_LONG])) {
					return TY_LONG;
				}	
				if (!overflow(num, ws[WS_ULONG])) {
					return TY_ULONG;
				}	
			}
			if (using_llong) {
				/* C99 */
				if (!overflow(num, ws[WS_LLONG])) {
					return TY_LLONG;
				}	
				if (!overflow(num, ws[WS_ULLONG])) {
					return TY_ULLONG;
				} else {
					return -1;
				}
			} else {
				/* C89 */
				return -1;
			}
		} else {
			/*
			 * 123
			 * int
			 * unsigned int
			 * long int
			 * unsigned long int
			 * long long int
			 * unsigned long long int
			 */
			if (!overflow(num, ws[WS_INT])) {
				return TY_INT;
			}	
			if (!overflow(num, ws[WS_UINT])) {
				return TY_UINT;
			}	
			if (!overflow(num, ws[WS_LONG])) {
				return TY_LONG;
			}	
			if (!overflow(num, ws[WS_ULONG])) {
				return TY_ULONG;
			}	
			if (using_llong) {
				/* C99 / GNU */
				if (!overflow(num, ws[WS_LLONG])) {
					return TY_LLONG;
				}	
				if (!overflow(num, ws[WS_ULLONG])) {
					return TY_ULLONG;
				} else {
					return -1;
				}	
			} else {
				/* C89 */
				return -1;
			}
		}
	}
	/* NOTREACHED */
	return -1;
}


static int
check_dec(char *num, int signfl, int long_flag) {
	if (signfl) {
		/* Unsigned */
		if (long_flag) {
			/*
			 * 10lu
			 * unsigned long int
			 * unsigned long long int
			 */
			if (overflow(num, max_dec_ulong)) {
				if (using_llong) {
					/* C99 */
					if (overflow(num, max_dec_ullong)) {
						return -1;
					} else {
						return TY_ULLONG;
					}
				} else {
					/* C89 */
					return -1;
				}
			} else {
				/*
				 * 06/19/08: Honor requests for
				 * long long constants instead
				 * of just using ulong
				 */
				if (long_flag < 2) {
					return TY_ULONG;
				} else {
					return TY_ULLONG;
				}
			}
		} else {
			/*
			 * 10u
			 * unsigned int
			 * unsigned long int
			 * unsigned long long int
			 */
			if (overflow(num, max_dec_uint)) {
				if (overflow(num, max_dec_ulong)) {
					if (using_llong) {
						/* C99 */
						if (overflow(num,
							max_dec_ullong)) {
							return -1;
						} else {
							return TY_ULLONG;
						}
					} else {
						/* C89 */
						return -1;
					}
				} else {
					return TY_ULONG;
				}
			} else {
				return TY_UINT;
			}
		}
	} else {
		/* Signed */
		if (long_flag) {
			/*
			 * 10l
			 * long int
			 * unsigned long int
			 * long long int
			 */
			if (overflow(num, max_dec_long)) {
				if (overflow(num, max_dec_ulong)) {
					if (using_llong) {
						/* C99 */
						if (overflow(num,
							max_dec_llong)) {
							/*
							 * 06/29/08: This was missing a check for unsigned
							 * long long
							 */
							if (!overflow(num, max_dec_ullong)) {
								lexwarning("Number is `unsigned long long'");
								return TY_ULLONG;
							} else {	
								return -1;
							}
						} else {
							return TY_LLONG;
						}
					} else {
						/* C89 */
						return -1;
					}
				} else {
					/*
					 * 06/19/08: Honor requests for
					 * long long constants instead
					 * of just using ulong
					 */
					if (long_flag < 2) {
						return TY_ULONG;
					} else {
						if (overflow(num,
							max_dec_llong)) {
							return TY_ULLONG;
						} else {
							return TY_LLONG;
						}
					}
				}
			} else {
				/*
				 * 06/19/08: Honor requests for
				 * long long constants instead
				 * of just using ulong
				 */
				if (long_flag < 2) {
					return TY_LONG;
				} else {
					return TY_LLONG;
				}
			}
		} else {
			/*
			 * 10
			 * int
			 * long int
			 * unsigned long int
			 * long long int
			 */
			if (overflow(num, max_dec_int)) {
				if (overflow(num, max_dec_long)) {
					if (overflow(num, max_dec_ulong)) {
						if (using_llong) {
							/* C99 */
							if (overflow(num, 
							max_dec_llong)) {
								/*
								 * 06/29/08: This was missing a check for unsigned
								 * long long
								 */
								if (!overflow(num, max_dec_ullong)) {
									lexwarning("Number is `unsigned long long'");
									return TY_ULLONG;
								} else {	
									return -1;
								}
							} else {
								return TY_LLONG;
							}
						} else {
							return -1;
						}	
					} else {
						return TY_ULONG;
					}
				} else {
					return TY_LONG;
				}
			} else {
				return TY_INT;
			}
		}
	}
	/* NOTREACHED */
	return -1;
}
		
		
	

/*
 * Returns the real type of the ascii contanst specified by num, based on
 * hexa_flag, octal_flag, signfl, long_flag, if the type recorded by those
 * variables had to be adjusted to make the value fit. Returns -1 if an
 * overflow was detected. Returns 0 if the type did not have to be
 * adjusted
 */ 
int
range_check(char *num,
		int hexa_flag,
		int octal_flag, 
		int signfl, 
		int long_flag,
		int ndigits) {

	char	*mode	= NULL;
	char	*start	= NULL;
	int	rc;

	if (hexa_flag == 1 || octal_flag == 1) {
		if (hexa_flag) {
			mode = "hexadecimal";
			start = num + 2; /* 0x... */
		} else {
			mode = "octal";
			start = num + 1; /* 0... */
		}
		while (*start == '0') {
			++start;
			--ndigits;
		}
		if (hexa_flag) {
			if (ndigits < min_hex_digits) {
				return 0;
			}
		} else { /* octal */
			if (ndigits < min_oct_digits) {
				return 0;
			}
		}	
		rc = check_hex_oct(start, hexa_flag, signfl, long_flag); 
	} else {
		if (ndigits < min_dec_digits) {
			return 0;
		}
		mode = "decimal";
		rc = check_dec(num, signfl, long_flag);
	}

	if (rc == -1) {
		lexerror("Overflow in %s constant", mode);
	}	

	return rc;
}


/* XXX write new regression test */

