/*
 * Copyright (c) 2005 - 2010, Nils R. Weller
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
#ifndef REG_H
#define REG_H

struct decl;
struct token;
struct icode_list;
struct stack_block;
struct function;
struct reg;

#include <stddef.h>

/*
 * Physical register
 */
struct reg {
	char		*name;
	int		type;
#define REG_GPR		1
#define REG_FPR		2
#define REG_SP		3
#define REG_BP		4
/*
#define REG_RP	5 return pointer
*/
	size_t		size; /* size of register in bytes */
	int		used; /* in use? */
	int		allocatable; /* if in use - may be reallocated? */
	/*
	 * 12/27/08: New flag to mark register as dedicated (not
	 * allocatable or otherwise generally usable - e.g. temp
	 * GPR or stack/frame pointer). used=0 and allocatable=0
	 * is not enough because that can sometimes happen with
	 * undedicated registers too
	 */
	int		dedicated;
	struct reg	**composed_of;
	struct vreg	*vreg; /* backing object */
/*#ifdef DEBUG6*/
	int		line;
	int		nallocs;
/*#endif*/
};


/*
 * Virtual register
 */
struct vreg {
	struct decl		*var_backed;
	struct token		*from_const;
	struct vreg		*from_ptr;
	struct stack_block	*stack_addr;
	struct type		*type;
	size_t			size;
	int			on_var;
	int			is_nullptr_const;

	/*
	 * if is_multi_reg_obj is nonzero, this object occupies multiple
	 * registers. It is absolutely essential to remember that this
	 * is not just a boolean flag - it must contain the NUMBER of
	 * registers required too!!! (e.g. 2 for ``long long'' on x86)
	 */
	int			is_multi_reg_obj;
	int			struct_ret;
	struct decl		*memberdecl;
	struct vreg		*parent;
	long			addr_offset;
#define VREG_SEQNO	1
#if VREG_SEQNO
	int			seqno;
#endif

#if 0
	struct reg		*preg;
	struct reg		*preg2;
#endif
	struct reg		*pregs[2];
};

int
reg_unused(struct reg *r);

int
reg_allocatable(struct reg *r);

void
reg_set_allocatable(struct reg *r);


void
reg_set_unallocatable(struct reg *r);

void
vreg_set_allocatable(struct vreg *r);

void
vreg_set_unallocatable(struct vreg *r);

void
reg_set_dedicated(struct reg *r);

int
is_x87_trash(struct vreg *vr);

#if 0
void
reg_save(struct function *f, struct reg *r, struct icode_list *il);
#endif

struct vreg *
dup_vreg(struct vreg *);

struct vreg *
vreg_alloc(struct decl *dec, struct token *constant, struct vreg *from_ptr,
	struct type *t);

struct reg *
vreg_faultin(struct reg *r, struct reg *r2,
struct vreg *vr, struct icode_list *il, int whatfor);

struct reg *
vreg_faultin_dedicated(struct reg *r, struct reg *r2,
struct vreg *vr, struct icode_list *il, int whatfor);

struct reg *
vreg_faultin_x87(struct reg *r, struct reg *r2,
struct vreg *vr, struct icode_list *il, int whatfor);

struct reg *
vreg_faultin_protected(struct vreg *prot, struct reg *r, struct reg *r2,
struct vreg *vr, struct icode_list *il, int whatfor);

struct reg *
vreg_faultin_ptr(struct vreg *vr, struct icode_list *il);

void
vreg_anonymify(struct vreg **vr, struct reg *r, struct reg *r2,
	struct icode_list *il);

void
vreg_do_anonymify(struct vreg *vr);

struct vreg *
vreg_disconnect(struct vreg *);

void
vreg_freetmpvrs(void);

void
allow_vreg_map_preg(void);
void
forbid_vreg_map_preg(void);

void
backend_vreg_map_preg(struct vreg *, struct reg *);
void
backend_vreg_unmap_preg(struct reg *);
void
backend_vreg_map_preg2(struct vreg *, struct reg *);
void
backend_vreg_unmap_preg2(struct reg *);

void
vreg_map_preg(struct vreg *vr, struct reg *r);
void
vreg_map_preg2(struct vreg *vr, struct reg *r);

void
vreg_map_preg_dedicated(struct vreg *vr, struct reg *r);

void
reg_set_unused(struct reg *);

void
free_preg(struct reg *r, struct icode_list *il, int invalidate, int savereg);
void
free_pregs_vreg(struct vreg *vr, struct icode_list *il,
	int invalidate, int savereg);

void
free_preg_dedicated(struct reg *r, struct icode_list *il, int invalidate, int savereg);


void
save_reg(struct reg *r, struct icode_list *il);

void
free_physreg(struct reg *r);

int
is_member_of_reg(struct reg *r, struct reg *member);

void
free_other_members(struct reg *r, struct reg *member);

void
vreg_set_new_type(struct vreg *vr, struct type *ty);

void
vreg_reinterpret_as(struct vreg **vr, struct type *from,
	struct type *to, struct icode_list *il);

struct vreg *
vreg_back_by_ptr(struct vreg *vr, struct reg *ptrreg, int is_backend);

int
vreg_needs_pic_reloc(struct vreg *);

struct vreg *
copy_vreg(struct vreg *vr);


#endif

