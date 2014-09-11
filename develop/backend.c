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
 *
 * A grabbag of function for managing the backend, and also for backend stuff
 * that is generic across two or more platforms
 */
#include "backend.h"
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include "error.h"
#include "icode.h"
#include "decl.h"
#include "functions.h"
#include "expr.h"
#include "attribute.h"
#include "debug.h"
#include "token.h"
#include "scope.h"
#include "misc.h"
#include "type.h"
#include "features.h"
#include "cc1_main.h"
#include "typemap.h"
#include "symlist.h"
#include "n_libc.h"

struct backend		*backend;
struct emitter		*emit;
struct reg		*tmpgpr;
struct reg		*tmpgpr2;
struct reg		*tmpfpr;

/*
 * 02/15/08: New: Register for PIC pointer. This is optional and currently
 * only used by SPARC
 * 02/02/09: ... and now for PPC as well
 */
struct reg		*pic_reg;
int			host_endianness;
char			*tunit_name;
size_t			tunit_size;
struct init_with_name	*init_list_head;
struct init_with_name	*init_list_tail;
extern int		archflag;
extern int		abiflag;

extern int		osx_call_renamed; /* XXX botch */

int                     backend_warn_inv;

struct stupidtrace_entry *stupidtrace_list_head;
struct stupidtrace_entry *stupidtrace_list_tail;

int
init_backend(FILE *fd, struct scope *s) {
	unsigned int	foo = 123;

	if (*(unsigned char *)&foo == 123) {
		host_endianness = ENDIAN_LITTLE;
	} else {
		/* Naive - doesn't work with PDP endianness */
		host_endianness = ENDIAN_BIG;
	}
	/*backend = &x86_backend;*/

#if 0
#ifdef __i386__	
	backend = &x86_backend;
#elif defined __amd64__
	backend = &amd64_backend;
#elif defined __sgi
	backend = &mips_backend;
#elif defined _AIX
	backend = &power_backend;
#else
#error "Unsupported target architecture"
#endif	
#endif
	switch (archflag) {
	case ARCH_X86:
		backend = &x86_backend;
		break;
	case ARCH_AMD64:
		backend = &amd64_backend;
		break;
	case ARCH_POWER:
		backend = &power_backend;
		break;
	case ARCH_MIPS:
		backend = &mips_backend;
		break;
	case ARCH_SPARC:
		backend = &sparc_backend;
		break;
	default:
		puts("UNKNOWN ARCHITECTURE!!!!!!!!!!");
		abort();
	}
	backend->abi = abiflag;

/*backend = &power_backend;*/

	return backend->init(fd, s);
}


struct vreg *
get_parent_struct(struct vreg *vr) {
	struct vreg	*vr2 = vr;

	while (vr2->parent != NULL) {
		if (vr2->from_ptr) break;
		vr2 = vr2->parent;
	}
	return vr2;
}	


struct vreg *	
check_need_indirect_loadstore(struct icode_instr *ip) {
	struct stack_block	*sb = NULL;
	unsigned long		offset = 0;

	/*
	 * The instruction has a support register... Is it
	 * needed?
	 */
	if (ip->src_vreg->stack_addr) {
		sb = ip->src_vreg->stack_addr;
	} else if (ip->src_vreg->var_backed
		&& ip->src_vreg->var_backed->stack_addr) {
		sb = ip->src_vreg->var_backed->stack_addr;
	} else if (ip->src_parent_struct
		&& ip->src_parent_struct->var_backed
		&& ip->src_parent_struct->var_backed->stack_addr) {
		sb = ip->src_parent_struct->var_backed->stack_addr;
		offset = calc_offsets(ip->src_vreg);
	}

	if (sb != NULL) {
		if (sb->offset + offset > (unsigned long)backend->max_displacement) {
			struct vreg	*ptr;
			struct vreg	*parent = NULL;

			if (ip->src_vreg->parent != NULL) {
				parent = get_parent_struct(
					ip->src_vreg);
			}
			
			/*
			 * Maximum offset exceeded! We have to
			 * indirect
			 */
			emit->addrof(ip->dat, ip->src_vreg,
				parent);
			ptr = vreg_back_by_ptr(ip->src_vreg,
				ip->dat, 1);
			return ptr;
		}
	}
	return NULL;
}


size_t
calc_align_bytes(size_t offset, struct type *curtype, struct type *nexttype, int struct_member) {
	size_t	cursize;
	size_t	alignto;
	size_t	oldoffset;

	/* 10/08/08: Caller handles bitfield size addition! */
	if (curtype->tbit == NULL) {
		cursize = backend->get_sizeof_type(curtype, NULL);
		offset += cursize;
	}

	if (struct_member && botch_x86_alignment(nexttype)) {
		alignto = 4;
	} else {
		alignto = backend->get_align_type(nexttype);
	}

	oldoffset = offset;

	while (offset % alignto) {
		++offset;
	}
	return offset - oldoffset;
}

static void
map_pregs(struct vreg *vr, struct reg **pregs) {
	if (vr != NULL) {
		if (pregs && pregs[0]) {
			backend_vreg_map_preg(vr, pregs[0]);
			if (vr->is_multi_reg_obj && pregs[1]) {
				backend_vreg_map_preg2(vr, pregs[1]);
			} else {
				vr->pregs[1] = NULL;
			}
		} else {
			vr->pregs[0] = NULL;
		}	
	}
}

static void
unmap_pregs(struct vreg *vr, struct reg **pregs) {
	if (vr != NULL) {
		if (pregs && pregs[0]) {
			backend_vreg_unmap_preg(pregs[0]);
			if (vr->is_multi_reg_obj && pregs[1]) {
				backend_vreg_unmap_preg2(pregs[1]);
			}
		}	
	}
}

