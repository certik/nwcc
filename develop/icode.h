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
#ifndef ICODE_H
#define ICODE_H

struct statement;
struct vreg;
struct reg;
struct expr;
struct fcall_data;
struct store_data;
struct stack_block;
struct type;
struct function;
struct control;
struct label;
struct token;
struct decl;
struct inline_asm_stmt;

struct copystruct {
	struct vreg	*dest_vreg;
	struct vreg	*src_vreg;
	struct reg	*dest_preg;
	struct reg	*src_preg;
	struct reg	*dest_from_ptr;
	struct reg	*src_from_ptr;
	struct reg	*dest_from_ptr_struct;
	struct reg	*src_from_ptr_struct;
	struct reg	*startreg;
};

struct putstructregs {
	struct reg	*destreg;
	struct reg	*ptrreg;
	struct vreg	*src_vreg;
};

struct copyreg {
	struct reg	*src_preg;
	struct reg	*dest_preg;
	struct type	*src_type;
	struct type	*dest_type;
};

struct allocadata {
	struct reg		*result_reg;
	struct reg		*size_reg;
	struct stack_block	*addr;
};	

struct vlasizedata {
	struct reg		*size;
	struct stack_block	*blockaddr;
	int			offset;	
};
/* XXX used by conv_fp and extend_sign */
struct extendsign {
	struct reg	*dest_preg;
	struct reg	*src_preg; /* XXX conv_fp-only */
	struct type	*src_type;
	struct type	*dest_type;
};

struct amd64_ulong_to_float {
	struct reg	*src_gpr;
	struct reg	*temp_gpr;
	struct reg	*dest_sse_reg;
	int		code;
};

struct amd64_negmask_data {
	struct reg	*target_fpr;
	struct reg	*support_gpr;
};

#include <stddef.h>


struct allocstack {
	size_t		nbytes;
	struct vreg	*patchme;
};

