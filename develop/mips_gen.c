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
 * MIPS backend
 * XXX there is some SGI assembler specific stuff here which may or may not work
 * with gas on Linux/MIPS
 */
#include "backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
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
#include "misc.h"
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
#include "mips_emit_as.h"
#include "cc1_main.h"
#include "features.h"
#include "n_libc.h"

static FILE			*out;
struct emitter_mips	*emit_mips;

#define N_GPRS 32
#define N_FPRS 32


struct reg		mips_gprs[N_GPRS];
static struct reg		mips_fprs[N_FPRS];
static struct vreg		saved_gprs[N_GPRS];
static struct stack_block	*saved_gprs_sb[N_GPRS];

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

static void
init_regs(void) {
	int	i;

	/* Registers are just named after their number */
	for (i = 0; i < N_GPRS; ++i) {
		static char *names[] = {
			"0", "1", "2", "3", "4", "5", "6", "7",
			"8", "9", "10", "11", "12", "13", "14", "15",
			"16", "17", "18", "19", "20", "21", "22", "23",
			"24", "25", "26", "27", "28", "29", "30", "31"
		};		
		mips_gprs[i].type = REG_GPR;
		mips_gprs[i].allocatable = 1;
		mips_gprs[i].size = 8;
		mips_gprs[i].name = names[i];
	}
	for (i = 0; i < N_FPRS; ++i) {
		static char	*names[] = {
			"f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
			"f8", "f9", "f10", "f11", "f12", "f13", "f14",
			"f15", "f16", "f17", "f18", "f19", "f20", "f21",
			"f22", "f23", "f24", "f25", "f26", "f27", "f28",
			"f29", "f30", "f31"
		};
		mips_fprs[i].type = REG_FPR;
		mips_fprs[i].allocatable = 1;
		mips_fprs[i].size = 8;
		mips_fprs[i].name = names[i];
	}

	/* Some registers with predefined meaning should not be allocated */
	/* 07/13/09: Dedicated settings were missing */
	mips_gprs[0].allocatable = 0; /* zero register */
	reg_set_dedicated(&mips_gprs[0]);
	mips_gprs[1].allocatable = 0; /* assembler temporary register */
	reg_set_dedicated(&mips_gprs[1]);
	mips_gprs[26].allocatable = 0; /* kernel */
	reg_set_dedicated(&mips_gprs[26]);
	mips_gprs[27].allocatable = 0; /* kernel */
	reg_set_dedicated(&mips_gprs[27]);
	mips_gprs[28].allocatable = 0; /* gp */
	reg_set_dedicated(&mips_gprs[28]);
	mips_gprs[29].allocatable = 0; /* sp */
	reg_set_dedicated(&mips_gprs[29]);
	mips_gprs[30].allocatable = 0; /* frame pointer */
	reg_set_dedicated(&mips_gprs[30]);
	mips_gprs[31].allocatable = 0; /* return address */
	reg_set_dedicated(&mips_gprs[31]);
	
	mips_gprs[25].allocatable = 0;
	tmpgpr = &mips_gprs[25];
	reg_set_dedicated(tmpgpr);

	mips_gprs[24].allocatable = 0;
	tmpgpr2 = &mips_gprs[24];
	reg_set_dedicated(tmpgpr2);

	if (picflag) {
		pic_reg = &mips_gprs[28];
		reg_set_dedicated(pic_reg);
	}
}


static void
do_invalidate(struct reg *r, struct icode_list *il, int save) {
	/* Neither allocatable nor used means SPECIAL register */
	if (!r->allocatable && !r->used) {
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
		do_invalidate(&mips_gprs[i], il, saveregs);
	}
	for (i = 0; i < N_FPRS; ++i) {
		do_invalidate(&mips_fprs[i], il, saveregs);
	}
}


static void
invalidate_except(struct icode_list *il, int save, int for_fcall,...) {
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
			if (&mips_gprs[i] == except[j]) {
				break;
			}	
		}
		if (except[j] != NULL) {
			continue;
		}
		do_invalidate(&mips_gprs[i], il, save);
	}	
}

static struct reg *
alloc_gpr(
	struct function *f, 
	int size, 
	struct icode_list *il,
	struct reg *dontwipe,
	
	int line) {
	return generic_alloc_gpr(f,size,il,dontwipe,mips_gprs,N_GPRS,
		callee_save_map, line);	
}	

static struct reg *
alloc_fpr(struct function *f, int size, struct icode_list *il,
struct reg *dontwipe) {
	int			i;
	struct reg	*ret = NULL;

	(void) f; (void) size; (void) il; (void) dontwipe;

	for (i = 0; i < N_FPRS; ++i) {
		if (!mips_fprs[i].used && mips_fprs[i].allocatable) {
			ret = &mips_fprs[i];
			break;
		}
	}
	if (ret == NULL) {
#if 0
		puts("uh-huh your floating point code is too heavy");
		puts("sorry.");
		irix_abort();
#endif
		ret = &mips_fprs[12];
		free_preg(ret, il, 1, 1);
	}
	ret->used = 1; /* 07/16/09: Ouch, this was missing */
	return ret;
}

static int 
init(FILE *fd, struct scope *s) {
	out = fd;

	init_regs();

	if (asmflag && strcmp(asmname, "as") != 0) {
		(void) fprintf(stderr, "Unknown MIPS assembler `%s'\n",
			asmflag);
		exit(EXIT_FAILURE);
	}	
	emit = &mips_emit_as;
	emit_mips = &mips_emit_mips_as;
	backend->emit = emit;
	return emit->init(out, s);
}

static int
get_ptr_size(void) {
	if (backend->abi != ABI_MIPS_N64) {
		return 4;
	} else {
		return 8;
	}
}	