int
do_xlate(
	struct function *f, 
	struct icode_instr **ipp) {

	struct icode_instr	*ip = *ipp;
	struct stack_block	*sb;
	struct allocstack	*as;
	int			found;

	map_pregs(ip->src_vreg, ip->src_pregs);
	map_pregs(ip->dest_vreg, ip->dest_pregs);
	if (ip->src_parent_struct) {
		if (ip->src_ptr_preg) {
			/*
			 * 08/05/07: UNBELIEVABLE! This used to map the
			 * register to the parent struct vreg, but not to
			 * the parent struct from_ptr vreg, whree it would
			 * have belonged! That one is used by the emitters,
			 * so stuff broke
			 */
			backend_vreg_map_preg(ip->src_parent_struct->from_ptr,
					ip->src_ptr_preg);
		}
	} else if (ip->src_ptr_preg) {
		backend_vreg_map_preg(ip->src_vreg->from_ptr, ip->src_ptr_preg);
	}
	if (ip->dest_parent_struct) {
		if (ip->dest_ptr_preg) {
			/*
			 * 08/05/07: See comment above about source parent
			 * pointer
			 */
			backend_vreg_map_preg(ip->dest_parent_struct->from_ptr,
				ip->dest_ptr_preg);
		}
	} else if (ip->dest_ptr_preg) {
		backend_vreg_map_preg(ip->dest_vreg->from_ptr, ip->dest_ptr_preg);
	}
	
	switch (ip->type) {
	case INSTR_CALL:
		if (ip->hints & HINT_INSTR_RENAMED) {
			osx_call_renamed = 1;
		}
		emit->call(ip->dat);
		osx_call_renamed = 0;
		break;
	case INSTR_CALLINDIR:
		emit->callindir(ip->dat);
		break;
	case INSTR_PUSH:
		emit->push(f, ip);
		break;
	case INSTR_ALLOCSTACK:
		/*
		 * XXX freestack updates total_allocated but allocstack
		 * doesn't. Is this good?
		 */
		as = ip->dat;
		emit->allocstack(f, as->nbytes);
		f->total_allocated += as->nbytes;
		if (as->patchme != NULL) {
			/* The space is actually used! */
			sb = make_stack_block(f->total_allocated, as->nbytes);
			as->patchme->var_backed->stack_addr = sb;
		}	
		break;
	case INSTR_FREESTACK:
		emit->freestack(f, ip->dat);
		break;
	case INSTR_ADJ_ALLOCATED:
		emit->adj_allocated(f, ip->dat);
		break;
	case INSTR_INDIR:
		break;
	case INSTR_MOV:
		emit->mov(ip->dat);
		break;
	case INSTR_SETREG:
		emit->setreg(ip->src_pregs[0], (int *)ip->dat);
		break;
	case INSTR_ADDROF: {
			struct vreg	*vr = NULL;

			if (ip->src_vreg != NULL
				&& ip->src_vreg->parent != NULL) {
				vr = get_parent_struct(ip->src_vreg);
			}

			emit->addrof(ip->dat, ip->src_vreg, vr);
		}
		break;
	case INSTR_INITIALIZE_PIC:
		emit->initialize_pic(ip->dat);
		break;
	case INSTR_INC:
		emit->inc(ip);
		break;
	case INSTR_DEC:
		emit->dec(ip);
		break;
	case INSTR_LABEL:
		/* generated labels are always local */
		emit->label(ip->dat, 0);
		break;
	case INSTR_NEG:
		emit->neg(ip->src_pregs, ip);
		break;
	case INSTR_SUB:
		emit->sub(ip->dest_pregs, ip);
		break;
	case INSTR_ADD:
		emit->add(ip->dest_pregs, ip);
		break;
	case INSTR_MUL:
		emit->mul(ip->dest_pregs, ip);
		break;
	case INSTR_DIV:
		emit->div(ip->dest_pregs, ip, 0);
		break;
	case INSTR_MOD:
		emit->mod(ip->dest_pregs, ip);
		break;
	case INSTR_SHL:
		emit->shl(ip->dest_pregs, ip);
		break;
	case INSTR_SHR:
		emit->shr(ip->dest_pregs, ip);
		break;
	case INSTR_OR:
		emit->or(ip->dest_pregs, ip);
		break;

	case INSTR_PREG_OR:
		emit->preg_or(ip->dest_pregs, ip);
		break;

	case INSTR_AND:
		emit->and(ip->dest_pregs, ip);
		break;
	case INSTR_XOR:
		emit->xor(ip->dest_pregs, ip);
		break;
	case INSTR_NOT:
		emit->not(ip->src_pregs, ip);
		break;
	case INSTR_RET:
		backend->do_ret(f, ip);
		break;
	case INSTR_SEQPOINT:
		break;
	case INSTR_STORE:
		/* XXX confusingly messed up order of args */
#if FEAT_DEBUG_DUMP_BOGUS_STORES
		if (ip->dest_vreg->stack_addr != NULL) {
			struct icode_instr      *tmp;
			struct stack_block      *sb = ip->dest_vreg->stack_addr;

			for (tmp = ip->next; tmp != NULL; tmp = tmp->next) {
				if (tmp->src_vreg
					&& tmp->src_vreg->stack_addr == sb) {
					if (tmp->type == INSTR_LOAD) {
						break;
					} else {
						emit->comment("used by ? as src");
					}
				} else if (tmp->dest_vreg
					&& tmp->dest_vreg->stack_addr == sb) {
					emit->comment("used by ? as dest");
				}
			}
			if (tmp == NULL) {
				emit->comment("seems unneeded");
			}
		}
#endif
		if (ip->dat != NULL) {
			struct vreg	*ptrvr;

			ptrvr = check_need_indirect_loadstore(ip);
			if (ptrvr != NULL) {
				emit->store(ptrvr, ip->dest_vreg);
				if (ip->src_vreg->is_multi_reg_obj) {
					emit->store(ptrvr, ip->dest_vreg);
				}
				break;
			}
		}
		emit->store(ip->src_vreg, ip->dest_vreg);
		if (ip->src_vreg->is_multi_reg_obj) {
			emit->store(ip->src_vreg, ip->dest_vreg);
		}
		break;
	case INSTR_WRITEBACK:
		emit->store(ip->src_vreg, ip->src_vreg);
		break;
	case INSTR_LOAD:
		if (ip->dat != NULL) {
			struct vreg	*ptrvr;

			ptrvr = check_need_indirect_loadstore(ip);
			if (ptrvr != NULL) {
				emit->load(ip->src_pregs[0], ptrvr);
				break;
			}
		}
		emit->load(ip->src_pregs[0], ip->src_vreg);
		break;
	case INSTR_LOAD_ADDRLABEL:
		emit->load_addrlabel(ip->dest_pregs[0], ip->dat);
		break;
	case INSTR_COMP_GOTO:
		emit->comp_goto(ip->dat);
		break;
	case INSTR_DEBUG:
		emit->comment(ip->dat);
		break;
	case INSTR_DBGINFO_LINE:
		emit->dwarf2_line(ip->dat);
		break;
	case INSTR_UNIMPL:
		emit->genunimpl();
		break;
	case INSTR_CMP:
		emit->cmp(ip->dest_pregs, ip);
		break;
	case INSTR_EXTEND_SIGN:
		emit->extend_sign(ip);
		break;
	case INSTR_CONV_FP:
		emit->conv_fp(ip);
		break;
	case INSTR_CONV_FROM_LDOUBLE:
		emit->conv_from_ldouble(ip);
		break;
	case INSTR_CONV_TO_LDOUBLE:
		emit->conv_to_ldouble(ip);
		break;
	case INSTR_COPYINIT:
		emit->copyinit(ip->dat);
		break;
	case INSTR_PUTSTRUCTREGS:
		emit->putstructregs(ip->dat);
		break;
	case INSTR_COPYSTRUCT:
		emit->copystruct(ip->dat);
		break;
	case INSTR_INTRINSIC_MEMCPY:
		emit->intrinsic_memcpy(ip->dat);
		break;
	case INSTR_ALLOCA: {
			struct allocadata	*ad = ip->dat;
			static struct vreg	vr;

			/*
			 * XXXXXXX 08/27/07 This sucks:
			 *  - alloca_ should be renamed alloca
			 *  - The store below saves some code
			 *    duplication (which is why it's here),
			 *    but makes it difficult to find!!! This
			 *    should go into icode_make_alloca()
			 *
			 * Was wrong because:
			 *
			 *  - Making the register anonymous and
			 *    type-less breaks on systems where the
			 *    ABI uses pointers of different size
			 *    than GPRs; e.g. MIPS/N32. Now we use
			 *    make_void_ptr_type() instead
			 */
			emit->alloca_(ad);

			/* Now save the result pointer */
			vr.stack_addr = ad->addr;
			vr.type = make_void_ptr_type();
			vr.size = backend->get_sizeof_type(vr.type, NULL);
			backend_vreg_map_preg(&vr, ad->result_reg);
			emit->store(&vr, &vr);
			backend_vreg_unmap_preg(ad->result_reg);
		}
		break;
	case INSTR_DEALLOCA:
		emit->dealloca(ip->dat, ip->src_pregs[0]);
		break;
	case INSTR_ALLOC_VLA:
		emit->alloc_vla(ip->dat);
		break;
	case INSTR_DEALLOC_VLA:
		emit->dealloc_vla(ip->dat, NULL);
		break;
	case INSTR_PUT_VLA_SIZE:
		emit->put_vla_size(ip->dat);
		break;
	case INSTR_RETR_VLA_SIZE:
		emit->retr_vla_size(ip->dat);
		break;
	case INSTR_LOAD_VLA:
		emit->load_vla(ip->dest_pregs[0],
			((struct type *)ip->dat)->vla_addr);
		break;
	case INSTR_BUILTIN_FRAME_ADDRESS:
		emit->frame_address(ip->dat);
		break;
	case INSTR_ASM:
		emit->inlineasm(ip->dat);
		break;
	case INSTR_BR_EQUAL:
	case INSTR_BR_NEQUAL:
	case INSTR_BR_GREATER:
	case INSTR_BR_SMALLER:
	case INSTR_BR_GREATEREQ:
	case INSTR_BR_SMALLEREQ:
	case INSTR_JUMP:
		emit->branch(ip);
		break;
	case INSTR_XCHG:
		emit->xchg(ip->src_pregs[0], ip->dest_pregs[0]);
		break;
	default:
		/* Must be machine specific */
		found = 1; /* XXX uh-huh should this not be 0? :( */
		if (backend->arch == ARCH_X86
			|| backend->arch == ARCH_AMD64) {
			switch (ip->type) {
			case INSTR_X86_FXCH:
				emit_x86->fxch(ip->dest_pregs[0],
					ip->src_pregs[0]);
				break;
			case INSTR_X86_FFREE:
				emit_x86->ffree(ip->src_pregs[0]);
				break;
			case INSTR_X86_FNSTCW:
				emit_x86->fnstcw(ip->src_vreg);
				break;
			case INSTR_X86_FLDCW:	
				emit_x86->fldcw(ip->src_vreg);
				break;
			case INSTR_X86_CDQ:
				emit_x86->cdq();
				break;
			case INSTR_X86_FIST:
				emit_x86->fist((struct fistdata *)ip->dat);
				break;
			case INSTR_X86_FILD:
				emit_x86->fild((struct filddata *)ip->dat);
				break;
			case INSTR_AMD64_CVTTSS2SI:
				emit_amd64->cvttss2si(ip);
				break;
			case INSTR_AMD64_CVTTSS2SIQ:
				emit_amd64->cvttss2siq(ip);
				break;
			case INSTR_AMD64_CVTTSD2SI:
				emit_amd64->cvttsd2si(ip);
				break;
			case INSTR_AMD64_CVTTSD2SIQ:
				emit_amd64->cvttsd2siq(ip);
				break;
			case INSTR_AMD64_CVTSI2SD:
				emit_amd64->cvtsi2sd(ip);
				break;
			case INSTR_AMD64_CVTSI2SS:
				emit_amd64->cvtsi2ss(ip);
				break;
			case INSTR_AMD64_CVTSI2SDQ:
				emit_amd64->cvtsi2sdq(ip);
				break;
			case INSTR_AMD64_CVTSI2SSQ:
				emit_amd64->cvtsi2ssq(ip);
				break;
			case INSTR_AMD64_CVTSD2SS:
				emit_amd64->cvtsd2ss(ip);
				break;
			case INSTR_AMD64_CVTSS2SD:
				emit_amd64->cvtss2sd(ip);
				break;
			case INSTR_AMD64_LOAD_NEGMASK:
				emit_amd64->load_negmask(ip);
				break;
			case INSTR_AMD64_XORPS:
				emit_amd64->xorps(ip);
				break;
			case INSTR_AMD64_XORPD:
				emit_amd64->xorpd(ip);
				break;
			case INSTR_AMD64_ULONG_TO_FLOAT:
				if (backend->arch == ARCH_AMD64) {
					emit_amd64->ulong_to_float(ip);
				} else {
					emit_x86->ulong_to_float(ip);
				}
				break;
			default:
				found = 0;
			}
		} else if (backend->arch == ARCH_POWER) {
			switch (ip->type) {
			case INSTR_POWER_SRAWI:
				emit_power->srawi(ip);
				break;
			case INSTR_POWER_RLDICL:
				emit_power->rldicl(ip);
				break;
			case INSTR_POWER_FCFID:
				emit_power->fcfid(ip);
				break;
			case INSTR_POWER_FRSP:
				emit_power->frsp(ip);
				break;
			case INSTR_POWER_RLWINM:
				emit_power->rlwinm(ip);
				break;
			case INSTR_POWER_SLWI:
				emit_power->slwi(ip);
				break;
			case INSTR_POWER_EXTSB:
				emit_power->extsb(ip);
				break;
			case INSTR_POWER_EXTSH:
				emit_power->extsh(ip);
				break;
			case INSTR_POWER_EXTSW:
				emit_power->extsw(ip);
				break;
			case INSTR_POWER_XORIS:
				emit_power->xoris(ip);
				break;
			case INSTR_POWER_LIS:
				emit_power->lis(ip);
				break;
			case INSTR_POWER_LOADUP4:
				emit_power->loadup4(ip);
				break;
			case INSTR_POWER_FCTIWZ:
				emit_power->fctiwz(ip);
				break;
			default:
				found = 0;
			}
		} else if (backend->arch == ARCH_MIPS) {
			switch (ip->type) {
			case INSTR_MIPS_MFC1:
				emit_mips->mfc1(ip);
				break;
			case INSTR_MIPS_MTC1:
				emit_mips->mtc1(ip);
				break;
			case INSTR_MIPS_CVT:
				emit_mips->cvt(ip);
				break;
			case INSTR_MIPS_TRUNC:
				emit_mips->trunc(ip);
				break;
			case INSTR_MIPS_MAKE_32BIT_MASK:
				emit_mips->make_32bit_mask(ip);
				break;
			default:
				found = 0;
			}
		} else if (backend->arch == ARCH_SPARC) {
			switch (ip->type) {
			case INSTR_SPARC_LOAD_INT_FROM_LDOUBLE:	
				emit_sparc->load_int_from_ldouble(ip);
				break;
			default:
				found = 0;
				break;
			}
		}
		if (found) {
			break;
		}
		printf("Unknown instruction - %d\n", ip->type);
		return -1;
	}

#if XLATE_IMMEDIATELY
	unmap_pregs(ip->src_vreg, ip->src_pregs);
	unmap_pregs(ip->dest_vreg, ip->dest_pregs);
	if (ip->src_parent_struct) {
		if (ip->src_ptr_preg) {
			/*
			 * 08/05/07: UNBELIEVABLE! This used to map the
			 * register to the parent struct vreg, but not to
			 * the parent struct from_ptr vreg, whree it would
			 * have belonged! That one is used by the emitters,
			 * so stuff broke
			 */
			backend_vreg_unmap_preg(
					ip->src_ptr_preg);
		}
	} else if (ip->src_ptr_preg) {
		backend_vreg_unmap_preg(ip->src_ptr_preg);
	}
	if (ip->dest_parent_struct) {
		if (ip->dest_ptr_preg) {
			/*
			 * 08/05/07: See comment above about source parent
			 * pointer
			 */
			backend_vreg_unmap_preg(
				ip->dest_ptr_preg);
		}
	} else if (ip->dest_ptr_preg) {
		backend_vreg_unmap_preg(ip->dest_ptr_preg);
	}
#endif /* XLATE_IMMEDIATELY */
	return 0;
}

