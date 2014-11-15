/*
 * Copyright (c) 2003 - 2010, Nils R. Weller
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
#ifndef TYPE_H
#define TYPE_H


struct decl;
struct type;
struct vreg;
struct token;
struct stack_block;
struct function;
struct initializer;

#include <stddef.h>

#ifndef PREPROCESSOR
#include "features.h"
#endif

/* For structure definitions */
struct ty_struct {
	char			*tag;
	char			*dummytag;
	struct scope		*scope;
	struct scope		*parentscope;

	/* number of members */
	int			nmemb;

	/* e.g. __attribute__((packed)) */
	struct attrib		*attrib;
	int			unnamed;
	int			incomplete;
	int			printed;
	int			is_union;
	int			alignment;
	unsigned int		references;
	size_t			size;
	struct decl		*flexible;
	struct ty_struct	*next;
};


/*
 * An array of ty_bit in ``struct type'' is used to describe a bitfield
 */
struct ty_bit {
	char			*name;
	int			numbits;

	/*
	 * 09/04/08: New offsets for the new bitfield storage layout.
	 * byte_offset indicates the offset from the surrounding
	 * bitfield storage unit
	 * bit_offset indicates the bitfield offset in the first byte
	 * which carries this bitfield
	 *
	 * (I.e. base offset*8 + byte_offset*8 + bit_offset gets us
	 * to the start of the bitfield)
	 */
	int			byte_offset;
	int			bit_offset;
	int			end_byte_offset;
	int			end_bit_offset;

	/*
	 * Absolute offset within struct. Needed to calculate offset in
	 * partial storage unit easily (e.g. a 3 byte partial storage
	 * unit starting at byte 1)
	 */
	int			absolute_byte_offset;

	/*
	 * 10/02/08: Base storage unit declaration. The offset within
	 * this storage unit is the difference between byte/bit_offset
	 * and bitfield_storage_unit->offset.
	 *
	 * Note that this unit has type ``array of N chars'', but it
	 * need not be a full storage unit because it does NOT
	 * include non-bitfield members! E.g. in
	 *
	 *    struct foo { char x; int bf:24; };
	 *
	 * ... the bf storage unit declaration is of type char[3] and
	 * begins after x. This storage unit chopping is done to
	 * simplify initializer handling.
	 */
	struct decl		*bitfield_storage_unit;
	/*
	 * 09/08/08: Number of shift bits needed to encode or decode
	 * a bitfield member to/from a bitfield storage unit
	 * !!!!!!!!! DANGER!!!!!!!!!!!!!!!!!! DO NOT CHANGE FROM INT
	 * TO OTHER TYPE! (Passed to const_from_value())
	 */
	int			shiftbits;
	/*
	 * Bitmask token which covers the range of this bitfield. NOTE
	 * that this doesn't take the bitfield location in its storage
	 * unit into account! So if we have a bitfield with 3 bits,
	 * then the bitmask token will be 0x7, REGARDLESS of whether
	 * the bitfield is stored in the high or low bits of a storage
	 * unit
	 */
	struct token		*bitmask_tok;
	struct token		*bitmask_tok_with_shiftbits;
	/*
	 * Inverted bitmask token. Unlike bitmask_tok, this DOES take
	 * the storage unit into account. If a bitfield with 3 bits
	 * is stored in the upper 4 bits of a byte, then the inverted
	 * bitmask is 0xf, i.e. it covers the LOWER 4 bits (and by
	 * ANDing the byte with the mask, the bitfield part is set to 0)
	 */
	struct token		*bitmask_inv_tok;
	struct token		*shifttok;
	struct token		*shifttok_signext_left;
	struct token		*shifttok_signext_right;

};

/*
 * Data structure to store string literal information. This is not
 * used by ``struct type'', but only by the ``struct token'' data
 * member, initializers, etc
 */
struct ty_string {
	char			*str; /* The string literal */
	unsigned long	count; /* .str%lu the `%lu' in asm output */
	size_t			size; /* Size of array (including null) */
	int			is_wide_char;
	struct type		*ty;
	struct ty_string	*next;
};

struct num;

struct ty_float {
	struct num	*num;
	unsigned long	count;
	struct ty_float	*next;
	struct ty_float	*prev;

	/*
	 * 02/19/08: New flag which means ``this constant is no longer
	 * needed''. In particular, the point is to avoid printing
	 * float constants that were used in constant (initializer)
	 * expressions, since those are not accessed in the asm code
	 */
	int		inactive;
};	

struct ty_llong {
	struct num	*num;
	unsigned long	count;
	int		loaded;
	struct ty_llong	*next;
};

