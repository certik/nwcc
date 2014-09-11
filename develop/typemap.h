#ifndef TYPEMAP_H
#define TYPEMAP_H

#include <stdio.h> /* size_t */

struct token;
struct tyval;
struct expr;
struct decl;
struct type;


/*
 * This does not take stuff like PDP-endianness into account,
 * and also assumes that all data pointers are the same size
 */
struct arch_properties {
	int	arch;
	int	abi;
	int	endianness;
	int	data_ptr_size;
	int	func_ptr_size;

	int	long_can_store_uint;
};

/*
 * If the number of bits in a host and a target type equal,
 * and the sign too, then we assume the types are compatible
 * for all arithmetic operations
 */
struct type_properties {
	int	code;
	int	sign;		/* sign */ 
	int	bytes;		/* size in bytes */
	int	align;		/* natural alignment in bytes */
	int	bits;		/* size in bits */
	char	*max_dec;	/* max range in decimal ascii */
	int	max_dec_len;	/* length of max_dec */
	char	*max_hex;	/* ... etc ..  */
	int	max_hex_len;
	char	*max_oct;
	int	max_oct_len;
};


/*
 * Properties of a target architecture. Not all of it is used yet,
 * but should be sooner or later.
 *
 * type_info points to an array containing type information. 
 * arch_info is a grabbag of other architecture stuff, including
 * some type info too (general pointer size)
 */
struct target_properties {
	struct type_properties	*type_info;
	struct arch_properties	*arch_info;
};

extern struct target_properties       *target_info;

struct num;

int	get_host_endianness(void);

struct arch_properties	*cross_get_target_arch_properties(void);

void	cross_set_char_signedness(int funsigned_char, int fsigned_char,
			int host_arch, int target_arch);
int	cross_get_char_signedness(void);
void	cross_initialize_type_map(int targetarch, int targetabi, int targetsys);
struct type_properties *cross_get_type_properties(int code);
int	cross_exec_op(struct token *tok, struct tyval *res, struct tyval *dest,
		struct tyval *src);
void	cross_do_conv(struct tyval *dest, int code, int save_type);
int	cross_convert_tyval(struct tyval *left, struct tyval *right, struct type **res);
size_t	cross_get_sizeof_type(struct type *);
struct num *cross_scan_value(const char *str, int type,
		int hexa_flag, int octal_flag, int fp_flag);
void	cross_print_value_by_type(FILE *out, void *value, int type, int ofmt);
void	cross_print_value_chunk(FILE *out, void *value, int type,
		int chunk_type, int chunk_type_rem, int chunkno);



/* Conversion routines */
size_t	cross_to_host_size_t(struct tyval *tv);
long long	cross_to_host_long_long(struct tyval *tv);
unsigned long long	cross_to_host_unsigned_long_long(struct tyval *);
void	cross_from_host_long(void *dest, long val, int type);

/* 04/13/08: To target value from host long long */
void	cross_to_type_from_host_long_long(void *value, int type, long long src);

void	cross_conv_host_to_target(void *src, int destty, int srcty);
void	cross_conv_value_to_target_size_t(void *buf, unsigned long value);
struct token	*cross_convert_const_token(struct token *, int ty);



struct ty_float;

struct ty_float	*put_float_const_list(struct num *vll);
void		remove_float_const_from_list(struct ty_float *);

int	cross_have_mapping(int first_type, int second_type);
struct token	*cross_make_int_token(long long value);

struct token	*cross_get_pow2_minus_1(struct token *tok);
struct token	*cross_get_pow2_shiftbits(struct token *tok);
struct token	*cross_get_bitfield_signext_shiftbits(struct type *);
struct type	*cross_get_bitfield_promoted_type(struct type *);
struct type	*cross_get_nearest_integer_type(int bytes, int base_bytes, int is_signed);
void		cross_calc_bitfield_shiftbits(struct decl *d);

#endif