static struct type *
get_size_t(void) {
	if (backend->abi != ABI_MIPS_N64) {
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
		if (backend->abi == ABI_MIPS_N64) {
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
	irix_abort();
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
		rvr.size = mips_gprs[0].size; 
		backend_vreg_map_preg(&rvr, &mips_gprs[2]);
		emit->store(&rvr, &rvr);
		backend_vreg_unmap_preg(&mips_gprs[2]);

		for (sb = f->alloca_head; sb != NULL; sb = sb->next) {
			emit->dealloca(sb, NULL);
		}

		emit->load(&mips_gprs[2], &rvr);
	}
	if (f->vla_head != NULL) {
		struct stack_block	*sb;
		static struct vreg	rvr;

		rvr.stack_addr = f->alloca_regs;
		rvr.size = mips_gprs[0].size;
		backend_vreg_map_preg(&rvr, &mips_gprs[2]);
		emit->store(&rvr, &rvr);
		backend_vreg_unmap_preg(&mips_gprs[2]);

		for (sb = f->vla_head; sb != NULL; sb = sb->next) {
			emit->dealloc_vla(sb, NULL);
		}

		emit->load(&mips_gprs[2], &rvr);
	}
	for (i = 0; i < N_GPRS; ++i) {
		if (saved_gprs[i].stack_addr != NULL) {
			emit->load(&mips_gprs[i], &saved_gprs[i]);
		}
	}
	emit->freestack(f, NULL);
	emit->ret(ip);
}

static struct reg *
get_abi_reg(int index, struct type *ty) {
	if (index == 0) {
		if (is_integral_type(ty)
			|| ty->tlist != NULL) {
			return &mips_gprs[4];
		} else {
			unimpl();
		}
	} else {
		unimpl();
	}
	return NULL;
}	

static struct reg *
get_abi_ret_reg(struct type *ty) {
	if (is_integral_type(ty)
		|| ty->tlist != NULL) {
		if (backend->abi == ABI_MIPS_N32
			|| backend->abi == ABI_MIPS_N64) { 
			return &mips_gprs[2];
		} else {
			unimpl();
		}
	} else {
		unimpl();
	}
	/* NOTREACHED */
	return NULL;
}

/* XXX platform-independent?!?! used by amd64 */
void
store_preg_to_var(struct decl *d, size_t size, struct reg *r)
;
#if 0

{
	static struct vreg	vr;

	vr.type = d->dtype;
	vr.size = size;
	vr.var_backed = d;
	vreg_map_preg(&vr, r);
	emit->store(&vr, &vr);
	r->used = 0;
}
#endif

#define IS_UNION_OR_ARRAY(ty) \
	((ty->code == TY_UNION && ty->tlist == NULL) \
	 || (ty->tlist && ty->tlist->type == TN_ARRAY_OF))

/* Get total size for struct field se, including alignment (except last) */
#define TOTAL_BYTES(se) \
	(se->next? se->next->dec->offset - se->dec->offset: \
	 backend->get_sizeof_type(se->dec->dtype, NULL))

static void
align_stack(struct function *f, struct type *t, size_t size) {
	size_t	align;

	align = backend->get_align_type(t);
	while ((f->total_allocated + size) % align) {
		++f->total_allocated;
	}
}

static void
do_map_parameter(
	struct function *f,
	int *gprs_used,
	int *fprs_used,
	size_t *stack_bytes_used,
	struct sym_entry *se) {

	size_t			size;
	int			need_gpr_block = 0;

	size = backend->get_sizeof_type(se->dec->dtype,0);
	if (is_integral_type(se->dec->dtype)
		|| se->dec->dtype->tlist != NULL) {
		if (*gprs_used < 8) {
			align_stack(f, se->dec->dtype, size); 
#if 0
			se->dec->stack_addr
				= stack_malloc(f, size); 
#endif
			if (*gprs_used == 0) {
				need_gpr_block = 1;
			}
			se->dec->stack_addr = make_stack_block(
				(8 - *gprs_used) * mips_gprs[0].size,
				size);
			se->dec->stack_addr->from_reg =
				&mips_gprs[4 + *gprs_used];
			++*gprs_used;
		} else {
			int	adjustment;

			/*
			 * 07/23/09: 64bit argument slots are right-adjusted
			 * on big endian, left on little endian
			 */
			if (get_target_endianness() == ENDIAN_LITTLE) {
				adjustment = 0;
			} else {
				adjustment = 8 - size;
			}
			se->dec->stack_addr = make_stack_block(
				*stack_bytes_used + adjustment /*(8 - size)*/, size);
			*stack_bytes_used += 8;
		}
	} else if (IS_FLOATING(se->dec->dtype->code)) {
		if (se->dec->dtype->code == TY_LDOUBLE) {
			/* 07/20/09: Align long double */
			if (*fprs_used < 8) {
				/* Registers */
				if (*fprs_used & 1) {
					++*fprs_used;
				}
			} else {
				/* Stack */
				if (*stack_bytes_used & 15) {
					/* Not 16-byte-aligned */
					*stack_bytes_used += 8;
					assert((*stack_bytes_used & 15) == 0);
				}
			}
		}

		if (*fprs_used < 8) {
			align_stack(f, se->dec->dtype, size); 
			se->dec->stack_addr
				= stack_malloc(f, size); 
			se->dec->stack_addr->from_reg =
				&mips_fprs[12 + *fprs_used];
			++*fprs_used;
			if (se->dec->dtype->code == TY_LDOUBLE) {
				++*fprs_used;
			}
		} else {
		/* XXX broken */
			se->dec->stack_addr = make_stack_block(
				*stack_bytes_used, size);
			if (size < 8) {
				/*
				 * 07/20/09: Floats take up an 8-byte
				 * slot as well
				 */
				*stack_bytes_used += mips_gprs[0].size;
			} else {
				*stack_bytes_used += size;
			}
		}
	} else if (se->dec->dtype->code == TY_STRUCT
		|| se->dec->dtype->code == TY_UNION) {
		int	temp;

#if 0
		*stack_bytes_used += se->dec->dtype->tstruc->size;
#endif
		if (*gprs_used < 8) {
			/*
			 * Some parts of the struct are passed in one or
			 * more registers. This requires us to allocate
			 * backing storage for the registers (which must
			 * be located at the top of the stack frame)
			 */
			int	total_slots = size / mips_gprs[0].size;

			if (*gprs_used == 0) {
				need_gpr_block = 1;
			}

			if (total_slots * mips_gprs[0].size != size) {
				++total_slots;
			}

			if (total_slots > 8 - *gprs_used) {
				total_slots = 8 - *gprs_used;
			}


			/* Allocate register backing block */
#if 0
			se->dec->stack_addr
				= stack_malloc(f, total_slots * mips_gprs[0].size); 
#endif
			se->dec->stack_addr = make_stack_block(
				(8 - *gprs_used) * mips_gprs[0].size,
				size);
#if 0
			/*
			 * Now resize it to cover the whole struct (there may
			 * be data behind the register area which is already
			 * allocated on the stack) (Does this even make any
			 * difference at all?)
			 */
			se->dec->stack_addr->nbytes = size;
#endif

			se->dec->stack_addr->from_reg =
				&mips_gprs[4+*gprs_used];
		} else {
			/*
			 * The entire struct is passed on the stack
			 */
			se->dec->stack_addr = make_stack_block(*stack_bytes_used,
				se->dec->dtype->tstruc->size);
		}

		temp = se->dec->dtype->tstruc->size;
		while (temp > 0) {
			/*++*slots_used;*/
			if (*gprs_used >= 8) {
				break;
			}
			++*gprs_used;
			temp -= mips_gprs[0].size;
		}

		if (temp > 0) {
			if (temp % mips_gprs[0].size) {
				temp += mips_gprs[0].size - (temp % mips_gprs[0].size);
			}
			*stack_bytes_used += temp;
		}
	} else {
		unimpl();
	}
	se->dec->stack_addr->is_func_arg = 1;

	if (need_gpr_block) {
		(void) stack_malloc(f, 8 * mips_gprs[0].size);
	}
}

static void
map_parameters(struct function *f, struct ty_func *proto) {
	int			i;
	int			gprs_used = 0;
	int			fprs_used = 0;
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
		f->fty->lastarg->stack_addr = stack_malloc(f, 64);
	}

	se = proto->scope->slist;
	if (f->proto->dtype->tlist->next == NULL
		&& (f->proto->dtype->code == TY_STRUCT
		|| f->proto->dtype->code == TY_UNION)) {
		/*
		 * Function returns struct/union - accomodate for
		 * hidden pointer (passed as first argument)
		 */
		size_t	ptrsize = backend->abi == ABI_MIPS_N64? 8: 4;

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
#if 0
		f->hidden_pointer->var_backed->stack_addr =
			stack_malloc(f, ptrsize);
#endif
		/*
		 * 07/23/09: Just allocate a whole block for all 8 registers
		 * in case we pass struct-by-value or somesuch. This is usually
		 * done in do_map_parameter() when encountering the first
		 * register, but we take that one up here, so the block must
		 * be created here
		 *
		 * Note that we assign the 64 byte block here, but all other
		 * allocations requiring a GPR save area slot will hard-code
		 * an offset into this block
		 */
		f->hidden_pointer->var_backed->stack_addr =
			stack_malloc(f, 8 * mips_gprs[0].size);
		f->hidden_pointer->var_backed->stack_addr->from_reg =
			&mips_gprs[4];
		++gprs_used;
	}	

	/*
	 * Allocate stack space for those arguments that were
	 * passed in registers, set addresses to existing
	 * space for those that were passed on the stack
	 */
	for (i = 0; i < proto->nargs; ++i, se = se->next) {
		do_map_parameter(f, &gprs_used, &fprs_used,
			&stack_bytes_used, se);
	}
	if (f->fty->variadic) {
		if (gprs_used >= 8) {
			/* Wow, all variadic stuff passed on stack */
			f->fty->lastarg->stack_addr->offset =
				stack_bytes_used;
		} else {
			/*
			 * 64 bytes were allocated, skip as many as
			 * necessary
			 */
			f->fty->lastarg->stack_addr->offset -=
				gprs_used * 8;
			f->fty->lastarg->stack_addr->from_reg =
				&mips_gprs[4 + gprs_used];
		}
	}
}

