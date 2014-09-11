/* 
 * Copyright (c) 2006 - 2010, Nils R. Weller
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
 * SPARC backend
 */
#include "backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include "scope.h"
#include "decl.h"
#include "type.h"
#include "decl.h"
#include "icode.h"
#include "functions.h"
#include "control.h"
#include "debug.h"
#include "token.h"
#include "error.h"
#include "functions.h"
#include "symlist.h"
#include "icode.h"
#include "stack.h"
#include "reg.h"
#include "subexpr.h"
#include "expr.h"
/* #include "x86_emit_gas.h" */
#include "inlineasm.h"
#include "sparc_emit_as.h"
#include "cc1_main.h"
#include "features.h"
#include "n_libc.h"

static FILE		*out;
struct emitter_sparc	*emit_sparc;

#define N_GPRS 32
#define N_FPRS 64 

struct reg			sparc_gprs[N_GPRS];
struct reg			*g_regs;
struct reg			*l_regs;
struct reg			*o_regs;
struct reg			*i_regs;

static struct reg		sparc_fprs[N_FPRS];
static struct vreg		saved_gprs[N_GPRS];
static struct stack_block	*saved_gprs_sb[N_GPRS];

/* XXX This SUCKS!! it is actually the save area start...
 * stack arguments are really at STACK_ARG_START+6*sparc_gprs[0].size
 * 01/04/08: Renamed
 */
#define ARG_SAVE_AREA_START ( \
	16 * sparc_gprs[0].size)

static int	callee_save_map[] = {
/* 0-3 */   0, 0, 0, 0,
/* 4-7 */   0, 0, 0, 0,
/* 8-11 */  0, 0, 0, 0,
/* 12-15 */ 0, 0, 0, 0,
/* 16-19 */ 1, 1, 1, 1, /* 16 - 23 = callee save temp */
/* 20-23 */ 1, 1, 1, 1,
/* 24-27 */ 0, 0, 0, 0,
/* 28-31 */ 0, 0, 1<<8, 0  /* 30 = callee save temp */
};

static int	floating_callee_save_map[] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 1, 1,
	1, 1, 1, 1,
	1, 1, 1, 1,
	1, 1, 1, 1,
	1, 1, 1, 1
};

static void
init_regs(void) {
	int		i;
	char		*p;
	static char	fpr_names[1024];
	static char	*names[] = {
		"g0", "g1", "g2", "g3", "g4", "g5", "g6", "g7",
		"o0", "o1", "o2", "o3", "o4", "o5", "o6", "o7",
		"l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7",
		"i0", "i1", "i2", "i3", "i4", "i5", "i6", "i7",
	};		

	g_regs = &sparc_gprs[0];
	o_regs = &sparc_gprs[8];
	l_regs = &sparc_gprs[16];
	i_regs = &sparc_gprs[24];

	for (i = 0; i < N_GPRS; ++i) {
		sparc_gprs[i].type = REG_GPR;
		sparc_gprs[i].allocatable = 1;
		if (backend->abi == ABI_SPARC64) {
			sparc_gprs[i].size = 8;
		} else {
			sparc_gprs[i].size = 4;
		}	
		sparc_gprs[i].name = names[i];
	}
	p = fpr_names;
	for (i = 0; i < N_FPRS; ++i) {
		sparc_fprs[i].type = REG_FPR;
		sparc_fprs[i].allocatable = 1;
		sparc_fprs[i].size = 8;
		sparc_fprs[i].name = p;
		p += sprintf(p, "f%d", i)+1;
	}

	/* Some registers with predefined meaning should not be allocated */
	/*
	 * 07/17/09: XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
	 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX!!!!!!!!!!!!!
	 * This should probably use reg_set_dedicated() to avoid errors
	 * like free_preg() setting allocatability! The matter should be
	 * investigated during the next SPARC debugging session
	 */
	for (i = 0; i < 8; ++i) {
		g_regs[i].allocatable = 0; /* Global registers */
	}	
	o_regs[6].allocatable = 0; /* stack pointer     o6 */
	i_regs[6].allocatable = 0; /* frame pointer     i6 */
	i_regs[7].allocatable = 0; /* 01/06/08: woah this was missing.. return address */
	
	tmpgpr = &g_regs[1];
	tmpgpr->allocatable = 0;

	/*
	 * tmpgpr2 is only used for 64bit address calculations
	 */
	if (backend->abi == ABI_SPARC64) {
		tmpgpr2 = &l_regs[7];
		tmpgpr2->allocatable = 0;
	}
	tmpfpr = &sparc_fprs[13];
	tmpfpr->allocatable = 0;
	if (picflag) {
		pic_reg = &l_regs[6];
		pic_reg->allocatable = 0;
	}	
}


static void
do_invalidate(struct reg *r, struct icode_list *il, int save) {
	/* Neither allocatable nor used means dedicated register */
	if (!r->allocatable && !r->used) {
		return;
	}
	free_preg(r, il, 1, save);
}

/*
 * XXX Hm should distinguish between function calls and other
 * invalidations
 * That would e.g. allow us not to save %i0-%i7 when doing a
 * function call (since a new register window without those
 * is created)
 */
static void
invalidate_gprs(struct icode_list *il, int saveregs, int for_fcall) {
	int	i;

	/* Save all gprs except g0-g7 */
	/*do_invalidate(&g_regs[1], il, saveregs) */
	for (i = 8; i < N_GPRS; ++i) {
		if (&sparc_gprs[i] == tmpgpr2
			|| &sparc_gprs[i] == pic_reg) {
			if (for_fcall && curfunc->pic_initialized) {
				/*
				 * Careful - we are using a PIC reg
				 * which is caller-save, so we have
				 * to save it here 
				 */
				free_preg(pic_reg, il, 1, 1);

				/* Restore unallocatability */
				pic_reg->allocatable = 0;
				pic_reg->used = 1;
			}
			continue;
		}
		do_invalidate(&sparc_gprs[i], il, saveregs);
	}
	for (i = 0; i < N_FPRS; ++i) {
		/*
		 * XXX this belongs into invalidate_fprs() or
		 * else rename this to invalidate_regs() ;-)
		 */
		do_invalidate(&sparc_fprs[i], il, saveregs);
	}
	(void) floating_callee_save_map;
}


static void
invalidate_except(struct icode_list *il, int save, int for_fcall,  ...) {
	int		i;
	struct reg	*except[8];
	struct reg	*arg;
	va_list		va;

	va_start(va, for_fcall);
	for (i = 0; (arg = va_arg(va, struct reg *)) != NULL; ++i) {
		except[i] = arg;
	}
	va_end(va);
	except[i] = NULL;

	for (i = 0; i < N_GPRS; ++i) {
		int	j;

		for (j = 0; except[j] != NULL; ++j) {
			if (&sparc_gprs[i] == except[j]) {
				break;
			}	
		}
		if (except[j] != NULL) {
			continue;
		}
		do_invalidate(&sparc_gprs[i], il, save);
	}	
}


static struct reg *
alloc_gpr(
	struct function *f, 
	int size, 
	struct icode_list *il,
	struct reg *dontwipe,
	
	int line) {

	if (backend->abi != ABI_SPARC64
		&& size == 8) {
		if (backend->multi_gpr_object) {
			backend->multi_gpr_object = 0;
		} else {
			backend->multi_gpr_object = 1;
		}
	}
	return generic_alloc_gpr(f,size,il,dontwipe,sparc_gprs,N_GPRS,
		callee_save_map, line);	
}	

/*
 * SPARC floating point register allocator. This is a little tricky
 * because we have 64 32bit FPRs which can be combined into pairs and
 * quadruples to form 64bit and 128bit double and long double sets.
 *
 * Register numbers have to be aligned, such that double may only
 * occupy a first register whose number is divisible by 2, and long
 * double one that is divisible by 4:
 *
 * [   f0   ][   f1   ][   f2   ][   f3   ][   f4   ]
 * |_________|_________|_________|_________|____ float slots
 * |___________________|___________________|____ double slots
 * |                                       |
 * |_______________________________________|____ long double slots
 *
 * We just always mark the corresponding slot allocated. This means
 * that if we're e.g. looking at f3 because we want to allocate a 32bit
 * float there, we also have to consider whether f0 is allocated to a
 * 4-fpr long double, in which case f3 is implicitly already in use!
 * Additionally we also have to look at f2 to check for the same
 * condition with double!
 *
 * XXX hmm this might have been a job for composed_of in struct reg..
 * but maybe not.
 */

#define GET_DOUBLE_BASE(regno) \
	(regno & ~1u)

#define GET_LDOUBLE_BASE(regno) \
	(regno & ~3u)

#define IS_DOUBLE_SLOT(base) \
	(sparc_fprs[base].vreg != NULL \
		&& sparc_fprs[base].vreg->type->code == TY_DOUBLE)

#define IS_LDOUBLE_SLOT(base) \
	(sparc_fprs[base].vreg != NULL \
		&& sparc_fprs[base].vreg->type->code == TY_LDOUBLE)

static struct reg *
alloc_fpr(struct function *f, int size, struct icode_list *il,
struct reg *dontwipe) {
	int			i;
	struct reg	*ret = NULL;
	int		limit;
	int		stepsize;
	int		base;
	static int	lastalloc = 0;

	if (size == 4) {
		/* Only lower 32 FPRs can be used for floats */
		limit = 32;
		stepsize = 1; /* Can use any fpr */
	} else {
		limit = 64;
		if (size == 8) {
			stepsize = 2; /* 2-reg-alignment */;
		} else { /* long double */
			stepsize = 4; /* 4-reg-alignment */
		}
	}
	(void) f; (void) size; (void) il; (void) dontwipe;