int
xlate_icode(
	struct function *f,
	struct icode_list *ilp,
	struct icode_instr **lastret) {
	struct icode_instr	*ip;

	if (ilp == NULL) {
		/* Empty function */
		return 0;
	}
	for (ip = ilp->head; ip != NULL; ip = ip->next) {
		if (do_xlate(f, &ip) != 0) {
			return -1;
		}
		if (ip->type == INSTR_RET) {
			*lastret = ip;
		}
	}
	return 0;
}

/*
 * For designated initializers: Allocate static designated initializer
 * and initialize it with init
 */
struct vreg *
vreg_static_alloc(struct type *ty, struct initializer *init) {
	struct vreg		*ret;
	static struct decl	dec;
	struct decl		*decp;
	struct decl		*dummy[2];

	(void) backend->get_sizeof_type(ty, NULL); /* XXX 12/07/24: needed? */
	dec.dtype = ty;

	decp = n_xmemdup(&dec, sizeof dec);
	decp->init = init;

	ret = vreg_alloc(decp, NULL, NULL, NULL);

	decp->dtype = n_xmemdup(decp->dtype, sizeof *decp->dtype);
	decp->dtype->is_func = 0;
	decp->dtype->storage = 0;
	decp->dtype->name = NULL;
	dummy[0] = decp;
	dummy[1] = NULL;
	store_decl_scope(curscope, dummy);

	return ret;
}

struct vreg *
vreg_stack_alloc(struct type *ty, struct icode_list *il, int on_frame,
	struct initializer *init) {

	struct vreg		*ret;
	static struct decl	dec;
	struct decl		*decp;
	size_t			size;

	size = backend->get_sizeof_type(ty, NULL);
	dec.dtype = ty;

	decp = n_xmemdup(&dec, sizeof dec);
	ret = vreg_alloc(decp, NULL, NULL, NULL);
	/*
	 * 08/18/07: The old icode_make_allocstack() solution was
	 * only suitable for allocating stack at the current stack
	 * pointer position. This is inadequate for allocating
	 * temporary storage like anonymous structs
	 */
	if (!on_frame) {
		/*
		 * Allocate at current stack pointer position
		 */
		icode_make_allocstack(ret, size, il);
	} else {
		/*
		 * Allocate when the  stack frame is created. We can
		 * easily do this by linking the declaration on the
		 * scope list of declarations!
		 */
		struct decl	*dummy[2];

		decp->dtype = n_xmemdup(decp->dtype, sizeof *decp->dtype);
		decp->dtype->is_func = 0;
		decp->dtype->storage = 0;
		decp->dtype->name = NULL;
		dummy[0] = decp;
		dummy[1] = NULL;

		/*
		 * 08/09/08: Mark this as a declaration which was not
		 * explicitly requested (since this function is usually
		 * called for anonymous struct return buffers and
		 * compound literals). This is needed at least for
		 * statements as expressions, where such a dummy
		 * declaration shouldn't be used as value;
		 *
		 *    struct s foo();
		 *    ({ foo(); expr; })
		 *                    ^__ will append declaration for
		 *                        foo's return value here
		 */
		decp->is_unrequested_decl = 1;
		store_decl_scope(curscope, dummy);
	}
	/*
	 * Careful now if we have an initializer. Since this function is
	 * only called with initializer for compound literals, we have
	 * to generate the initializer exactly here! The slightest
	 * reordering can cause garbage results because the literal is
	 * probably part of a larger expression such as:
	 *
	 *    printf("%d\n", (struct foo){ .bar = rand() }.bar);
	 *
	 * ... here reordering may cause the call to be performed before
	 * initialization. Likewise doing the initialization too too early
	 * may cause non-constant initializers to misbehave
	 */
	if (init != NULL) {
		/*
		 * Only set init member to initilizer for call to
		 * init_to_icode(). We have to remove it again afterwards
		 * because otherwise it will be initialized again when
		 * the variable is created
		 */
		decp->init = init;

		backend->invalidate_gprs(il, 1, 0);

		/*
		 * 09/14/07: Forgot to create a vreg for the declaration!
		 * This must be done here because the declaration is not
		 * linked on the declaration list, where it would be done
		 * automatically
		 */
#if 0
		decp->vreg = vreg_alloc(decp, NULL, NULL, NULL);
		vreg_set_new_type(decp->vreg, decp->dtype);
#endif
		init_to_icode(decp, il);
		decp->init = NULL;
	}
	return ret;
}

struct initializer *
make_null_block(struct sym_entry *se,
	struct type *ty,
	struct type *struct_ty,
	int remaining) {

	struct initializer	*ret;
	size_t			size = 0;
	size_t			msize;
	size_t			align;
	struct sym_entry	*startse = se;
	int			struct_align = 1 ;
	int			start_offset = 0;

	if (struct_ty != NULL) {
		struct_align = backend->get_align_type(struct_ty);
		if (se != NULL) {
			start_offset = se->dec->offset;
		}	
	}	

	/*
	 * 07/19/08: Handle unions correctly! The caller then has to
	 * supply the remaining byte count which is needed to  pad
	 * the initializer size to the whole union size (e.g. if an
	 * int union member is initialized and the union contains a
	 * long long, 4 bytes of padding are needed).
	 */
	if (struct_ty != NULL && struct_ty->code == TY_UNION) {
		size = remaining;
	} else {	
		if (se != NULL) {
			/* Struct */
			struct decl	*last_storage_unit = NULL;

			for (; se != NULL; se = se->next) {
				/*
				 * 08/07/07: Changed this stuff, hope it's correct
				 * now
				 *
				 * 10/12/08: Handle bitfields
				 */
				if (se->has_initializer) {
					/*
					 * 10/12/08: Can (for now) only be an already initialized
					 * bitfield - skip it
					 */
					;
				} else if (se->dec->dtype->tbit != NULL) {
					/* Bitfield case - don't align */
					if (se->dec->dtype->tbit->numbits == 0) {
						/* Storage unit terminator - ignore */
						;
					} else if (se->dec->dtype->tbit->bitfield_storage_unit
						!= last_storage_unit) {
						/* Create storage unit */
						last_storage_unit = se->dec->dtype->tbit->
							bitfield_storage_unit;
						size += backend->get_sizeof_type(
							last_storage_unit->dtype, NULL);
					} else {
						/* Already handled */
						;
					}
				} else {
					align = get_struct_align_type(se->dec->dtype);

					/*
					 * 10/12/08: This was apparently completely
					 * broken, in that it incremented ``align'',
					 * not ``size''
					 */
					while ((struct_align+start_offset+size) % align) {
/*						++align;*/
						++size;
					}
				/*	size += align - orig_align;   10/12/08: Replaced with above*/
					size += backend->get_sizeof_type(se->dec->dtype, NULL);
				}
			}
		} else {
			/* Array */
			align = backend->get_align_type(ty);
			msize = backend->get_sizeof_type(ty, NULL);
	
			if (align > msize) {
				size = align * remaining;
			} else {
				size = msize * remaining;
			}
		}
	}
	/* XXX this is BORKORORENORK */

	/* 08/07/07: Removed line below, should not be necessary anymore */
#if 0
	while (size % struct_align /*backend->struct_align*/) ++size;
#endif
	if (size == 0) {
		/* 10/12/08: False alarm - No null initializer needed! */
		return NULL;
	}

	ret = alloc_initializer();
	ret->type = INIT_NULL;

	ret->data = n_xmemdup(&size, sizeof size);

	if (struct_ty != NULL) {
		ret->left_alignment = struct_align;
		ret->left_type = startse->dec->dtype;
	} else {
		ret->left_alignment = align;
	}

	return ret;
}