static void
make_local_variables(struct function *f) {
	struct scope	*scope;
	int				i;
	size_t			size;

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
			align = backend->get_align_type(dec[i]->dtype);
			while ((f->total_allocated + size) % align) {
				++f->total_allocated;
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
	char			*funcname;
	struct ty_func		*proto;
	struct scope		*scope;
	struct icode_instr	*lastret = NULL;
	struct stack_block	*sb;
	struct sym_entry	*se;
	size_t			alloca_bytes = 0;
	size_t			vla_bytes = 0;

	
	emit->setsection(SECTION_TEXT);
	/* XXX use emit->intro() ??? */
	x_fprintf(out, "\t.align 2\n");
	if (f->proto->dtype->storage != TOK_KEY_STATIC) {
		x_fprintf(out, "\t.globl %s\n", f->proto->dtype->name);
	}
	x_fprintf(out, "\t.ent %s\n", f->proto->dtype->name);

	emit->label(f->proto->dtype->name, 1);
	funcname = f->proto->dtype->name;

	proto = f->proto->dtype->tlist->tfunc;

	map_parameters(f, proto);

	stack_align(f, 16); /* 07/19/09: 16 bytes for long double */
	make_local_variables(f);
	stack_align(f, 16);

	/*
	 * Allocate storage for saving callee-saved registers
	 * (but defer saving them until sp has been updated)
	 */
	nsaved = 0;
	for (mask = 1, i = 0; i < N_GPRS; ++i, mask <<= 1) {
		if ((f->callee_save_used & mask)
			|| i == 28 /* global pointer */	
			|| i == 30 /* frame pointer */
			|| i == 31 /* return address */) {
#if ! USE_ZONE_ALLOCATOR
			/*
			 * 07/12/09: Whoops, not allocating a new block and
			 * setting the frame pointer flag breaks since the
			 * introduction of the zone allocator
			 */
			if (saved_gprs_sb[i] == NULL)
#endif
			{
				saved_gprs_sb[i] =
					make_stack_block(0, 8);

				/*
				 * The frame pointer cannot be used yet
				 * because it needs to be saved as well
				 */
				saved_gprs_sb[i]->use_frame_pointer = 0;
			}
			f->total_allocated += 8;
			saved_gprs[i].stack_addr = saved_gprs_sb[i];
			saved_gprs[i].size = 8;
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
	stack_align(f, mips_gprs[0].size);
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

	stack_align(f, mips_gprs[0].size);

	if (f->alloca_head != NULL || f->vla_head != NULL) {
		/*
		 * Get stack for saving return value register (r4) before
		 * performing free() on alloca()ted blocks
		 */
		f->alloca_regs = make_stack_block(0, mips_gprs[0].size);
		f->total_allocated += mips_gprs[0].size;
		f->alloca_regs->offset = f->total_allocated;
	}
	
	stack_align(f, 16); /* 07/19/09: 16 bytes for long double */

	if (f->total_allocated > 0) {
		emit->allocstack(f, f->total_allocated);
	}

	/*
	 * Local variable offsets can only now be patched because the
	 * MIPS frame pointer points to the bottom of the frame, not
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
				/* XXX This ignores pointers to VLAs */
				continue;
			}
			if (dec[i]->stack_addr->is_func_arg
				&& !dec[i]->stack_addr->from_reg) {
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

	/*
	 * 07/19/09: hidden_pointer's address was patched in the loop (!) above
	 * so the offset ended up getting changed multiple times
	 */
	if (f->hidden_pointer) {
		f->hidden_pointer->var_backed->stack_addr->offset =
			f->total_allocated -
			f->hidden_pointer->var_backed->stack_addr->offset;
	}


#if 0 /* 07/20/09: This was apparently done twice! Done again below */
	if (f->fty->variadic) {
		f->fty->lastarg->stack_addr->offset =
			f->total_allocated - 
			f->fty->lastarg->stack_addr->offset;
	}
#endif

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
	if (f->alloca_head != NULL || f->vla_head != NULL) {
		f->alloca_regs->offset = f->total_allocated -
			f->alloca_regs->offset;
	}

	/*
	 * Construct mask of saved registers (must be in little
	 * endian format)
	 */
	mask = 0;
	for (i = 0; i < N_GPRS; ++i) {
		if (saved_gprs[i].stack_addr != NULL) {
			mask |= 1 << i;
		}
	}

	x_fprintf(out, "\t.mask 0x%x,-%lu\n", mask,
		f->total_allocated - f->callee_save_offset);
	if (mask != 0) {
		for (i = 0; i < N_GPRS; ++i) {
			if (saved_gprs[i].stack_addr != NULL) {
				saved_gprs[i].stack_addr->offset =
					f->total_allocated -
					saved_gprs[i].stack_addr->offset;
					
				backend_vreg_map_preg(&saved_gprs[i],
					&mips_gprs[i]);
				emit->store(&saved_gprs[i],
					&saved_gprs[i]);	
				backend_vreg_unmap_preg(
					&mips_gprs[i]);
				mips_gprs[i].used = 0;
			}
		}
	}
	x_fprintf(out, "\tmove $fp, $sp\n");

	/*
	 * Patch parameter offsets and save corresponding argument
	 * registers to stack, if necessary
	 */
	for (i = 0; i < proto->nargs; ++i, se = se->next) {
		if (se->dec->stack_addr->from_reg != NULL) {
			/* Write argument register contents to stack */
			if ((se->dec->dtype->code == TY_STRUCT
				|| se->dec->dtype->code == TY_UNION)
				&& se->dec->dtype->tlist == NULL) {
				/* Passed on stack */
#if 0 
				se->dec->stack_addr->offset =
					f->total_allocated +
					se->dec->stack_addr->offset;
#endif
				generic_store_struct_arg_slots(f, se);
			} else {
				store_preg_to_var(se->dec,
					se->dec->stack_addr->nbytes,
					se->dec->stack_addr->from_reg);
			}
		} else {
			/*
			 * Argument passed on stack;
			 * fp . . . . . . [frame start] [arguments ...]
			 * ^ lowest address           highest address ^
			 * So the offset (with fp) is the size of the entire
			 * stack frame plus the offset in the stack arg area
			 */
			se->dec->stack_addr->offset =
				f->total_allocated +
				se->dec->stack_addr->offset;
		}
	}
	if (f->hidden_pointer) {
		struct decl	*d = f->hidden_pointer->var_backed;

		store_preg_to_var(d,
			d->stack_addr->nbytes,
			d->stack_addr->from_reg);
	}
	if (f->fty->variadic) {
		size_t	saved_offset;

		if (f->fty->lastarg->stack_addr->from_reg == NULL) {
			/* Entirely on stack */
			f->fty->lastarg->stack_addr->offset =
				f->total_allocated +
				f->fty->lastarg->stack_addr->offset;
		} else {
			struct reg	*r =
				f->fty->lastarg->stack_addr->from_reg;

			/*
			 * 07/20/09: Get offset from frame pointer to
			 * last argument (this used to be done above,
			 * which wrongly applied to the case ``entirely
			 * on stack'' as well)
			 */
			f->fty->lastarg->stack_addr->offset =
				f->total_allocated - 
				f->fty->lastarg->stack_addr->offset;

			/*
			 * Save all GPRs to their corresponding save
			 * area location
			 */
			saved_offset = f->fty->lastarg->stack_addr->offset;
			for (; r != &mips_gprs[4+8]; ++r) {
				store_preg_to_var(f->fty->lastarg, 8, r);
				f->fty->lastarg->stack_addr->offset += 8;
			}
			f->fty->lastarg->stack_addr->offset = saved_offset;
		}
	}

	/*
	 * 07/17/09: Changed PIC stuff slightly for Linux. At first I tried
	 * to generate an INSTR_INITIALIZE_PIC, but we have to generate the
	 * initialization sequence immediately because emit->zerostack()
	 * below already needs a properly set up PIC register
	 */
	if (sysflag == OS_IRIX) {
		x_fprintf(out, "\t.set noat\n");
		x_fprintf(out, "\tlui $1, %%hi(%%neg(%%gp_rel(%s)))\n",
			funcname);
		x_fprintf(out, "\taddiu $1, $1, %%lo(%%neg(%%gp_rel(%s)))\n",
			funcname);
		x_fprintf(out, "\tdaddu $gp, $1, $25\n");
		x_fprintf(out, "\t.set at\n");
	} else if (picflag) {
		emit->initialize_pic(f);
	}

	/*
	 * 08/27/07: This wrongly came before the gp_rel stuff above, thus
	 * yielding a crash
	 */
	if (f->alloca_head != NULL) {
		emit->zerostack(f->alloca_tail, alloca_bytes);
	}
	if (f->vla_head != NULL) {
		emit->zerostack(f->vla_tail, vla_bytes);
	}	

	if (xlate_icode(f, f->icode, &lastret) != 0) {
		return -1;
	}
#if 0
	if (lastret != NULL) {
		struct icode_instr	*tmp;

		for (tmp = lastret->next; tmp != NULL; tmp = tmp->next) {
			if (tmp->type != INSTR_SETITEM) {
				lastret = NULL;
				break;
			}
		}
	}

	if (lastret == NULL) { 
		do_ret(f, NULL);
	}
#endif

	x_fprintf(out, "\t.end %s\n", f->proto->dtype->name);

	return 0;
}

#if XLATE_IMMEDIATELY

static int
gen_prepare_output(void) {
	/*
	 * 07/12/09: Amazing, this was missing. I guess MIPS support was
	 * broken for 0.7.8/9 then?!
	 */
	x_fprintf(out, "\t.option pic2\n");
	x_fprintf(out, "\t.section .rodata,0x1,0x2,0,8\n");
	x_fprintf(out, "\t.section .data,0x1,0x3,0,8\n");
	x_fprintf(out, "\t.section .text,0x1,0x6,4,4\n");
	return 0;
}

static int
gen_finish_output(void) {
	/*
	 * 07/12/09: Amazing, this was missing. I guess MIPS support was
	 * broken for 0.7.8/9 then?!
	 */
	emit->global_extern_decls(global_scope.extern_decls.data,
			global_scope.extern_decls.ndecls);
	emit->global_static_decls(
			global_scope.static_decls.data,
			global_scope.static_decls.ndecls);
#if 0
	emit->static_decls();
#endif
	emit->static_init_vars(static_init_vars);
	emit->static_uninit_vars(static_uninit_vars);
	emit->static_init_thread_vars(static_init_thread_vars);
	emit->static_uninit_thread_vars(static_uninit_thread_vars);

#if 0 /* 07/12/09: Strings and fp constans have already been emitted */
	emit->struct_inits(init_list_head);

	emit->empty();
	emit->strings(str_const); /* XXX bad */
	emit->fp_constants(float_const);
#endif
	x_fflush(out);
	return 0;
}

#else

static int
gen_program(void) {
	struct function		*func;
	
	x_fprintf(out, "\t.option pic2\n");
	x_fprintf(out, "\t.section .rodata,0x1,0x2,0,8\n");
	x_fprintf(out, "\t.section .data,0x1,0x3,0,8\n");
	x_fprintf(out, "\t.section .text,0x1,0x6,4,4\n");
#if 0
	emit->global_decls();
#endif
	emit->global_extern_decls(global_scope.extern_decls.data,
			global_scope.extern_decls.ndecls);
	emit->global_static_decls(
			global_scope.static_decls.data,
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

	x_fprintf(out, ".section .text\n");
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
	size_t *stack_bytes_used,
	struct icode_list *il) {

	struct vreg	*dest;

	/*
	 * All types are passed as double words  
	 * (right-justified)
	 */
	/*size_t*/ int	size;

	(void) bytes_left;

	size = backend->get_sizeof_type(vr->type, NULL);
	if (vr->type->tlist
		&& vr->type->tlist->type ==
		TN_ARRAY_OF) {
		size = 4;
	}

	if (vr->type->code == TY_LDOUBLE && vr->type->tlist == NULL) {
		/*
		 * 07/19/09: Align long double to even slot number
		 */
		if ((*stack_bytes_used / 8) & 1) {
			*stack_bytes_used += 8;
		}
	} else if (8 - size > 0) {
		/* Need to right-adjust */
		/* 07/15/09: Not for little endian */
		if (get_target_endianness() != ENDIAN_LITTLE) {
			*stack_bytes_used += 8 - size;
		}
	}
	dest = vreg_alloc(NULL, NULL, NULL, NULL);
	dest->type = vr->type;
	dest->size = vr->size;
	if (dest->type->code == TY_LDOUBLE && dest->type->tlist == NULL) {
		dest->is_multi_reg_obj = 2;
	}
	dest->stack_addr = make_stack_block(
		*stack_bytes_used, dest->size); 
	dest->stack_addr->use_frame_pointer = 0;
	*stack_bytes_used += size;


	/*
	 * 12/27/08: Use new dedicated functions which do not set
	 * the ``used'' flag (which tarshes the dedicated property
	 * of the register and makes it allocatable eventually
	 */
	if (IS_FLOATING(vr->type->code)) {
		/*
		 * 07/14/09: Don't use a temp GPR for an FP item,
		 * which triggers sanity checks and probably never
		 * worked correctly (if the item is already FPR-
		 * resident, the faultin would try to ``copyreg''
		 * it to the GPR)
		 */
		vreg_faultin(NULL, NULL, vr, il, 0);
		vreg_map_preg(dest, vr->pregs[0]);
		if (dest->is_multi_reg_obj) {
			vreg_map_preg2(dest, vr->pregs[1]);
		}
	} else {
		vreg_faultin_dedicated(tmpgpr, NULL, vr, il, 0);
		vreg_map_preg_dedicated(dest, tmpgpr);
	}
	icode_make_store(curfunc, dest, dest, il);


	/*
	 * 07/17/09: For little endian, which isn't right-adjusted:
	 * Fill slot after argument
	 */
	if (8 - size > 0) {
		if (get_target_endianness() == ENDIAN_LITTLE) {
			*stack_bytes_used += 8 - size;
		}
	}
}

/* XXX generic? */
void
put_arg_into_reg(
	struct reg *regset,
	int *index, int startat,
	struct vreg *vr,
	struct icode_list *il);

unsigned long	reg_offset;

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
 * 07/17/09: This used to calculate in terms of GPR/FPR slots,
 * which is wrong. So like on PPC, we now add an argument slot
 * per scalar argument (except long double), and then align to
 * save area slot alignment (for structs).
 */
size_t
mips_calc_stack_bytes(struct vreg **vrs, int nvrs, int *takes_struct) {
	size_t	nbytes = 0;
	int	i;
	int	stackstruct = 0;

	for (i = 0; i < nvrs; ++i) {
		/*if (is_integral_type(vrs[i]->type)
			|| vrs[i]->type->tlist != NULL) {
			if (gprs_used < 8) {
				++gprs_used;
			} else {
				nbytes += 8;
			}
		} else if (IS_FLOATING(vrs[i]->type->code)) {
			if (fprs_used < 8) {
				++fprs_used;
			} else {
				if (vrs[i]->type->code == TY_LDOUBLE) {
					nbytes += 16;
				} else {
					nbytes += 8;
				}
			}
		} else*/
		
		if (vrs[i]->type->code == TY_STRUCT
			|| vrs[i]->type->code == TY_UNION) {
			nbytes += vrs[i]->type->tstruc->size;
			stackstruct = 1;
		} else if (vrs[i]->type->code == TY_LDOUBLE
			&& vrs[i]->type->tlist == NULL) {
			nbytes += 16;
		} else {
			nbytes += mips_gprs[0].size;
		}
	}
	if (nbytes & 15) { /* Align to 16 for long double */
		nbytes += 16 - (nbytes & 15);
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

void
generic_double_vreg_to_ldouble(struct vreg *ret, struct icode_list *il);
void
generic_conv_fp_to_or_from_ldouble(struct vreg *ret,
	struct type *to, struct type *from,
	struct icode_list *il);




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
	int			ret_is_anon_struct = 0;


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

#if 1 /* XXX should go! Use rettype */
			tnsav = ty->tlist;
			ty->tlist = NULL;
#endif
			/* XXX hm */
			/*
			 * 08/23/07: This gave a bus error because alignemtn
			 * was missing. Fortunately we have the vreg_stack_alloc()
			 * flag which says ``allocate buffer when creating stack
			 * frame, not now'', and it seems to work here. This was
			 * still disabled because it didn't work on x86, for
			 * unknown reasons
			 */
			struct_lvalue = vreg_stack_alloc(ty, il, 1 /*0*/, NULL);

#if 1 /*  XXX should go! Use rettype */
			ty->tlist = tnsav;
#endif
			/*
			 * 08/05/08: Don't add to allpushed since struct is
			 * created on frame. This bug occurred on AMD64 and
			 * hasn't been verified on MIPS yet
			 */
	/*		allpushed += struct_lvalue->size;*/
			while (allpushed % 8) ++allpushed;
			ret_is_anon_struct = 1;
		}

		/* Hidden pointer is passed in first GPR! */
#if 0
		ii = icode_make_addrof(NULL, struct_lvalue, il);
		append_icode_list(il, ii);
#endif
		{
			struct reg *r;

			/*ii*/ r = make_addrof_structret(struct_lvalue, il);
			free_preg(&mips_gprs[4], il, 1, 1);
			icode_make_copyreg(&mips_gprs[4], r /*ii->dat*/,
				NULL, NULL, il);
			++gprs_used;
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

	allpushed += mips_calc_stack_bytes(vrs, nvrs, &takes_struct);
	if (takes_struct) {
		/* memcpy() will be called to pass argument(s) */
		backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
	}
	if (allpushed > 0) {
		icode_make_allocstack(NULL, allpushed, il);
	}

	for (i = 0; i < nvrs; ++i) {
		if ((fcall->functype->variadic
			&& i >= fcall->functype->nargs)
			|| fcall->functype->nargs == -1) {
			need_dap = 1;
		}

		if (vrs[i]->parent) {
			vr2 = get_parent_struct(vrs[i]);
		} else {
			vr2 = NULL;
		}

		/*
		 * 07/14/09: Removed from_const check - that wrongly
		 * included floating point constants!
		 */
		if (is_integral_type(vrs[i]->type)
			|| vrs[i]->type->tlist	
			/*|| vrs[i]->from_const*/) {
			if (vrs[i]->type->tlist == NULL
				&& (IS_CHAR(vrs[i]->type->code)
				|| IS_SHORT(vrs[i]->type->code))) {
				struct type	*ty =
					make_basic_type(TY_INT);

				vrs[i] = backend->
					icode_make_cast(vrs[i], ty, il);
			}
			if (gprs_used < 8) { /* 4 in o32 */
				put_arg_into_reg(mips_gprs,
					&gprs_used, 4, vrs[i], il);	
			} else {
				pass_arg_stack(vrs[i], 0,
					&stack_bytes_used, il);
			}
		} else if (vrs[i]->type->code == TY_STRUCT
			|| vrs[i]->type->code == TY_UNION) {
#if 0
			pass_struct_union(vrs[i],
				&gprs_used, &fprs_used,	&stack_bytes_used, il);
#endif
			generic_pass_struct_union(vrs[i],
				&gprs_used, NULL, &stack_bytes_used, il);
			if (gprs_used >= 8) {
				/*
				 * XXXXXX the function thinks in terms of slots,
				 * but we are still stuck with GPRs on MIPS. So
				 * limit them to 8
				 */
				gprs_used = 8;
			}
		} else if (IS_FLOATING(vrs[i]->type->code)) {
			/*
			 * For variadic functions, floating point values
			 * go into gprs (floats are promoted to double)
			 */
			if (/*fcall->functype->variadic
				&&*/ need_dap) {
				if (vrs[i]->type->code == TY_FLOAT) {
					vrs[i] = backend->
						icode_make_cast(vrs[i],
							make_basic_type(TY_DOUBLE), il);
				} else if (vrs[i]->type->code == TY_LDOUBLE) {
					if (gprs_used & 1) {
						++gprs_used;
					}
				}

				if (gprs_used < 8) {
					if (vrs[i]->type->code == TY_LDOUBLE) {
						struct vreg	*tmp;
	free_pregs_vreg(vrs[i], il, 1, 1);
						tmp = dup_vreg(vrs[i]);
/*						tmp->pregs[0] = NULL;
hello*/
						free_preg(&mips_gprs[4+gprs_used], il, 1, 1);
						free_preg(&mips_gprs[4+gprs_used+1], il, 1, 1);
						vreg_faultin(&mips_gprs[4+gprs_used],
							&mips_gprs[4+gprs_used+1],
							tmp, il, 0);
						reg_set_unallocatable(&mips_gprs[4+gprs_used]);
						reg_set_unallocatable(&mips_gprs[4+gprs_used+1]);
						gprs_used += 2;
					} else {
						vreg_reinterpret_as(&vrs[i], vrs[i]->type,
							make_basic_type(TY_ULLONG), il);
						put_arg_into_reg(mips_gprs,
							&gprs_used, 4, vrs[i], il);	
					}
				} else {
					pass_arg_stack(vrs[i], 0,
						&stack_bytes_used, il);
				}
			} else {
				if (vrs[i]->type->code == TY_LDOUBLE) {
					/*
					 * Align FPR number (note that
					 * pass_arg_stack() aligns for
					 * the stack case)
					 */
					if (fprs_used < 8) {
						if (fprs_used & 1) {
							++fprs_used;
						}
					}
				}

				if (fprs_used < 8) {
					put_arg_into_reg(mips_fprs,
						&fprs_used, 12, vrs[i], il);	
				} else {
					pass_arg_stack(vrs[i], 0,
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
#if 0
		ii = icode_make_addrof(NULL, struct_lvalue, il);
		append_icode_list(il, ii);
#endif
		struct reg *r;

		/*ii*/ r = make_addrof_structret(struct_lvalue, il);

		icode_make_copyreg(&mips_gprs[4], r /*ii->dat*/, NULL, NULL, il);

		free_preg(r /*ii->dat*/, il, 0, 0);
	}

	for (i = 0; i < gprs_used; ++i) {
		/* Don't save argument registers */
		mips_gprs[4+i].used = 0;
		mips_gprs[4+i].vreg = NULL;
	
		/* 12/29/07: This was wrong (see SPARC) */
#if 0
		mips_gprs[4+i].allocatable = 1;
#endif
	}

	for (i = 0; i < fprs_used; ++i) {
		/* Don't save argument fp registers */
		mips_fprs[12+i].used = 0;
		mips_fprs[12+i].vreg = NULL;
		mips_fprs[12+i].allocatable = 0;
	}
		
	if (!takes_struct) {
		backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
	}

	for (i = 0; i < gprs_used; ++i) {
		mips_gprs[4+i].allocatable = 1;
	}
	for (i = 0; i < fprs_used; ++i) {
		mips_fprs[12+i].allocatable = 1;
	}

	if (ty->tlist->type == TN_POINTER_TO) {
		/* Need to indirect thru function pointer */
		vreg_faultin(&mips_gprs[12], NULL, tmpvr, il, 0);
		ii = icode_make_call_indir(tmpvr->pregs[0]);
		tmpvr->pregs[0]->used = 0;
		tmpvr->pregs[0]->vreg = NULL;
	} else {
		ii = icode_make_call(ty->name);
	}	
	append_icode_list(il, ii);
	if (allpushed > 0) {
		ii = icode_make_freestack(allpushed);
		append_icode_list(il, ii);
	}

	ret = vreg_alloc(NULL, NULL, NULL, NULL);
	ret->type = ty;

	/*
	 * XXX this stuff SUCKS!
	 */
#if 0
	if (ty->tlist->next != NULL) {
#endif

	if ((ty->tlist->type == TN_POINTER_TO
		&& ty->tlist->next->next != NULL)
		|| (ty->tlist->type == TN_FUNCTION
		&& ty->tlist->next != NULL)) {	
		/* Must be pointer */
		ret->pregs[0] = &mips_gprs[2];
	} else {
#if 1 /* 06/17/08: XXX Should go, use rettype! */
		struct type_node	*tnsav = ty->tlist;

		ty->tlist = NULL;
#endif
		if (is_integral_type(ty)) {
			ret->pregs[0] = &mips_gprs[2];
		} else if (ty->code == TY_FLOAT
			|| ty->code == TY_DOUBLE
			|| ty->code == TY_LDOUBLE) {
			ret->pregs[0] = &mips_fprs[0];
			if (ty->code == TY_LDOUBLE) {
				ret->pregs[1] = &mips_fprs[2];
				ret->is_multi_reg_obj = 2;
			}
		} else if (ty->code == TY_STRUCT
			|| ty->code == TY_UNION) {
			if (ret_is_anon_struct) {
				ret = struct_lvalue;
			}
			ret->struct_ret = 1;
			ret->pregs[0] = NULL;
		} else if (ty->code == TY_VOID) {
			; /* Nothing! */
		}
		ty->tlist = tnsav;
	}
	for (i = 4; i < 12; ++i) {
		reg_set_allocatable(&mips_gprs[i]);
	}

	if (ret->pregs[0] != NULL) {
		vreg_map_preg(ret, ret->pregs[0]);
	}	
	if (ret->is_multi_reg_obj) {
		vreg_map_preg2(ret, ret->pregs[1]);
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
	struct type		*rtype = curfunc->rettype; /*proto->dtype;*/

	/* 06/17/08: Don't use type kludgery, but rettype! */
#if 0
	oldtn = rtype->tlist;
	rtype->tlist = rtype->tlist->next;
#endif
	

	if (vr != NULL) {
		if (is_integral_type(rtype)
			|| rtype->code == TY_ENUM /* 06/15/09: Was missing?!? */
			|| rtype->tlist != NULL) {
			vreg_faultin(&mips_gprs[2], NULL, vr, il, 0);
		} else if (rtype->code == TY_FLOAT
			|| rtype->code == TY_DOUBLE) {
			vreg_faultin(&mips_fprs[0], NULL, vr, il, 0);
		} else if (rtype->code == TY_LDOUBLE) {
			vreg_faultin(&mips_fprs[0], &mips_fprs[2], vr, il, 0);
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
	if (ret == src) {
		/* XXX ... */
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
		; /* Nothing to do */
	} else if (IS_FLOATING(to->code)) {
		if (!IS_FLOATING(from->code)) {
			struct reg	*r;
			struct vreg	*double_vr;

			r = backend->alloc_fpr(curfunc, 0, il, NULL);
			icode_make_mips_mtc1(r, src, il); 
			free_preg(ret->pregs[0], il, 0, 0);
			ret->pregs[0] = r;

			if (to->code == TY_LDOUBLE) {
				double_vr = dup_vreg(ret);
				vreg_set_new_type(double_vr, make_basic_type(TY_DOUBLE));
			} else {
				double_vr = ret;
			}

			icode_make_mips_cvt(/*ret*/double_vr, src, il);

			if (to->code == TY_LDOUBLE) {
				/*
				 * 07/19/09: Integer to emulated long double
				 * conversion
				 */
				generic_double_vreg_to_ldouble(ret, il);
			}
		} else if (to->code != from->code) {
			/* From fp to fp */
			/* 07/16/09: long double now emulated using double */
			if (to->code == TY_LDOUBLE || from->code == TY_LDOUBLE) {
				generic_conv_fp_to_or_from_ldouble(ret, to, from, il);
			} else {
				icode_make_mips_cvt(ret, src, il);
			}
		}
	} else if (IS_FLOATING(from->code)) {	
		struct reg	*r;

		/* ``to'' has already been found to be non-fp */

		icode_make_mips_trunc(ret, src, il);
		r = ALLOC_GPR(curfunc, 0, il, NULL);
		icode_make_mips_mfc1(r, src, il); 
		free_preg(ret->pregs[0], il, 0, 0);
		ret->pregs[0] = r;
	} else {
		int	to_size = backend->get_sizeof_type(to, NULL);
		int	from_size = backend->get_sizeof_type(from, NULL);	
		struct icode_instr	*ii;
		unsigned			needand = 0;

		/* integer <-> integer */ 
		if (to_size == from_size) {
			;
		} else if (to->tlist != NULL) {
			; /* XXX what 2 do */ 
		} else if (to_size < from_size) {

			/* Truncate */
			if (to->tlist != NULL) {
				/* XXX hmm... */
			} else if (IS_CHAR(to->code)) {
				needand = 0xff;
			} else if (IS_SHORT(to->code)) {
				needand = 0xffff;
			} else if (IS_INT(to->code)
				|| IS_LONG(to->code)) {
				/*
				 * Must be coming from a 64bit long or long long
				 */
				icode_make_mips_make_32bit_mask(tmpgpr, il);
				ii = icode_make_and(ret, NULL);
				append_icode_list(il, ii);
			} else {
				unimpl();
			}
		} else {
			/* Destination type bigger - sign- or zero-extend */
			if (from->sign == TOK_KEY_UNSIGNED) {
				/*
				 * zero-extend - nothing to do! Unless, of
				 * course, we're converting to 64bit values.
				 * Because 32bit operations immediately
				 * sign-extend to the 64bit register
				 * portion, it is then necessary to AND with
				 * a 32bit mask
				 */
				if (IS_LLONG(to->code) || IS_LONG(to->code)) {
					icode_make_mips_make_32bit_mask(tmpgpr, il);
					ii = icode_make_and(ret, NULL);
					append_icode_list(il, ii);
				}
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

	vreg_set_new_type(ret, orig_to);
	vreg_map_preg(ret, ret->pregs[0]);
	if (ret->type->code == TY_BOOL && ret->type->tlist == NULL) {
		boolify_result(ret, il);
	} else if (ret->type->code == TY_LDOUBLE && ret->type->tlist == NULL) {
		ret->is_multi_reg_obj = 2;
		vreg_map_preg2(ret, ret->pregs[1]);
	}

	return ret;
}

static void
icode_make_structreloc(struct copystruct *cs, struct icode_list *il) {
	relocate_struct_regs(cs, &mips_gprs[4], &mips_gprs[5], &mips_gprs[6],
		il);
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
		do_print_gpr(&mips_gprs[i]);
		if (((i+1) % 3) == 0) {
			putchar('\n');
		}	
	}
}

static int
is_multi_reg_obj(struct type *t) {
	if (t->code == TY_LDOUBLE && t->tlist == NULL) {
		/*
		 * 07/14/09: Note that this isn't true for o32 if we
		 * ever do support that (then we also have to disable
		 * long double emulation settings)
		 */
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

static int
have_immediate_op(struct type *ty, int op) {
	(void) ty; (void) op;
	if (Oflag == -1) { /* XXX */
		return 0;
	}
	return 0;
}	

struct backend mips_backend = {
	ARCH_MIPS,
	0, /* ABI */
	0, /* multi_gpr_object */
	8, /* structure alignment (set by init()) */
	1, /* need pic initialization? */
	1, /* emulate long double?  As of 07/15/09: YES! */
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
	NULL, /* prepare_load_addrlabel */
	icode_make_cast,
	icode_make_structreloc,
	generic_icode_initialize_pic, 
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