	for (i = 0; i < limit; i += stepsize) {
#define REGFREE(r) (!r.used && r.allocatable)
		if (REGFREE(sparc_fprs[i])) {
			/*
			 * For double and long double, we actually need
			 * multiple free 32bit FPRs
			 */
			if (stepsize > 1) {
				/* Need 2 or 4 regs */
				if (!REGFREE(sparc_fprs[i+1])) {
					continue;
				}
				if (stepsize == 4) {
					/* Need 4 regs */
					if (!REGFREE(sparc_fprs[i+2])
						|| !REGFREE(sparc_fprs[i+3])) {
						continue;
					}
				} else {
					/*
					 * Must be double - check if we are
					 * in an allocated long double set
				 	 */
					base = GET_LDOUBLE_BASE(i);
					if (base != i
						&& IS_DOUBLE_SLOT(base)) {
						if (!REGFREE(sparc_fprs[base])) {
							continue;
						}
					}
				}
			} else {
				/*
				 * This is a float - check if we are
				 * in an allocated double or long double
				 * register set
				 */
				base = GET_DOUBLE_BASE(i);
				if (base != i
					&& IS_DOUBLE_SLOT(base)) {
					if (!REGFREE(sparc_fprs[base])) {
						/* Already taken! */
						continue;
					}
				}
				base = GET_LDOUBLE_BASE(i);
				if (base != i
					&& IS_LDOUBLE_SLOT(base)) {
					if (!REGFREE(sparc_fprs[base])) {
						/* Already taken! */
						continue;
					}
				}
			}
			
			ret = &sparc_fprs[i];
			lastalloc = i;
			break;
		}
	}
	if (ret == NULL) {
		/*
		 * We have to free a register that is already in use.
		 * In case of double and long double, we need an entire
		 * set of 32bit registers. We need to inspect the vreg
		 * corresponding to our registers to determine whether
		 * we need to save multiple small items or jus a big
		 * one.
		 *
		 * Note that lastalloc may have been aligned for a
		 * different type!
		 */
		lastalloc &= ~(unsigned)(stepsize - 1); /* Align */	
		lastalloc += stepsize;
		lastalloc %= limit;
		ret = &sparc_fprs[lastalloc];

		if (stepsize == 1) {
			if (ret->vreg && ret->vreg->size == 4 && ret->used) {
				/* Free a float for new one! */
				free_preg(ret, il, 1, 1);
			} else {
				base = GET_DOUBLE_BASE(lastalloc);
				if (IS_DOUBLE_SLOT(base)
					&& !REGFREE(sparc_fprs[base])) {
					/* Using double slot */
					free_preg(&sparc_fprs[base], il, 1, 1);
				} else {
					/* Using long double slot */
					base = GET_LDOUBLE_BASE(lastalloc);
					free_preg(&sparc_fprs[base], il, 1, 1);
				}
			}
		} else if (stepsize == 2) {
			if (ret->vreg && ret->vreg->size == 8 && ret->used) {
				/* Free a double for a new one! */
				free_preg(ret, il, 1, 1);
			} else {
				base = GET_LDOUBLE_BASE(lastalloc);
				if (IS_LDOUBLE_SLOT(base)
					&& !REGFREE(sparc_fprs[base])) {
					free_preg(&sparc_fprs[base], il, 1, 1);
				} else {
					/*
					 * There must be one or two floats
					 * occupying our desired double slot
					 */
					if (!REGFREE(sparc_fprs[lastalloc])) {
						free_preg(&sparc_fprs[lastalloc],
							il, 1, 1);
					}
					if (!REGFREE(sparc_fprs[lastalloc+1])) {
						free_preg(
							&sparc_fprs[lastalloc+1],
							il, 1, 1);
					}
				}
			}
		} else /* if (stepsize == 4) */ {
			if (ret->vreg && ret->vreg->size == 16 && ret->used) {
				/* Free a long double for a new one! */
				free_preg(ret, il, 1, 1);
			} else {
				/*
				 * There may be up to 2 doubles and or up to
				 * 4 floats occupying our slot
				 */
				for (i = 0; i < 4; ++i) {
					if (!REGFREE(sparc_fprs[lastalloc+i])) {
						free_preg(&sparc_fprs[
							lastalloc+i], il, 1, 1);
					}
				}
			}
		}
	}
	ret->used = 1;
	return ret;
}

static int 
init(FILE *fd, struct scope *s) {
	out = fd;

	init_regs();

	if (asmflag && strcmp(asmname, "as") != 0) {
		(void) fprintf(stderr, "Unknown SPARC assembler `%s'\n",
			asmflag);
		exit(EXIT_FAILURE);
	}	
	emit = &sparc_emit_as;
	emit_sparc = &sparc_emit_sparc_as;
	
	backend->emit = emit;
	return emit->init(out, s);
}

static int
get_ptr_size(void) {
	if (backend->abi != ABI_SPARC64) {
		return 4;
	} else {
		return 8;
	}
}	

static struct type *
get_size_t(void) {
	if (backend->abi != ABI_SPARC64) {
		return make_basic_type(TY_UINT);
	} else {
		return make_basic_type(TY_ULONG);
	}
}

static struct type *
get_uintptr_t(void) {
	return make_basic_type(TY_ULONG);
}	

static struct type *
get_wchar_t(void) {
	return make_basic_type(TY_INT);
}



static size_t
get_sizeof_basic(int type) {
	switch (type) {
	case TY_ENUM:
		return 4; /* XXX */

	case TY_INT:
	case TY_UINT:
	case TY_LONG:
	case TY_ULONG:
		if (backend->abi == ABI_SPARC64) {
			if (IS_LONG(type)) {
				return 8;
			}
		}
		return 4;

	case TY_LLONG:
	case TY_ULLONG:
		return 8;

	case TY_CHAR:
	case TY_UCHAR:
	case TY_SCHAR:
	case TY_BOOL:
		return 1;

	case TY_SHORT:
	case TY_USHORT:
		return 2;

	case TY_FLOAT:
		return 4;

	case TY_DOUBLE:
		return 8;
	case TY_LDOUBLE:
		return 16;
	default:
	printf("err sizeof cannot cope w/ it, wuz %d\n", type); 
	abort();
		return 1; /* XXX */
	}
}

static void
do_ret(struct function *f, struct icode_instr *ip) {
	int	i;

	if (f->alloca_head != NULL) {
		struct stack_block	*sb;
		static struct vreg	rvr;

		rvr.stack_addr = f->alloca_regs;
		rvr.size = sparc_gprs[0].size;
		backend_vreg_map_preg(&rvr, &i_regs[0]);
		emit->store(&rvr, &rvr);
		backend_vreg_unmap_preg(&i_regs[0]);
		if (ip && ip->src_vreg && ip->src_vreg->is_multi_reg_obj) {
			rvr.stack_addr = f->alloca_regs->next;
			backend_vreg_map_preg(&rvr, &i_regs[1]);
			emit->store(&rvr, &rvr);
			backend_vreg_unmap_preg(&i_regs[1]);
		}	

		for (sb = f->alloca_head; sb != NULL; sb = sb->next) {
			emit->dealloca(sb, NULL);
		}

		rvr.stack_addr = f->alloca_regs;
		backend_vreg_map_preg(&rvr, &i_regs[0]);
		emit->load(&i_regs[0], &rvr);
		backend_vreg_unmap_preg(&i_regs[0]);
		if (ip && ip->src_vreg && ip->src_vreg->is_multi_reg_obj) {
			rvr.stack_addr = f->alloca_regs->next;
			backend_vreg_map_preg(&rvr, &i_regs[1]);
			emit->load(&i_regs[1], &rvr);
			backend_vreg_unmap_preg(&i_regs[1]);
		}	
	}
	if (f->vla_head != NULL) {
		struct stack_block	*sb;
		static struct vreg	rvr;

		rvr.stack_addr = f->alloca_regs;
		rvr.size = sparc_gprs[0].size;
		backend_vreg_map_preg(&rvr, &i_regs[0]);
		emit->store(&rvr, &rvr);
		backend_vreg_unmap_preg(&i_regs[0]);
		if (ip && ip->src_vreg && ip->src_vreg->is_multi_reg_obj) {
			rvr.stack_addr = f->alloca_regs->next;
			backend_vreg_map_preg(&rvr, &i_regs[1]);
			emit->store(&rvr, &rvr);
			backend_vreg_unmap_preg(&i_regs[1]);
		}	

		for (sb = f->vla_head; sb != NULL; sb = sb->next) {
			emit->dealloc_vla(sb, NULL);
		}

		rvr.stack_addr = f->alloca_regs;
		backend_vreg_map_preg(&rvr, &i_regs[0]);
		emit->load(&i_regs[0], &rvr);
		backend_vreg_unmap_preg(&i_regs[0]);
		if (ip && ip->src_vreg && ip->src_vreg->is_multi_reg_obj) {
			rvr.stack_addr = f->alloca_regs->next;
			backend_vreg_map_preg(&rvr, &i_regs[1]);
			emit->load(&i_regs[1], &rvr);
			backend_vreg_unmap_preg(&i_regs[1]);
		}
	}

	for (i = 8; i < N_GPRS; ++i) {
		if (saved_gprs[i].stack_addr != NULL) {
			emit->load(&sparc_gprs[i], &saved_gprs[i]);
		}
	}
	emit->freestack(f, NULL);
	emit->ret(ip);
}

/* XXX platform-independent?!?! used by amd64 */
void
store_preg_to_var(struct decl *d, size_t size, struct reg *r);

#define IS_UNION_OR_ARRAY(ty) \
	((ty->code == TY_UNION && ty->tlist == NULL) \
	 || (ty->tlist && ty->tlist->type == TN_ARRAY_OF))

/* Get total size for struct field se, including alignment (except last) */
#define TOTAL_BYTES(se) \
	(se->next? se->next->dec->offset - se->dec->offset: \
	 backend->get_sizeof_type(se->dec->dtype, NULL))


#define CUR_SAVE_AREA_OFFSET (ARG_SAVE_AREA_START + \
	*slots_used * 8 \
)