struct init_with_name *
make_init_name(struct initializer *init) {
	static unsigned long	count;
	char			name[128];
	struct init_with_name	*ret;

	sprintf(name, "_Agginit%lu", count++);
	ret = n_xmalloc(sizeof *ret);
	ret->name = n_xstrdup(name);
	ret->init = init;
	ret->next = NULL;

	if (init_list_head == NULL) {
		init_list_head = init_list_tail = ret;
	} else {
		init_list_tail->next = ret;
		init_list_tail = init_list_tail->next;
	}	
	return ret;
}

size_t
get_sizeof_const(struct token *constant) {
	if (constant->type == TOK_STRING_LITERAL) {
		struct ty_string	*str;

		str = constant->data;
		return str->size;
	} else {
		return backend->get_sizeof_basic(constant->type);
	}
}

size_t
get_sizeof_elem_type(struct type *t) {
	struct type_node	*head = t->tlist;
	size_t			ret;

	t->tlist = t->tlist->next;
	ret = backend->get_sizeof_type(t, NULL);
	t->tlist = head;
	return ret;
}	

size_t
get_sizeof_decl(struct decl *d, struct token *tok) {
	if (d->size == 0) {
		d->size = backend->get_sizeof_type(d->dtype, tok);
	}
	return d->size;
}

int
botch_x86_alignment(struct type *ty) {
	if (backend->arch == ARCH_X86) {
		if (ty->tlist == NULL
			&& (IS_LLONG(ty->code)
				|| ty->code == TY_DOUBLE
				|| ty->code == TY_LDOUBLE)) {
			return 1; /* XXX Support gcc command line flags... */
		}
	}
	return 0;
}

static size_t
get_union_align(struct type *ty) {
	struct sym_entry	*se;
	size_t			maxalign = 0;

	for (se = ty->tstruc->scope->slist;
		se != NULL;
		se = se->next) {
		size_t	align;

		if (botch_x86_alignment(se->dec->dtype)) {
			align = 4;
		} else {
			align = backend->
				get_align_type(se->dec->dtype);
		}
		
		if (align > maxalign) {
			maxalign = align;
		}	
	}	
	return maxalign;
}

size_t
get_struct_align_type(struct type *ty) {
	if ((ty->fastattr & CATTR_ALIGNED) == 0) {
		/*
		 * 08/06/09: Silly gcc behavior: A long long is usually 8-byte aligned,
		 * but ISN'T if it's a struct member! This will cause problems with
		 * 64bit ``struct stat'' members and other such library interfaces.
		 */
		if (botch_x86_alignment(ty)) {
			return 4;
		}
	}
	return backend->get_align_type(ty);
}

/*
 * XXX this isn't as platform-independent and invariable as the author of
 * this stuff would have you believe. For starters, consider ``long double''
 * on x86 and x86-64. 
 */
size_t
get_align_type(struct type *ty) {
	int	ret;

	if (ty == NULL) {
		/*
		 * 05/27/08: Get highest alignment for platform
		 */
		switch (backend->arch) {
		case ARCH_X86:
			/*
			 * XXXXXXXXXXXXXXXXXXXX
			 * This is messed up! ``Standard'' Intel ABI says
			 * no type aligns higher than 4, but that's what
			 * gcc does by default for double and long long,
			 * but it can be turned off too and does not
			 * behave consistently in structs and arrays!
			 */
			/*return get_align_type(make_basic_type(TY_DOUBLE));*/
			return 16;
		case ARCH_AMD64:
			return get_align_type(make_basic_type(TY_LDOUBLE));
		case ARCH_POWER:
			/* XXX is this correct? How about long long on 32bit? */
			if (sysflag == OS_AIX) {
				/*
				 * 01/29/08: The highest alignment used by gcc is 16.
				 * I don't know why or which types (if any) are affected
				 * yet, but we'll just do the same thing
				 */
				return 16;
#if 0
				return get_align_type(make_basic_type(TY_LONG));
#endif
			} else {
				/* 
				 * 11/24/08: 128bit long double for Linux/PPC64
				 * XXX what about PPC32?
				 */
				return get_align_type(make_basic_type(TY_LDOUBLE));
			}
		case ARCH_MIPS:
			return get_align_type(make_basic_type(TY_LDOUBLE));
		case ARCH_SPARC:
			return get_align_type(make_basic_type(TY_LDOUBLE));
		default:
			unimpl();
		}
	}


	/*
	 * 05/29/08: CANOFWORMS: Use higher alignment on x86
	 * (8 for double and long long, 16 for unqualified
	 * __attribute__((aligned))), like gcc does by default
	 *
	 * XXX We should use command line options to configure
	 * this instead of macros
	 */
#if ALIGN_X86_LIKE_GCC
#    define X86IFY(val) /* nothing - keep original value */
#else
#    define X86IFY(val) if (backend->arch == ARCH_X86 && val > 4) val = 4
#endif
	if (ty->fastattr & CATTR_ALIGNED) {
		return lookup_attr(ty->attributes, ATTRS_ALIGNED)->iarg;
	}



	if (ty->tbit != NULL) {
		/*
		 * 08/17/08: Bitfield alignment!
		 */

		ret = backend->get_align_type(
				cross_get_bitfield_promoted_type(ty));
		X86IFY(ret);
		if (backend->arch == ARCH_X86) {
			/*
			 * unsigned long long bitfields have 4-byte alignment
			 * in gcc even when a plain long long doesn't! This
			 * is independent of the size of the bitfield, which
			 * may also be 64bit and still yield 4-byte-alignment
			 */
			ret = 4;
		}
		return ret;
	} else if (ty->tlist == NULL
		|| ty->tlist->type != TN_ARRAY_OF) {

		if (ty->tlist == NULL) {
			if (ty->code == TY_UNION) {
				ret = get_union_align(ty);
				X86IFY(ret);
				return ret;
			} else if (ty->code == TY_STRUCT) {
				struct attrib	*a;

				a = lookup_attr(ty->tstruc->attrib, ATTRS_ALIGNED);
				if (ty->tstruc->alignment && a == NULL) {
					ret = ty->tstruc->alignment;
					X86IFY(ret);
					return ret;
				}

				if (a != NULL) {
					ty->tstruc->alignment = a->iarg;
#if 0
				} else if (ty->tstruc->scope->slist->next == NULL) {
					/* Only one member */
					if (botch_x86_alignment(ty)) {
						ty->tstruc->alignment = 4;
					} else {
						ty->tstruc->alignment = backend->
							get_align_type(
							ty->tstruc->scope->slist->dec->dtype);
					}
#endif
				} else {
					ty->tstruc->alignment =
						get_union_align(ty);
				}

				ret = ty->tstruc->alignment;
				if (a == NULL) {
					X86IFY(ret);
				}	
				return ret;
			} else if (ty->code == TY_LDOUBLE) {
				if (backend->arch == ARCH_AMD64
					|| sysflag == OS_OSX) {
					return 16;
				} else if (backend->arch == ARCH_X86) {
					return 4;
				}
			}
		}
		ret = backend->get_sizeof_type(ty, NULL);
		/* XXX */
		X86IFY(ret);
		return ret;
	} else {
		struct type	tmp = *ty;
		tmp.tlist = tmp.tlist->next;
		return get_align_type(&tmp);
	}
}

unsigned long
calc_offsets(struct vreg *vr) {
        size_t  ret = 0;

        do {
                if (vr->parent->type->code == TY_STRUCT) {
                        ret += vr->memberdecl->offset;
                }
                if (vr->from_ptr) {
                        break;
                }
                vr = vr->parent;
        } while (vr->parent != NULL);
        return ret;
}

int
calc_slot_rightadjust_bytes(int size, int total_size) {
	int pad;

	if (size < total_size) {
		pad = total_size - size;
	} else {
		pad = 0;
	}
	return pad;
}


void
as_align_for_type(FILE *out, struct type *ty, int struct_member) {
        unsigned long   align;
        unsigned long   alignbits;

	(void) struct_member;
	assert(backend->arch != ARCH_X86);

        align = backend->get_align_type(ty);

	if (backend->arch == ARCH_SPARC) {
		alignbits = align;
	} else {	
        	alignbits = 0;
		while (align >>= 1) {
			++alignbits;
		}
	} 

        /* Make low-order bits of location counter zero */
        x_fprintf(out, "\t.align %lu\n", alignbits);
}