struct icode_instr {
	int			type;
#define INSTR_SEQPOINT		1 /* pseudo */
#define INSTR_LABEL		2
#define INSTR_JUMP		3 /* Unconditional branch */
#define INSTR_CMP		5	
#define INSTR_EXTEND_SIGN	10 /* pseudo */	
#define INSTR_CONV_FP		11 /* pseudo */
#define INSTR_CONV_TO_LDOUBLE	12 /* pseudo */
#define INSTR_CONV_FROM_LDOUBLE	13 /* pseudo */
#define INSTR_CALL		20
#define INSTR_CALLINDIR		21
#define INSTR_LOAD		30
#define INSTR_LOAD_ADDRLABEL	31	/* 07/20/08 */
#define INSTR_COMP_GOTO		32	/* 07/20/08 */
#define INSTR_STORE		35
#define INSTR_WRITEBACK		37
#define INSTR_COPYINIT		40 /* pseudo */
#define INSTR_COPYSTRUCT	41 /* pseudo */
#define INSTR_ASM		42 /* pseudo */
#define INSTR_ALLOCA		43 /* pseudo */
#define INSTR_INTRINSIC_MEMCPY	44 /* pseudo */
#define INSTR_DEALLOCA		45 /* pseudo */

#define INSTR_BUILTIN_FRAME_ADDRESS	46 /* pseudo */

#define INSTR_PUTSTRUCTREGS	47 /* 11/06/08: pseudo */

#define INSTR_ALLOC_VLA		50 /* pseudo */
#define INSTR_DEALLOC_VLA	51 /* pseudo */

#define INSTR_PUT_VLA_SIZE	52 /* pseudo */
#define INSTR_RETR_VLA_SIZE	53 /* pseudo */	

#define INSTR_LOAD_VLA		55

#define INSTR_PUSH		60
#define INSTR_FREESTACK		70
#define INSTR_ALLOCSTACK	71
#define INSTR_INDIR		80
#define INSTR_ADDROF		81
#define INSTR_DEC		89
#define INSTR_INC		90
#define INSTR_NEG		91
#define INSTR_SETREG		92
#define INSTR_XCHG		93
#define INSTR_MOV		100
#define INSTR_ADD		110
#define INSTR_SUB		111	
#define INSTR_MUL		112
#define INSTR_DIV		113
#define INSTR_MOD		114
#define INSTR_SHL		115
#define INSTR_SHR		116
#define INSTR_AND		117
#define INSTR_OR		118
#define INSTR_XOR		119
#define INSTR_NOT		120

#define INSTR_PREG_OR		121

#define INSTR_SETITEM		125 /* pseudo */
#define INSTR_UNIMPL		126 /* pseudo */ 
#define INSTR_DEBUG		130 /* pseudo */
#define INSTR_PROPVREG		131 /* pseudo */
#define INSTR_ADJ_ALLOCATED	132 /* pseudo */
#define INSTR_INITIALIZE_PIC	133 /* pseudo */

#define INSTR_DBGINFO_LINE	135 /* debugging pseudo */

#define INSTR_BR_EQUAL		140
#define INSTR_BR_NEQUAL		141
#define INSTR_BR_GREATER	142
#define INSTR_BR_SMALLER	143
#define INSTR_BR_GREATEREQ	144
#define INSTR_BR_SMALLEREQ	145
	
#define INSTR_RET		160

#define INSTR_X86_FXCH		500
#define INSTR_X86_FFREE		501
#define INSTR_X86_FNSTCW	502
#define INSTR_X86_FLDCW		503
#define INSTR_X86_CDQ		520


#define INSTR_X86_FILD		530
#define INSTR_X86_FIST		531

#define INSTR_MIPS_MFC1		600
#define INSTR_MIPS_MTC1		601
#define INSTR_MIPS_CVT		602
#define INSTR_MIPS_TRUNC	603
#define INSTR_MIPS_MAKE_32BIT_MASK	604

#define INSTR_AMD64_CVTSI2SD	700
#define INSTR_AMD64_CVTSI2SS	701
#define INSTR_AMD64_CVTTSD2SI	702
#define INSTR_AMD64_CVTTSS2SI	703
#define INSTR_AMD64_CVTSD2SS	704
#define INSTR_AMD64_CVTSS2SD	705

/*
 * 04/11/08: The missing link!! Instructions for 64bit integer to SSE fp conversion
 */
#define INSTR_AMD64_CVTSI2SDQ	706
#define INSTR_AMD64_CVTSI2SSQ	707

/*
 * 04/12/08: Negation for SSE values
 */
#define INSTR_AMD64_LOAD_NEGMASK	708	/* pseudo */
#define INSTR_AMD64_XORPS		709	
#define INSTR_AMD64_XORPD		710
/* 08/01/08: 64bit target cvttsd/ss versions */
#define INSTR_AMD64_CVTTSD2SIQ  711
#define INSTR_AMD64_CVTTSS2SIQ  712

/* 08/02/08: New pseudo instr for absurd unsigned long (long) to float conv */


#define INSTR_AMD64_ULONG_TO_FLOAT	713


#define INSTR_POWER_SRAWI	800
#define INSTR_POWER_RLWINM	801
#define INSTR_POWER_SLWI	802
#define INSTR_POWER_EXTSH	803
#define INSTR_POWER_RLDICL	804
#define INSTR_POWER_EXTSB	805
#define INSTR_POWER_EXTSW	806
#define INSTR_POWER_FCFID	807
#define INSTR_POWER_FRSP	808
#define INSTR_POWER_SET_LOAD_DOUBLE	810
#define INSTR_POWER_UNSET_LOAD_DOUBLE	811
#define INSTR_POWER_XORIS	820
#define INSTR_POWER_LIS		821
#define INSTR_POWER_LOADUP4	822
#define INSTR_POWER_FCTIWZ	823

#define INSTR_SPARC_LOAD_INT_FROM_LDOUBLE	830

#if 0
	void			*data;
	struct vreg		*then_vreg;
#endif
	/*
	 * Virtual and physical source/destination registers. The reason
	 * pregs need to be recorded as well is that vr->preg cannot be
	 * used in the backend because any vreg may have multiple pregs
	 * associated with it during its lifetime
	 *
	 * XXX Some of these things are probably only needed for loads/
	 * stores. By specializing more, we can save memory
	 */
	struct reg		**dest_pregs;
	struct vreg		*dest_vreg;
	struct reg		**src_pregs;
	struct vreg		*src_vreg;
	struct vreg		*src_parent_struct;
	struct vreg		*dest_parent_struct;
	struct reg		*src_ptr_preg;
	struct reg		*dest_ptr_preg;
	void			*dat;
	struct icode_instr	*next;
	unsigned long		seqno;