static void
do_map_parameter(
	struct function *f,
	int *slots_used,
	size_t *stack_bytes_used,
	struct sym_entry *se) {

	size_t			size;
	int			is_func_arg = 1;
	int			is_struct = 0;

	/*
	 * 01/05/07: Heavy changes... Now we think more in terms of ``slots''
	 * instead of gprs and fprs, and also assign fp arguments to stack
	 * save area slots. Hope this is correct now, needs more testing
	 */
	(void) f;

	size = backend->get_sizeof_type(se->dec->dtype,0);

	if ((se->dec->dtype->code == TY_STRUCT
		|| se->dec->dtype->code == TY_UNION)
		&& se->dec->dtype->tlist == NULL) {
		is_struct = 1;
	}

	if (is_integral_type(se->dec->dtype) || is_struct || se->dec->dtype->tlist) {
		if (*slots_used < 6) {
			if (is_struct) {
				/*
				 * Allocate storage for saving the struct -
				 * remember the caller only passed a
				 * pointer
				 */
				stack_align(curfunc, 16);

				/*
				 * 12/29/07: ATTENTION! We use the struct storage
				 * area to save the pointer to the source struct
				 * itself before copying is carried out! This is
				 * to avoid memcpy() for another struct trashing
				 * those pointers. So the allocated space must be
				 * at least 8 bytes
				 */
	
				se->dec->stack_addr =
					stack_malloc(curfunc, size >= 8? size: 8);
				is_func_arg = 0;
				stack_align(curfunc, 16);
			} else {
				/*
				 * The variable can be saved in the
				 * caller provided register save area
				 * (right-adjusted if below xword size)
				 */
				se->dec->stack_addr = make_stack_block(0, size);
				se->dec->stack_addr->offset = CUR_SAVE_AREA_OFFSET
					+ (sparc_gprs[0].size - size);
			}
			se->dec->stack_addr->from_reg =
				&i_regs[*slots_used];
		} else {
			/* Passed on stack */
			if (is_struct) {
				se->dec->stack_addr = make_stack_block(
					CUR_SAVE_AREA_OFFSET
					/* + (sparc_gprs[0].size - size)*/, size);

				/* XXX kludge... incompletec means from_reg points to stack block!!!!! */
				se->dec->dtype->incomplete = 1;
				se->dec->stack_addr->from_reg =
					(struct reg *)stack_malloc(curfunc, size);
				((struct stack_block *)se->dec->stack_addr->from_reg)->is_func_arg = 0;
				stack_align(curfunc, 16);
			} else {
				se->dec->stack_addr = make_stack_block(
					CUR_SAVE_AREA_OFFSET + (sparc_gprs[0].size - size), size);
			}
			is_func_arg = 1;
			
			*stack_bytes_used += sparc_gprs[0].size;	
		}
		++*slots_used;
	} else if (IS_FLOATING(se->dec->dtype->code)) {
		if (*slots_used < 16) {
			int	regs_needed = 1;
			int	right_adjusted = 0;

			/*
			 * 12/28/07: Align register number to multiple of two or
			 * four for double and long double
			 */
			switch (se->dec->dtype->code) {
			case TY_FLOAT:
				regs_needed = 1;
				right_adjusted = 1;
				break;
			case TY_DOUBLE:
				regs_needed = 2;
				break;
			case TY_LDOUBLE:
				if (*slots_used & 1) {
					/* Not 4-reg-aligned! */
					++*slots_used;
				}
				regs_needed = 4;
				break;
			}

			/*
			 * Having now aligned the register number for the type, and
			 * having determined the number of needed registers, we can
			 * reevaluate whether enough regs are available
			 */

			/*
			 * 12/25/07: Changed to stack_malloc from make_stack_block
			 * since FP values are not stored in the caller save area!
			 */
			se->dec->stack_addr = make_stack_block(0, size);
			se->dec->stack_addr->offset = CUR_SAVE_AREA_OFFSET;

			se->dec->stack_addr->from_reg =
				&sparc_fprs[*slots_used * 2 + right_adjusted];

			++*slots_used;
			if (regs_needed == 4) {
				++*slots_used;
			}
		} else {
			int	pad = 0;

			/* 01/05/08: Changed this... working now? */
			if (se->dec->dtype->code == TY_FLOAT) {
				*stack_bytes_used += 4;
				pad = 4;
			}
			se->dec->stack_addr = make_stack_block(
				CUR_SAVE_AREA_OFFSET + pad, size);
			*stack_bytes_used += size;
			++*slots_used;
			if (size == 16) {
				++*slots_used;
			}
		}
	} else {
		unimpl();
	}

	/*
	 * 12/29/07: This used to UNCONDITIONALLY set is_func_arg to 1! This is
	 * wrong for structs and unions passed by value. Those require LOCAL
	 * frame allocation (i.e. not in parent's register/stack save area), and
	 * are then copied using memcpy().
	 */
	se->dec->stack_addr->is_func_arg = is_func_arg;    /*1; */
}

static void
map_parameters(struct function *f, struct ty_func *proto) {
	int			i;
	int			slots_used = 0;
	size_t			stack_bytes_used = 0;
	struct sym_entry	*se;

	if (f->fty->variadic) {
		/*
		 * Allocate enough storage for all registers. If none 
		 * of the unprototyped variadic arguments are passed
		 * in registers (quite unlikely), then the allocation
		 * below is redundant
		 */
		f->fty->lastarg = alloc_decl();

		/* Register save area starts at 24 on 32bit ppc */
		/* XXX is 24 for 32bit and 48 for 64bit right?? */
#define REG_SAVE_AREA_OFFSET (6 * sparc_gprs[0].size)		
		f->fty->lastarg->stack_addr =
			make_stack_block(/*REG_SAVE_AREA_OFFSET*/
				ARG_SAVE_AREA_START, 0);
		f->fty->lastarg->stack_addr->is_func_arg = 1;
	}

	se = proto->scope->slist;
	if (f->proto->dtype->tlist->next == NULL
		&& (f->proto->dtype->code == TY_STRUCT
		|| f->proto->dtype->code == TY_UNION)) {
		/*
		 * Function returns struct/union - accomodate for
		 * hidden pointer (passed as first argument)
		 */
		size_t	ptrsize = backend->get_ptr_size();

		f->hidden_pointer = vreg_alloc(NULL, NULL, NULL, NULL);
		f->hidden_pointer->size = ptrsize;
		f->hidden_pointer->var_backed = alloc_decl();
		f->hidden_pointer->var_backed->dtype =
			n_xmemdup(f->proto->dtype, sizeof *f->proto->dtype);
		f->hidden_pointer->var_backed->dtype->tlist =
			alloc_type_node();
		f->hidden_pointer->type = f->hidden_pointer->var_backed->dtype;
		f->hidden_pointer->var_backed->dtype->tlist->type =
			TN_POINTER_TO;

		/* Pointer goes to register save area */
		f->hidden_pointer->var_backed->stack_addr =
			make_stack_block(0, ptrsize);
		f->hidden_pointer->var_backed->stack_addr->offset =
			ARG_SAVE_AREA_START;
		f->hidden_pointer->var_backed->stack_addr->from_reg =
			&i_regs[0];
		f->hidden_pointer->var_backed->stack_addr->is_func_arg = 1;
		++slots_used;
	}

	/*
	 * Allocate stack space for those arguments that were
	 * passed in registers, set addresses to existing
	 * space for those that were passed on the stack
	 */
	for (i = 0; i < proto->nargs; ++i, se = se->next) {
		do_map_parameter(f /*&gprs_used, &fprs_used*/  ,
			&slots_used,
			&stack_bytes_used, se);
	}
	if (f->fty->variadic) {
		if (slots_used >= 6) {
			/* Wow, all variadic stuff passed on stack */
			;
		} else {
			f->fty->lastarg->stack_addr->from_reg =
				&i_regs[slots_used];
		}
		f->fty->lastarg->stack_addr->offset +=
			slots_used * sparc_gprs[0].size + stack_bytes_used;
	}
}

static void
make_local_variables(struct function *f) {
	struct scope	*scope;
	int		i;
	size_t		size;

	for (scope = f->scope; scope != NULL; scope = scope->next) {
		struct stack_block	*sb;
		struct scope		*tmp;
		struct decl		**dec;
		size_t			align;

		for (tmp = scope; tmp != NULL; tmp = tmp->parent) {
			if (tmp == f->scope) {
				break;
			}
		}

		if (tmp == NULL) {
			/* End of function reached */
			break;
		}
		if (scope->type != SCOPE_CODE) continue;

		dec = scope->automatic_decls.data;
		for (i = 0; i < scope->automatic_decls.ndecls; ++i) {
			if (dec[i]->stack_addr != NULL) { /* XXX sucks */
				continue;
			} else if (IS_VLA(dec[i]->dtype->flags)) {
                                /*
                                 * 05/22/11: Handle pointers to VLAs properly;
                                 * We have to create a metadata block to
                                 * record dimension sizes, but we allocate
                                 * the pointers themselves on the stack
                                 *
                                 *   char (*p)[N];
                                 *
                                 * ... "p" on stack, N in metadata block
                                 */
                                if (dec[i]->dtype->tlist->type == TN_POINTER_TO) {
                                        ;
                                } else {
                                        continue;
                                }
			}

			size = backend->
				get_sizeof_decl(dec[i], NULL);

			align = align_for_cur_auto_var(dec[i]->dtype, f->total_allocated);
			if (align) {
				(void)stack_malloc(f, align);
			}
			sb = stack_malloc(f, size);

			sb->nbytes = size;
			dec[i]->stack_addr = sb;
		}
	}
}