void
as_print_string_init(FILE *o, size_t howmany, struct ty_string *str) {
        char    *p;
	char	*wchar_type = NULL;
	int	wchar_size = 0;
        size_t  i;

	if (str->is_wide_char) {
		/* XXX This assumes the size will always be 4 */
		switch (backend->arch) {
		case ARCH_X86:   wchar_type = "long"; break;
		case ARCH_AMD64: wchar_type = "long"; break;
		case ARCH_MIPS:  wchar_type = "word"; break;
		case ARCH_POWER: wchar_type = "long"; break;
		case ARCH_SPARC: wchar_type = "word"; break;
		default: unimpl();
		}
		x_fprintf(o, ".%s\t", wchar_type);
		wchar_size = backend->get_sizeof_type(
					backend->get_wchar_t(), NULL);

		assert(wchar_size == 4);
	} else {
	        x_fprintf(o, ".byte\t");
	}

        for (i = 0, p = str->str; i < str->size-1; ++p, ++i) {
		if (str->is_wide_char) {
			if (wchar_size == 2) {
				x_fprintf(o, "0x00%02x", (unsigned char)*p);
			} else if (wchar_size == 4) {
				x_fprintf(o, "0x000000%02x", (unsigned char)*p);
			} else {
				unimpl();
			}
		} else {
	                x_fprintf(o, "0x%x", (unsigned char)*p);
		}

                if (i+1 < str->size-1) {
                        if (i > 0 && (i % 10) == 0) {
				if (str->is_wide_char) {
                        	        x_fprintf(o, "\n.%s\t", wchar_type);
				} else {
                        	        x_fprintf(o, "\n.byte\t");
				}
                        } else {
                                x_fputc(',', o);
                        }
                }
        }

        if (howmany >= str->size) {
                if (str->size > 1) {
                        (void) fprintf(o, ", ");
                }
                (void) fprintf(o, "0");
        }
        x_fputc('\n', o);
}

struct reg *
generic_alloc_gpr(
	struct function *f, 
	int size, 
	struct icode_list *il,
	struct reg *dontwipe,
	struct reg *regset,
	int nregs,
	int *csave_map,
	int line) {

	int			i;
	int			save = 0;
	int			least_idx = -1;
	int			regno;
	static int		last_alloc;
	struct reg		*ret = NULL;

	(void) size;
	(void) line;
	(void) dontwipe;
	(void) f;
	for (i = 0; i < nregs; ++i) {
		if (reg_unused(&regset[i])
			&& reg_allocatable(&regset[i])) {
			ret = &/*mips_gprs*/regset[i];
			last_alloc = i;
			break;
		} else {
			if (!optimizing /* || !reg_allocatable(...)*/) {
				continue;
			}
		}
	}
	if (ret == NULL) {
		/*
	 	 * Save and hand out register with least
	 	 * references
	 	 */
		save = 1;
		if (!optimizing) {
			static int	cur;
			int		iterations = 0;

			if (cur == last_alloc) {
				/*
				 * Ensure two successive allocs always
				 * use different registers
				 */
				cur = (cur + 1) % nregs;
			}	

			do {
				if (cur == nregs) cur = 0;
				ret = &regset[cur++];
				if (++iterations == nregs) {
					/*
					 * Ouch, no register can be allocated.
					 * This will probably only ever happen
					 * with inline asm statements using too
					 * many registers .... HOPEFULLY!!
					 */
					return NULL;
				}	

				/*
				 * 10/18/07: Wow, the check below didn't
				 * use reg_allocatable(). Thus the AMD64
				 * sub-registers for r8-r15 were not
				 * considered when determining
				 * allocatability
				 */
			} while ((dontwipe != NULL && ret == dontwipe)
				/*|| !ret->allocatable*/
				|| !reg_allocatable(ret));
			last_alloc = cur - 1;
		} else {
			int	idx;

			idx = least_idx == -1? 0: least_idx;
			if (idx == last_alloc) {
				idx = (idx + 1) % nregs;
			}	
			ret = &/*mips_gprs*/regset[idx];
			last_alloc = idx;
		}
	}

	regno = ret - /*mips_gprs*/regset;
	if (csave_map != NULL) {
		f->callee_save_used |= csave_map[regno] << regno;
	}
	
	if (save) {
		struct reg	*top_reg = NULL;

		/*
		 * IMPORTANT: It is assumed that an allocatable register
		 * has a vreg, hence no ret->vreg != NULL check here.
		 * Reusing a preg without a vreg is obviously a bug
		 * because without a vreg, it cannot be saved anywhere.
		 * See reg_set_unallocatable()/vreg_faultin_protected()
		 *
		 * 10/30/07: This didn't work for AMD64 sub-registers.
		 * Example: We are allocating r10, but only r10d is used.
		 * In that case we can't insist on ret being mapped to a
		 * vreg. That's a bug. Another thing is that smaller-
		 * than-GPR size requests weren't honored, which is not
		 * used on AMD64 anyway, but could be used at some point.
		 * This is also implemented (but untested) now.
		 *
		 * XXX This currently only works with one sub-register
		 * per register (e.g. ah/al for ax wouldn't work)
		 */
		if (!ret->used) {
			/*
			 * Find register to free (there must be one because
			 * the ``save'' flag is set)
			 */
			top_reg = ret;
			do {
				ret = ret->composed_of[0];
			} while (!ret->used);
		}

		if (ret->vreg->from_const == NULL
			&& ret->vreg->var_backed == NULL
			&& ret->vreg->from_ptr == NULL
			&& ret->vreg->parent == NULL) {
			/* Anonymous register - must be saved */
			free_preg(ret, il, 1, 1);
		}
		ret->vreg = NULL;

		if (size == 0) {
			/* Request to allocate top register */
			if (top_reg != NULL) {
				/* Change ret back to top */
				ret = top_reg;
			}
		} else if (ret->size != (unsigned)size) {
			/*
			 * Freed sub-register does not match
			 * the desired size
			 */
			if (top_reg != NULL) {
				ret = top_reg;
			}

			/*
			 * Check for composed_of != NULL because some
			 * callers supply size info even when there is
			 * no need to distinguish between sizes. E.g.
			 * FPRs are usually 8 bytes and have no sub
			 * registers, but generic_alloc_gpr() may be
			 * called with a 4 byte size argument for a
			 * float
			 */
			while (ret->composed_of && ret->size != (unsigned)size) {
				ret = ret->composed_of[0];
			}	
		}
	}

	ret->used = ret->allocatable = 1;
	if (ret == NULL) {
		debug_log_regstuff(ret, NULL, DEBUG_LOG_FAILEDALLOC);
	} else {
		debug_log_regstuff(ret, NULL, DEBUG_LOG_ALLOCGPR);
	}
#ifdef DEBUG6
	if (ret != NULL) {
		ret->line = line;
		++ret->nallocs;
	}
#endif
	return ret;
}

/*
 * This function is used to tell us whether a tlist really does include
 * a VLA component. This is sometimes necessary to know when we modify
 * typelists
 */
int
vla_type_has_constant_size(struct type_node *tn) {
	for (; tn != NULL; tn = tn->next) {
		if (tn->type == TN_VARARRAY_OF) {
			/* Not constant */
			return 0;
		}
	}
	/* Constant! */
	return 1;
}

int
is_immediate_vla_type(struct type *ty) {
	if (!IS_VLA(ty->flags)) {
		return 0;
	}
	if (ty->tlist == NULL) {
		return 0;
	}
	if (ty->tlist->type == TN_POINTER_TO) {
		return 0;
	}

	/* XXX */
	return 1;
}
	