/* For enum definitions */
struct ty_enum {
	char *tag;
	struct {
		char		*name;
		struct decl	*dec;
		struct token	*value;
	}    *members;
	int	 nmemb;
};

/* For function declarations */
#define FDTYPE_KR	1
#define FDTYPE_ISO	2

struct ty_func { 
	char		*name;
	struct scope	*scope;
	struct decl	*lastarg;
	int		nargs;	/* number of arguments */
	int		was_just_declared;
	struct type	*ret;	/* return type */

	/*
	 * 04/11/08: Paramter name list for K&R functions. This is
	 * just used to resolve the parameter types as soon as
	 * those are encountered, and can be ignored (deleted)
	 * afterwards
	 */
	struct ntab	*ntab;
	char		*asmname; /* assembler name */
	int		type;	/* K&R or ISO declaration */
	int		variadic; /* Variadic function? */
};


#define TN_ARRAY_OF	(1)
#define TN_POINTER_TO	(1 << 1)
#define TN_FUNCTION	(1 << 2)
#define TN_VARARRAY_OF	(1 << 3)


/*
 * This structure describes the properties of a declarator, i.e.
 * ``pointer-to'', ``array-of-N'', ``function pointer'', etc
 */
struct type_node {
	/* Designates array of/pointer to/function */
	int				type;

	/* If this is an array, arrayarg specifies the number of elements */
#if REMOVE_ARRARG 
	int				have_array_size;
	struct expr			*variable_arrarg;
#else
	struct expr			*arrarg;
#endif
	/* 
	 * Number of elements for string constants, and after parsing
	 * arrays with constant size
	 */
	size_t				arrarg_const;

	/*
	 * If this is a pointer, ptrarg specifies whether the pointer is
	 * qualified (TOK_KEY_VOLATILE/CONST/RESTRICT) or not (0)
	 */
	int				ptrarg;

	/*
	 * 05/22/11: If this is a VLA, record the runtime metadata
	 * block index number for this node. For example, in
	 *
	 *    char buf[x][y];
	 *    printf("%d\n", sizeof *buf);
	 *
	 * ... sizeof will have to access the SECOND hidden block
	 * size variable
	 */
	int				vla_block_no;

	/*
	 * If this is a function, tfunc specifies its arguments.
	 * This is a null pointer in case of ``T f(void)''
	 */
	struct ty_func		*tfunc;

	/* Next node in list */
	struct type_node	*next;

	/* Previous node in list */
	struct type_node	*prev;
};


struct type {
	int		line; /* line in source file */
	char		*file; /* name of source file */
	char		*name;
	int		is_func; /* is function declaration? */
	int		is_def;	/* is struct/func definition? */

	/* Structure/union/enum-specific data */
	int			incomplete; /* is incomplete */
	struct ty_struct	*tstruc;
	struct ty_enum		*tenum;

	/*
	 * tbit is used for bitfield members. A bitfield is designated by
	 * a struct member of type (u)int/bool with tbit != NULL. That is,
	 * struct ty_struc *t = lookup_struct("tag", SCOPE_NESTED);
	 * if (t != NULL) {
	 *     struct type *ty = t->members[0].dtype;
	 *     if (ty->code == TY_INT ||
	 *       ty->code == TY_UINT || 
	 *         ty->code == TY_BOOL) {
	 *         if (ty->tbit != NULL) {
	 *            fist member is bitfield!
	 *         }
	 *     }
	 *  }
	 * The same applies to unions
	 */
	struct ty_bit		*tbit;

	/* General data */
	int			code; /* integer, double, etc */
	int			implicit;/* implicit declaration? */
#define IMPLICIT_FDECL	(1)
#define IMPLICIT_INT	(1 << 1)
	int			sign; /* signed or unsigned */
	int			storage; /* storage duration */

#if 0
	int			is_const; /* const? */
	int			is_volatile; /* volatile? */
	int			is_restrict; /* restrict? */
	int			is_inline; /* inline? */
	int			is_vla; /* VLA? (any component) */
#endif