static int
gen_function(struct function *f) {
	unsigned int		mask;
	int			i;
	int			nsaved;
	int			min_bytes_pushed;
	int			need_reiteration = 0;
	struct ty_func		*proto;
	struct icode_instr	*lastret = NULL;
	struct stack_block	*sb;
	struct sym_entry	*se;
	size_t			alloca_bytes = 0;
	size_t			vla_bytes = 0;

	
	/* XXX use emit->intro() ??? */
#if 0
	x_fprintf(out, "\t.section \".text\"\n");
#endif
	emit->setsection(SECTION_TEXT);

	/* 4 = 4 bytes, not bits?! */
	x_fprintf(out, "\t.align 4\n");
	if (f->proto->dtype->storage != TOK_KEY_STATIC) {
		x_fprintf(out, "\t.global %s\n", f->proto->dtype->name);
	}
	
	x_fprintf(out, "\t.type %s, #function\n", f->proto->dtype->name);
	emit->label(f->proto->dtype->name, 1);

	proto = f->proto->dtype->tlist->tfunc;

	/* Create space for saving frame pointer */
	f->total_allocated += sparc_gprs[0].size;

	map_parameters(f, proto);

	stack_align(f, 16);
	make_local_variables(f);
	stack_align(f, 16);

	/*
	 * Allocate storage for saving callee-saved registers
	 * (but defer saving them until sp has been updated)
	 */
	nsaved = 0;
	for (mask = 1, i = 0; i < N_GPRS; ++i, mask <<= 1) {
		if (f->callee_save_used & mask) {
			if (saved_gprs_sb[i] == NULL) {
				saved_gprs_sb[i] =
					make_stack_block(0, sparc_gprs[0].size);

				/*
				 * The frame pointer cannot be used yet
				 * because it needs to be saved as well
				 */
			}
			f->total_allocated += sparc_gprs[0].size;
			saved_gprs[i].stack_addr = saved_gprs_sb[i];
			saved_gprs[i].size = sparc_gprs[0].size;
			saved_gprs[i].stack_addr->offset =
				f->total_allocated;
			++nsaved;
		} else {
			saved_gprs[i].stack_addr = NULL;
		}	
	}
	f->callee_save_offset = f->total_allocated;

	/*
	 * Allocate storage for temporarily saving GPRs. Offsets need to
	 * be patched once more when the size of the entire stack frame
	 * is known :-(
	 */
	for (sb = f->regs_head; sb != NULL; sb = sb->next) {
		stack_align(f, sb->nbytes);
		f->total_allocated += sb->nbytes;
		sb->offset = f->total_allocated;
	}
	/*
	 * Allocate storage for saving alloca() pointers, and initialize
	 * it to zero (must be patched like temporary register storage)
	 */
	stack_align(f, sparc_gprs[0].size);
	for (sb = f->alloca_head; sb != NULL; sb = sb->next) {
		f->total_allocated += sb->nbytes;
		alloca_bytes += sb->nbytes;
		sb->offset = f->total_allocated;
	}

	/*
	 * Allocate storage for saving VLA data, and initialize
	 * it to zero (must be patched like temporary register storage)
	 */
	for (sb = f->vla_head; sb != NULL; sb = sb->next) {
		f->total_allocated += sb->nbytes;
		vla_bytes += sb->nbytes;
		sb->offset = f->total_allocated;
	}	
	if (f->alloca_head != NULL || f->vla_head != NULL) {
		/*
		 * Get stack for saving return value registers before
		 * performing free() on alloca()ted blocks
		 */
		f->alloca_regs = make_stack_block(0, sparc_gprs[0].size);
		f->total_allocated += sparc_gprs[0].size;
		f->alloca_regs->offset = f->total_allocated;
		f->alloca_regs->next = make_stack_block(0, sparc_gprs[0].size);
		f->total_allocated += sparc_gprs[0].size;
		f->alloca_regs->next->offset = f->total_allocated;
	}	
		
	stack_align(f, 16);

	/*
	 * The SPARC ABI requires the caller to allocate storage
	 * for saving argument registers and passing stack arguments,
	 * the latter of which is recorded by max_bytes_pushed.
	 * Unfortunately, it is possible that unrequested function
	 * calls must be generated, e.g. to perform software arithmetic
	 * with the __nwcc*() functions.
	 * Therefore, we always allocate a minimum save area to ensure
	 * that no surprises with hidden calls can happen.
	 * XXX obviously it may be beneficial to omit this if no
	 * calls actually happen 
	 */ 
	min_bytes_pushed = 6 * sparc_gprs[0].size + 20 * sparc_fprs[0].size; /* reg save area */

	if (f->max_bytes_pushed < min_bytes_pushed) {
		f->max_bytes_pushed = min_bytes_pushed;
		while (f->max_bytes_pushed % 8) ++f->max_bytes_pushed;
	}

	f->total_allocated += f->max_bytes_pushed; /* Parameter area */
/*	f->total_allocated += sparc_gprs[1].size; *//* saved sp */
	/* register window save area and hidden parameter  - 16+1 */
	f->total_allocated += 17 * sparc_gprs[0].size;

	/* 16 byte alignment */
	while (f->total_allocated % 16) ++f->total_allocated;

	if (f->total_allocated > 0) {
		emit->allocstack(f, f->total_allocated);
	}

	for (i = 1; i < N_GPRS; ++i) {
		if (saved_gprs[i].stack_addr != NULL) {
			/* Register is callee-save! */
			backend_vreg_map_preg(&saved_gprs[i], &sparc_gprs[i]);
			emit->store(&saved_gprs[i], &saved_gprs[i]);
			backend_vreg_unmap_preg(&sparc_gprs[i]);
			sparc_gprs[i].used = 0;
		}
	}

	/*
	 * Patch parameter offsets and save corresponding argument
	 * registers to stack, if necessary
	 */
	se = proto->scope->slist;
	for (i = 0; i < proto->nargs; ++i, se = se->next) {
		/*
		 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
		 * 01/05/08: This comment seems completely bogus since
		 * no patching at all is done on SPARC!!! Only thing
		 * done here is saving arg registers to stack areas
		 * Local variable offsets may be wrong - do those
		 * need patching????
		 *
		 *
		 * There are two cases where the stack address must
		 * be patched:
		 *
		 *      - The argument is passed on the stack
		 *      - The argument is passed in a register, but
		 *        backed by a register save area slot
		 *
		 * In either case, we need an offset into the stack
		 * frame of the caller, which can only now be
		 * computed.
		 *
		 * (this also applies to hidden_pointer for struct
		 * returns, which is passed in gpr3.)
		 *
		 * fp . . . . . . [frame start] [arguments ...]
		 * ^ lowest address           highest address ^
		 * So the offset (with fp) is the size of the entire
		 * stack frame plus the offset in the stack arg area
		 */
		if (se->dec->stack_addr->from_reg != NULL
			&& !se->dec->dtype->incomplete) { /* XXX kludge */
			/* Write argument register contents to stack */
			if ((se->dec->dtype->code == TY_STRUCT
				|| se->dec->dtype->code == TY_UNION)
				&& se->dec->dtype->tlist == NULL) {

				/*
				 * 12/29/07: Save register with address
				 * of struct/union passed by value. We
				 * can't memcpy() the struct/union yet
				 * because that could trash other
				 * argument registers!
				 */

				save_struct_ptr(se->dec);
				need_reiteration = 1;
			} else {
				/*
				 * Passed in register, but backed by
				 * register save area
				 */
				store_preg_to_var(se->dec,
					se->dec->stack_addr->nbytes,
					se->dec->stack_addr->from_reg);
			}
		} else {
			if ((se->dec->dtype->code == TY_STRUCT
				|| se->dec->dtype->code == TY_UNION)
				&& se->dec->dtype->tlist == NULL) {
				need_reiteration = 1;
			}
		}
	}

	if (need_reiteration) {
		/*
		 * 12/29/07; Now memcpy() structs/unions passed by value
		 * the struct pointers are currently stored at the stack
		 * location where the struct will be saved! (A minimum
		 * size of 8 bytes is guaranteed)
		 */
		se = proto->scope->slist;
		for (i = 0; i < proto->nargs; ++i, se = se->next) {
			if ( 1 /*se->dec->stack_addr->from_reg != NULL */) {
				if ((se->dec->dtype->code == TY_STRUCT
					|| se->dec->dtype->code == TY_UNION)
					&& se->dec->dtype->tlist == NULL) {
					if (se->dec->dtype->incomplete) {  /* XXX */
						/*
						 * 01/12/08: The struct pointer was
						 * passed on the stack. We can use
						 * %i0 as a temporary register because
						 * all argument registers have already
						 * been saved to the stack at this
						 * point
						 *
						 * XXX We abuse from_reg to hold the
						 * stack address to which the struct
						 * will be copied, so it must be saved
						 * first
						 */
						struct stack_block	*target_address;

						target_address = (struct stack_block *)
							se->dec->stack_addr->from_reg;
						se->dec->stack_addr->from_reg = &i_regs[0];
						target_address->from_reg =
							se->dec->stack_addr->from_reg;

						reload_struct_ptr(se->dec);

						/*
						 * Now we have the struct pointer in the
						 * register and can replace the pointer
						 * save area slot with the target address
					 	 */
						se->dec->stack_addr = target_address;

						/* Restore kludged flag */
						se->dec->dtype->incomplete = 0;
					} else {
						reload_struct_ptr(se->dec);
					}
					copy_struct_regstack(se->dec);
				}
			}
		}
	}

	if (f->hidden_pointer) {
		struct decl	*d = f->hidden_pointer->var_backed;

#if 0
		d->stack_addr->offset = f->total_allocated + d->stack_addr->offset;
#endif
		store_preg_to_var(d,
			d->stack_addr->nbytes,
			d->stack_addr->from_reg);
	}
	if (f->fty->variadic) {
		size_t	saved_offset;

		if (f->fty->lastarg->stack_addr->from_reg == NULL) {
			/* Entirely on stack */
#if 0
			f->fty->lastarg->stack_addr->offset =
				f->total_allocated +
				f->fty->lastarg->stack_addr->offset;
			unimpl();
#endif
			/*
			 * 12/25/07: This had the offset patchery above
			 * commented out, with just the unimpl in place.
			 * It turns out that there is apparently nothing
			 * to be done here
			 */
		} else {
			struct reg	*r =
				f->fty->lastarg->stack_addr->from_reg;

			saved_offset = f->fty->lastarg->stack_addr->offset;
			for (; r != &i_regs[6]; ++r) {
				store_preg_to_var(f->fty->lastarg, 
					sparc_gprs[0].size, r);
				f->fty->lastarg->stack_addr->offset +=
					sparc_gprs[0].size;
			}
			f->fty->lastarg->stack_addr->offset = saved_offset;
		}
	}
	if (f->alloca_head != NULL) {
		emit->zerostack(f->alloca_tail, alloca_bytes);
	}
	if (f->vla_head != NULL) {
		emit->zerostack(f->vla_tail, vla_bytes);
	}	

	if (xlate_icode(f, f->icode, &lastret) != 0) {
		return -1;
	}
	emit->outro(f);

	return 0;
}

#if XLATE_IMMEDIATELY

static int
gen_prepare_output(void) {
	if (picflag) {
		emit->pic_support();
	}
	return 0;
}

static int
gen_finish_output(void) {
	emit->static_init_vars(static_init_vars);
	emit->static_init_thread_vars(static_init_thread_vars);
	emit->static_uninit_vars(static_uninit_vars);
	emit->static_uninit_thread_vars(static_uninit_thread_vars);

	emit->global_extern_decls(global_scope.extern_decls.data,
		global_scope.extern_decls.ndecls);
	if (emit->extern_decls) {
		emit->extern_decls();
	}

	if (emit->support_buffers) {
		emit->support_buffers();
	}
	x_fflush(out);
	return 0;
}

#else

static int
gen_program(void) {
	struct function		*func;

	if (picflag) {
		emit->pic_support();
	}

#if 0
	emit->global_decls();
#endif
	emit->global_extern_decls(global_scope.extern_decls.data,
			global_scope.extern_decls.ndecls);
	emit->global_static_decls(global_scope.static_decls.data,
			global_scope.static_decls.ndecls);
#if 0
	emit->static_decls();
#endif
	emit->static_init_vars(static_init_vars);
	emit->static_uninit_vars(static_uninit_vars);
	emit->static_init_thread_vars(static_init_thread_vars);
	emit->static_uninit_thread_vars(static_uninit_thread_vars);

	emit->struct_inits(init_list_head);

	emit->empty();
	emit->strings(str_const); /* XXX bad */
	emit->fp_constants(float_const);

	for (func = funclist; func != NULL; func = func->next) {
		curfunc = func;
		if (gen_function(func) != 0) {
			return -1;
		}
		emit->empty();
		emit->empty();
	}
	x_fflush(out);

	return 0;
}

#endif