size_t
get_sizeof_type(struct type *t, struct token *tok) {
	if (IS_VLA(t->flags) && !vla_type_has_constant_size(t->tlist)) {
		if (t->tlist != NULL && t->tlist->type == TN_POINTER_TO) {
			/*
			 * 05/22/11: This is a pointer to a VLA of some sort.
			 * We can determine the pointer size without knowing
			 * anything about the VLA because all pointers are
			 * the same size for all backends, so we generously
			 * provide it to the caller here instead of throwing
			 * an error. This is currently needed at least by
			 * expr_to_icode(), where assignments of the type
			 *
			 *    char (*gnu)[i], (*foo)[j];
			 *    gnu = foo;
			 *
			 * ... set the size of the result expression to
			 * sizeof(char(*)[i])
			 */
			;
		} else {
			puts("BUG: get_sizeof_type() applied to VLA, should use "
				"get_sizeof_vla_type() instead!!!!");
			abort();
		}
	}
	if (t->tlist != NULL) {
		/*
		 * May not be called with function argument,
		 * so this has to be a pointer or an array
		 */
		if (t->tlist->type == TN_ARRAY_OF) {
			size_t			elem_size;
			struct type_node	*tmp;

#if REMOVE_ARRARG 
			if (!t->tlist->have_array_size) {
#else
			if (t->tlist->arrarg
				&& t->tlist->arrarg->const_value == NULL) {
#endif
				/*
				 * An unspecified array size is ok if this
				 * function was called internally because
				 * then it came from vreg_alloc() or the
				 * likes. This is to permit something like
				 * extern struct foo bar[];
				 * bar;
				 * XXX probably should never be called in
				 * contexts where the above is ok
				 */
				if (tok != NULL) {
					/* Not called internally */
					errorfl(tok,
					"Cannot take size of incomplete type");
				}
warningfl(tok, "(BUG?:) Cannot take size of incomplete type");				
				return 0;
			}

#if ! REMOVE_ARRARG
			if (t->tlist->arrarg_const == 0) {
				t->tlist->arrarg_const =
					cross_to_host_size_t(
						t->tlist->arrarg->const_value);
			}
#endif

			tmp = t->tlist;
			t->tlist = t->tlist->next;
			elem_size = backend->get_sizeof_type(t, tok);
			t->tlist = tmp;

			return t->tlist->arrarg_const * elem_size;
		} else if (t->tlist->type == TN_POINTER_TO) {
			return backend->get_ptr_size();
		} else {
			/* TN_FUNCTION */
			return backend->get_ptr_size(); /* XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX */
			if (tok != NULL) {
				/* User used ``sizeof'' explicitly */
				errorfl(tok,
					"`sizeof' operator cannot be applied"
					" to functions");
			} else {
				/* Function was called internally */
				puts("BUG: sizeof() applied to function");
				abort();
			}
			return 0;
		}
		return backend->get_ptr_size();
	}

	if (t->code == TY_STRUCT) {
		return t->tstruc->size;
	} else if (t->code == TY_UNION) {
		return t->tstruc->size;
	} else {
/*		return backend->get_sizeof_basic(t->code);*/
		return cross_get_sizeof_type(t);
	}
}




/*
 * 07/24/07: NEW! A sizeof for VLAs. This generates code to fetch
 * the hidden VLA sizes at runtimes, and to multiply them. Consequently
 * it returns a vreg with the result
 */
struct vreg *
get_sizeof_vla_type(struct type *ty, struct icode_list *il) {
	size_t			ulong_size = backend->get_sizeof_type(
					make_basic_type(TY_ULONG), NULL);
	int			base_size;
	/*struct reg		*res_reg;*/
	struct type_node	*tn;
	struct type_node	*saved_tlist;
	struct token		*tok;
	struct vreg		*ret;
	struct stack_block	*block_addr = ty->vla_addr;
	
	/*
	 * 06/01/11: Nonsensical (unused) register allocation removed! This
	 * register was never mapped to a vreg, and neither marked unallocatable,
	 * such that subsequent register allocations stumbled over null pointer
	 * errors.
	res_reg = ALLOC_GPR(curfunc, ulong_size, il, NULL); 
	*/

	if (ty->tlist != NULL && ty->tlist->type == TN_POINTER_TO) {
		/* Not much to be done */
		base_size = backend->get_ptr_size();
	} else {	
		/*
		 * Get base type size, e.g. sizeot(int), etc., if this is a
		 * (possibly multi-dimensional) array.
		 * ATTENTION: If this is an array of pointers, the base
		 * type size is sizeof(ptr) instead of the genuine base
		 * type size
		 */
		base_size = 0;
		for (tn = ty->tlist; tn != NULL; tn = tn->next) {
			if (tn->type == TN_POINTER_TO) {
				/* Yes, base is pointer */
				base_size = backend->get_ptr_size();
				break;
			}
		}	
		if (base_size == 0) {
			/* This is an array of non-pointer types */
			saved_tlist = ty->tlist;
			ty->tlist = NULL;
			ty->flags &= ~FLAGS_VLA;
			base_size = backend->get_sizeof_type(ty, NULL);
			ty->flags |= FLAGS_VLA;

			ty->tlist = saved_tlist;
		}
	}

	tok = const_from_value(&base_size, make_basic_type(TY_INT)); 
	ret = vreg_alloc(NULL, tok, NULL, make_basic_type(TY_INT));

	/*
	 * Anonymify base size and make it a size_t
	 * XXX const_from_value should be fixed instead!
	 */
	ret = backend->icode_make_cast(ret, backend->get_size_t(), il);
	
	if (ty->tlist != NULL && ty->tlist->type != TN_POINTER_TO) {
		/*
		 * Must be an array of VLAs, or of pointers to VLAs,
		 * or a plain VLA. We don't attempt to fold constant
		 * dimensions yet. Instead we just calculate it all
		 * dynamically. (XXX)
		 */
		struct vreg		*factor_vreg;
		struct token		*factor_tok;
		int			factor_size;
		int			vla_idx = 0;
		struct icode_instr	*ii;

		for (tn = ty->tlist; tn != NULL; tn = tn->next) {
			/*
			 * 05/22/11: Explictly store block index numbering
			 * information in the type node. This fixes incorrect
			 * block handling in constructs like
			 *
			 *       char buf[x][y];
			 *       sizeof *x;  // used wrong block
			 */
			vla_idx = tn->vla_block_no;

			if (tn->type == TN_POINTER_TO) {
				/*
				 * This already went into the ``base size''
				 * of this array - nothing left to do
				 */
				break;
			} else if (tn->type == TN_ARRAY_OF) {
#if ! REMOVE_ARRARG
				if (tn->arrarg_const == 0) {
					tn->arrarg_const =
						cross_to_host_size_t(
						tn->arrarg->const_value);
				}
#endif
				factor_size = tn->arrarg_const;
				factor_tok = const_from_value(&factor_size,
					make_basic_type(TY_INT));	
				factor_vreg = vreg_alloc(NULL, factor_tok,
					NULL, make_basic_type(TY_INT));	

				reg_set_unallocatable(ret->pregs[0]);
				factor_vreg = backend->icode_make_cast(
					factor_vreg,
					make_basic_type(TY_ULONG),
					il);
				reg_set_allocatable(ret->pregs[0]);
			} else {
				/* Must be VLA */
				struct reg	*dimsize;

				reg_set_unallocatable(ret->pregs[0]);
				dimsize = ALLOC_GPR(curfunc, ulong_size, il, 0);
				reg_set_allocatable(ret->pregs[0]);

				factor_vreg = icode_make_retr_vla_size(
					dimsize, block_addr, vla_idx, il);	
		/*		++vla_idx;*/
			}

			backend->icode_prepare_op(&ret, &factor_vreg,
				TOK_OP_MULTI, il);	
			ii = icode_make_mul(ret, factor_vreg);
			append_icode_list(il, ii);
		}
	}
	return ret;
}


/*
 * 05/22/11: Get element size of the passed pointer or array type
 * (i.e. what the pointer points or what the array contains - which
 * has previously been determined to be a VLA of some sort)
 */
struct vreg *
get_sizeof_elem_vla_type(struct type *ty, struct icode_list *il) {
	struct type_node	*head = ty->tlist;
	struct vreg		*ret;

	ty->tlist = ty->tlist->next;
	ret = get_sizeof_vla_type(ty, il);
	ty->tlist = head;
	return ret;
}


/* XXX platform-independent?!?! used by amd64 */
void
store_preg_to_var(struct decl *d, size_t size, struct reg *r) {
	static struct vreg	vr;

	vr.type = d->dtype;
	vr.size = size;
	vr.var_backed = d;

	/*
	 * 01/28/09: This didn't handle multi-GPR items, which broke
	 * with long long on PPC32!
	 * XXX how about long double on SPARC? Is this ever encountered
	 * here?
	 * 01/31/09: Type can be NULL?! Is that ok?
	 */
	if (d->dtype != NULL) {
		vr.is_multi_reg_obj = backend->is_multi_reg_obj(d->dtype);
	}
	backend_vreg_map_preg(&vr, r);
	emit->store(&vr, &vr);
	if (vr.is_multi_reg_obj) {
		backend_vreg_map_preg2(&vr, r+1 /* XXX */);
		emit->store(&vr, &vr);
		backend_vreg_unmap_preg2(r+1);
	}
	backend_vreg_unmap_preg(r);
	r->used = 0;
}


void
store_preg_to_var_off(struct decl *d, size_t off, size_t size, struct reg *r,
	struct reg *temp) {

	static struct vreg      vr;
	static struct vreg      ptr;

	vr.type = d->dtype;
	vr.size = size;
	vr.var_backed = d;
	vr.from_ptr = NULL; /* needs reset because vr is static */
	/*      vr.from_ptr = &ptr;*/

	/*
	 * Load address of variable into register
	 */
	backend_vreg_map_preg(&ptr, temp);

	vr.addr_offset = off;
	emit->addrof(temp, &vr, NULL);

	if (backend->arch != ARCH_POWER && backend->arch != ARCH_MIPS) {
		/*
		 * 07/22/09: addr_offset must be implemented in emit_addrof()
		 * for every architecture/emitter which uses this function!
		 * It is optional and currently only used for this purpose
		 */
		unimpl();
	}

	/*
	 * Store register through pointer
	 */
	vr.type = backend->get_size_t();
	vr.size = backend->get_sizeof_type(vr.type, NULL);
	vr.from_ptr = &ptr;
	vr.var_backed = NULL;
	backend_vreg_map_preg(&vr, r);
	emit->store(&vr, &vr);
}





/* XXX generic? */
void
put_arg_into_reg(
	struct reg *regset,
	int *index, int startat,
	struct vreg *vr,
	struct icode_list *il) {

	struct reg	*r;
	struct reg	*r2 = NULL;

	r = &regset[startat + *index];
	if (vr->is_multi_reg_obj) {
		r2 = &regset[startat + *index + 1];
	}
	if (vr->pregs[0] == NULL
		|| vr->pregs[0] != r
		|| r->vreg != vr
		|| r2) {

		int is_dedicated = 0;

		/*
		 * 07/24/09: On PowerPC, the temporary FPR f13 is also
		 * a floating point argument register. So it is
		 * generally dedicated (should not be used for other
		 * things) with the special expection of using it as
		 * argument register. So we distinguish for the temp
		 * FPR (possibly also on for other architectures)
		 * below. Note that the temp GPR should NOT be an
		 * argument register because it may be needed to
		 * compute pointer addresses in the very process of
		 * passing arguments
		 */
		if (r->dedicated && r == tmpfpr) {
			free_preg_dedicated(r, il, 1, 1);
			is_dedicated = 1;
		} else {
			free_preg(r, il, 1, 1);
		}
		if (r2 != NULL) {
			if (r2->dedicated && r2 == tmpfpr) {
				free_preg_dedicated(r2, il, 1, 1);
				is_dedicated = 1;
			} else {
				free_preg(r2, il, 1, 1);
			}
		}
		
		if (is_dedicated) {
			vreg_faultin_dedicated(r, r2, vr, il, 0);
		} else {
			vreg_faultin(r, r2, vr, il, 0);
		}
	} else {
		if (r == tmpfpr || (r2 != NULL && r2 == tmpfpr)) {
			vreg_map_preg_dedicated(vr, r);
		} else {
			vreg_map_preg(vr, r);
		}
	}
	reg_set_unallocatable(r);
	if (r2) {
		reg_set_unallocatable(r2);
		++*index;
	}
	++*index;
}

/*
 * XXX the alignment stuff is totally botched.. it should be done in
 * this routine and not in print_init_expR()..
 * also, this could probably be unified with gas/nasm print_init_list()
 */
void
generic_print_init_list(FILE *out, struct decl *dec, struct initializer *init,
	void (*print_init_expr)(struct type *, struct expr *)) {

	struct sym_entry	*se = NULL;
	int	is_struct = 0;

	if (dec
		&& (dec->dtype->code == TY_STRUCT
			|| dec->dtype->code == TY_UNION)
		&& dec->dtype->tlist == NULL) {
		se = dec->dtype->tstruc->scope->slist;
		is_struct = 1;
	}	

	for (; init != NULL; init = init->next) {
		if (init->type == INIT_NESTED) {
			struct decl	*nested_dec = NULL;
			struct type_node	*saved_tlist = NULL;

			if (se == NULL) {
				/*
				 * May be an array of structs, in
				 * which case the struct declaration
				 * is needed for alignment
				 */
				if (dec && dec->dtype->code == TY_STRUCT) {
					nested_dec = alloc_decl();
					nested_dec->dtype = dec->dtype;
					saved_tlist = dec->dtype->tlist;
					dec->dtype->tlist = NULL;
				}
			} else {
				nested_dec = se->dec;
			}
			generic_print_init_list(out, nested_dec, init->data,
				print_init_expr);
			if (saved_tlist != NULL) {
				dec->dtype->tlist = saved_tlist;
				free(nested_dec);
			}
		} else if (init->type == INIT_EXPR) {
			struct expr	*ex;

			ex = init->data;
			print_init_expr(ex->const_value->type, ex);
		} else if (init->type == INIT_NULL) {
			x_fprintf(out, "\t.%s %lu\n",
				backend->arch == ARCH_SPARC? "skip": "space",
				(unsigned long)*(size_t *)init->data);	
			/*
			 * 03/01/10: Don't do this for variable
			 * initializers. See x86_emit_gas.c
			 */
			if (init->varinit == NULL) {
				for (; se != NULL && se->next != NULL; se = se->next) {
					;
				}
			}
		}
		if (se != NULL) {
			se = se->next;
		}
	}	
	if (is_struct) {
		int	align = backend->get_align_type(dec->dtype);

		assert(backend->arch != ARCH_X86);

		if (backend->arch != ARCH_SPARC) {
			if (align == 2) align = 1;
			else if (align == 4) align = 2;
			else if (align == 8) align = 3;
		}

		/* XXX or use .space?! */
		x_fprintf(out, "\t.align %d\n",  align);
	}
}

/*
 * This function relocates structure pointer and size values to
 * different registers if necessary, in preparation for a structure
 * assignment. The point is that on most architectures, like MIPS
 * and PPC, the memcpy() arguments go into GPRs, and we have to
 * ensure that moving one of these values does not trash another
 * value because it is resident in the destination GPR
 */
void
relocate_struct_regs(struct copystruct *cs,
	struct reg *r0, struct reg *r1, struct reg *r2,
	struct icode_list *il) {	

	struct reg	*curregs[4];
	struct reg	*tmp;
	int		i;
	
	curregs[0] = cs->src_from_ptr;
	curregs[1] = cs->dest_from_ptr;
	curregs[2] = cs->src_from_ptr_struct;
	curregs[3] = cs->dest_from_ptr_struct;

	reg_set_unallocatable(r0);
	reg_set_unallocatable(r1);
	reg_set_unallocatable(r2);

	/*
	 * 11/01/07: This was missing
	 */
	for (i = 0; i < 4; ++i) {
		if (curregs[i] != NULL) {
			reg_set_unallocatable(curregs[i]);
		}
	}

	for (i = 0; i < 4; ++i) {
		/*
		 * 11/01/07: This was missing
		 */
		if (curregs[i] == NULL) {
			continue;
		}
		if (curregs[i] == r0
			|| curregs[i] == r1
			|| curregs[i] == r2) {
			/* Move elsewhere */
			tmp = ALLOC_GPR(curfunc, curregs[i]->size, il, NULL);
			icode_make_copyreg(tmp, curregs[i],
				curregs[i]->vreg->type, /* XXX ok? */
			curregs[i]->vreg->type, /* XXX ok? */
				il);

			/*
			 * 11/01/07: This was missing!!!!!!!! The
			 * registers were relocated, but the register
			 * information was not updated. Terrible!
			 */
			switch (i) {
			case 0:
				cs->src_from_ptr = tmp;
				break;
			case 1:
				cs->dest_from_ptr = tmp;
				break;
			case 2:
				cs->src_from_ptr_struct = tmp;
				break;
			case 3:
				cs->dest_from_ptr_struct = tmp;
				break;
			}
		}
	}
	free_preg(r0, il, 0, 0);
	free_preg(r1, il, 0, 0);
	free_preg(r2, il, 0, 0);
	for (i = 0; i < 4; ++i) {
		if (curregs[i] != NULL) {
			free_preg(curregs[i], il, 0, 0);
		}	
	}
}


/*
 * 12/29/07: Saves struct address register to stack - SPARC-specific?
 */
void
save_struct_ptr(struct decl *dec) {
	static struct vreg	dest_vreg;

	dest_vreg.var_backed = dec;
	dest_vreg.type = make_void_ptr_type();
	dest_vreg.size = backend->get_sizeof_type(dest_vreg.type, NULL);
	dest_vreg.pregs[0] = dec->stack_addr->from_reg;
	emit->store(&dest_vreg, &dest_vreg);
}


/*
 * 12/29/07: Reloads struct address from stack - SPARC-specific?
 */
void
reload_struct_ptr(struct decl *dec) {
	static struct vreg	dest_vreg;

	dest_vreg.var_backed = dec;
	dest_vreg.type = make_void_ptr_type();
	dest_vreg.size = backend->get_sizeof_type(dest_vreg.type, NULL);
	emit->load(dec->stack_addr->from_reg, &dest_vreg);
}

/*
 * This function copies the structure pointed to by dec->stack_addr->
 * from_reg to the stack block designated by dec->stack_addr, by
 * calling emit->copystruct().
 * The purpose is just to set up the data structures required by
 * copystruct()
 * 12/29/07: SPARC-specific?
 */
void
copy_struct_regstack(struct decl *dec) {
	static struct copystruct	cs;
	static struct vreg		src_vreg;
	static struct vreg		dest_vreg;

	src_vreg.type = dest_vreg.type = dec->dtype;
	src_vreg.size = backend->get_sizeof_type(dec->dtype, NULL);
	dest_vreg.size = src_vreg.size;
	dest_vreg.var_backed = dec;

	cs.src_from_ptr = dec->stack_addr->from_reg;
	cs.src_vreg = &src_vreg;
	cs.dest_vreg = &dest_vreg;
	emit->copystruct(&cs);
}

/*
 * This is a new attempt at generic_print_init_list(), which uses gas/UNIX
 * as style syntax, but performs alignment in the same way as the x86 one
 */
void
new_generic_print_init_list(FILE *out, struct decl *dec, struct initializer *init,
	void (*print_init_expr)(struct type *, struct expr *)) {

	struct sym_entry	*se = NULL;
	struct sym_entry	*startse = NULL;

	if (dec
		&& (dec->dtype->code == TY_STRUCT
			|| dec->dtype->code == TY_UNION)
		&& dec->dtype->tlist == NULL) {
		se = dec->dtype->tstruc->scope->slist;
	}	
	for (; init != NULL; init = init->next) {
		if (init->type == INIT_NESTED) {
			struct decl		*nested_dec = NULL;
			struct decl		*storage_unit = NULL;
			struct type_node	*saved_tlist = NULL;
			
			if (se == NULL) {
				/*
				 * May be an array of structs, in
				 * which case the struct declaration
				 * is needed for alignment
				 */
				if (dec && dec->dtype->code == TY_STRUCT) {
					nested_dec = alloc_decl();
					nested_dec->dtype = dec->dtype;
					saved_tlist = dec->dtype->tlist;
					dec->dtype->tlist = NULL;
				}
			} else {
				nested_dec = se->dec;
			}
			new_generic_print_init_list(out, nested_dec, init->data,
				print_init_expr);
			if (saved_tlist != NULL) {
				dec->dtype->tlist = saved_tlist;
				free(nested_dec);
			}

                        /*
                         * 10/08/08: If this is a bitfield initializer, match
                         * (skip) all affected bitfield declarations in this
                         * struct. This is important for alignment
                         */
			if (se != NULL && se->dec->dtype->tbit != NULL) {
				storage_unit = se->dec->dtype->tbit->bitfield_storage_unit;
				/*
				 * Skip all but last initialized bitfield, which is needed
				 * for alignment below
				 */
				if (se->next == NULL) {
					/*
					 * This is already the last struct member, which
					 * also happens to be a bitfield
					 */
					;
				} else {
					do {
						se = se->next;
					} while (se != NULL
						&& se->next != NULL
						&& se->dec->dtype->tbit != NULL
						&& se->dec->dtype->tbit->bitfield_storage_unit
							== storage_unit);

					if (se != NULL
						&& (se->dec->dtype->tbit == NULL
						|| se->dec->dtype->tbit->bitfield_storage_unit
							!= storage_unit)) {
						/*
						 * Move back to last BF member -
						 * so we can align for next 
						 * member
						 */
						se = se->prev;
					}
				}
                        }
		} else if (init->type == INIT_EXPR) {
			struct expr	*ex;

			ex = init->data;
			print_init_expr(ex->const_value->type, ex);
			if (se != NULL && se->dec->dtype->tbit != NULL) {
				/*
				 * Skip alignment stuff below, UNLESS
				 * we are dealing with the last member
				 * of the struct, in which case we may
				 * have to pad to align for the start
				 * of the struct
				 */
				if (se->next != NULL) {
					continue;
				}
			}
		} else if (init->type == INIT_NULL) {
			if (init->varinit && init->left_type->tbit != NULL) {
				continue;
			} else {
				x_fprintf(out, "\t.%s %lu\n",
					backend->arch == ARCH_SPARC? "skip": "space",
					(unsigned long)*(size_t *)init->data);	
				startse = se;
				/*
				 * 03/01/10: Don't do this for variable
				 * initializers. See x86_emit_gas.c
				 */
				if (init->varinit == NULL) {
					for (; se != NULL && se->next != NULL; se = se->next) {
						;
					}
				}
			}
		}
		if (se != NULL) {
			/* May need alignment */
			struct decl	*d = NULL;
			struct type	*ty = NULL;
			size_t		nbytes;

			if (se->next != NULL) {
				/* We may have to align for the next member */
				if (se->next->dec->dtype->tbit != NULL) {
					/* Don't align bitfields! */
					;
				} else {
					d = se->next->dec;
					ty = d->dtype;
				}
			} else if (dec->dtype->tstruc->scope->slist->next) {
				/*
				 * We've reached the end of the struct and
				 * may have to pad the struct, such that if
				 * we have an array of structs, every element
				 * is properly aligned.	
				 *
				 * Note that we have to use the whole struct
				 * alignment, not just first member alignment
				 */
				ty = dec->dtype;
				if (init->type == INIT_NULL) {
					/*
					 * 08/08/07: Same fix as in x86 struct
					 * init functions
					 */
					size_t	curoff = startse->dec->offset +
						*(size_t *)init->data;
					size_t	alignto = backend->get_align_type(ty);
					size_t	tmp = 0;

					while ((curoff + tmp) % alignto) {
						++tmp;
					}
					if (tmp > 0) {
						x_fprintf(out, "\t.%s %lu\n",
						backend->arch == ARCH_SPARC?	
						"skip": "space",
						tmp);
					}
				} else {	
					d = dec->dtype->tstruc->scope->slist->dec;
				}
			}

			if (d != NULL) {
				unsigned long	offset;


				/*
				 * 10/08/08: Handle bitfields
				 */
				if (se->dec->dtype->tbit != NULL) {
					/*
					 * Align for next member. We are at
					 *
					 *    address_of_storage_unit + size_of_storage_unit
					 *
					 * We only get here if the last bitfield in the
					 * current unit is processed, so we have to account
					 * for the entire partial storage unit.
					 *
					 * Note that we're setting the offset AFTER the current
					 * item because calc_align_bytes() doesn't do this for
					 * us
					 */
					offset = se->dec->dtype->tbit->bitfield_storage_unit->offset
						+ backend->get_sizeof_type(se->dec->dtype->tbit->
						bitfield_storage_unit->dtype, NULL);
				} else {
					offset = se->dec->offset;
				}



				nbytes = calc_align_bytes(/*se->dec->*/offset,
					se->dec->dtype, ty, 1);
				if (nbytes) {
					x_fprintf(out, "\t.%s %lu\n",
						backend->arch == ARCH_SPARC?
						"skip": "space",
						nbytes);
				}
			}
			se = se->next;
		}
	}	
}

size_t
generic_print_init_var(FILE *out, struct decl *d, size_t segoff,
	void (*print_init_expr)(struct type *, struct expr *),
	int skip_is_space) {

	struct type	*dt = d->dtype;
	size_t		size;
	size_t		ret = 0;
		
	if (DECL_UNUSED(d)) {
		return 0;
	}

	/* Constant initializer expression */
	x_fprintf(out, "%s:\n", dt->name);
	new_generic_print_init_list(out, d, d->init, print_init_expr);

	ret = size = backend->get_sizeof_decl(d, NULL);
	if (d->next != NULL) {
		unsigned long	align;
		struct decl	*tmpd;

		/*
		 * Now we have to check which of the next variables
		 * is actually used. Because if it's not used, it's
		 * not printed, and then we'd get wrong alignment
		 */
		for (tmpd = d->next; tmpd != NULL; tmpd = tmpd->next) {
			if (!DECL_UNUSED(tmpd)) {
				break;
			}
		}

		if (tmpd != NULL) {
			align = calc_align_bytes(segoff,
				d->dtype, tmpd->dtype, 0);
			if (align) {
				/*
				 * XXX is this really needed?!?! doesn't the
				 * SPARC assembler have "space" or why was
				 * skip usedh ere???
				 */
				x_fprintf(out, "\t.%s %lu\n",
					skip_is_space? "space": "skip", align);
				ret += align;
			}
		}
	}
	return ret;
}

int
generic_same_representation(struct type *dest, struct type *src) {
	size_t	dest_size = backend->get_sizeof_type(dest, NULL);
	size_t	src_size = backend->get_sizeof_type(src, NULL);

	if ((is_integral_type(dest)
		|| dest->tlist != NULL)
		&& (is_integral_type(src)
			|| src->tlist != NULL)
		&& dest_size == src_size) {
		return 1;
	} else {
		return 0;
	}	
}	

void
store_reg_to_stack_block(struct reg *r, struct stack_block *sb) {
	static struct vreg	vr;

	vr.type = make_void_ptr_type();
	vr.size = backend->get_sizeof_type(vr.type, NULL);
	vr.stack_addr = sb;
	backend_vreg_map_preg(&vr, r);
	emit->store(&vr, &vr);
	backend_vreg_unmap_preg(r);
}

unsigned long
align_for_cur_auto_var(struct type *ty, unsigned long curoff) {
	unsigned long	align = backend->get_align_type(ty);
	unsigned long	origoff = curoff;

	while (curoff % align) {
		++curoff;
	}
	return curoff - origoff;
}

int
arch_without_offset_limit(void) {
	switch (backend->arch) {
	case ARCH_X86:
	case ARCH_AMD64:
		/*
		 * x86 and AMD64 have variable-length encodings which
		 * allow for ``unlimited'' (within GPR range) offsets
		 */
		return 1;
	case ARCH_MIPS:
	case ARCH_POWER:
	case ARCH_SPARC:
		/* These archs are limited (especially SAPRC)! */
		return 0;
	default:
		unimpl();
	}
	/* NOTREACHED */
	return 0;
}	

char *
generic_elf_section_name(int value) {
	switch (value) {
	case SECTION_INIT:
		return "data";
	case SECTION_UNINIT:
		return "bss";
	case SECTION_RODATA:
		return "rodata";
	case SECTION_TEXT:
		return "text";
	case SECTION_INIT_THREAD:
		return "tdata";
	case SECTION_UNINIT_THREAD:
		return "tbss";
	default:
		unimpl();
	}
	/* NOTREACHED */
	return "";
}

char *
generic_mach_o_section_name(int value) {
	if (value == SECTION_RODATA) {
		return "cstring";
	} else if (value == SECTION_UNINIT) {
		return "data";
	}
	return generic_elf_section_name(value);
}


/*
 * 04/05/08: Alignment across scope boundaries wasn't working, because the
 * approach we use is to align for the next variable while allocating the
 * current variable, and this was not done if the current scope ended.
 * Hence this new function to look up the next variable to align for. This
 * stuff sucks, and we should instead align for the CURRENT variable as it
 * is encountered
 */
struct decl *
get_next_auto_decl_in_scope(struct scope *s, int i) {
	struct decl	**dec = s->automatic_decls.data;

	for (;;) {
		if (i+1 < s->automatic_decls.ndecls) {
			/*
			 * Currently VLAs are implemented in terms of
			 * malloc() and free(), so do not consider them
			 * for alignment
			 */
			if (!IS_VLA(dec[i+1]->dtype->flags)) {
				return dec[i+1];
			}
			++i;
		} else {
			/* Scope ends here, try next one */
			do {
				s = s->next;
			} while (s != NULL && s->type != SCOPE_CODE);
			if (s == NULL) {
				/* No more local variables */
				return NULL;
			}	
			i = -1;  /* i+1 = 0 */
			dec = s->automatic_decls.data;
		}
	}
	/* NOTREACHED */
	return NULL;
}


struct stupidtrace_entry *
put_stupidtrace_list(struct function *f) {
	struct stupidtrace_entry	*ent;
	static struct stupidtrace_entry	nullent;
	char			buf[128];
	static unsigned long	count;

	ent = n_xmalloc(sizeof *ent);
	*ent = nullent;
	ent->func = f;
	sprintf(buf, "_Strace%lu", count++);
	ent->bufname = n_xstrdup(buf);
	if (stupidtrace_list_head == NULL) {
		stupidtrace_list_head = stupidtrace_list_tail = ent;
	} else {
		stupidtrace_list_tail->next = ent;
		stupidtrace_list_tail = ent;
	}
	return ent;
}


struct reg * /*icode_instr **/
make_addrof_structret(struct vreg *struct_lvalue, struct icode_list *il) {
	struct type     	*orig_type = struct_lvalue->type;
	struct type     	*temp_type = dup_type(struct_lvalue->type);
	struct reg		*ret;

	temp_type->tlist = NULL;
	struct_lvalue->type = temp_type;

	/*ii =*/ ret = icode_make_addrof(NULL, struct_lvalue, il);
/*	append_icode_list(il, ii);*/

	struct_lvalue->type = orig_type;
	return ret; /*ii->dat;*/
}