	/*
	 * 01/18/09: Append sequence number. This can give us a rough estimate
	 * of the distance between two icode instructions. E.g. a list of
	 *
	 *    LOAD; ADD; STORE
	 *
	 * will have append sequence numbers N, N+1 and N+2. Currently we only
	 * use this on PPC to decide whether a branch instruction requires an
	 * indirect branch because the target label (which is also an icode
	 * instruction) is too far away. The indirect branch case is more
	 * expensive in code size and performance, so we wish to avoid it if 
	 * possible).
	 *
	 * Note that this is imprecise for two reasons:
	 *
	 *     - An icode instruction may map to multiple target instructions,
	 * the number of which is not taken into account (e.g. it may expand to
	 * a memcpy() call with some preparations, or it may be a list of inline
	 * asm instructions)
	 *
	 *     - There is a possibility for sequence numbers to get mixed up
	 * when icode lists are merged in reverse order (but this possibly rarely
	 * or never happens currently)
	 */
	unsigned long		append_seqno;
	int			hints; /* 06/02/08 */
#define HINT_INSTR_NEXT_NOT_SECOND_LLONG_WORD	1
#define HINT_INSTR_GENERIC_MODIFIER		(1 << 1) /* instruction-specific */
	/*
	 * 01/29/08: Specifically requests an unsigned variant of the instruction.
	 * This is currently only used for PPC32 long long comparison; The less
	 * significant word has to be compared unsigned. This worked without a
	 * hint on x86 because the signed/unsigned distinction is done for the
	 * branch, not cmp.
	 *
	 * XXX Maybe there is a better way to fix it (e.g. change vregs of cmp
	 * to unsigned)
	 */
#define HINT_INSTR_UNSIGNED			(1 << 2)
#define HINT_INSTR_RENAMED			(1 << 3)
};

struct icode_list {
	struct icode_instr	*head;
	struct icode_instr	*tail;
	struct vreg		*res;
};

unsigned long
get_label_count(void);

int
icode_list_length(struct icode_list *);

void
boolify_result(struct vreg *vr, struct icode_list *il);

int
pro_mote(struct vreg **, struct icode_list *, int eval);

/*
 * Functions for creating icode instructions
 */

void
icode_make_copystruct(
	struct vreg *dest,
	struct vreg *src,
	struct icode_list *il);

void
icode_make_putstructregs(
	struct reg *dest,
	struct reg *ptrreg,
	struct vreg *src,
	struct icode_list *il);

struct int_memcpy_data {
	struct reg	*dest_addr;
	struct reg	*src_addr;
	struct reg	*nbytes;
	struct reg	*temp_reg;
	int		type;  /* BUILTIN_MEMCPY or _MEMSET */
};

void
icode_make_intrinsic_memcpy_or_memset(
	int type,  /* BUILTIN_MEMCPY or _MEMSET */	
	struct vreg *dest,
	struct vreg *src,
	struct vreg *nbytes,
	int may_call_lib,
	struct icode_list *il);

void
icode_make_alloca(struct reg *r, struct vreg *size_vr,
	struct stack_block *sb,
	struct icode_list *il);

void
icode_make_alloc_vla(struct stack_block *vla,
	struct icode_list *il);


void
icode_make_put_vla_size(struct reg *size, struct stack_block *sb, int idx,
	struct icode_list *il);	