static void
pass_arg_stack(
	struct vreg *vr,
	size_t bytes_left,
	int *slots_used,
	size_t *stack_bytes_used,
	struct icode_list *il) {

	struct vreg	*dest;

	/*
	 * All types are passed as double words  
	 * (right-justified)
	 */
	size_t	size;
	int	is_struct = 0;


	(void) bytes_left;

	size = backend->get_sizeof_type(vr->type, NULL);
	if (vr->type->tlist
		&& vr->type->tlist->type ==
		TN_ARRAY_OF) {
		size = /*4*/sparc_gprs[0].size;
	} else if (vr->type->tlist == NULL
		&& (vr->type->code == TY_STRUCT
		|| vr->type->code == TY_UNION)) {
		/* Struct/union-by-value is passed by address */
		size = sparc_gprs[0].size;
		is_struct = 1;
	}

	if (/*4*/sparc_gprs[0].size - size > 0) {
		/* Need to right-adjust */
		*stack_bytes_used += /*4*/sparc_gprs[0].size - size;
	}

	dest = vreg_alloc(NULL, NULL, NULL, NULL);
	dest->type = vr->type;
	dest->size = vr->size;

	{
		int pad;
		if (size < sparc_gprs[0].size) {
			pad = sparc_gprs[0].size - size;
		} else {
			pad = 0;
		}
		dest->stack_addr = make_stack_block(CUR_SAVE_AREA_OFFSET + pad, dest->size);
	}


	dest->stack_addr->use_frame_pointer = 0;
	*stack_bytes_used += size;
	++*slots_used;
	if (dest->size > 8) {
		++*slots_used;
	}

	/*
	 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXX 12/12/07
	 * Should we really use tmpgr here????
	 */
{
	struct reg	*temp;
	int		i;

	if (!is_floating_type(vr->type)) {
		for (i = 0; i < 6; ++i) {
			reg_set_unallocatable(&o_regs[i]);
		}		
		if (is_struct) {
			reg_set_unallocatable(vr->pregs[0]);
		}

		temp = ALLOC_GPR(curfunc, backend->get_sizeof_type(vr->type, 0), il, NULL); 

		for (i = 0; i < 6; ++i) {
			reg_set_allocatable(&o_regs[i]);
		}		
		if (is_struct) {
			reg_set_allocatable(vr->pregs[0]);
		}
	} else {
		for (i = 0; i < 13; ++i) {
			reg_set_unallocatable(&sparc_fprs[i]);
		}

		temp = backend->alloc_fpr(curfunc, backend->get_sizeof_type(vr->type, 0), il, NULL);

		for (i = 0; i < 13; ++i) {
			reg_set_allocatable(&sparc_fprs[i]);
		}
	}

	if (is_struct) {
		vr = n_xmemdup(vr, sizeof *vr);
		vr->type = n_xmemdup(vr->type, sizeof *vr->type);
		append_typelist(vr->type, TN_POINTER_TO, NULL, NULL, 0);
		icode_make_copyreg( /*tmpgpr*/  temp, vr->pregs[0], NULL, NULL, il);
	} else {
		vreg_faultin(  /*tmpgpr*/  temp, NULL, vr, il, 0);
	}
	vreg_map_preg(dest, /* tmpgpr */ temp);
	icode_make_store(curfunc, dest, dest, il);
free_preg(temp, il, 1, 0);

	/*
	 * We have to ensure that the frontend never permanently uses
	 * tmpgpr
	 */
#if 0
	tmpgpr->vreg = NULL;
	tmpgpr->used = 0;
#endif
  }
}


void
put_arg_into_reg(
	struct reg *regset,
	int *index, int startat,
	struct vreg *vr,
	struct icode_list *il);

extern unsigned long	reg_offset; /* XXX mips ... */


/*
 * This function handles potential fpr register clashes. For example:
 *
 *    func(expr, expr2);
 *
 *    - expr2 may have been evaluated and loaded into fpr N/N+1 BEFORE the
 * function call is performed
 *    - expr is loaded into fpr N+1
 *    - expr2 is moved to N+2 where it is expected by func
 *
 * Here, if expr2 spans two registers, and expr just one (right-adjuted),
 * the load for expr will trash part of expr2 if we do not explicitly
 * check for this scenario
 */
static void
put_sparc_fp_arg_into_reg(
	struct reg *regset,
	int *index, int startat,
	struct vreg *vr,
	struct icode_list *il) {

	if (vr->type->code == TY_FLOAT) {
		/*
		 * Must be right-adjusted. Is this slot used for a double?
		 */
		if (regset[*index - 1].used) {
			if (!regset[*index - 1].allocatable) {
				puts("BUG?!?!: prev fpr not allocatable");
				abort();
			} else {
				free_preg(&regset[*index - 1], il, 1, 1);
			}
		} else {
			/*
			 * Is this slot the second half of a loaded long
			 * double? (odd-numbered)
			 */
			if (((*index - 1) & 1) == 0
				&& *index >= 3) { 
				struct reg	*r = &regset[*index - 3];

				if (r->vreg
					&& r->vreg->pregs[0]
					&& r->vreg->pregs[0] == r) {
					if (r->vreg->type->code == TY_LDOUBLE) {
						if (!r->allocatable) {
							puts("BUG?!?!? prev fpr not allocatable (long double)");
							abort();
						} else {
							free_preg(r, il, 1, 1);
						}
					}
				}
			}
		}
	} else if (vr->type->code == TY_DOUBLE) {
		/*
		 * Preceding slot may be long double
		 */
		if ((*index & 1) != 0) {
			struct reg	*r = &regset[*index - 1];

			if (r->vreg
				&& r->vreg->pregs[0]
				&& r->vreg->pregs[0] == r) {
				if (r->vreg->type->code == TY_LDOUBLE) {
					if (!r->allocatable) {
						puts("BUG?!?!? prev fpr not allocatable (long double)");
						abort();
					} else {
						free_preg(r, il, 1, 1);
					}
				}
			}
		}
	}
	put_arg_into_reg(regset, index, startat, vr, il);
}




static size_t
sparc_calc_stack_bytes(struct vreg **vrs, int nvrs, int *takes_struct);


static struct vreg *
icode_make_fcall(struct fcall_data *fcall, struct vreg **vrs, int nvrs,
struct icode_list *il)
{
	unsigned long		allpushed = 0;
	struct vreg		*tmpvr;
	struct vreg		*ret = NULL;
	struct vreg		*vr2;
	struct type		*ty;
	struct icode_instr	*ii;
	struct type_node	*tn;
	struct vreg		*struct_lvalue;
	size_t			stack_bytes_used = 0;
	int			i;
	int			takes_struct = 0;
	int			need_dap = 0;
	int			struct_return = 0;
	int			gprs_used = 0;
	int			fprs_used = 0;
	int			slots_used = 0;
	int			is_multi_reg_obj = 0;
	int			ret_is_anon_struct = 0;
	unsigned char		gprs_used_map[8];

        /*
	 * 23/12/08: Record numbers of actually used GPRS to avoid them
	 * from being mistaken for GPRS that were passed as a result of
	 * e.g. an FP argument incrementing the slot counter
	 */
	memset(gprs_used_map, 0, sizeof gprs_used_map);

	ty = fcall->calltovr->type;
	tmpvr = fcall->calltovr;

	tn = ty->tlist;
	if (tn->type == TN_POINTER_TO) {
		/* Called thru function pointer */
		tn = tn->next;
	}

	struct_lvalue = fcall->lvalue;

	if ((ty->code == TY_STRUCT
		|| ty->code == TY_UNION)
		&& tn->next == NULL) {
		struct_return = 1;

		/*
		 * This is a big struct that doesn't fit into
		 * regsiters, so it is necessary to pass a pointer
		 * to some space to store the result into in r4
		 */
		if (struct_lvalue == NULL || fcall->need_anon) {
			struct type_node	*tnsav;
			/*
			 * Result of function is not assigned so we need
			 * to allocate storage for the callee to store
			 * its result into
			 */

/* XXX ARGH This is bogus on ppc..need to preallocate */
#if 1 /* 06/17/08: Should go! Use rettype instead */ 
			tnsav = ty->tlist;
			ty->tlist = NULL;
#endif

			/*
			 * 12/29/07: Allocate anon return struct on stack frame
			 * instead of current temporary argument save area.
			 * Otherwise stuff is getting trashed.
			 */
			struct_lvalue = vreg_stack_alloc(ty, il, /*1*/ 1, NULL);

#if 1
			ty->tlist = tnsav;
#endif
			/*
			 * 08/05/08: Don't add to allpushed since struct is
			 * created on frame. This bug occurred on AMD64 and
			 * hasn't been verified on SPARC yet
			 */
	/*		allpushed += struct_lvalue->size;*/
			/*while (allpushed % 4*/ /* XXX *//*) ++allpushed;*/
			while (allpushed % sparc_gprs[0].size) ++allpushed;
			ret_is_anon_struct = 1;
		}

		/* Hidden pointer is passed in first GPR! */
		{
			struct reg 	*r;
			/*ii*/ r = make_addrof_structret(struct_lvalue, il);
			free_preg(&o_regs[0], il, 1, 1);
		
			icode_make_copyreg(&o_regs[0], r /*ii->dat*/,
				NULL, NULL, il);
			reg_set_unallocatable(&o_regs[0]);
		}
		gprs_used_map[slots_used] = 1;
		++slots_used;
	}

	/*
	 * 07/20/08: This wrongly took an implicit return type into account
	 * to determine whether default argument promotions are needed!
	 */
	if (fcall->functype->nargs == -1
		/*|| ty->implicit*/) {
		/* Need default argument promotions */
		need_dap = 1;
	}

	allpushed += sparc_calc_stack_bytes(vrs, nvrs, &takes_struct);
	allpushed *= 2;
	if (takes_struct) {
		/* memcpy() will be called to pass argument(s) */
		backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
	}

	if (/*allpushed > 0*/ 1) {
		if ((int)allpushed > curfunc->max_bytes_pushed) {
			curfunc->max_bytes_pushed = allpushed;
		}	
	}