	int			flags;
#define FLAGS_CONST	(1)
#define FLAGS_VOLATILE	(1 << 1)
#define FLAGS_RESTRICT	(1 << 2)
#define FLAGS_INLINE	(1 << 3)
#define FLAGS_VLA	(1 << 4)
#define FLAGS_THREAD	(1 << 5)
#define FLAGS_FUNCNAME	(1 << 6)
#define FLAGS_EXTERN_INLINE	(1 << 7)
/*
 * 02/10/09: Indicates that the variable was renamed using __asm__().
 * This requires a literal interpretation of the name on OSX
 */
#define FLAGS_ASM_RENAMED	(1 << 8)
/*
 * 02/04/10: Indicates that the type is a size_t (this can be
 * used to distinguish the result type of ``sizeof(int)'' from
 * an ordinary unsigned int or unsigned long to warn about
 * printf("%d\n", sizeof(int));)
 */
#define FLAGS_SIZE_T		(1 << 9)

#define IS_CONST(flags) (flags & FLAGS_CONST)
#define IS_VOLATILE(flags) (flags & FLAGS_VOLATILE)
#define IS_RESTRICT(flags) (flags & FLAGS_RESTRICT)
#define IS_INLINE(flags) (flags & FLAGS_INLINE)
#define IS_VLA(flags) (flags & FLAGS_VLA)
#define IS_THREAD(flags) (flags & FLAGS_THREAD)
#define IS_FUNCNAME(flags) (flags & FLAGS_FUNCNAME)
#define IS_EXTERN_INLINE(flags) (flags & FLAGS_EXTERN_INLINE)
#define IS_ASM_RENAMED(flags) (flags & FLAGS_ASM_RENAMED)
#define IS_SIZE_T(flags) (flags & FLAGS_SIZE_T)

	struct stack_block	*vla_addr;

	struct attrib		*attributes; /* GNU C __attribute__()s */
	struct attrib		*attributes_tail;
	unsigned int		fastattr; /* quick attribute check flags */

	/*
	 * Declarator list. This is not used in 
	 * structure/... definitions!
	 */
	struct type_node	*tlist;			

	struct type_node	*tlist_tail;

};


#define CMPTY_SIGN	1
#define CMPTY_CONST	(1 << 2)
#define CMPTY_ALL	(CMPTY_SIGN|CMPTY_CONST)
#define CMPTY_ARRAYPTR	(1 << 3)
#define CMPTY_TENTDEC	(1 << 4)

struct ty_struct	*alloc_ty_struct(void);
struct ty_llong		*alloc_ty_llong(void);
struct ty_string	*alloc_ty_string(void);
struct ty_string	*make_ty_string(const char *, size_t);
unsigned long		ty_string_count(void);
struct ty_bit		*alloc_ty_bit(void);
struct ty_enum		*alloc_ty_enum(void);
struct ty_func		*alloc_ty_func(void);
struct type_node	*alloc_type_node(void);
struct type		*alloc_type(void);
int	compare_types(struct type *dest, struct type *src, int flag);
int		check_init_type(struct type *dest, struct expr *init);
void	copy_type(struct type *dest, const struct type *src, int full);
struct type_node *
copy_tlist(struct type_node **dest, const struct type_node *tlist);
struct type		*make_basic_type(int code);
struct type		*make_void_ptr_type(void);
struct type		*make_array_type(int size, int is_wide_char);
void			append_typelist(struct type *t,
				int type, void *type_arg, struct ty_func *tf,
				struct token *tok);

char			*ret_type_to_text(struct type *t);
char			*type_to_text(struct type *t);
int			is_arithmetic_type(struct type *t);
int			is_scalar_type(struct type *t);
int			is_floating_type(struct type *t);
int			is_array_type(struct type *t);
int			is_integral_type(struct type *t);
int			is_basic_agg_type(struct type *t);
int			is_arr_of_ptr(struct type *t);
int			is_nullptr_const(struct token *, struct type *);
int			type_without_sign(int code);
struct token		*const_from_type(struct type *ty, int from_alignment,
		int	extype, struct token *tok);
struct token		*fp_const_from_ascii(const char *asc, int type);
struct token		*const_from_value(void *value, struct type *ty);
struct token		*const_from_string(const char *value);

int	check_types_assign(
	struct token *t,
	struct type *left,
	struct vreg *right,
	int to_const_ok,
	int silent);
struct type		*addrofify_type(struct type *t);
void			functype_to_rettype(struct type *t);
struct type		*dup_type(struct type *t);

int			is_transparent_union(struct type *);
struct type		*get_transparent_union_type(
		struct token *,
		struct type *left, struct vreg *right);
void			init_to_array_size(struct type *ty,
		struct initializer *init);
int			func_returns_void(struct function *);
int		is_modifyable(struct type *ty);
struct type	*func_to_return_type(struct type *);
void		ppcify_constant(struct token *tok);

/*
 * 05/22/10: Function to determine whether any type node in the
 * list is a VLA
 */
int	is_any_elem_vla(struct type_node *tn);

#endif