void
icode_make_put_vla_whole_size(struct reg *size, struct stack_block *sb,
		        struct icode_list *il);

struct vreg *
icode_make_retr_vla_size(struct reg *size, struct stack_block *sb, int idx,
	struct icode_list *il);	


struct builtinframeaddressdata {
	struct reg	*result_reg;
	struct reg	*temp_reg;
	size_t		*count;
};

void
icode_make_builtin_frame_address(struct reg *r, struct reg *r2, size_t *n,
	struct icode_list *il);	

void
icode_make_allocstack(struct vreg *vr, size_t size, struct icode_list *il);

void
icode_make_copyreg(struct reg *dest, struct reg *src,
		struct type *desttype, struct type *srctype,
		struct icode_list *il);

struct icode_instr *
icode_make_setreg(struct reg *r, int value);

struct icode_instr *
icode_make_neg(struct vreg *vr);

void
icode_make_xchg(struct reg *r1, struct reg *r2, struct icode_list *il);

struct icode_instr *
icode_make_branch(struct icode_instr *dest, int btype, struct vreg *vr);

struct icode_instr *
icode_make_jump(struct icode_instr *label);

struct icode_instr *
icode_make_sub(struct vreg *dest, struct vreg *src);

struct icode_instr *
icode_make_add(struct vreg *dest, struct vreg *src);

struct icode_instr *
icode_make_mul(struct vreg *dest, struct vreg *src);

struct icode_instr *
icode_make_div(struct vreg *dest, struct vreg *src);

struct icode_instr *
icode_make_shl(struct vreg *dest, struct vreg *src);

struct icode_instr *
icode_make_shr(struct vreg *dest, struct vreg *src);

struct icode_instr *
icode_make_mod(struct vreg *dest, struct vreg *src);

struct icode_instr *
icode_make_or(struct vreg *dest, struct vreg *src);

void
icode_make_preg_or(struct reg *dest, struct reg *src, struct icode_list *il);

struct icode_instr *
icode_make_and(struct vreg *dest, struct vreg *src);

struct icode_instr *
icode_make_indir(struct reg *);

void
icode_make_copyinit(struct decl *, struct icode_list *);

void
icode_make_asm(struct inline_asm_stmt *, struct icode_list *);

struct icode_instr *
icode_make_store_indir(struct vreg *dest, struct vreg *src);

struct icode_instr *
icode_make_call(const char *name);


struct icode_instr *
icode_make_call_indir(struct reg *r);

struct icode_instr *
icode_make_push(struct vreg *vr, struct icode_list *il);

struct icode_instr *
icode_make_xor(struct vreg *dest, struct vreg *src);

struct icode_instr *
icode_make_not(struct vreg *vr);

struct icode_instr *
icode_make_setitem(struct vreg *vr);

struct icode_instr *
icode_make_propvreg(struct vreg *vr);

void
icode_make_load(struct reg *r, struct vreg *parent_struct,
	int is_not_first_load, struct icode_list *il);

void
icode_make_load_addrlabel(struct reg *r, struct icode_instr *label,
	struct icode_list *il);

extern int	unimpl_instr;

void
icode_make_unimpl(struct icode_list *il);

void
icode_make_debug(struct icode_list *il, const char *msg, ...);


struct statement;

void
icode_make_dbginfo_line(struct statement *stmt, struct icode_list *il);

void
do_add_sub(struct vreg **left, struct vreg **right,
	int op, struct token *optok, struct icode_list *il, int eval);

void
icode_make_store(struct function *f, struct vreg *dest,
	struct vreg *src, struct icode_list *il);

struct icode_instr *
icode_make_freestack(size_t bytes);

struct icode_instr *
icode_make_adj_allocated(int bytes);

#if 0
struct icode_instr *
icode_make_seqpoint(struct var_access *stores);
#endif

struct icode_instr *
icode_make_indir(struct reg *r);

struct reg * /*icode_instr **/
icode_make_addrof(struct reg *r, struct vreg *vr, struct icode_list *il);