	for (i = 0; i < nvrs; ++i) {
		/* 
		 * 12/23/08: Don't check type->implicit (which may indicate
		 * implicit int instead of implicit delaration through call)
		 */
		if ((fcall->functype->variadic
			&& i >= fcall->functype->nargs)
			|| /*fcall->calltovr->type->implicit*/ fcall->functype->nargs == -1) {
			need_dap = 1;
		}

		if (vrs[i]->parent) {
			vr2 = get_parent_struct(vrs[i]);
		} else {
			vr2 = NULL;
		}	
		if (is_integral_type(vrs[i]->type)
			|| vrs[i]->type->tlist	
			/*|| vrs[i]->from_const*/ ) {
			if (vrs[i]->type->tlist == NULL
				&& (IS_CHAR(vrs[i]->type->code)
					|| IS_SHORT(vrs[i]->type->code))) {
				vrs[i] = backend->
					icode_make_cast(vrs[i],
						make_basic_type(TY_INT), il);
			}
			if (slots_used < 6) {
				gprs_used_map[slots_used] = 1;
				put_arg_into_reg(o_regs,
					/* &gprs_used*/ &slots_used, 0, vrs[i], il);	
			} else {
				pass_arg_stack(vrs[i], 0,
					&slots_used, &stack_bytes_used, il);
			}
		} else if (vrs[i]->type->code == TY_STRUCT
			|| vrs[i]->type->code == TY_UNION) {
			int	regidx = -1;

			/*
			 * Structs/unions passed by value are really
			 * passed by address in the SPARC ABI
			 */
			struct reg	*addrreg;

			/*ii =*/addrreg = icode_make_addrof(NULL, vrs[i], il);
/*			append_icode_list(il, ii);*/
			vreg_map_preg(vrs[i], addrreg /*ii->dat*/);
			if (slots_used < 6) {
				if (addrreg /*ii->dat*/ != &o_regs[slots_used]) {
					free_preg(&o_regs[slots_used], il, 1, 1);
					icode_make_copyreg(&o_regs[slots_used],
						addrreg /*ii->dat*/, NULL, NULL, il);
				}

				regidx = slots_used;
#if 0
				++gprs_used;
#endif
				gprs_used_map[slots_used] = 1;
				++slots_used;
			} else {
				struct vreg	*vr;

				vr = n_xmemdup(vrs[i], sizeof *vrs[i]);
				vr->type = addrofify_type(vrs[i]->type);
				vr->size = backend->get_sizeof_type(vr->type, NULL);
				vreg_map_preg(vr, addrreg /*ii->dat*/);
				pass_arg_stack(/*vrs[i]*/vr, 0,
					&slots_used, &stack_bytes_used, il);
			}

			if (regidx == -1 || addrreg /*ii->dat*/ != &o_regs[regidx]) {
				free_preg(addrreg /*ii->dat*/, il, 1, 0);
			}

			/* 12/17/07: This (unallocatable) was missing! */
			if (regidx != -1) {
				reg_set_unallocatable(&o_regs[regidx]);
			}
		} else if (IS_FLOATING(vrs[i]->type->code)) {
			/*
			 * For variadic functions, floating point values
			 * go into gprs (floats are promoted to double)
			 */
			if (need_dap) {
				if (vrs[i]->type->code == TY_FLOAT) {
					vrs[i] = backend->
						icode_make_cast(vrs[i],
							make_basic_type(TY_DOUBLE), il);
				} else if (vrs[i]->type->code == TY_LDOUBLE) {
					/*
					 * Long double is tricky because
					 * it 1) is passed in two gprs, and
					 * 2) those gprs need to be aligned
					 */
					if (slots_used < 6) {
						if (slots_used & 1) {
							/* Not aligned! */
							++slots_used;
						}
					}
				}
				vreg_faultin(NULL, NULL, vrs[i], il, 0);

				if (slots_used < 6) {
					struct type	*oty = vrs[i]->type;

					vreg_anonymify(&vrs[i], NULL, NULL, il);
					free_preg(vrs[i]->pregs[0], il, 1, 1);
					vrs[i] = vreg_disconnect(vrs[i]);

					vrs[i]->type = make_basic_type(TY_ULONG);
					vrs[i]->size = 8;
					free_preg(&o_regs[slots_used], il, 1, 1);
					vreg_faultin(&o_regs[slots_used],
						NULL,
						vrs[i], il, 0);
					
					reg_set_unallocatable(&o_regs[slots_used]);

					++slots_used;
					if (oty->code == TY_LDOUBLE) {
						if (slots_used < 6) {
							struct vreg	*tmp;

							tmp = n_xmemdup(vrs[i],
								sizeof *vrs[i]);
							tmp->pregs[0] = NULL;
							free_preg(&o_regs[slots_used], il, 1, 1);
							vreg_faultin(
							&o_regs[slots_used],
							NULL, tmp, il, 0);

							/* 12/17/07: Unallocable missing */
							reg_set_unallocatable(
								&o_regs[slots_used]);
							++slots_used;
						} else {
							unimpl();
						}
					}
				} else {
					if (vrs[i]->type->code == TY_LDOUBLE) {
						/*
						 * 01/12/08: Align for long double if we're at an
						 * odd-numbered slot
						 */
						if (slots_used & 1) {
							++slots_used;
							stack_bytes_used += 8;
						}
					}
					pass_arg_stack(vrs[i], 0,
						&slots_used, &stack_bytes_used, il);
				}
			} else {
				if (slots_used < 16) {
					/*
					 * put_arg_into_reg() only increments by one; However
					 * for double and long double we need two or four
					 * FPRs, respectively
					 */
					fprs_used = slots_used * 2;
					if (vrs[i]->type->code == TY_FLOAT) {
						/*
						 * 12/28/07: As it turns out, floats are passed
						 * with double-FPRs too, and they are right-	
						 * adjusted, if the current register number is
						 * not odd-numberedd
						 */
						if ((fprs_used & 1) == 0) {
							++fprs_used;
						}
					} else if (vrs[i]->type->code == TY_LDOUBLE) {
						if (fprs_used & 3) {
							/* Not 4-reg-aligned */
							++slots_used;
							fprs_used += 2;
						}
					}

					/* 01/11/08: New function to save fprs if needed */
					put_sparc_fp_arg_into_reg(sparc_fprs,
						&fprs_used, 0, vrs[i], il);

					++slots_used;
					if (vrs[i]->type->code == TY_DOUBLE) {
						reg_set_unallocatable(&sparc_fprs[fprs_used]);
						++fprs_used;
					} else if (vrs[i]->type->code == TY_LDOUBLE) {
						reg_set_unallocatable(&sparc_fprs[fprs_used++]);
						reg_set_unallocatable(&sparc_fprs[fprs_used++]);
						reg_set_unallocatable(&sparc_fprs[fprs_used++]);
						++slots_used;
					}
				} else {
					if (vrs[i]->type->code == TY_LDOUBLE) {
						/*
						 * 01/12/08: Align for long double if we're at
						 * an odd-numbered slot
						 */
						if (slots_used & 1) {
							++slots_used;
							stack_bytes_used += 8;
						}
					}
					pass_arg_stack(vrs[i], 0,
						&slots_used, &stack_bytes_used, il);
				}
			}
		} else {
			unimpl();
		}	
		
		/* We can free the register(s) - marked unallocatable */
		/*free_pregs_vreg(vrs[i], il, 0, 0);*/
		if (vr2 && vr2->from_ptr && vr2->from_ptr->pregs[0]
			&& vr2->from_ptr->pregs[0]->vreg == vr2->from_ptr) {
			free_preg(vr2->from_ptr->pregs[0], il, 0, 0);
		}	
	}

	if (struct_return) {
        /* XXX uh-huh what's going on here?!!?! why this AGAIN??? */
		struct reg *r;
		/*ii*/r = make_addrof_structret(struct_lvalue, il);

		icode_make_copyreg(&o_regs[0], r /*ii->dat*/, NULL, NULL, il);

		free_preg(r /*ii->dat*/, il, 0, 0);
	}	

	gprs_used = slots_used >= 6? 6: slots_used;
	for (i = 0; i < gprs_used; ++i) {
		if (!gprs_used_map[i]) {
			/*
			 * 12/23/08: The GPR isn't actually used for an
			 * argument (presumably an FP arg caused it to
			 * be skipped), so it has to be freed during
			 * invalidation
			 */
			continue;
		}

		/* Don't save argument registers */
		reg_set_unused(&o_regs[i]);

		/*
		 * 12/29/07: This was restoring allocatability too early!
		 * If we mark the argument registers allocatable, they may
		 * be used to build addresses for any stores performed by
		 * the invalidate_gprs() below, thus trashing their
		 * contents. Note that we explicitly have to disassociate
		 * the vreg from the preg, since invalidate_gprs() won't
		 * do this because they are already marked unused
		 */
		o_regs[i].vreg = NULL;
		o_regs[i].allocatable = 0;
	}
	fprs_used = slots_used * 2;
	if (fprs_used > 32) {
		fprs_used = 32;
	}
	for (i = 0; i < fprs_used; ++i) {
		/* 12/29/07: Don't save fp argument registers */
		reg_set_unused(&sparc_fprs[i]);
		sparc_fprs[i].vreg = NULL;
		sparc_fprs[i].allocatable = 0;
	}

	if (!takes_struct) {
		backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
	}

	for (i = 0; i < gprs_used; ++i) {
		o_regs[i].allocatable = 1;
	}
	for (i = 0; i < fprs_used; ++i) {
		sparc_fprs[i].allocatable = 1;
	}

	if (ty->tlist->type == TN_POINTER_TO) {
		/* 
		 * Need to indirect through function pointer. It is
		 * necessary to mark argument registers unallocatable
		 * because the function pointer itself may be loaded
		 * indirectly, e.g. through a struct pointer, and
		 * that pointer then too needs to be faulted in
		 */
		for (i = 0; i < gprs_used; ++i) {
			o_regs[i].allocatable = 0;
		}

		/* 12/13/07 Don't use tmpgpr!!! */
		vreg_faultin(/*tmpgpr*/ NULL, NULL, tmpvr, il, 0);

		for (i = 0; i < gprs_used; ++i) {
			o_regs[i].allocatable = 1;
		}
		ii = icode_make_call_indir(tmpvr->pregs[0]);
		free_preg(tmpvr->pregs[0], il, 1, 0);
	} else {
		ii = icode_make_call(ty->name);
	}	
	append_icode_list(il, ii);

	ret = vreg_alloc(NULL, NULL, NULL, NULL);
	ret->type = ty;

	if ((ty->tlist->type == TN_POINTER_TO
		&& ty->tlist->next->next != NULL)
		|| (ty->tlist->type == TN_FUNCTION
		&& ty->tlist->next != NULL)) {	
		/* Must be pointer */
		ret->pregs[0] = &o_regs[0];
	} else {
#if 1 /* XXX Should go! Use rettype instead */
		struct type_node	*tnsav = ty->tlist;

		ty->tlist = NULL;
#endif
		if (backend->abi != ABI_SPARC64
			&& IS_LLONG(ty->code)) {
			is_multi_reg_obj = 2;
		}
		if (is_integral_type(ty)) {
			ret->pregs[0] = &o_regs[0];
			if (is_multi_reg_obj) {
				ret->pregs[1] = &o_regs[1];
			}
		} else if (ty->code == TY_FLOAT
			|| ty->code == TY_DOUBLE
			|| ty->code == TY_LDOUBLE) {
			ret->pregs[0] = &sparc_fprs[0];
		} else if (ty->code == TY_STRUCT
			|| ty->code == TY_UNION) {
			if (ret_is_anon_struct) {
				/*
				 * 08/16/07: Added this
				 */
				ret = struct_lvalue;
			}	
			ret->struct_ret = 1;
			ret->pregs[0] = NULL;
		} else if (ty->code == TY_VOID) {
			; /* Nothing! */
		}
#if 1
		ty->tlist = tnsav;
#endif
	}
	for (i = 0; i < 6; ++i) {
		reg_set_allocatable(&o_regs[i]);
	}

