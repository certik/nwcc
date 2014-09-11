/*
 * Copyright (c) 2004 - 2010, Nils R. Weller
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
#ifndef DECL_H
#define DECL_H

/* Forward declarations */
struct	type;
struct	expr;
struct	token;
struct	vreg;
struct	init_with_name;

#define DECL_NOINIT		(1)
#define DECL_CONSTINIT		(1 << 1)
#define DECL_VARINIT		(1 << 2)
#define DECL_CAST		(1 << 3)
#define DECL_FUNCARG		(1 << 4)
#define DECL_FUNCARG_KR		(1 << 5)
#define DECL_STRUCT		(1 << 6)

#define DECL_UNUSED(d) (d->dtype->storage == TOK_KEY_STATIC \
	&& d->references == 0)

#include <stddef.h>

struct decl {
int seqno;
	/*
	 * XXX I forgot why ``name'' is in type rather than decl, but
	 * I believe there is a good reason :-/
	 *
	 * (MrNutty: ``I KEEP ALL MY RECORDS IN THIS LOAF OF BREAD,
	 * FOR REASONS I NO LONGER REMEMBER!!'' -
         * http://www.yellow5.com/pokey/archive/index376.html)
	 */
	/*char		*name;*/
	struct type			*dtype;

	/*
	 * 03/27/08: Record whether the declaration is invalid
	 * because C's wonderful tentative declarations trashed it.
	 * Invalid declarations may not be used for anything
	 */
	int				invalid;

	/*
	 * 08/09/08: Declaration which was implicitly created as a result
	 * of a compound literal or anonymous struct return value. This is
	 * needed to ignore such declarations in stmt-as-expr
	 */
	int				is_unrequested_decl;

	/*
	 * 10/01/08: For struct members: Record whether the member is within
	 * a bitfield storage unit, e.g. in
	 *
	 * struct foo { int x:1; char y; short z; };
	 *
	 * ... all members will be within the same unit, so they all have the
	 * flag set
	 *
	 * XXX too much memory! We should shove all flags (above ones too)
	 * into one integer
	 */
/*	struct decl			*is_within_bitfield_storage_unit;*/

	/*
	 * 03/26/08: Since we have to remove the vreg member, enum values
	 * have to be represented in a new member! An symbolic enum constant
	 * has a ``struct decl'' with dtype->tenum pointing to the enum
	 * type instance, and tenum_value pointing to the specific value
	 * XXX wow this shouldn'te ven be a token but a number, so we can
	 * free the token
	 */
	struct token			*tenum_value;
	struct initializer		*init;
	struct init_with_name		*init_name;
	struct token			*tok;
	char				*asmname;
	int				has_def;
	int				has_symbol;
	int				was_not_extern;
/*	int				is_alias;*/ /* e.g. __PRETTY_FUNCTION/__func__ */
	struct decl			*is_alias;
	unsigned			references;
	unsigned			real_references;

	size_t				offset;

	size_t				size;
	/*
	 * 03/27/08: Finally this vreg stuff can go! We do not cache any
	 * vreg stuff anymore because it isn't really needed, and has even
	 * become wrong with the introduction of the zone allocator
	 */
/*	struct vreg			*vreg;*/
	struct stack_block		*stack_addr;
	/* Next is only used to form the lists of static variables */
	struct decl			*next;
};


struct decl	**parse_decl(struct token **toklist, int mode);
void		append_decl(struct decl **, struct decl **, struct decl *);
struct decl	*alloc_decl(void);
void		check_incomplete_tentative_decls(void);

#endif