struct icode_instr *
icode_make_inc(struct vreg *vr);

struct icode_instr *
icode_make_dec(struct vreg *vr);

struct icode_instr *
icode_make_label(const char *tmpl);
		
struct icode_instr *
icode_make_ret(struct vreg *vr);

struct icode_instr *
icode_make_cmp(struct vreg *dest, struct vreg *src);

void
icode_make_initialize_pic(struct function *f, struct icode_list *il);

void
icode_make_extend_sign(struct vreg *vr, struct type *to, struct type *from,
	struct icode_list *il);

void
icode_make_conv_fp(struct reg *destr, struct reg *srcr, struct type *to,
	struct type *from,
	struct icode_list *il);

void
icode_make_conv_to_ldouble(struct vreg *dest, struct vreg *src,
	struct icode_list *il);

void
icode_make_conv_from_ldouble(struct vreg *dest, struct vreg *src,
	struct icode_list *il);

void
icode_make_x86_fxch(struct reg *r, struct reg *r2, struct icode_list *il);

void
icode_make_x86_ffree(struct reg *r, struct icode_list *il);

void
icode_make_x86_store_x87cw(struct vreg *vr, struct icode_list *il);

void
icode_make_x86_load_x87cw(struct vreg *vr, struct icode_list *il);

void
icode_make_x86_cdq(struct icode_list *il);

struct filddata {
	struct reg	*r;
	struct vreg	*vr;
};

struct fistdata {
	struct reg	*r;
	struct reg	*r2;
	struct vreg	*vr;
	struct type	*target_type;
};

void
icode_make_comp_goto(struct reg *addr, struct icode_list *il);

struct stack_block *
icode_alloc_reg_stack_block(struct function *, size_t bytes);

void
icode_make_x86_fild(struct reg *r, struct vreg *vr, struct icode_list *il);

void
icode_make_x86_fist(struct reg *r, struct vreg *vr, struct type *ty,
	struct icode_list *il);

void
icode_make_mips_mtc1(struct reg *dest, struct vreg *src,
struct icode_list *il);

void
icode_make_mips_cvt(struct vreg *dest, struct vreg *src,
struct icode_list *il);

void
icode_make_mips_trunc(struct vreg *dest, struct vreg *src,
struct icode_list *il);

void
icode_make_mips_make_32bit_mask(struct reg *r, struct icode_list *il);

void
icode_make_mips_mfc1(struct reg *dest, struct vreg *src,
struct icode_list *il);

void
icode_make_power_srawi(struct reg *dest, struct reg *src, int bits,
struct icode_list *il); 

void
icode_make_power_rldicl(struct reg *dest, struct reg *src, int bits,
struct icode_list *il);

void
icode_make_power_fcfid(struct reg *dest, struct reg *src,
struct icode_list *il);		

void
icode_make_power_frsp(struct reg *dest, struct icode_list *il);

void
icode_make_power_rlwinm(struct reg *dest, struct reg *src, int value,
struct icode_list *il);

void
icode_make_power_slwi(struct reg *dest, struct reg *src, int bits,
struct icode_list *il);

void
icode_make_power_extsb(struct reg *dest, struct icode_list *il);

void
icode_make_power_extsh(struct reg *dest, struct icode_list *il);

void
icode_make_power_extsw(struct reg *dest, struct icode_list *il);

void
icode_make_power_xoris(struct reg *, int, struct icode_list *il);

void
icode_make_power_lis(struct reg *, int, struct icode_list *il);

void
icode_make_power_loadup4(struct reg *r, struct vreg *vr,
struct icode_list *il);

void
icode_make_power_fctiwz(struct reg *r, int for_unsigned, struct icode_list *il);

void
icode_make_power_extsh(struct reg *r, struct icode_list *il);

void
icode_make_amd64_cvtsi2sd(struct reg *dest, struct vreg *src,
struct icode_list *il);

void
icode_make_amd64_cvtsi2ss(struct reg *dest, struct vreg *src,
struct icode_list *il);