	if (ret->pregs[0] != NULL) {
		vreg_map_preg(ret, ret->pregs[0]);
	}
	if (is_multi_reg_obj) {
		vreg_map_preg2(ret, ret->pregs[1]);
		ret->is_multi_reg_obj = 2;
	}
	
	ret->type = n_xmemdup(ret->type, sizeof *ret->type);
	if (ret->type->tlist->type == TN_POINTER_TO) {
		copy_tlist(&ret->type->tlist, ret->type->tlist->next->next);
	} else {
		copy_tlist(&ret->type->tlist, ret->type->tlist->next);
	}	
	if (ret->type->code != TY_VOID || ret->type->tlist) {
		ret->size = backend->get_sizeof_type(ret->type, NULL);
	}

	return ret;
}

static int
icode_make_return(struct vreg *vr, struct icode_list *il) {
	struct icode_instr	*ii;
#if 0
	struct type_node	*oldtn;
#endif
	struct type		*rtype = curfunc->rettype; /*curfunc->proto->dtype;*/

	/* 06/17/08: Use rettype instead of type kludgery */
#if 0
	oldtn = rtype->tlist;
	rtype->tlist = rtype->tlist->next;
#endif

	if (vr != NULL) {
		if (is_integral_type(rtype)
			|| rtype->code == TY_ENUM /* 06/15/09: Was missing?!? */
			|| rtype->tlist != NULL) {
			/* XXX long long ?!?!!?!??!?? */
			if (vr->is_multi_reg_obj) {
				vreg_faultin(&i_regs[0],
					&i_regs[1], vr, il, 0);
			} else {
				vreg_faultin(&i_regs[0], NULL,
					vr, il, 0);
			}
		} else if (rtype->code == TY_FLOAT
			|| rtype->code == TY_DOUBLE
			|| rtype->code == TY_LDOUBLE) {
			/* XXX ldouble ... */
			vreg_faultin(&sparc_fprs[0], NULL, vr, il, 0);
		} else if (rtype->code == TY_STRUCT
			|| rtype->code == TY_UNION) {
			/* vr may come from pointer */
			vreg_faultin_ptr(vr, il);
			icode_make_copystruct(NULL, vr, il);
		}
	}
	ii = icode_make_ret(vr);
	append_icode_list(il, ii);

#if 0
	rtype->tlist = oldtn;
#endif
	return 0;
}

static void 
icode_prepare_op(
	struct vreg **dest,
	struct vreg **src,
	int op,
	struct icode_list *il) {

	vreg_faultin(NULL, NULL, *dest, il, 0);
	vreg_faultin(NULL, NULL, *src, il, 0);
	(void) op;
}


void
change_preg_size(struct vreg *ret, struct type *to, struct type *from,
	struct icode_list *il) {
	struct icode_instr	*ii;

	int	to_is_64bit = 0;
	int	from_is_64bit = 0;

	if (to->tlist == NULL) {
		if (IS_LLONG(to->code)
			|| (backend->abi == ABI_SPARC64
				&& IS_LONG(to->code))) {
			to_is_64bit = 1;
		}
	}
	if (from->tlist == NULL) {
		if (IS_LLONG(from->code)
			|| (backend->abi == ABI_SPARC64
				&& IS_LONG(from->code))) {
			from_is_64bit = 1;
		}
	}

	if (IS_CHAR(from->code) && IS_CHAR(to->code)) {
		if (from->sign == TOK_KEY_UNSIGNED
			&& to->code == TY_SCHAR) {
			unimpl();
		}
	} else if (to_is_64bit) {
		if (!from_is_64bit && !from->tlist) {
			if (backend->abi != ABI_SPARC64) {
				ret->pregs[1] = ret->pregs[0];
				ret->pregs[0] = ALLOC_GPR(curfunc
					, 0, il, NULL);
				if (from->sign != TOK_KEY_UNSIGNED) {
					unimpl();
				} else {
					ii = icode_make_setreg(
						ret->pregs[0], 0);
					append_icode_list(il, ii);
				}
			} else {
				if (from->sign == TOK_KEY_UNSIGNED
					|| from->code == TY_CHAR) {
					int	bits = 0;

					if (IS_CHAR(from->code)) {
						bits = 56;
					} else if (from->code == TY_USHORT) {
						bits = 48;
					} else if (from->code == TY_UINT) {
						bits = 32;
					} else {
						unimpl();
					}

					(void) bits;
					unimpl();
				} else {
					/* from signed */
					if (from->code == TY_SCHAR) {
					} else if (from->code == TY_SHORT) {
					} else if (from->code == TY_INT) {
					} else {
					}
					unimpl();
				}		
			}
		}
	} else if (from_is_64bit) {
		struct reg	*r = ret->pregs[0];

		if (backend->abi != ABI_SPARC64) {
			ret->pregs[0] = ret->pregs[1];
			free_preg(r, il, 1, 0);
		}	

		/*
		 * Seems r can become gpr0. I do not know why
		 * this is happening but it is terrible
		 */
		sparc_gprs[0].allocatable = sparc_gprs[0].used = 0;

		goto truncate_further;
	} else {
		/* Neither is long long, but they are different */ 
		int	needand;
		int	needextsh;
		int	needlwi;

truncate_further:
		needand = needextsh = needlwi = 0;

		if (to->code == TY_SCHAR) {
			needand = 0xff;
			needlwi = 24;
		} else if (to->code == TY_CHAR || to->code == TY_UCHAR) {
			needand = 0xff;
		} else if (to->code == TY_SHORT) {
			needand = 0xffff;
			needextsh = 1;
		} else if (to->code == TY_USHORT) {
			needand = 0xffff;
		} else if (to->code == TY_INT) {
		} else if (to->code == TY_UINT) {
			/*needand = 0xffffffff;*/
		} else if (to->code == TY_LONG) {
		} else if (to->code == TY_ULONG) {
			/*needand = 0xffffffff;*/
		}

		if (needand) {
			ii = icode_make_setreg(tmpgpr, needand);
			append_icode_list(il, ii);
			ii = icode_make_and(ret, NULL);
			append_icode_list(il, ii);
		}
		if (needlwi) {
			unimpl();
		}
		if (needextsh) {
			unimpl();
		}
	}
	sparc_gprs[0].allocatable = sparc_gprs[0].used = 0;
}


/*
 * Most of the time, instructions give meaning to data. This function
 * generates code required to convert virtual register ``src'' to type
 * ``to'' where necessary
 */
