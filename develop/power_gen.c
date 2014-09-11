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
 * PowerPC backend
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
#include "power_emit_as.h"
#include "cc1_main.h"
#include "features.h"
#include "n_libc.h"

static FILE		*out;
struct emitter_power	*emit_power;

#define N_GPRS 32
#define N_FPRS 32


struct reg			power_gprs[N_GPRS];
struct reg			power_fprs[N_FPRS];
static struct vreg		saved_gprs[N_GPRS];
static struct stack_block	*saved_gprs_sb[N_GPRS];
struct vreg			float_conv_mask;

/*
 * 02/01/09: This definition should work for Linux/PPC64 and AIX/PPC32.
 * Untested with AIX/PPC64 yet
 */
#define STACK_ARG_START (6 * power_gprs[0].size)

/* 10/29/08: New */
#define CUR_SAVE_AREA_OFFSET (STACK_ARG_START + \
	*slots_used * power_gprs[0].size \
)

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
	static char	*names[] = {
		"0", "1", "2", "3", "4", "5", "6", "7",
		"8", "9", "10", "11", "12", "13", "14", "15",
		"16", "17", "18", "19", "20", "21", "22", "23",
		"24", "25", "26", "27", "28", "29", "30", "31"
	};		

	/* Registers are just named after their number */
	for (i = 0; i < N_GPRS; ++i) {
		power_gprs[i].type = REG_GPR;
		power_gprs[i].allocatable = 1;
		if (backend->abi == ABI_POWER64) {
			power_gprs[i].size = 8;
		} else {
			power_gprs[i].size = 4;
		}	
		power_gprs[i].name = names[i];
	}
	for (i = 0; i < N_FPRS; ++i) {
		power_fprs[i].type = REG_FPR;
		power_fprs[i].allocatable = 1;
		power_fprs[i].size = 8;
		power_fprs[i].name = names[i];
	}

	/* Some registers with predefined meaning should not be allocated */
	reg_set_dedicated(&power_gprs[0]); /* zero register */
	reg_set_dedicated(&power_gprs[1]); /* stack pointer */
	reg_set_dedicated(&power_gprs[2]); /* table of contents pointer */


	/*
	 * 11/26/08: The frame pointer wasn't set to unallocatable?!!?!?!?!
	 * That's just amazing...
	 */
	reg_set_dedicated(&power_gprs[31]); /* frame pointer */
	
	/*
	 * XXX gpr0 is a bad choice as temp register because it
	 * behaves differtly in different contexts. For example,
	 * lwz 0, addr
	 * stw 123, 0(0)
	 * ... crashes. I'm not sure which gpr is best to use yet,
	 * so I'm using gpr13 for now, which is the first non-
	 * volatile register
	 *
	 * 11/08/08: Second temp GPR for things like address calculations,
	 * where we may need a static variable plus a large offset that
	 * cannot be computed and added without an extra register
	 */ 
	if (sysflag == OS_AIX) {
		tmpgpr = &power_gprs[13];
		/*tmpgpr->allocatable = 0;*/
		reg_set_dedicated(tmpgpr);
		tmpgpr2 = &power_gprs[14];
		/*tmpgpr2->allocatable = 0;*/
		reg_set_dedicated(tmpgpr2);
	} else {
		/*
		 * 10/17/08: On Linux, r13 is reserved as thread ID register, so use
		 * r14 as a temp reg instead!
		 */
		power_gprs[13].allocatable = 0; /* thread ID */
		tmpgpr = &power_gprs[14];
		/*tmpgpr->allocatable = 0;*/
		reg_set_dedicated(tmpgpr);
		tmpgpr2 = &power_gprs[15];
		/*tmpgpr2->allocatable = 0;*/
		reg_set_dedicated(tmpgpr2);
	}

	tmpfpr = &power_fprs[13];
	/*tmpfpr->allocatable = 0;*/
	reg_set_dedicated(tmpfpr);

	if (mintocflag) {
		if (sysflag == OS_AIX) {
			pic_reg = &power_gprs[15];
		} else {
			pic_reg = &power_gprs[16];
		}
		reg_set_dedicated(pic_reg);
	}
}


static void
do_invalidate(struct reg *r, struct icode_list *il, int save) {
	/* Neither allocatable nor used means dedicated register */
	if ((!r->allocatable && !r->used) || r->dedicated) {
		return;
	}

	free_preg(r, il, 1, save);
}

/*
 * XXX Hm should distinguish between function calls and other
 * invalidations
 */
static void
invalidate_gprs(struct icode_list *il, int saveregs, int for_fcall) {
	int	i;

	(void) for_fcall;
	for (i = 0; i < N_GPRS; ++i) {
		do_invalidate(&power_gprs[i], il, saveregs);
	}
	for (i = 0; i < N_FPRS; ++i) {
		/*
		 * XXX this belongs into invalidate_fprs() or
		 * else rename this to invalidate_regs() ;-)
		 */
		do_invalidate(&power_fprs[i], il, saveregs);
	}
	(void) floating_callee_save_map;
}


static void
invalidate_except(struct icode_list *il, int save, int for_fcall, ...) {
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
			if (&power_gprs[i] == except[j]) {
				break;
			}	
		}
		if (except[j] != NULL) {
			continue;
		}
		do_invalidate(&power_gprs[i], il, save);
	}	
}


static struct reg *
alloc_gpr(
	struct function *f, 
	int size, 
	struct icode_list *il,
	struct reg *dontwipe,
	
	int line) {

	if (backend->abi != ABI_POWER64
		&& size == 8) {
		if (backend->multi_gpr_object) {
			backend->multi_gpr_object = 0;
		} else {
			backend->multi_gpr_object = 1;
		}
	}

	return generic_alloc_gpr(f,size,il,dontwipe,power_gprs,N_GPRS,
		callee_save_map, line);	
}	

static struct reg *
alloc_fpr(struct function *f, int size, struct icode_list *il,
struct reg *dontwipe) {
	int			i;
	struct reg	*ret = NULL;

	(void) f; (void) size; (void) il; (void) dontwipe;

	for (i = 0; i < N_FPRS; ++i) {
		if (!power_fprs[i].used && power_fprs[i].allocatable) {
			ret = &power_fprs[i];
			break;
		}
#if 0
		/* XXX this does not work, I wonder why!!!! */
		ret = generic_alloc_gpr(f, size, il, NULL, power_fprs+1,
			N_FPRS - 1,
			NULL, 0);
#endif
	}
	if (ret == NULL) {
		ret = &power_fprs[13];
		free_preg(ret, il, 1, 1);
	}
	ret->used = 1; /* 11/19/08: Wow... this was missing */
	return ret;
}

static int 
init(FILE *fd, struct scope *s) {
	out = fd;

	if (sysflag == OS_AIX) {
		backend->emulate_long_double = 0;
	} else {
		backend->emulate_long_double = 1;
	}

	init_regs();

	if (asmflag && strcmp(asmname, "as") != 0) {
		(void) fprintf(stderr, "Unknown PowerPC assembler `%s'\n",
			asmflag);
		exit(EXIT_FAILURE);
	}	
	emit = &power_emit_as;
	emit_power = &power_emit_power_as;
	
	backend->emit = emit;
	return emit->init(out, s);
}

static int
get_ptr_size(void) {
	if (backend->abi != ABI_POWER64) {
		return 4;
	} else {
		return 8;
	}
}	