void
icode_make_amd64_cvttsd2si(struct reg *dest, struct reg *src,
struct icode_list *il);

void
icode_make_amd64_cvttsd2siq(struct reg *dest, struct reg *src,
struct icode_list *il);

void
icode_make_amd64_cvttss2si(struct reg *dest, struct reg *src,
struct icode_list *il);

void
icode_make_amd64_cvttss2siq(struct reg *dest, struct reg *src,
struct icode_list *il);

void
icode_make_amd64_cvtsd2ss(struct reg *r, struct icode_list *il);

void
icode_make_amd64_cvtss2sd(struct reg *r, struct icode_list *il);

void
icode_make_amd64_cvtsi2sdq(
	struct reg *dest,
	struct vreg *src,
	struct icode_list *il);

void
icode_make_amd64_cvtsi2ssq(
	struct reg *dest,
	struct vreg *src,
	struct icode_list *il);


void
icode_make_amd64_load_negmask(struct reg *dest, struct reg *support, int for_double,
	struct icode_list *il);

void
icode_make_amd64_ulong_to_float(struct reg *src_gpr, struct reg *temp,
	struct reg *dest_sse_reg, int is_double, struct icode_list *il); 

void
icode_make_amd64_xorps(struct vreg *dest, struct reg *r, struct icode_list *);
void
icode_make_amd64_xorpd(struct vreg *dest, struct reg *r, struct icode_list *);

void
icode_make_sparc_load_int_from_ldouble(struct reg *, struct vreg *,
	struct icode_list *il);	

struct icode_instr *
icode_make_writeback(struct function *f, struct vreg *vr);

struct type *
promote(struct vreg **left, struct vreg **right, 
	int op0, struct token *optok, struct icode_list *il, int eval);


void init_to_icode(struct decl *d, struct icode_list *il);

int
emul_conv_ldouble_to_double(struct vreg **temp_lres,
	struct vreg **temp_rres,
	struct vreg *lres,
	struct vreg *rres,
	struct icode_list *ilp,
	int eval);

void
icode_align_ptr_up_to(struct vreg *ptr,
			int target_alignment,
			int addend,
			struct icode_list *il);

/*
 * Compile an expression to icode
 */
struct vreg *
expr_to_icode(struct expr *e, struct vreg *lvalue, struct icode_list *il,
		int purpose, int resval_not_used, int eval);

struct vreg *
fcall_to_icode(struct fcall_data *, struct icode_list *, struct token *t,
		int eval);

struct stack_block *
vla_decl_to_icode(struct type *ty, struct icode_list *il);

struct icode_instr *
compare_vreg_with_zero(struct vreg *vr, struct icode_list *il);

/*
 * Functions to manipulate icode lists
 */
struct icode_list *
alloc_icode_list(void);



void	
append_icode_list(struct icode_list *, struct icode_instr *);

void
merge_icode_lists(struct icode_list *dest, struct icode_list *src);

void
put_label_scope(struct label *l);

void
put_expr_scope(struct expr *e);

void
put_ctrl_scope(struct control *c);

struct icode_list *
ctrl_to_icode(struct control *);

struct icode_list *
xlate_to_icode(struct statement *slist, int inv_gprs_first);

void
xlate_func_to_icode(struct function *func);

struct icode_instr *
copy_icode_instr(struct icode_instr *);

struct vreg *
promote_bitfield(struct vreg *vr, struct icode_list *il);

void
mask_source_for_bitfield(struct type *ty, struct vreg *vr,
		struct icode_list *il, int for_reading);

void
load_and_decode_bitfield(struct vreg **, struct icode_list *);


void
decode_bitfield(struct type *ty, struct vreg *vr, struct icode_list *il);


void
write_back_bitfield(struct vreg *destvr, struct vreg *lres,
		struct type *desttype, struct icode_list *il);

void
write_back_bitfield_by_assignment(struct vreg *lres, struct vreg *rres,
		struct icode_list *ilp);

void
add_const_to_vreg(struct vreg *valist_vr, int size, struct icode_list *il);


#endif