static struct vreg *
icode_make_cast(struct vreg *src, struct type *to, struct icode_list *il) {
	struct vreg		*ret;
	struct type		*from = src->type;
	struct type		*orig_to = to;
	struct icode_instr	*ii;

	ret = src;

	if (ret->type->tlist != NULL
		|| (ret->type->code != TY_STRUCT
		&& ret->type->code != TY_UNION)) {	
		vreg_anonymify(&ret, NULL, NULL /*r*/, il);
	}	

	/* XXX anonymify.. */
	if (ret == src) {
		ret = vreg_disconnect(src);
	}
	ret->type = to;

	if (to->code == TY_VOID) {
		if (to->tlist == NULL) {
			ret->size = 0;
			free_pregs_vreg(ret, il, 0, 0);
			return ret;
		}
	} else {
		ret->is_nullptr_const = 0;
	}	
	ret->size = backend->get_sizeof_type(to, NULL);

	if (from->tlist != NULL && to->tlist != NULL) {
		/*
		 * Pointers are always of same size
		 * and use same registers
		 */
		return ret;
	} else if (to->tlist != NULL) {
		/*
		 * Integral type to pointer type - cast to
		 * uintptr_t to get it to the same size
		 */
		to = backend->get_uintptr_t();
	}	
	
	if (to->code != from->code) {
		/*
		 * XXX source type may be trashed. This is perhaps a
		 * bug in vreg_anonymify()!!? this kludge fixes it
		 * for now ...
		 */
		src = n_xmemdup(src, sizeof *src);
		src->type = from;
		src->size = backend->get_sizeof_type(from, 0);
	}

	/*
	 * We may have to move the item to a different
	 * register as a result of the conversion
	 */
	if (to->code == from->code) {
		return ret; /* Nothing to do */
	} else if (to->tlist || from->tlist) {
		; /* XXX hmm */
	} else if (IS_FLOATING(to->code)) {
		if (backend->abi == ABI_SPARC64
			&& (IS_LONG(from->code) || 
				IS_LLONG(from->code))) {

			/*
			 * Load value as integer, then conv_fp it. That's
			 * it!
			 */
			struct vreg	*double_vreg;
			struct vreg	*tmpret;
			struct reg	*fpreg;
			int		to_size;

			to_size = backend->get_sizeof_type(to, NULL);

			/* Save integer to stack */
			tmpret = n_xmemdup(ret, sizeof *ret); /* XXX :-( */
			tmpret->type = from;
			tmpret->size = backend->get_sizeof_type(from, NULL);
			vreg_map_preg(tmpret, tmpret->pregs[0]);
			free_preg(tmpret->pregs[0], il, 1, 1);

			/* Now load as double */
			double_vreg = n_xmemdup(tmpret, sizeof *tmpret);
			double_vreg->type = make_basic_type(TY_DOUBLE);
			
			if (to_size == 4) {
				to_size = 8;
			}
			fpreg = backend->alloc_fpr(curfunc, to_size, il, NULL);
			vreg_faultin(fpreg, NULL, double_vreg,
				il, 0);
			vreg_map_preg(ret, fpreg);
			icode_make_conv_fp(ret->pregs[0], ret->pregs[0],
				to, from, il);
		} else if (!IS_FLOATING(from->code)) {
			struct vreg	*fp_vreg;

			/*
			 * We have to load the integer value into a
			 * floating point register and then perform
			 * a fitos/fitod on it 
		 	 */
			fp_vreg = ret; 

			if (IS_CHAR(from->code) || IS_SHORT(from->code)) {
				struct type	*ity = make_basic_type(TY_INT);

				if (from->code == TY_SCHAR
					|| from->code == TY_SHORT) {
					/* Have to sign-extend */

					icode_make_extend_sign(ret, ity,
						from, il);
				}
				from = ity; 
			}
			vreg_reinterpret_as(&fp_vreg, from, to, il);
			icode_make_conv_fp(fp_vreg->pregs[0],
				fp_vreg->pregs[0], to, from, il);

			vreg_map_preg(ret, fp_vreg->pregs[0]);
		} else if (to->code != from->code) {
			/*
			 * From fp to fp.
			 * Double to float means we can cut the 2-reg set
			 * Float to double means we may have to allocate a
			 * new 2-reg set (currently always done - the
			 * icode_make_conv_fp interface sucks)
			 */
			/* XXXXXXXXX need to aligned alloc reg !!!! */
			struct reg	*r;

			reg_set_unallocatable(ret->pregs[0]);
			r = backend->alloc_fpr(curfunc,
				backend->get_sizeof_type(to, NULL), il, 0);
			icode_make_conv_fp(r, ret->pregs[0], to, from, il);
			ret->pregs[0] = r;
		}
	} else if (IS_FLOATING(from->code)) {	
		struct reg	*r;
		struct vreg	*double_vreg;
		struct vreg	*int_vreg;

		/*
		 * ``to'' has already been found to be non-fp.
		 * We have to load as float/double (depending on
		 * whether the source integer is 32bit or 64bit)
		 * and then fitos/fitod it
		 */

		r = ALLOC_GPR(curfunc, 0, il, NULL);
		double_vreg = vreg_alloc(NULL, NULL, NULL, NULL);
		vreg_map_preg(double_vreg, ret->pregs[0]);
		double_vreg->type = from;
		double_vreg->size = backend->get_sizeof_type(from, NULL);
		icode_make_conv_fp(ret->pregs[0], ret->pregs[0], to, from, il);
		icode_make_store(curfunc, double_vreg, double_vreg, il);
		free_preg(ret->pregs[0], il, 1, 0);

		int_vreg = vreg_alloc(NULL, NULL, NULL, NULL);
		int_vreg->type = to;
		int_vreg->size = backend->get_sizeof_type(to, NULL);
		int_vreg->stack_addr = double_vreg->stack_addr;

		/*
		 * 05/22/08: Workaround for long double load kludge.
		 * XXX fix this once and for all!
		 */

		icode_make_sparc_load_int_from_ldouble(r, int_vreg, il);
		ret->pregs[0] = r;
	} else {
		int	to_size = backend->get_sizeof_type(to, NULL); 
		int	from_size = backend->get_sizeof_type(from, NULL); 
		unsigned needand = 0;

		if (to_size == from_size) {
			;
		} else if (to->tlist != NULL) {
			; /* XXX What 2 do */
		} else if (to_size < from_size) {
			/* Truncate */
			if (to->tlist != NULL) {
				/* XXX hmm */
			} else if (IS_CHAR(to->code)) {
				needand = 0xff;
			} else if (IS_SHORT(to->code)) {
				needand = 0xffff;
			} else if (IS_INT(to->code)
				|| IS_LONG(to->code)) {
				/* Must be from 64bit long or long long */
				needand = 0xffffffff;
			} else {
				unimpl();
			}
		} else {
			/* to_size > from_size - sign- or zero-extend */
			if (from->sign == TOK_KEY_UNSIGNED) {
				needand = 0xffffffff;
			} else {
				/* sign-extend */
				icode_make_extend_sign(ret, to, from, il);
			}
		}
		if (needand) {
			ii = icode_make_setreg(tmpgpr, needand);
			append_icode_list(il, ii);
			ii = icode_make_and(ret, NULL);
			append_icode_list(il, ii);
		}
	}

	vreg_set_new_type(ret, orig_to); /* because of uintptr_t stuff */
	vreg_map_preg(ret, ret->pregs[0]);
	if (backend->abi != ABI_SPARC64
		&& IS_LLONG(to->code)
		&& to->tlist == NULL) {
		ret->is_multi_reg_obj = 2;
		vreg_map_preg2(ret, ret->pregs[1]);
	} else {
		ret->is_multi_reg_obj = 0;
		ret->pregs[1] = NULL;
	}
	if (ret->type->code == TY_BOOL && ret->type->tlist == NULL) {
		boolify_result(ret, il);
	}	

	return ret;
}

static void
icode_make_structreloc(struct copystruct *cs, struct icode_list *il) {
	relocate_struct_regs(cs, &o_regs[0], &o_regs[1],
		&o_regs[2], il);
}

void
generic_icode_initialize_pic(struct function *f, struct icode_list *il) {
	if (!f->pic_initialized) {
		if (!pic_reg->dedicated) { /* 07/17/09: For MIPS until SPARC
					    * handles dedicated regs
					    * properly!*/
			free_preg(pic_reg, il, 1, 1);
		}
		f->pic_vreg = vreg_alloc(NULL,NULL,NULL,NULL);
		vreg_set_new_type(f->pic_vreg, make_void_ptr_type());
		vreg_map_preg_dedicated(f->pic_vreg, pic_reg);
		/*
		 * 07/17/09: The line below was discovered while
		 * porting to MIPS. It cannot possibly be right
		 * because we don't want to trash the PIC reg!
		 * Probably should set the flag to 0, or better
		 * yet use reg_set_dedicated()
		 */
/*		pic_reg->allocatable = 1;*/
		reg_set_dedicated(pic_reg);

		/*
		 * 07/17/09: For MIPS, it is always done in the
		 * function prologue
		 */
		if (backend->arch != ARCH_MIPS) {
			icode_make_initialize_pic(f, il);
		}
	} else {
		vreg_faultin_dedicated(pic_reg, NULL, f->pic_vreg, il, 0);
	}
}


static void
do_print_gpr(struct reg *r) {
	printf("%s=%d ", r->name, r->used);
	if (r->vreg && r->vreg->pregs[0] == r) {
		printf("<-> %p", r->vreg);
	}
}	

static void
debug_print_gprs(void) {
	int	i;
	
	for (i = 0; i < N_GPRS; ++i) {
		if ((i % 3) == 0) {
			printf("\t\t");
		} else {
			putchar('\t');
		}
		do_print_gpr(&sparc_gprs[i]);
		if (((i+1) % 3) == 0) {
			putchar('\n');
		}	
	}
}

static int
is_multi_reg_obj(struct type *t) {
	(void) t;
	if (backend->abi != ABI_SPARC64
		&& IS_LLONG(t->code)
		&& t->tlist == NULL) {
		return 2;
	} 
	return 0;
}	

static struct reg *
name_to_reg(const char *name) {
	(void) name;
	return NULL;
}

static struct reg *
asmvreg_to_reg(
	struct vreg **vr0,
	int ch,
	struct inline_asm_io *io,
	struct icode_list *il,
	int faultin) {

	struct vreg	*vr = *vr0;

	(void) ch; (void) io; (void) il; (void)faultin;

	if ((vr->type->code == TY_STRUCT || vr->type->code == TY_UNION)
		&& vr->type->tlist == NULL) {
		errorfl(io->expr->tok,
			"Cannot load struct/union into register");
	}	
	return NULL;
}

static char *
get_inlineasm_label(const char *tmpl) {
	char	*ret = n_xmalloc(strlen(tmpl) + sizeof "inlasm");
	sprintf(ret, "inlasm%s", tmpl);
	return ret;
}	

static struct reg *
get_abi_reg(int index, struct type *ty) {
	if (index == 0
		&& (is_integral_type(ty)
		|| ty->tlist != NULL)) {
		return &o_regs[0];
	} else {
		unimpl();
	}
	return NULL;
}

static struct reg *
get_abi_ret_reg(struct type *ty) {
	if (is_integral_type(ty) || ty->tlist != NULL) {
		return &o_regs[0];
	} else {
		unimpl();
	}
	/* NOTREACHED */
	return NULL;
}

/*
 * Calculates the amount of stack bytes needed to pass vrs to
 * a function, if any. This is because it's more convenient
 * to pre-allocated any needed space in advance, rather than
 * doing it when needed. Special care must be taken to ensure
 * that this stuff is always in sync with the code that
 * actually pushes the arguments onto the stack.
 *
 * It is assumed that the stack starts out being word aligned.
 *
 */
static size_t
sparc_calc_stack_bytes(struct vreg **vrs, int nvrs, int *takes_struct) {
	size_t	nbytes = 0;
	int	i;
	int	stackstruct = 0;

	for (i = 0; i < nvrs; ++i) {
		nbytes += sparc_gprs[0].size * 2;
		if (vrs[i]->type->code == TY_STRUCT
			|| vrs[i]->type->code == TY_UNION) {
			nbytes += sparc_gprs[0].size;
			stackstruct = 1;
		}
	}
	while (nbytes % 16) {
		++nbytes;
	}

	/* Allocate space for saving registers */
	if (stackstruct) {
		/* XXXXXXXX wow reg_offset seems broken!??! */
		reg_offset = nbytes;
		nbytes += 8 * 16;
		*takes_struct = 1;
	} else {
		*takes_struct = 0;
	}

	return nbytes;
}

static int
have_immediate_op(struct type *ty, int op) {
	(void) ty; (void) op;
	if (Oflag == -1) { /* XXX */
		return 0;
	}	
	return 0;
}	

struct backend sparc_backend = {
	ARCH_SPARC,
	0, /* ABI */
	0, /* multi_gpr_object */
	8, /* structure alignment (set by init()) */
	1, /* need pic initialization? */
	0, /* emulate long double? */
	0, /* relax alloc gpr order? */
	4095, /* max displacement */
	-4096, /* min displacement */
	have_immediate_op,
	init,
	is_multi_reg_obj,
	get_ptr_size,
	get_size_t,
	get_uintptr_t,
	get_wchar_t,
	get_sizeof_basic,
	get_sizeof_type,
	get_sizeof_elem_type,
	get_sizeof_decl,
	get_sizeof_const,
	get_sizeof_vla_type,
	get_align_type,
	gen_function,
#if XLATE_IMMEDIATELY
	gen_prepare_output,
	gen_finish_output,
#else
	gen_program,
#endif
	NULL,
	NULL,
	invalidate_gprs,
	invalidate_except,
	/*generic_*/ alloc_gpr,
	NULL,
	alloc_fpr,
	NULL,
	icode_make_fcall,
	icode_make_return,
	NULL,
	icode_prepare_op,
	NULL, /* prepare_load_addrlabel */
	icode_make_cast,
	icode_make_structreloc,
	generic_icode_initialize_pic, /* icode_initialize_pic */
	NULL, /* icode_complete_func */
	make_null_block,
	make_init_name,
	debug_print_gprs,
	name_to_reg,
	asmvreg_to_reg,
	get_inlineasm_label,
	do_ret,
	get_abi_reg,
	get_abi_ret_reg,
	generic_same_representation
};