static struct type *
get_size_t(void) {
	if (backend->abi != ABI_POWER64) {
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
		if (backend->abi == ABI_POWER64) {
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
		if (sysflag == OS_AIX) {
			return 8;
		} else {
			return 16;
		}
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

		/*
		 * 11/17/08: XXXXXXX Woah shouldn't this (and the VLA stuff)
		 * be handling floating point as well!?
		 */

		rvr.stack_addr = f->alloca_regs;
		rvr.size = power_gprs[0].size;
		backend_vreg_map_preg(&rvr, &power_gprs[3]);
		emit->store(&rvr, &rvr);
		backend_vreg_unmap_preg(&power_gprs[3]);
		if (ip && ip->src_vreg && ip->src_vreg->is_multi_reg_obj) {
			rvr.stack_addr = f->alloca_regs->next;
			backend_vreg_map_preg(&rvr, &power_gprs[4]);
			emit->store(&rvr, &rvr);
			backend_vreg_unmap_preg(&power_gprs[4]);
		}	

		for (sb = f->alloca_head; sb != NULL; sb = sb->next) {
			emit->dealloca(sb, NULL);
		}

		rvr.stack_addr = f->alloca_regs;
		backend_vreg_map_preg(&rvr, &power_gprs[3]);
		emit->load(&power_gprs[3], &rvr);
		backend_vreg_unmap_preg(&power_gprs[3]);
		if (ip && ip->src_vreg && ip->src_vreg->is_multi_reg_obj) {
			rvr.stack_addr = f->alloca_regs->next;
			backend_vreg_map_preg(&rvr, &power_gprs[4]);
			emit->load(&power_gprs[4], &rvr);
			backend_vreg_unmap_preg(&power_gprs[4]);
		}	
	}
	if (f->vla_head != NULL) {
		struct stack_block	*sb;
		static struct vreg	rvr;

		rvr.stack_addr = f->alloca_regs;
		rvr.size = power_gprs[0].size;
		backend_vreg_map_preg(&rvr, &power_gprs[3]);
		emit->store(&rvr, &rvr);
		backend_vreg_unmap_preg(&power_gprs[3]);
		if (ip && ip->src_vreg && ip->src_vreg->is_multi_reg_obj) {
			rvr.stack_addr = f->alloca_regs->next;
			backend_vreg_map_preg(&rvr, &power_gprs[4]);
			emit->store(&rvr, &rvr);
			backend_vreg_unmap_preg(&power_gprs[4]);
		}	

		for (sb = f->vla_head; sb != NULL; sb = sb->next) {
			emit->dealloc_vla(sb, NULL);
		}

		rvr.stack_addr = f->alloca_regs;
		backend_vreg_map_preg(&rvr, &power_gprs[3]);
		emit->load(&power_gprs[3], &rvr);
		backend_vreg_unmap_preg(&power_gprs[3]);
		if (ip && ip->src_vreg && ip->src_vreg->is_multi_reg_obj) {
			rvr.stack_addr = f->alloca_regs->next;
			backend_vreg_map_preg(&rvr, &power_gprs[4]);
			emit->load(&power_gprs[4], &rvr);
			backend_vreg_unmap_preg(&power_gprs[4]);
		}	
	}

	for (i = 0; i < N_GPRS; ++i) {
		if (saved_gprs[i].stack_addr != NULL) {
			emit->load(&power_gprs[i], &saved_gprs[i]);
		}
	}
	emit->freestack(f, NULL);
	if (backend->abi == ABI_POWER64) {
		x_fprintf(out, "\tld 31, -8(1)\n");
	} else {	
		x_fprintf(out, "\tlwz 31, -4(1)\n");
	}	
	emit->ret(ip);
}

static struct reg *
get_abi_reg(int index, struct type *ty) {
	if (index == 0
		&& (is_integral_type(ty)
		|| ty->tlist != NULL)) {
		return &power_gprs[3];
	} else {
		unimpl();
	}	
	return NULL;
}	

static struct reg *
get_abi_ret_reg(struct type *ty) {
	if (is_integral_type(ty)
		|| ty->tlist != NULL) {
		return &power_gprs[3];
	} else {
		unimpl();
	}
	/* NOTREACHED */
	return NULL;
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

static void
do_map_parameter(
	struct function *f,
	int *fprs_used,
	int *slots_used,
	size_t *stack_bytes_used,
	struct sym_entry *se) {

	size_t			size;

	(void) f;


	size = backend->get_sizeof_type(se->dec->dtype,0);
	if (is_integral_type(se->dec->dtype)
		|| se->dec->dtype->tlist != NULL) {
		if (/**gprs_used*/ *slots_used < 8) {
			se->dec->stack_addr = make_stack_block(0, size);
			se->dec->stack_addr->offset = CUR_SAVE_AREA_OFFSET;
		/* Right-adjust for small types */

                        /* Right-adjust for small types */

			if (backend->abi != ABI_POWER64 && size > 4) {
				/*
				 * 01/28/09: Don't adjust for long long on 32bit
				 * PPC! This trashed the offset
				 */
				;
			} else {
				se->dec->stack_addr->offset += power_gprs[0].size - size;
			}

			se->dec->stack_addr->from_reg =
				&power_gprs[3 + *slots_used /*gprs_used*/];
		} else {
			/*
			 * 01/29/08: This attempted to right-adjust (gprsize - size)
			 * even for long long items on PPC32
			 */
			se->dec->stack_addr = make_stack_block(
				/*STACK_ARG_START+ *stack_bytes_used*/
				CUR_SAVE_AREA_OFFSET +
				 ((size > power_gprs[0].size)? 0: (power_gprs[0].size - size)), 
				 size);
			/**stack_bytes_used +=4;*/ /* XXX */
			*stack_bytes_used += power_gprs[0].size;	
			if (backend->is_multi_reg_obj(se->dec->dtype)) {
				*stack_bytes_used += power_gprs[0].size;
			}
		}
		++*slots_used;
		if (backend->is_multi_reg_obj(se->dec->dtype)) {
			++*slots_used;
		}
	} else if (IS_FLOATING(se->dec->dtype->code)) {
		/*
		 * 10/30/08: Use 12 registers instead of 8 for argument passing
		 */
		if (/**fprs_used*/ (fprs_used? *fprs_used: *slots_used) < 13) {
			se->dec->stack_addr = make_stack_block(0, size);

			/*
			 * 10/30/08: Setting offset was completely missing?!?
			 */
			 #if 0
			se->dec->stack_addr->offset=  *slots_used * power_gprs[0].size;
			#endif

			se->dec->stack_addr->offset = CUR_SAVE_AREA_OFFSET;
			se->dec->stack_addr->from_reg =
				&power_fprs[1 + (fprs_used? *fprs_used: *slots_used)];
			if (fprs_used != NULL) {
				++*fprs_used;
			}
		} else {
		/* XXX broken */
			int	pad = 0;

			if (se->dec->dtype->code == TY_FLOAT) {
				*stack_bytes_used += 4;
				pad = 4;
			}
			se->dec->stack_addr = make_stack_block(
				/*STACK_ARG_START+ *stack_bytes_used*/
				CUR_SAVE_AREA_OFFSET + pad, size);
			*stack_bytes_used += /*power_gprs[0].*/size;	
		}
		++*slots_used;
		if (backend->abi == ABI_POWER32) {
			/*
			 * 01/26/08: On PPC32 double takes up two
			 * 32bit argument slots. Long double takes
			 * two (on AIX) or four (on Linux)
			 */
			if (se->dec->dtype->code == TY_DOUBLE
				|| se->dec->dtype->code == TY_LDOUBLE) {
				++*slots_used;
				if (sysflag != OS_AIX && se->dec->dtype->code == TY_LDOUBLE) {
					*slots_used  += 2;
				}
			}
		} else  {
			/*
			 * 64bit ABI - On Linux this requires two
			 * argument slots for an 128bit long double
			 */
			if (se->dec->dtype->code == TY_LDOUBLE) {
				if (sysflag != OS_AIX) {
					++*slots_used;
				}
			}
		}
	} else if (se->dec->dtype->code == TY_STRUCT
		|| se->dec->dtype->code == TY_UNION) {
		int temp;

		se->dec->stack_addr = make_stack_block(CUR_SAVE_AREA_OFFSET,
			se->dec->dtype->tstruc->size);
		*stack_bytes_used += se->dec->dtype->tstruc->size;
		if (*slots_used < 8) {
			/*
			 * 05/22/11: Account for empty structs (a GNU
			 * C silliness) being passed
			 */
			if (se->dec->dtype->tstruc->size == 0) {
				se->dec->stack_addr->from_reg = NULL;
			} else {
				se->dec->stack_addr->from_reg =
					&power_gprs[3 + *slots_used];
			}
		}

		temp = se->dec->dtype->tstruc->size;
		while (temp > 0) {
			++*slots_used;
			temp -= power_gprs[0].size;
		}
	} else {
		unimpl();
	}
	se->dec->stack_addr->is_func_arg = 1;
}

static void
map_parameters(struct function *f, struct ty_func *proto) {
	int			i;
	int			fprs_used = 0;
	int			*p_fprs_used;
	int			slots_used = 0;
	size_t			stack_bytes_used = 0;
	struct sym_entry	*se;

	if (sysflag == OS_AIX) {
		p_fprs_used = &fprs_used;
	} else {
		p_fprs_used = NULL;
	}

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
#define REG_SAVE_AREA_OFFSET (6 * power_gprs[0].size)		
		f->fty->lastarg->stack_addr =
			make_stack_block(/*REG_SAVE_AREA_OFFSET*/STACK_ARG_START, 0);
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
		f->hidden_pointer->var_backed->dtype->tlist->type =
			TN_POINTER_TO;
		f->hidden_pointer->type = f->hidden_pointer->var_backed->dtype;
		f->hidden_pointer->var_backed->stack_addr =
			make_stack_block(/*24*/REG_SAVE_AREA_OFFSET, ptrsize);
		f->hidden_pointer->var_backed->stack_addr->from_reg =
			&power_gprs[3];
		++slots_used;
	}	

	/*
	 * Allocate stack space for those arguments that were
	 * passed in registers, set addresses to existing
	 * space for those that were passed on the stack
	 */
	for (i = 0; i < proto->nargs; ++i, se = se->next) {
		do_map_parameter(f, /*&gprs_used, &fprs_used,*/
			p_fprs_used,
			&slots_used,
			&stack_bytes_used, se);
	}
	if (f->fty->variadic) {
		if (/*gprs_used*/slots_used >= 8) {
			/* Wow, all variadic stuff passed on stack */
		} else {
			/*
			 * (64) 32 bytes were allocated, skip as many as
			 * necessary
			 */
			f->fty->lastarg->stack_addr->from_reg =
				&power_gprs[3 + /*gprs_used*/slots_used];
		}
		f->fty->lastarg->stack_addr->offset +=
			slots_used * power_gprs[0].size /*+ stack_bytes_used*/;
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

			/*
			 * 10/28/08: Copied SPARC way of aligning for
			 * local variables
			 */
			size = backend->
				get_sizeof_decl(dec[i], NULL);
			size = backend->
				get_sizeof_decl(dec[i], NULL);
			align = align_for_cur_auto_var(dec[i]->dtype, f->total_allocated);
			if (align) {
				(void)stack_malloc(f, align);
			}

			sb = stack_malloc(f, size/*+align*/);
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
	struct ty_func		*proto;
	struct scope		*scope;
	struct icode_instr	*lastret = NULL;
	struct stack_block	*sb;
	struct sym_entry	*se;
	size_t			alloca_bytes = 0;
	size_t			vla_bytes = 0;

	
	/* XXX use emit->intro() ??? */
	if (sysflag == OS_AIX) {
		x_fprintf(out, "\t.csect .text[PR]\n");
	} else {
		/*	x_fprintf(out, "\t.section \".text\"\n");*/
		/*
		 * 10/28/08: Linux wants functions to be in section .opd
		 * instead of .text, or we'll get an illegal instruction
		 * error when the function is called. No idea what this is
		 * about yet
		 */
        	x_fprintf(out, "\t.section \".opd\",\"aw\"\n");
	}

	/* Instructions are 4 bytes in PPC64 too! */
	x_fprintf(out, "\t.align 2\n");
	if (f->proto->dtype->storage != TOK_KEY_STATIC) {
		x_fprintf(out, "\t.globl %s\n", f->proto->dtype->name);
		if (sysflag == OS_AIX) {
			x_fprintf(out, "\t.globl .%s\n", f->proto->dtype->name);
		}
	}
	
	if (sysflag == OS_AIX) {
		x_fprintf(out, "\t.csect %s[DS]\n", f->proto->dtype->name);
	}

	emit->label(f->proto->dtype->name, 1);
	if (sysflag == OS_AIX) {
		if (backend->abi == ABI_POWER64) {
			x_fprintf(out, "\t.llong ");
		} else {
			x_fprintf(out, "\t.long ");
		}
		x_fprintf(out, ".%s, TOC[tc0], 0\n", f->proto->dtype->name);
		x_fprintf(out, "\t.csect .text[PR]\n");
	} else {
		if (backend->abi == ABI_POWER64) {
			x_fprintf(out, "\t.quad ");
		} else {
			x_fprintf(out, "\t.long ");
		}
		x_fprintf(out, ".L.%s, .TOC.@tocbase, 0\n", f->proto->dtype->name);
		x_fprintf(out, ".previous\n");
		x_fprintf(out, "\t.section \".text\"\n");
	}

	/*emit->label(f->proto->dtype->name, 1);*/
	if (sysflag == OS_AIX) {
		x_fprintf(out, ".%s:\n", f->proto->dtype->name);
	} else {
		x_fprintf(out, ".L.%s:\n", f->proto->dtype->name);
	}

	proto = f->proto->dtype->tlist->tfunc;

	/* Create space for saving frame pointer */
	f->total_allocated += power_gprs[31].size;

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
					make_stack_block(0, power_gprs[0].size);

				/*
				 * The frame pointer cannot be used yet
				 * because it needs to be saved as well
				 */
				saved_gprs_sb[i]->use_frame_pointer = 0;
			}
			f->total_allocated += power_gprs[0].size;
			saved_gprs[i].stack_addr = saved_gprs_sb[i];
			saved_gprs[i].size = power_gprs[0].size;
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
	stack_align(f, power_gprs[0].size);
	for (sb = f->alloca_head; sb != NULL; sb = sb->next) {
		f->total_allocated += sb->nbytes;
		alloca_bytes += sb->nbytes;
		sb->offset = f->total_allocated;
	}
	if (f->alloca_head != NULL || f->vla_head != NULL) {
		/*
		 * Get stack for saving return value registers before
		 * performing free() on alloca()ted blocks
		 */
		f->alloca_regs = make_stack_block(0, power_gprs[0].size);
		f->total_allocated += power_gprs[0].size;
		f->alloca_regs->offset = f->total_allocated;
		f->alloca_regs->next = make_stack_block(0, power_gprs[0].size);
		f->total_allocated += power_gprs[0].size;
		f->alloca_regs->next->offset = f->total_allocated;
	}	

	/*
	 * Allocate storage for saving VLA data, and initialize it to zero
	 */
	for (sb = f->vla_head; sb != NULL; sb = sb->next) {
		f->total_allocated += sb->nbytes;
		vla_bytes += sb->nbytes;
		sb->offset = f->total_allocated;
	}

	stack_align(f, 8);

	/*
	 * The PowerOpen ABI requires the caller to allocate storage
	 * for saving registers and passing stack arguments, recorded
	 * by max_bytes_pushed. Unfortunately, it is possible that
	 * unrequested function calls must be generated, e.g. to
	 * perform software arithmetic with the __nwcc*() functions,
	 * or to copy a structure initializer using memcpy().
	 * Therefore, we always allocate a minimum save area to ensure
	 * that no surprises with hidden calls can happen.
	 * XXX obviously it may be beneficial to omit this if no
	 * calls actually happen 
	 */ 
	min_bytes_pushed =
		  power_gprs[0].size * 2    /* linkage bytes */ 
		+ 18 * power_gprs[0].size;  /* reg save area */

	if (f->max_bytes_pushed < min_bytes_pushed) {
		f->max_bytes_pushed = min_bytes_pushed;
		while (f->max_bytes_pushed % 8) ++f->max_bytes_pushed;
	}

	f->total_allocated += f->max_bytes_pushed; /* Parameter area */
	f->total_allocated += power_gprs[1].size; /* saved sp */

	/*
	 * 01/15/09: Add another GPR! This was needed because a test
	 * case (fp_2.c) triggered a condition where a temp gpr save
	 * (f->regs_head) overlapped with the save area by 8 bytes and
	 * thus trashed it
	 * XXX Find out what these 8 bytes are and why they were off.
	 * Are there cases where we need even more?
	 * XXX 01/18/09: This should be unneeded now that we traced
	 * it to calc_stack_bytes() and fixed it?!
	 */
	f->total_allocated += 2*power_gprs[0].size;

	x_fprintf(out, "\tmflr 0\n");

	if (backend->abi == ABI_POWER64) {
		x_fprintf(out, "\tstd 0, 16(1)\n");
		x_fprintf(out, "\tstd 31, -8(1)\n");
	} else {
		x_fprintf(out, "\tstw 0, 8(1)\n");
		x_fprintf(out, "\tstw 31, -4(1)\n");
	}	

	if (f->total_allocated > 0) {
		emit->allocstack(f, f->total_allocated);
	}

	/*
	 * Local variable offsets can only now be patched because the
	 * PowerPC frame pointer points to the bottom of the frame, not
	 * the top, which is terrible.
	 */
	for (scope = f->scope; scope != NULL; scope = scope->next) {
		struct scope		*tmp;
		struct decl		**dec;

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
			if (IS_VLA(dec[i]->dtype->flags)) {
				continue;
			} else if (dec[i]->stack_addr->is_func_arg
				/*&& !dec[i]->stack_addr->from_reg*/) {
				/*
				 * Argument passed on stack - needs different
				 * kind of patchery later
				 */
				continue;
			}
			dec[i]->stack_addr->offset =
				f->total_allocated - dec[i]->stack_addr->offset;
		}
	}
	if (f->fty->variadic) {
		f->fty->lastarg->stack_addr->offset =
			f->total_allocated /*-*/ +
			f->fty->lastarg->stack_addr->offset;
	}

	se = proto->scope->slist; 


	for (sb = f->regs_head; sb != NULL; sb = sb->next) {
		sb->offset = f->total_allocated - sb->offset;
	}
	for (sb = f->alloca_head; sb != NULL; sb = sb->next) {
		sb->offset = f->total_allocated - sb->offset;
	}
	for (sb = f->vla_head; sb != NULL; sb = sb->next) {
		sb->offset = f->total_allocated - sb->offset;
	}
	if (f->alloca_regs != NULL) {
		f->alloca_regs->offset = f->total_allocated
			- f->alloca_regs->offset;
		f->alloca_regs->next->offset = f->total_allocated
			- f->alloca_regs->next->offset;
	}	

	if (nsaved > 0) {
		for (i = 0; i < N_GPRS; ++i) {
			if (saved_gprs[i].stack_addr != NULL) {
				saved_gprs[i].stack_addr->offset =
					f->total_allocated -
					saved_gprs[i].stack_addr->offset;
					
				backend_vreg_map_preg(&saved_gprs[i],
					&power_gprs[i]);
				emit->store(&saved_gprs[i],
					&saved_gprs[i]);	
				backend_vreg_unmap_preg(
					&power_gprs[i]);
				reg_set_unused(&power_gprs[i]);
			}
		}
	}

	/*
	 * Patch parameter offsets and save corresponding argument
	 * registers to stack, if necessary
	 */
	for (i = 0; i < proto->nargs; ++i, se = se->next) {
		/*
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
		if (se->dec->stack_addr->from_reg != NULL) {
			/* Write argument register contents to stack */
			if ((se->dec->dtype->code == TY_STRUCT
				|| se->dec->dtype->code == TY_UNION)
				&& se->dec->dtype->tlist == NULL) {
				generic_store_struct_arg_slots(f, se);
			} else {
				/*
				 * Passed in register, but backed by
				 * register save area
				 */

				/*
				 * 11/03/08: The variable already has an offset
				 * into the storage area, beginning from the
				 * top of the stack frame. So to use the frame
				 * pointer - which points at the bottom of the
				 * frame - we need to add the frame size to the
				 * offset
				 */
				se->dec->stack_addr->offset += f->total_allocated;
				store_preg_to_var(se->dec,
					se->dec->stack_addr->nbytes,
					se->dec->stack_addr->from_reg);
			}
		} else {
			/*
			 * Argument passed on stack
			 */
			/*
			 * 11/03/08: The variable already has an offset
			 * into the storage area, beginning from the
			 * top of the stack frame. So to use the frame
			 * pointer - which points at the bottom of the
			 * frame - we need to add the frame size to the
			 * offset
			 */
			se->dec->stack_addr->offset += f->total_allocated;
		}
	}
	if (f->hidden_pointer) {
		struct decl	*d = f->hidden_pointer->var_backed;

		d->stack_addr->offset = f->total_allocated + d->stack_addr->offset;
		store_preg_to_var(d,
			d->stack_addr->nbytes,
			d->stack_addr->from_reg);
	}
	if (f->fty->variadic) {
		size_t	saved_offset;

		if (f->fty->lastarg->stack_addr->from_reg == NULL) {
			/* Entirely on stack */
			;
		} else {
			struct reg	*r =
				f->fty->lastarg->stack_addr->from_reg;

			saved_offset = f->fty->lastarg->stack_addr->offset;
			for (; r != &power_gprs[3+8]; ++r) {
				store_preg_to_var(f->fty->lastarg, 
					power_gprs[0].size, r);
				f->fty->lastarg->stack_addr->offset +=
					power_gprs[0].size;
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

	return 0;
}


#if XLATE_IMMEDIATELY

static int
gen_prepare_output(void) {
	return 0;
}

static int
gen_finish_output(void) {
	struct function	*func;

	if (sysflag == OS_AIX) {
		x_fprintf(out, "\t.toc\n");
	} else {
		x_fprintf(out, "\t.section \".toc\",\"aw\"\n");
		if (mintocflag) {
			x_fprintf(out, "\t.LCTOC0:\n");
			x_fprintf(out, "\t.tc .LCTOC1[TC], .LCTOC1\n");
			x_fprintf(out, ".section \".toc1\", \"aw\"\n");
			x_fprintf(out, "\t.LCTOC1 = .+32768\n");
		}
	}

	/* XXX is this acceptable here? Should maybe be merged with
	 * other .tc declarations
	 */
	if (!mintocflag) {
		for (func = funclist; func != NULL; func = func->next) {
			char	*name = func->proto->dtype->name;
			int	suppress = 0;

			if (IS_INLINE(func->proto->dtype->flags) &&
				(func->proto->dtype->storage == TOK_KEY_EXTERN
				|| func->proto->dtype->storage == TOK_KEY_STATIC)) {
				/*
				 * 03/04/09: Static/extern inline functions
				 * which are never referenced are not emitted.
				 * So suppress their TOC references too
				 */
				if (func->proto->references == 0) {
					suppress = 1;
				}
			}
			if (!suppress) {
				x_fprintf(out, "_Toc_%s:\n", name);
				x_fprintf(out, "\t.tc %s[TC], %s\n", name, name);
			}
		}
	}

	emit->static_init_vars(static_init_vars);
	emit->static_init_thread_vars(static_init_thread_vars);
	emit->static_uninit_vars(static_uninit_vars);
	emit->static_uninit_thread_vars(static_uninit_thread_vars);
	emit->global_extern_decls(global_scope.extern_decls.data,
		global_scope.extern_decls.ndecls);
	if (emit->extern_decls) {
		emit->extern_decls();
	}
#if 0
	/* Done elsewhere */
	emit->struct_inits(init_list_head);
	emit->fp_constants(float_const);
	emit->llong_constants(llong_const);
#endif
	emit->support_buffers();
	x_fflush(out);
	return 0;
}

#else

static int
gen_program(void) {
	struct function		*func;

	if (sysflag == OS_AIX) {
		x_fprintf(out, "\t.toc\n");
	} else {
		x_fprintf(out, "\t.section \".toc\",\"aw\"\n");
		if (mintocflag) {
			x_fprintf(out, "\t.LCTOC0:\n");
			x_fprintf(out, "\t.tc .LCTOC1[TC], .LCTOC1\n");
			x_fprintf(out, ".section \".toc1\", \"aw\"\n");
			x_fprintf(out, "\t.LCTOC1 = .+32768\n");
		}
	}

	emit->extern_decls();

	if (sysflag == OS_AIX) {
		x_fprintf(out, "\t.csect .text[PR]\n");
	} else {
		x_fprintf(out, "\t.section \".text\"\n");
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
	emit->llong_constants(llong_const);
	emit->support_buffers();

	if (sysflag == OS_AIX) {
		x_fprintf(out, "\t.csect .text[PR]\n");
	} else {
		x_fprintf(out, "\t.section \".text\"\n");
	}

	for (func = funclist; func != NULL; func = func->next) {
		curfunc = func;
		if (gen_function(func) != 0) {
			return -1;
		}
		emit->empty();
		emit->empty();
	}

	if (sysflag == OS_AIX) {
		x_fprintf(out, "\t.toc\n");
		/* XXX is this acceptable here? Should maybe be merged with
		 * other .tc declarations
		 */
		for (func = funclist; func != NULL; func = func->next) {
			char	*name = func->proto->dtype->name;
			x_fprintf(out, "_Toc_%s:\n", name);
			x_fprintf(out, "\t.tc %s[TC], %s\n", name, name);
		}
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

	(void) bytes_left;

	size = backend->get_sizeof_type(vr->type, NULL);
	if (vr->type->tlist
		&& vr->type->tlist->type ==
		TN_ARRAY_OF) {
		size = /*4*/power_gprs[0].size;
	}

	/*
	 * 01/15/09: This wrongly attempted to right-adjust 128bit long
	 * double items, which trashed the stack byte count
	 */
	if (sysflag != OS_AIX && (vr->type->code != TY_LDOUBLE || vr->type->tlist != NULL)) {
		if (/*4*/power_gprs[0].size - size > 0) {
			/* Need to right-adjust */
			*stack_bytes_used += /*4*/power_gprs[0].size - size;
		}
	}

	dest = vreg_alloc(NULL, NULL, NULL, NULL);

	#if 0
	dest->type = vr->type;
	dest->size = vr->size;
	dest->is_multi_reg_obj = vr->is_multi_reg_obj;
	#endif

	/*
	 * 01/29/08: The ad-hoc assignment of type and size neglected
	 * to set the multi-GPR flag! (For long long on PPC32) So use
	 * the proper way
	 */
	vreg_set_new_type(dest, vr->type);

	/* 10/29/08: Use corrected SPARC method to align argument slots */
{
	int pad = calc_slot_rightadjust_bytes(size, power_gprs[0].size);

	/*
	 * Just CUR_SAVE_AREA_OFFSET is off by 64 - will begin at 176 instead
	 * of 112 starting from the stack pointer
	 */
	dest->stack_addr = make_stack_block(CUR_SAVE_AREA_OFFSET + pad, dest->size);
}


	dest->stack_addr->use_frame_pointer = 0;
	*stack_bytes_used += size;

	/* 10/29/08: Slot changes from SPARC */
	++*slots_used;
	if (dest->size > power_gprs[0].size) {
		++*slots_used;
	}

	/*
	 * 10/29/08: Don't unconditionally load to tempgpr, which is wrong
	 * because it won't work for floating point values and because it
	 * will mark tmpgpr mapped and used
	 */
{
	struct reg      *temp = NULL;
	struct reg      *temp2 = NULL;
	int             i;

	if (!is_floating_type(vr->type)) {
		for (i = 0; i < 6; ++i) {
			reg_set_unallocatable(&power_gprs[3+i]);
		}

		temp = ALLOC_GPR(curfunc, backend->get_sizeof_type(vr->type, 0), il, NULL);
		if (vr->is_multi_reg_obj && backend->abi != ABI_POWER64) {
			temp2 = ALLOC_GPR(curfunc, backend->get_sizeof_type(vr->type, 0), il, NULL);
		}

		for (i = 0; i < 6; ++i) {
			reg_set_allocatable(&power_gprs[3+i]);
		}
	} else {
		for (i = 0; i < 13; ++i) {
			reg_set_unallocatable(&power_fprs[1+i]);
		}

		temp = backend->alloc_fpr(curfunc, backend->get_sizeof_type(vr->type, 0), il, NULL);
		if (vr->is_multi_reg_obj) {
			temp2 = backend->alloc_fpr(curfunc, backend->get_sizeof_type(vr->type, 0), il, NULL);
		}

		for (i = 0; i < 13; ++i) {
			reg_set_allocatable(&power_fprs[1+i]);
		}
	}

	{
		vreg_faultin(  /*tmpgpr*/  temp, temp2, vr, il, 0);
	}
	vreg_map_preg(dest, /* tmpgpr */ temp);
	if (temp2 != NULL) {
		vreg_map_preg2(dest, temp2);
	}
	icode_make_store(curfunc, dest, dest, il);

	free_preg(temp, il, 1, 0);
	if (temp2 != NULL) {
		free_preg(temp2, il, 1, 0);
	}
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
 * Pass a struct/union to a function. The first members of a
 * struct are passed in GPRs/FPRs, the rest on the stack. The
 * first members of unions are passed only in GPRs, the rest
 * on the stack. ...at least that's what the n32/n64 ABIs say!
 *
 * nwcc passes all structs/unions on the stack, just like on
 * x86. This avoids a lot of complexity. It also means that
 * nwcc is not link compatible with MIPSpro and gcc in this
 * regard. This breaks support for some library functions, so
 * I may end up supporting n32 fully eventually.
 * 
 * XXX this doesn't seem to take stack alignment into account
 * at all
 */
int
generic_pass_struct_union(
	struct vreg *vr, /*int *gprs_used, int *fprs_used,*/
	int *slots_used,
	size_t *stack_bytes_used,
	size_t *real_stack_bytes_used,
	struct icode_list *il) { 

	struct vreg	*destvr;
	struct vreg	*reg_vrs[8];
	int		i;
	int		gprs_used = *slots_used > 8? 8: *slots_used;
	int		size;
	int		already_passed = 0;
	int		orig_vr_size = vr->size;
	int		orig_real_stack_bytes_used = 0;
	struct reg	*startreg = NULL;

	/* Architecture-specific settings */
	int		max_gpr_slots = 0;
	int		slot_size = 0;
	struct reg	*regset = NULL;

	if (real_stack_bytes_used != NULL) {
		orig_real_stack_bytes_used = *real_stack_bytes_used;
	}

	/*
	 * 05/22/11: Account for empty structs (a GNU
	 * C silliness) being passed
	 */
	if (vr->size == 0) {
		return 0;
	}


	/*
	 * 07/22/09: Make this usable for MIPS as well
	 * As much data of the struct as possible is put into GPR
	 * argument slots. This is architecture-specific in how
	 * many GPRs are used and GPR size (4 or 8 bytes)
	 * XXX Thus should probably be passed as function
	 * parameters
	 */
	if (backend->arch == ARCH_POWER) {
		slot_size = power_gprs[0].size;
		max_gpr_slots = 8;
		regset = power_gprs + 3;
	} else if (backend->arch == ARCH_MIPS) {
		slot_size = mips_gprs[0].size;
		max_gpr_slots = 8;
		regset = mips_gprs + 4;
	} else {
		unimpl();
	}

	if (gprs_used < 8) {
		/* Pass first slots in registers */
		int			temp = vr->size;
		struct reg		*tempreg;
		struct reg		*r;

		for (i = 0; i < 8; ++i) {
			reg_set_unallocatable(&regset[i]  /*&power_gprs[3+i]*/);
		}
		tempreg = ALLOC_GPR(curfunc, backend->get_ptr_size(), il, 0);
		for (i = 0; i < 8; ++i) {
			reg_set_allocatable(&regset[i]  /*&power_gprs[3+i]*/);
		}

		r = startreg = &regset[gprs_used];  /*&power_gprs[3+gprs_used];*/
		while (r != &regset[max_gpr_slots]) {  /*&power_gprs[3+8]) {*/
			if (temp <= 0) {
				break;
			}
			if (temp >= slot_size) {  /*(int)power_gprs[0].size) {*/
				already_passed += slot_size;  /*power_gprs[0].size;*/
			} else {
				already_passed += temp;
			}
			temp -= slot_size;  /*power_gprs[0].size;*/
			++gprs_used;
			++r;
		}

		if (real_stack_bytes_used != NULL) {
			int remainder = vr->size - already_passed;

			if (remainder > 0) {
				if (remainder % slot_size) {
					remainder += slot_size -
						remainder % slot_size;
				}
				*real_stack_bytes_used += remainder;
			}
		}

		/* Take address of source and save to registers */
		vreg_faultin_ptr(vr, il);
		/*ii =*/(void) icode_make_addrof(tempreg, vr, il);
		/*append_icode_list(il, ii);*/
		icode_make_putstructregs(startreg, tempreg, vr, il);
		free_preg(tempreg, il, 1, 0);

		/*
		 * Because these registers are not backed by anything
		 * meaningful, we have to create stack vregs so that we
		 * can save and restore them if, say, another struct is
		 * copied using memcpy()
		 * Passing structs in registers does suck.
		 */
		while (--r != &regset[-1]) {  /*&power_gprs[3 - 1]) {*/
			struct vreg	*nonsense;
			nonsense = vreg_alloc(NULL,NULL,NULL,NULL);
			nonsense->type = backend->get_size_t();
			nonsense->size = backend->get_sizeof_type(
				nonsense->type, NULL);
			vreg_map_preg(nonsense, r);
			free_preg(r, il, 1, 1);
			vreg_faultin(r, NULL, nonsense, il, 0);
		}

		if (already_passed >= (int)vr->size) {
			/* All done */
			goto out;
		}
	} else {
		/* All passed on stack */
		if (real_stack_bytes_used != NULL) {
			int remainder = vr->size;

			if (remainder % slot_size) {
				remainder += slot_size -
					remainder % slot_size;
			}
			*real_stack_bytes_used += remainder;
		}
	}


	destvr = vreg_alloc(NULL, NULL, NULL, NULL);
	destvr->type = vr->type;
	destvr->size = vr->size;
	destvr->var_backed = alloc_decl();
/* XXX alignment, small struct adjustment, etc */

	if (backend->arch == ARCH_POWER) {
		destvr->var_backed->stack_addr = make_stack_block(
				CUR_SAVE_AREA_OFFSET + already_passed, vr->size);
		destvr->var_backed->stack_addr->use_frame_pointer = 0;
	} else if (backend->arch == ARCH_MIPS) {
		int remainder = ROUND_UP_TO_MULTIPLE(vr->size - already_passed, mips_gprs[0].size);

		destvr->var_backed->stack_addr = stack_malloc(curfunc, remainder);
		destvr->var_backed->stack_addr->use_frame_pointer = 0;
		/*
		 * We have to explicitly set the offset because it may not be
		 * the correct address! For example, if we process args left to
		 * right and pass two structs, then the second one will have a
		 * lower address (on the stack frame) after stack_malloc()
		 * But it is really located at the higher address. We call
		 * stack_malloc() for every stack argument anyway, so the
		 * storage exists, but we carefully hardcode the offset to
		 * the right location so things can't get mixed up
		 */
		destvr->var_backed->stack_addr->offset = orig_real_stack_bytes_used;
	} else {
		unimpl();
	}


	/*
	 * 07/23/09: This accounts for the entire struct as ``stack bytes''
	 * without subtracting those bytes that were put into GPRs. This
	 * is very unexpected but seems to work on PPC, so we keep it for
	 * PPC for now
	 * XXX Verify on PPC
	 */
	if (stack_bytes_used != NULL) {
		*stack_bytes_used += vr->size;
	}

	/*
	 * Save occupied argument registers before call to memcpy() (in
	 * so that we can reload them after memcpy()
	 */
	for (i = 0; i < gprs_used; ++i) {
		reg_vrs[i] = regset[i].vreg;  /*power_gprs[3+i].vreg;*/
	}

	backend->invalidate_gprs(il, 1, INV_FOR_FCALL);

	if (already_passed > 0) {
		/* First slots have already been passed in registers */
		vr = dup_vreg(vr);
		vr->addr_offset = already_passed;

		/*
		 * 07/23/09: There was apparently a horrible bug here which
		 * was never discovered on PPC: The memcpy() call will copy
		 * the whole struct size even though part of the struct was
		 * already passed in registers! So there was a buffer overflow
		 * here
		 */
		vr->size = vr->size - already_passed;
	}

	vreg_faultin_ptr(vr, il);
	icode_make_copystruct(destvr, vr, il);

	/* Restore argument registers */  
	for (i = 0; i < gprs_used; ++i) {
		vreg_faultin(&regset[i]  /*&power_gprs[3+i]*/, NULL,
			reg_vrs[i], il, 0);
		reg_set_unallocatable(&regset[i] /*&power_gprs[3+i]*/);
	}

out:

	/*
	 * Use cached vreg size, since vr may be a partial item for memcpy()
	 */
	size = orig_vr_size; /*vr->size;*/
	while (size > 0) {
		/* 01/25/09: This used hardcoded 8 */
		size -= slot_size;  /*power_gprs[0].size;*/
		++*slots_used;
	}

	return 0;
}


/*
 * 01/15/08: We used to use the MIPS stack calculation function, which
 * is wrong for PPC and probably MIPS too because it thinks in terms of
 * GPRs and FPRs instead of slots. Like SPARC we just add 2*gpr size
 * for every non-struct arg now. This is a bit too generous but better
 * safe than sorry 
 * XXX we may wish to improve this though
 */
size_t
ppc_calc_stack_bytes(struct vreg **vrs, int nvrs, int *takes_struct) {
        size_t  nbytes = 0;
        int     i;
        int     stackstruct = 0;

        for (i = 0; i < nvrs; ++i) {
                if (vrs[i]->type->code == TY_STRUCT
                        || vrs[i]->type->code == TY_UNION) {
                        nbytes += vrs[i]->type->tstruc->size;
                        stackstruct = 1;
                } else {
			nbytes += 2 * power_gprs[0].size;
			if (vrs[i]->type->code == TY_LDOUBLE
				&& vrs[i]->type->tlist == NULL) {
				if (sysflag != OS_AIX && backend->abi == ABI_POWER32) {
					nbytes += 2 * power_gprs[0].size;
				}
			}
		}
        }
        while (nbytes % 8) {
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
	int			*p_fprs_used;
	int			slots_used = 0;
	int			is_multi_reg_obj = 0;
	int			ret_is_anon_struct = 0;
	unsigned char		gprs_used_map[N_GPRS];
	unsigned char		fprs_used_map[N_FPRS];

	if (sysflag == OS_AIX) {
		p_fprs_used = &fprs_used;
	} else {
		p_fprs_used = NULL;
	}

	/*
	 * 23/12/08: Record numbers of actually used GPRS to avoid them
	 * from being mistaken for GPRS that were passed as a result of
	 * e.g. an FP argument incrementing the slot counter
	 */
	memset(gprs_used_map, 0, sizeof gprs_used_map);
	memset(fprs_used_map, 0, sizeof fprs_used_map);

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

#if 1 /* XXX Should go! Use rettype instead */
			tnsav = ty->tlist;
			ty->tlist = NULL;
#endif
			/* XXX hm */
			struct_lvalue = vreg_stack_alloc(ty, il, 1 /*0*/,NULL);

#if 1
			ty->tlist = tnsav;
#endif
			/*
			 * 08/05/08: Don't add to allpushed since struct is
			 * created on frame. This bug occurred on AMD64 and
			 * hasn't been verified on PPC yet
			 */
	/*		allpushed += struct_lvalue->size;*/
			/*while (allpushed % 4*/ /* XXX *//*) ++allpushed;*/
			while (allpushed % power_gprs[0].size) ++allpushed;
			ret_is_anon_struct = 1;
		}

		/* Hidden pointer is passed in first GPR! */
		{
			struct reg	*r;
			/*ii*/ r = make_addrof_structret(struct_lvalue, il);

			free_preg(&power_gprs[3], il, 1, 1);
		
			icode_make_copyreg(&power_gprs[3], r /*ii->dat*/,
				NULL, NULL, il);
			gprs_used_map[3] = 1;
			++slots_used;
		}
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

	allpushed += ppc_calc_stack_bytes(vrs, nvrs, &takes_struct);
	if (takes_struct) {
		/* memcpy() will be called to pass argument(s) */
		backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
	}
	if (/*allpushed > 0*/ 1) {
		int	linkage_bytes = backend->abi == ABI_POWER64? 16: 8;
		/*icode_make_allocstack(NULL, allpushed, il);*/
		allpushed += linkage_bytes;
		allpushed += 18 * power_gprs[0].size; /* XXX 18 comes from 64bit abi */
		if ((int)allpushed > curfunc->max_bytes_pushed) {
			curfunc->max_bytes_pushed = allpushed;
		}	
		allpushed -= linkage_bytes;
	}

	for (i = 0; i < nvrs; ++i) {
		if ((fcall->functype->variadic
			&& i >= fcall->functype->nargs)
			|| (fcall->calltovr->type->implicit & IMPLICIT_FDECL)) {
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
				/*
				 * 11/07/08: Convert to long instead of
				 * int, so as to sign-extend for argument
				 * slot on 64bit as well
				 */
				vrs[i] = backend->	
					icode_make_cast(vrs[i],
						make_basic_type(TY_LONG), il);
			}
			if (slots_used < 8) {
				gprs_used_map[3+slots_used] = 1;
				if (vrs[i]->is_multi_reg_obj) {
					gprs_used_map[3+slots_used+1] = 1;
				}

				put_arg_into_reg(power_gprs,
					&slots_used /*gprs_used*/, 3, vrs[i], il);	

				/*
				 * 01/28/09: For long long items, the second
				 * word may have to be passed on the stack
				 */
				if (vrs[i]->is_multi_reg_obj
					&& slots_used == 9) {
					struct vreg     *tempvr;

					tempvr = vreg_alloc(NULL,NULL,NULL,NULL);
					vreg_set_new_type(tempvr,
						make_basic_type(TY_UINT));
					vreg_map_preg(tempvr,
						&power_gprs[3+slots_used-1]);
					--slots_used;
					pass_arg_stack(tempvr, 0, &slots_used,
						&stack_bytes_used, il);
				}
			} else {
				pass_arg_stack(vrs[i], 0, &slots_used,
					&stack_bytes_used, il);
			}
		} else if (vrs[i]->type->code == TY_STRUCT
			|| vrs[i]->type->code == TY_UNION) {
			generic_pass_struct_union(vrs[i],
				&slots_used, &stack_bytes_used, NULL, il);
		} else if (IS_FLOATING(vrs[i]->type->code)) {
			/*
			 * For variadic functions, floating point values
			 * go into gprs (floats are promoted to double)
			 */
			if (need_dap) {
				/*
				 * 10/31/07: Added cast for double too
				 * even though it already has that type.
				 * This just has the effect of anonymi-
				 * fying the double. Otheriwse the
				 * reinterpretation as a long long in
				 * 32bit mode below will break if we
				 * have an unanoymified constant here,
				 * since the fp constant is wrongly
				 * reinterpreted as integer constant
				 */
				if (vrs[i]->type->code == TY_FLOAT
					|| vrs[i]->type->code ==
					TY_DOUBLE) {
					vrs[i] = backend->
						icode_make_cast(vrs[i],
							make_basic_type(TY_DOUBLE), il);
				} else if (vrs[i]->type->code == TY_LDOUBLE) {
                                        /*
                                         * Long double is tricky because
                                         * it 1) is passed in two gprs, and
                                         * 2) those gprs need to be aligned
                                         */
                                        if (slots_used < 8) {
		#if 0
                                                if (slots_used & 1) {
                                                        /*
							 * Not aligned! (Despite
							 * being even because we
							 * begin at r3)
							 */
                                                        ++slots_used;
                                                }
			#endif
                                        }
				}

				/*
				 * XXXXXXXXXX we have to put this stuff into
				 * both fp and gp regs to satisfy both
				 * accidently undeclared functions and
				 * variadic ones!
				 */
				if ((slots_used < 7 && backend->abi != ABI_POWER64)
					|| slots_used < 8) { /* need at least two */
					struct type	*oty = vrs[i]->type;


					vreg_anonymify(&vrs[i], NULL, NULL, il);
					free_preg(vrs[i]->pregs[0], il, 1, 1);
					vrs[i] = vreg_disconnect(vrs[i]);
					vrs[i]->pregs[0] = NULL;

					/*
					 * 12/29/08: Use unsigned long long instead of
					 * unsigned long. This probably only worked by
					 * accident on PPC32
					 */
					vrs[i]->type = make_basic_type(TY_ULLONG);
					vrs[i]->size = 8;
					if (backend->abi != ABI_POWER64) {
						vrs[i]->is_multi_reg_obj = 2;
						gprs_used_map[3+slots_used] = 1;
						gprs_used_map[3+slots_used+1] = 1;
						vreg_faultin(&power_gprs[3+slots_used],
							&power_gprs[3+slots_used+1],
							vrs[i], il, 0);
					
						if (slots_used == 7) {
							/*
							 * 01/27/09: The second part
							 * of the item is passed on
							 * the stack because there are
							 * no free GPRs left
							 */
							struct vreg	*tempvr;
							tempvr = vreg_alloc(NULL,NULL,NULL,NULL);
							vreg_set_new_type(tempvr,
								make_basic_type(TY_UINT));
							vreg_map_preg(tempvr,
								&power_gprs[3+slots_used+1]);
							++slots_used;
							pass_arg_stack(tempvr, 0, &slots_used,
								&stack_bytes_used, il);
						} else {
							slots_used += 2;
						}
					} else {
						/*
						 * 12/29/08: Reset multi-register flag!
						 * (which is set when passing a long
						 * double)
						 */
						if (sysflag == OS_AIX || oty->code != TY_LDOUBLE) {
							vrs[i]->is_multi_reg_obj = 0;
							vreg_faultin(&power_gprs[3+
								slots_used],
								NULL, vrs[i], il, 0);
							++slots_used;
						}
					}


					if (sysflag != OS_AIX && oty->code == TY_LDOUBLE) {
						if (slots_used < 8) {
                                                        struct vreg     *tmp;

							/*
							 * 12/23/08: Use dup_vreg() instead
							 * of x_memdup() (changed to be able
							 * to track sequence numbers)
							 */
							tmp = dup_vreg(vrs[i]);

                                                        tmp->pregs[0] = NULL;
							gprs_used_map[3+slots_used] = 1;
				gprs_used_map[3+slots_used+1] = 1;
                                                        free_preg(&power_gprs[3+slots_used], il, 1, 1);
                                             free_preg(&power_gprs[3+slots_used+1], il, 1, 1);
                                                        vreg_faultin(
                                                        &power_gprs[3+slots_used],
                                                        &power_gprs[3+slots_used+1], tmp, il, 0);
                                                      /*  NULL, tmp, il, 0);*/

                                                        /* 12/17/07: Unallocable missing */
                                                        reg_set_unallocatable(
                                                                &power_gprs[3+slots_used]);
                                                reg_set_unallocatable(
                                                     &power_gprs[3+slots_used+1]);
/*                                                        ++slots_used;*/
                                                       slots_used += 2;
						} else {
							pass_arg_stack(vrs[i], 0, &slots_used,
								&stack_bytes_used, il);
						}
					}
				} else {
					pass_arg_stack(vrs[i], 0, &slots_used,
						&stack_bytes_used, il);
				}
			} else {
				if ((p_fprs_used? *p_fprs_used: slots_used) < 13) {
					fprs_used_map[1+ (p_fprs_used? *p_fprs_used: slots_used)] = 1;
					put_arg_into_reg(power_fprs,
						(p_fprs_used? p_fprs_used: &slots_used), 1, vrs[i], il);	
					if (p_fprs_used) {
						/*
						 * 01/28/09: The nonsense AIX ABI wants us
						 * to increment both fprs_used and slots_used
						 * for an FP arg, and only slots_used for an
						 * integer arg. OR SO IT SEEMS!!!
						 * The effect is that if we use the first
						 * FP register, then we can't use the first
						 * GP register. But if we use the first GP
						 * register, we can still use the first FP
						 * register
						 */
						++slots_used;
						if (vrs[i]->type->code == TY_DOUBLE
							|| vrs[i]->type->code == TY_LDOUBLE) {
							if (backend->abi != ABI_POWER64) {
								++slots_used;
							}
						}
					}
				} else {
					pass_arg_stack(vrs[i], 0, &slots_used,
						&stack_bytes_used, il);
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

		/*ii*/ r = make_addrof_structret(struct_lvalue, il);

		icode_make_copyreg(&power_gprs[3], r /*ii->dat*/, NULL, NULL, il);

		free_preg(r /*ii->dat*/, il, 0, 0);
	}	

	/* 12/26/08: Use 8 instead of 6 */
	gprs_used = slots_used >= 8? 8: slots_used;
/* gprs_used = slots_used >= 6? 6: slots_used;*/
	for (i = 0; i < gprs_used; ++i) {
		/* Don't save argument registers */
		if (!gprs_used_map[3+i]) {
			/*
			 * 12/23/08: This slot number was incremented
			 * because of e.g. an FPR argument. This means
			 * the register may still contain a non-argument
			 * item which must be saved by invalidate_gprs()
			 */
			continue;
		}
		reg_set_unused(&power_gprs[3+i]);

		/*
		 * 12/29/07: vreg=NULL new, allocatable was wrong - see
		 * SPARC
		 */
		power_gprs[3+i].vreg = NULL;
		/*
		 * 10/29/08: According to SPARC this was resetting
		 * allocatability too early
		 */
		power_gprs[3+i].allocatable = 0;
	}

	/*
	 * 12/26/08: 13 instead of slots_used*2... The old version seems
	 * wrong in so many ways... (e.g. if fprs_used=32, then there's an
	 * off-by-1 overflow because we access up to  fprs[1+fprs_used])
	 */
	if (p_fprs_used != NULL) {
		;
	} else {
		fprs_used = slots_used >= 13? 13: slots_used;
	}

	for (i = 0; i < fprs_used; ++i) {
		/* 10/29/08: Don't save fp argument registers */
		if (!fprs_used_map[1+i]) {
			/*
			 * 12/23/08: This slot number was incremented
			 * because of e.g. an GPR argument. This means
			 * the register may still contain a non-argument
			 * item which must be saved by invalidate_gprs()
			 */
			continue;
		}
		reg_set_unused(&power_fprs[1+i]);
		power_fprs[1+i].vreg = NULL;
		power_fprs[1+i].allocatable = 0;
	}

	if (!takes_struct) {
		backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
	}

	for (i = 0; i < gprs_used; ++i) {
		power_gprs[3+i].allocatable = 1;
	}

	for (i = 0; i < fprs_used; ++i) {
		power_fprs[1+i].allocatable = 1;
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
			power_gprs[3+i].allocatable = 0;
		}

		/*
		 * 12/27/08: Use dedicated function
		 */
		vreg_faultin_dedicated(tmpgpr, NULL, tmpvr, il, 0);

		for (i = 0; i < gprs_used; ++i) {
			power_gprs[3+i].allocatable = 1;
		}
		ii = icode_make_call_indir(tmpvr->pregs[0]);
		tmpvr->pregs[0]->used = 0;
		tmpvr->pregs[0]->vreg = NULL;
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
		ret->pregs[0] = &power_gprs[3];
	} else {
#if 1 /* XXX Should go! Use rettype */
		struct type_node	*tnsav = ty->tlist;

		ty->tlist = NULL;
#endif
		if (backend->abi != ABI_POWER64
			&& IS_LLONG(ty->code)) {
			is_multi_reg_obj = 2;
		}
		if (is_integral_type(ty)) {
			ret->pregs[0] = &power_gprs[3];
			if (is_multi_reg_obj) {
				ret->pregs[1] = &power_gprs[4];
			}
		} else if (ty->code == TY_FLOAT
			|| ty->code == TY_DOUBLE
			|| ty->code == TY_LDOUBLE) {

			if (sysflag != OS_AIX && ty->code == TY_LDOUBLE) {
				ret->pregs[0] = &power_fprs[1];
				ret->pregs[1] = &power_fprs[2];
				is_multi_reg_obj = 2;
			} else {
				ret->pregs[0] = &power_fprs[0];
			}
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
	for (i = 3; i < 11; ++i) {
		reg_set_allocatable(&power_gprs[i]);
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
	struct type		*rtype = curfunc->rettype; /*proto->dtype;*/

	/* 06/17/08: Use rettype instead of type kludgery */

	if (vr != NULL) {
		if (is_integral_type(rtype)
			|| rtype->code == TY_ENUM /* 06/15/09: Was missing?!? */
			|| rtype->tlist != NULL) {
			/* XXX long long ?!?!!?!??!?? */
			if (vr->is_multi_reg_obj) {
				vreg_faultin(&power_gprs[3],
					&power_gprs[4], vr, il, 0);
			} else {
				vreg_faultin(&power_gprs[3], NULL,
					vr, il, 0);
			}
		} else if (rtype->code == TY_FLOAT
			|| rtype->code == TY_DOUBLE
			|| rtype->code == TY_LDOUBLE) {
			/* XXX ldouble ... */
			if (sysflag != OS_AIX && rtype->code == TY_LDOUBLE) {
				/*
				 * XXX We have to anonymify the item and free
				 * its associated registers here because
				 * vreg_faultin() cannot properly move multi-GPR
				 * objects which have been loaded but need to be
				 * put into other registers instead. Maybe this
				 * would work if we implemented emit_xchg() for
				 * PPC
				 */
				vreg_anonymify(&vr, NULL, NULL, il);
				free_pregs_vreg(vr, il, 1, 1);
				vreg_faultin(&power_fprs[1], &power_fprs[2], vr, il, 0);
			} else {
				vreg_faultin(&power_fprs[0], NULL, vr, il, 0);
			}
		} else if (rtype->code == TY_STRUCT
			|| rtype->code == TY_UNION) {
			/* vr may come from pointer */
			vreg_faultin_ptr(vr, il);
			icode_make_copystruct(NULL, vr, il);
		}
	}
	ii = icode_make_ret(vr);
	append_icode_list(il, ii);

	return 0;
}

static void 
icode_prepare_op(
	struct vreg **dest,
	struct vreg **src,
	int op,
	struct icode_list *il) {

	if ((op == TOK_OP_DIVIDE
		|| op == TOK_OP_MULTI
		|| op == TOK_OP_MOD
		|| op == TOK_OP_PLUS
		|| op == TOK_OP_MINUS
		|| op == TOK_OP_BSHL
		|| op == TOK_OP_BSHR)
		&& (backend->abi != ABI_POWER64
			&& IS_LLONG(dest[0]->type->code)
			&& dest[0]->type->tlist == NULL)) {
		/*
		 * Div/mul and 64bit add/sub are done in software -
		 * prepare for function call
		 *
		 * XXX the invalidate may trash stuff which is still
		 * needed! e.g. a pointer thru which we assign the
		 * result of the operation (sparc typemap.c bug)
		 */
		/* XXX woah that invalidate is kinda heavy... */
		backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
	}
	vreg_faultin(NULL, NULL, *dest, il, 0);
	vreg_faultin(NULL, NULL, *src, il, 0);
	(void) op;
}

static void
icode_prepare_load_addrlabel(struct icode_instr *ii) {
	(void) ii;
}

#define TO_ZERO() icode_make_copyreg(&power_gprs[0], ret->pregs[0], NULL, NULL, il)
#define FROM_ZERO() icode_make_copyreg(ret->pregs[0], &power_gprs[0], NULL, NULL, il)


static void
change_preg_size(struct vreg *ret, struct type *to, struct type *from,
	struct icode_list *il) {
	struct icode_instr	*ii;

	int	to_is_64bit = 0;
	int	from_is_64bit = 0;

	if (to->tlist == NULL) {
		if (IS_LLONG(to->code)
			|| (backend->abi == ABI_POWER64
				&& IS_LONG(to->code))) {
			to_is_64bit = 1;
		}
	}
	if (from->tlist == NULL) {
		if (IS_LLONG(from->code)
			|| (backend->abi == ABI_POWER64
				&& IS_LONG(from->code))) {
			from_is_64bit = 1;
		}
	}

	if (IS_CHAR(from->code) && IS_CHAR(to->code)) {
		if (from->sign == TOK_KEY_UNSIGNED
			&& to->code == TY_SCHAR) {
			TO_ZERO();
			icode_make_power_slwi(&power_gprs[0],
				&power_gprs[0], 24, il);
			icode_make_power_srawi(&power_gprs[0],
				&power_gprs[0], 24, il);
			FROM_ZERO();
		}
	} else if (to_is_64bit) {
		if (!from_is_64bit && !from->tlist) {
			if (backend->abi != ABI_POWER64) {
				ret->pregs[1] = ret->pregs[0];
				ret->pregs[0] = ALLOC_GPR(curfunc
					, 0, il, NULL);
				if (from->sign != TOK_KEY_UNSIGNED) {
					icode_make_power_srawi(ret->pregs[0],
						ret->pregs[1], 31, il);
				} else {
					ii = icode_make_setreg(
						ret->pregs[0], 0);
					append_icode_list(il, ii);
				}
			} else {
				TO_ZERO();
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
					icode_make_power_rldicl(&power_gprs[0],
						&power_gprs[0], bits, il);
				} else {
					/* from signed */
					if (from->code == TY_SCHAR) {
						icode_make_power_extsb(
							&power_gprs[0], il);
					} else if (from->code == TY_SHORT) {
						icode_make_power_extsh(
							&power_gprs[0], il);
					} else if (from->code == TY_INT) {
						icode_make_power_extsw(
							&power_gprs[0], il);
					} else {
						unimpl();
					}
				}		
				FROM_ZERO();
			}
		}
	} else if (from_is_64bit) {
		struct reg	*r = ret->pregs[0];

		if (backend->abi != ABI_POWER64) {
			ret->pregs[0] = ret->pregs[1];
			free_preg(r, il, 1, 0);
		}	

		/*
		 * Seems r can become gpr0. I do not know why
		 * this is happening but it is terrible
		 */
		power_gprs[0].allocatable = power_gprs[0].used = 0;

		goto truncate_further;
	} else {
		/* Neither is long long, but they are different */ 

truncate_further:
		;
	}
	power_gprs[0].allocatable = power_gprs[0].used = 0;
}



/*
 * Convert double to long double or the other way around.
 * The source must be register-resident!
 *
 * XXX This function is probably generic(ish0 and should be used
 * for MIPS as well
 *
 * 07/15/09: Ok!! Agree it looks generic enough for PPC/MIPS
 */
void
generic_conv_to_or_from_ldouble(struct vreg *ret, struct type *to, struct type *from, 
	struct icode_list *il) {

	struct vreg 	*vr;
	struct vreg	*tempsrc;

	/*
	 * Set source to unused before invalidating
	 * gprs. This is needed because we'll get
	 * multi-reg save errors otherwise (only one
	 * reg is mapped to a multi-reg object so
	 * freeing fails)
	 */
	ret->pregs[0]->used = 0;

	/* Invalidate gprs */
	backend->invalidate_gprs(il, 1, INV_FOR_FCALL);

	/* Allocate long double on stack */
	vr = vreg_stack_alloc(to, il, 1, NULL);

	/* Temporarily save source fp value to memory */
	tempsrc = vreg_stack_alloc(from, il, 1, NULL);
	vreg_map_preg(tempsrc, ret->pregs[0]);
	if (ret->is_multi_reg_obj) {
		vreg_map_preg2(tempsrc, ret->pregs[1]);
	}
	
	/*
	 * Save register(s) to memory. We cannot use free_preg()
	 * here because that will see tempsrc being var_backed
	 * to the stack buffer and just don't save the register!
	 * XXX maybe the free_preg() interface is broken or needs
	 * a force_write flag?
	 */
	icode_make_store(curfunc, tempsrc, tempsrc, il);

	/* Perform conversion */
	if (to->code == TY_LDOUBLE) {
		icode_make_conv_to_ldouble(vr, tempsrc, il);
	} else {
		icode_make_conv_from_ldouble(vr, tempsrc, il);
	}

	vreg_faultin(NULL, NULL, vr, il, 0);
	ret->pregs[0] = vr->pregs[0];
	ret->pregs[1] = vr->pregs[1];
}


void
generic_conv_fp_to_or_from_ldouble(struct vreg *ret,
	struct type *to, struct type *from,
	struct icode_list *il) {

	struct vreg	*tempvr;
	struct type	*to_type = to;
	struct type	*from_type = from;

	if (to->code == TY_FLOAT) {
		/*
		 * Make the result a double - then we just
		 * have to change the vreg type to
		 * reinterpret the value as a float
		 */
		tempvr = ret;
		to_type = make_basic_type(TY_DOUBLE);
	} else if (from->code == TY_FLOAT) {
		/*
		 * The source is a float, so we have to
		 * supply a vreg of type double because
		 * the input must be double. Since the
		 * value is register-resident, no
		 * conversion is necessary
		 */
		tempvr = vreg_disconnect(ret);
		from_type = make_basic_type(TY_DOUBLE);
		vreg_set_new_type(tempvr, from_type);
		vreg_map_preg(tempvr, ret->pregs[0]);
		if (backend->arch == ARCH_MIPS) {
			struct vreg	*fromvr;

			fromvr = dup_vreg(ret);
			vreg_set_new_type(fromvr, from);
			icode_make_mips_cvt(tempvr, fromvr, il);
		}
	} else {
		tempvr = ret;
	}
	generic_conv_to_or_from_ldouble(/*ret*/tempvr, to_type, from_type, il);
	ret->pregs[0] = tempvr->pregs[0];
	ret->pregs[1] = tempvr->pregs[1];
}

static struct vreg * 
conv_int_to_fp_32(struct vreg *ret, struct icode_list *il,
	struct type *from, struct type *to) {

	struct reg		*fpreg;
	struct reg		*lower_word;
	struct vreg		*llong_vreg = NULL;
	struct vreg		*double_vreg = NULL;
	struct icode_instr	*ii;


	if (sysflag != OS_AIX && to->code == TY_LDOUBLE) {
		static int	warned;

		if (!warned) {
			warningfl(NULL, "Integer to long double conversion is not "
			"implemented, which may break this code");
			warned = 1;
		}
	}

	if (from->code < TY_INT) {
		int	intcode;

		if (from->sign == TOK_KEY_UNSIGNED) {
			intcode = TY_UINT;
		} else {
			intcode = TY_INT;
		}
		ret = vreg_disconnect(ret);

		change_preg_size(ret,
			make_basic_type(intcode), from, il);
		ret = vreg_disconnect(ret);
		from = make_basic_type(intcode);
		ret->is_multi_reg_obj = 0;
		ret->type = to;
		ret->size = 4;
	}
	if (float_conv_mask.var_backed == NULL) {
		float_conv_mask.var_backed =
			alloc_decl();
		float_conv_mask.size = 8;
		float_conv_mask.type =
			n_xmemdup(make_basic_type(TY_DOUBLE),
			sizeof(struct type));;
		float_conv_mask.type->name =
			"_Float_conv_mask";
		float_conv_mask.var_backed->dtype =
			float_conv_mask.type;
	}

	/*
	 * XXX This stuff, particularly long long,
	 * is quite 32bit-specific :-(
	 */
	fpreg = backend->alloc_fpr(curfunc, 0, il, NULL);
	lower_word = ALLOC_GPR(curfunc, 0, il, NULL);
	icode_make_power_lis(lower_word, 0x4330, il);
	TO_ZERO();
	icode_make_power_xoris(&power_gprs[0],
		0x8000, il);
	llong_vreg = vreg_alloc(NULL,NULL,NULL,NULL);
	llong_vreg->type = make_basic_type(TY_LLONG);
	llong_vreg->size = 8;

	if (backend->abi != ABI_POWER64) {
		llong_vreg->is_multi_reg_obj = 2;
		vreg_map_preg(llong_vreg, lower_word);
		vreg_map_preg2(llong_vreg, &power_gprs[0]);
	} else {
		llong_vreg->is_multi_reg_obj = 0;

		/*
		 * Move upper word to upper word in register
		 */
		icode_make_power_slwi(&power_gprs[0],
			&power_gprs[0], 32, il);

		/* OR lower word into it as well */
		icode_make_preg_or(&power_gprs[0],
			lower_word, il);
		vreg_map_preg(llong_vreg, &power_gprs[0]);

	}

	icode_make_store(curfunc, llong_vreg, llong_vreg, il);
	free_preg(ret->pregs[0], il, 1, 0);
	/*
 	 * The vreg_map_preg() above sets gpr0 to
 	 * allocatable, which is no good
 	 */
	power_gprs[0].allocatable
		= power_gprs[0].used = 0;

	/*
	 * XXX this should be complete nonsense, like all
	 * other such stack_addr assignemnts! that is
	 * because the address is (always?) a null pointer
	 * which is only later patched up on behalf of
	 * icode_make_allocstack()
	 */
	ret->stack_addr = llong_vreg->stack_addr;

	if (to->code == TY_FLOAT) {
		/* We have to load as double ... */
		double_vreg = n_xmemdup(llong_vreg, sizeof *llong_vreg);
		double_vreg->is_multi_reg_obj = 0;
		double_vreg->type = make_basic_type(TY_DOUBLE);
	}

	power_gprs[0].allocatable = power_gprs[0].used = 0;
	vreg_faultin(fpreg, NULL, double_vreg? double_vreg: ret, il, 0); 
	reg_set_unallocatable(fpreg);
	vreg_faultin(NULL, NULL, &float_conv_mask, il, 0);
	ret->pregs[0] = fpreg;
	ii = icode_make_sub(ret, &float_conv_mask);
	append_icode_list(il, ii);
	if (to->code == TY_FLOAT) {
		/* Truncate to float */
		icode_make_power_frsp(fpreg, il);
	}
	reg_set_allocatable(fpreg);
	return ret;
}

void
generic_double_vreg_to_ldouble(struct vreg *ret, struct icode_list *il) {
	/*
	 * Now convert double result vreg to long double
	 */
	struct vreg	*tempret;

	/*
	 * ret is a long double vreg containing our double value i
	 * one fpr - Create a temporary double vreg for the
	 * conversion routine
	 */
	tempret = vreg_disconnect(ret);
	vreg_set_new_type(tempret, make_basic_type(TY_DOUBLE));
	vreg_map_preg(tempret, tempret->pregs[0]);

	generic_conv_to_or_from_ldouble(tempret, make_basic_type(TY_LDOUBLE),
	 	make_basic_type(TY_DOUBLE), il);
	vreg_map_preg(ret, tempret->pregs[0]);
	vreg_map_preg2(ret, tempret->pregs[1]);
}

void
generic_store_struct_arg_slots(struct function *f, struct sym_entry *se) {
	int	temp_size = se->dec->dtype->tstruc->size;
	int	regidx;
	int	offset = 0;

	/* Architecture-specific settings (XXX pass as parameters?) */
	struct reg	*regset = NULL;
	int		gpr_size = 0;
	int		gpr_slots = 0;

	if (backend->arch == ARCH_POWER) {
		regset = power_gprs + 3;
		gpr_size = power_gprs[0].size;
		gpr_slots = 8;
	} else if (backend->arch == ARCH_MIPS) {
		regset = mips_gprs + 4;
		gpr_size = mips_gprs[0].size;
		gpr_slots = 8;
	} else {
		unimpl();
	}


	regidx = (struct reg *)se->dec->stack_addr->from_reg - regset /*&power_gprs[3]*/;

	/* Passed on stack */
	if (backend->arch == ARCH_POWER) { /* XXX caller should do this */
		se->dec->stack_addr->offset = f->total_allocated + se->dec->stack_addr->offset;
	}
	for (; regidx < gpr_slots; ++regidx) {
		int		slot_size;
		struct reg	*tempreg;

		if (backend->arch == ARCH_MIPS) {
			/*
			 * The first temp GPR contains the base address for PIC
			 * code, and it hasn't been processed/saved yet, so we
			 * shouldn't trash it and use temp GPR 2 instead
			 */
			tempreg = tmpgpr2;
		} else {
			tempreg = tmpgpr;
		}

		/* Got a whole slot to write? */
		if (temp_size >= gpr_size) {  /*(int)power_gprs[0].size) {*/
			slot_size = gpr_size; /*power_gprs[0].size;*/
		} else {
			slot_size = temp_size;
		}
		store_preg_to_var_off(se->dec,
			offset,
			slot_size,
			/*&power_gprs*/ &regset[regidx],
			/*tmpgpr*/ tempreg);

		offset += slot_size;
		temp_size -= slot_size;
		if (temp_size <= 0) {
			break;
		}
	}
}

static struct vreg * 
conv_int_to_fp_64(struct vreg *ret, struct icode_list *il,
	struct type *from, struct type *to) {

	int	emulated_ldouble = 0;

	if (from->code < TY_LONG) {
		int	longcode;

		if (from->sign == TOK_KEY_UNSIGNED) {
			longcode = TY_ULONG;
		} else {
			longcode = TY_LONG;
		}
		ret = vreg_disconnect(ret);
		change_preg_size(ret, make_basic_type(longcode), from, il);
		ret = vreg_disconnect(ret);
		from = make_basic_type(longcode);
		ret->type = to;
		ret->size = backend->get_sizeof_type(to, NULL);
	}

	if (sysflag != OS_AIX && to->code == TY_LDOUBLE) {
		/*
		 * Convert integer to double first, then to long double
		 */
		emulated_ldouble = 1;
		to = make_basic_type(TY_DOUBLE);
	}

	if (IS_LONG(from->code) || 
		IS_LLONG(from->code)) {

		/*
		 * Load value as double, then fcfid it. That's
		 * it!
		 * XXX this only works for signed 64bit to
		 * double :-/ The other stuff just sucks too
		 * much
		 */
		struct vreg	*double_vreg;
		struct vreg	*tmpret;
		struct reg	*fpreg;


		/* Save integer to stack */
		/*
		 * 12/24/08: Use dup_vreg() instead of memdup
		 * (allows us to track sequence numbers)
		 */
		tmpret = dup_vreg(ret); /* XXX :-( */
		tmpret->type = from;
		tmpret->size = backend->get_sizeof_type(from, NULL);
		vreg_map_preg(tmpret, tmpret->pregs[0]);
		free_preg(tmpret->pregs[0], il, 1, 1);

		/*
		 * 12/24/08: Use dup_vreg() instead of memdup
		 * (allows us to track sequence numbers)
		 */
		double_vreg = dup_vreg(tmpret);
		double_vreg->type = make_basic_type(TY_DOUBLE);
		
		/* Now load it as double */
		fpreg = backend->alloc_fpr(curfunc, 0, il, NULL);
		vreg_faultin(fpreg, NULL, double_vreg,
			il, 0);
		icode_make_power_fcfid(fpreg, fpreg, il);
		vreg_map_preg(ret, fpreg);
	} else {
		unimpl();
	}

	if (emulated_ldouble) {
		generic_double_vreg_to_ldouble(ret, il);
	}
	return ret;
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
		/*
		 * 12/23/08: dup_vreg() instead of memdup. This allows
		 * us to track sequence numbers
		 */
		src = dup_vreg(src);
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
		if (!IS_FLOATING(from->code)) {
			if (backend->abi == ABI_POWER64) {
				ret = conv_int_to_fp_64(ret, il, from, to);
			} else {
				ret = conv_int_to_fp_32(ret, il, from, to);
			}
		} else if (to->code != from->code) {
			/* From fp to fp */
			if (sysflag != OS_AIX && (to->code == TY_LDOUBLE || from->code == TY_LDOUBLE)) {
				generic_conv_fp_to_or_from_ldouble(ret, to, from, il);
			}
			if (from->code != TY_FLOAT && to->code == TY_FLOAT) {

				icode_make_power_frsp(ret->pregs[0], il);
			}	
		}
	} else if (IS_FLOATING(from->code)) {	
		struct reg	*r;
		struct vreg	*double_vreg;
		struct vreg	*int_vreg;

		/*
		 * ``to'' has already been found to be non-fp.
		 * We have to round to float, then fctiwz the
		 * result (the backend does both in one go),
		 * save the result FPR as double, and then
		 * read the upper 4 bytes as integer word
		 */

		r = ALLOC_GPR(curfunc, 0, il, NULL);

		/*
		 * 12/23/08: Use fctidz for unsigned 32/64bit integers
		 * and signed 64bit integers on PPC64
		 */
		if ( (  (to->sign == TOK_KEY_UNSIGNED
			&& to->code >= TY_UINT)

			|| (to->code >= TY_LONG)   )

			&& backend->abi == ABI_POWER64) {
			/* 12/22/08: Use fctidz for unsigned */
			icode_make_power_fctiwz(ret->pregs[0], 1, il);
		} else { 
			icode_make_power_fctiwz(ret->pregs[0], 0, il);
		}

		double_vreg = vreg_alloc(NULL, NULL, NULL, NULL);
		vreg_map_preg(double_vreg, ret->pregs[0]);
		double_vreg->type = make_basic_type(TY_DOUBLE);
		double_vreg->size = 8;
		icode_make_store(curfunc, double_vreg, double_vreg, il);
		free_preg(ret->pregs[0], il, 1, 0);

		if (backend->abi == ABI_POWER64) {
			/*
			 * 12/25/08: The 64bit version, which was missing!
			 */
			int_vreg = vreg_alloc(NULL, NULL, NULL, NULL);
			vreg_set_new_type(int_vreg, make_basic_type(TY_LONG));
			int_vreg->stack_addr = double_vreg->stack_addr;
			vreg_faultin(NULL, NULL, int_vreg, il, 0);
			ret->pregs[0] = int_vreg->pregs[0];
		} else {
			int_vreg = vreg_alloc(NULL, NULL, NULL, NULL);
			int_vreg->type = make_basic_type(TY_INT);
			int_vreg->size = 4;
			int_vreg->stack_addr = double_vreg->stack_addr;

			icode_make_power_loadup4(r, int_vreg, il); 
			ret->pregs[0] = r;
			if (IS_LLONG(to->code)) {
				/* XXX */
				ret->pregs[1] = ALLOC_GPR(curfunc, 0, il, NULL);
			} 
		}
	} else {
		int		to_size = backend->get_sizeof_type(to, NULL);
		int		from_size = backend->get_sizeof_type(from, NULL);
                unsigned	needand = 0;
		struct icode_instr	*ii;

                if (to_size == from_size) {
                        if (from->sign != TOK_KEY_UNSIGNED) {
				/*
				 * If the source type is signed, we have to
				 * mask off any possibly set sign bits so
				 * they are not interpreted as values.
				 * (E.g. source is 32bit signed and negative,
				 * and sitting in a 64bit register, and taret
				 * is 32bit unsigned
				 */
				 switch (to_size) {
				 case 1: needand = 0xff; break;
				 case 2: needand = 0xffff; break;
				 case 4: needand = 0xffffffff; break;
				 }
			}
                } else if (to->tlist != NULL) {
                        ; /* XXX What 2 do */
                } else if (to_size < from_size) {
                        /* Truncate */
			if (backend->abi != ABI_POWER64
				&& IS_LLONG(from->code)) {
				struct reg	*r = ret->pregs[0];
				ret->pregs[0] = ret->pregs[1];
				free_preg(r, il, 1, 0);
				power_gprs[0].allocatable =
					power_gprs[0].used = 0;
			} /*else*/
			
			/*
			 * 01/29/08: AND for long long as well (duh)
			 */
			if (to->tlist != NULL) {
				/* XXX hmm */
			} else if (IS_CHAR(to->code)) {
				needand = 0xff;
			} else if (IS_SHORT(to->code)) {
				needand = 0xffff;
			} else if (IS_INT(to->code)
				|| IS_LONG(to->code)
				|| IS_LLONG(to->code)
				|| to->code == TY_ENUM) {
				/* Must be from 64bit long or long long */
				if (backend->abi == ABI_POWER64) {
					needand = 0xffffffff;
				}
			} else {
				unimpl();
			}
                } else {
			/* to_size > from_size - sign- or zero-extend */
			if (backend->abi != ABI_POWER64
				&& IS_LLONG(to->code)) {
				ret->pregs[1] = ret->pregs[0];
				ret->pregs[0] = ALLOC_GPR(curfunc, 0, il, NULL);
				if (from->sign != TOK_KEY_UNSIGNED) {
					icode_make_power_srawi(ret->pregs[0],
						ret->pregs[1], 31, il);
				} else {
					ii = icode_make_setreg(ret->pregs[0],
						0);
					append_icode_list(il, ii);
				}
			} else {
				if (from->sign == TOK_KEY_UNSIGNED) {
					needand = 0xffffffff;
				} else {
					/* sign-extend */
					icode_make_extend_sign(ret, to, from, il);
				}
			}
                }
                if (needand) {
			struct vreg	*tmpvr = vreg_alloc(NULL,NULL,NULL,NULL);
			vreg_set_new_type(tmpvr, make_basic_type(TY_ULONG));
			/*
			 * 12/27/08: Use new function vreg_map_preg_dedicated
			 * which does not set the ``used'' flag (which
			 * trashes the dedicated property of the register
			 * and makes it allocatable eventually)
			 */
			vreg_map_preg_dedicated(tmpvr, tmpgpr);
                        ii = icode_make_setreg(tmpgpr, needand);
                        append_icode_list(il, ii);
                        ii = icode_make_and(ret, /*NULL*/tmpvr);
                        append_icode_list(il, ii);
                }

	}

	vreg_set_new_type(ret, orig_to); /* because of uintptr_t stuff */
	vreg_map_preg(ret, ret->pregs[0]);
	if (backend->abi != ABI_POWER64
		&& IS_LLONG(to->code)
		&& to->tlist == NULL) {
		ret->is_multi_reg_obj = 2;
		vreg_map_preg2(ret, ret->pregs[1]);
	} else {
		if (sysflag != OS_AIX && to->code == TY_LDOUBLE && to->tlist == NULL) {
			ret->is_multi_reg_obj = 2;
			vreg_map_preg2(ret, ret->pregs[1]);
		} else {
			ret->is_multi_reg_obj = 0;
			ret->pregs[1] = NULL;
		}
	}
	if (ret->type->code == TY_BOOL && ret->type->tlist == NULL) {
		boolify_result(ret, il);
	}	

	return ret;
}

static void
icode_make_structreloc(struct copystruct *cs, struct icode_list *il) {
	relocate_struct_regs(cs, &power_gprs[3], &power_gprs[4],
		&power_gprs[5], il);
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
		do_print_gpr(&power_gprs[i]);
		if (((i+1) % 3) == 0) {
			putchar('\n');
		}	
	}
}

static int
is_multi_reg_obj(struct type *t) {
	(void) t;
	if (backend->abi != ABI_POWER64
		&& IS_LLONG(t->code)
		&& t->tlist == NULL) {
		return 2;
	} else {
		if (sysflag != OS_AIX && t->code == TY_LDOUBLE && t->tlist == NULL) {
			return 2;
		}
		return 0;
	}
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

static int
have_immediate_op(struct type *ty, int op) {
	(void) ty; (void) op;
	if (Oflag == -1) { /* XXX */
		return 0;
	}
	return 0;
}

struct backend power_backend = {
	ARCH_POWER,
	0, /* ABI */
	0, /* multi_gpr_object */
	8, /* structure alignment (set by init()) */
	0, /* need pic initialization? */
	1, /* emulate long double. BEWARE changed in init()! */
	0, /* relax alloc gpr order */
	32767, /* max displacement */
	-32768, /* min displacement */
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
	icode_prepare_load_addrlabel,
	icode_make_cast,
	icode_make_structreloc,
	NULL, /* icode_initialize_pic */
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

