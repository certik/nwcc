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
 * x86 backend
 * (XXX much of this stuff can probably be adapted to different
 * architectures)
 */
#include "amd64_gen.h"
#include "backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
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
#include "cc_main.h"
#include "stack.h"
#include "reg.h"
#include "subexpr.h"
#include "expr.h"
/* #include "x86_emit_gas.h" */
#include "inlineasm.h"
#include "x86_emit_nasm.h"
#include "x86_emit_gas.h"
#include "x86_gen.h"
#include "amd64_emit_yasm.h"
#include "amd64_emit_gas.h"
#include "cc1_main.h"
#include "n_libc.h"



static FILE			*out;
static struct scope		*tunit;
static int			use_nasm = 1; /* XXX */

static int			rbx_saved;
struct vreg			csave_rbx;
struct emitter_amd64		*emit_amd64;

int				amd64_need_negmask;
int				amd64_need_ulong_float_mask;


#define N_GPRS	6	
#define N_ARGREGS 6 

struct reg		*amd64_argregs[] = {
	/* rdi, rsi, rdx, rcx, r8, r9 */
	&amd64_x86_gprs[5], &amd64_x86_gprs[4],
	&amd64_x86_gprs[3], &amd64_x86_gprs[2],
	&amd64_gprs[8], &amd64_gprs[9]
};

struct reg		amd64_x86_gprs[7];
struct reg		amd64_gprs[16];
struct reg		amd64_gprs_32bit[16];
struct reg		amd64_gprs_16bit[16];
struct reg		amd64_gprs_8bit[16];
struct reg		amd64_sil;
struct reg		amd64_dil;

static int		callee_save_map[] = {
	0, 0, 0, 0, /* r8 - r11 */
	1, 1, 1, 1 /* r12 - r15 */
};		


static void
init_regs(void) {
	static struct reg	nullreg;
	int			i, j;
	static const struct {
		struct reg	*regs;
		char		*names[9];
	} rps[] = {
		{ amd64_x86_gprs,
			{"rax","rbx","rcx","rdx","rsi","rdi",0,0,0}},
		{ NULL, {0,0,0,0,0,0,0,0,0} }
	};
	
	for (i = 0; rps[i].regs != NULL; ++i) {
		nullreg.type = REG_GPR;
		nullreg.allocatable = 1;
		for (j = 0; rps[i].names[j] != NULL; ++j) {
			rps[i].regs[j] = nullreg;
			rps[i].regs[j].composed_of =
				n_xmalloc(2 * sizeof(struct reg *));
			rps[i].regs[j].composed_of[0] = &x86_gprs[j];
			rps[i].regs[j].composed_of[1] = NULL;
			rps[i].regs[j].size = 8;
			rps[i].regs[j].name = rps[i].names[j];
		}
	}
	
	amd64_sil.size = 1;
	amd64_sil.name = "sil";
	amd64_sil.type = REG_GPR;
	amd64_sil.allocatable = 1;
	x86_gprs[4].composed_of[0]->composed_of =
		n_xmalloc(2 * sizeof(struct reg *));
	x86_gprs[4].composed_of[0]->composed_of[0] = &amd64_sil;
	x86_gprs[4].composed_of[0]->composed_of[1] = NULL;
	
	amd64_dil.size = 1;
	amd64_dil.name = "dil";
	amd64_dil.type = REG_GPR;
	amd64_dil.allocatable = 1;
	x86_gprs[5].composed_of[0]->composed_of =
		n_xmalloc(2 * sizeof(struct reg *));
	x86_gprs[5].composed_of[0]->composed_of[0] = &amd64_dil;
	x86_gprs[5].composed_of[0]->composed_of[1] = NULL;
	
	for (i = 8; i < 16; ++i) {
		static char	*new_gpr_names[] = {
			"r8", "r9", "r10", "r11",
			"r12", "r13", "r14", "r15"
		};
		static char	*new_gpr_names_32[] = {
			"r8d", "r9d", "r10d", "r11d",
			"r12d", "r13d", "r14d", "r15d"
		};
		static char	*new_gpr_names_16[] = {
			"r8w", "r9w", "r10w", "r11w",
			"r12w", "r13w", "r14w", "r15w"
		};
		static char	*new_gpr_names_8[] = {
			"r8b", "r9b", "r10b", "r11b",
			"r12b", "r13b", "r14b", "r15b"
		};
		amd64_gprs[i].name = new_gpr_names[i-8];
		amd64_gprs[i].size = 8;
		amd64_gprs[i].type = REG_GPR;
		amd64_gprs[i].allocatable = 1;
		amd64_gprs[i].composed_of = n_xmalloc(2 * sizeof(struct reg*));
		amd64_gprs[i].composed_of[0] = &amd64_gprs_32bit[i];
		amd64_gprs[i].composed_of[1] = NULL;

		amd64_gprs_32bit[i].name = new_gpr_names_32[i-8];
		amd64_gprs_32bit[i].size = 4;
		amd64_gprs_32bit[i].type = REG_GPR;
		amd64_gprs_32bit[i].allocatable = 1;
		amd64_gprs_32bit[i].composed_of
			= n_xmalloc(2 * sizeof(struct reg*));
		amd64_gprs_32bit[i].composed_of[0] = &amd64_gprs_16bit[i];
		amd64_gprs_32bit[i].composed_of[1] = NULL;
		
		amd64_gprs_16bit[i].name = new_gpr_names_16[i-8];
		amd64_gprs_16bit[i].size = 2;
		amd64_gprs_16bit[i].type = REG_GPR;
		amd64_gprs_16bit[i].allocatable = 1;
		amd64_gprs_16bit[i].composed_of
			= n_xmalloc(2 * sizeof(struct reg*));
		amd64_gprs_16bit[i].composed_of[0] = &amd64_gprs_8bit[i];
		amd64_gprs_16bit[i].composed_of[1] = NULL;

		amd64_gprs_8bit[i].name = new_gpr_names_8[i-8];
		amd64_gprs_8bit[i].size = 1;
		amd64_gprs_8bit[i].type = REG_GPR;
		amd64_gprs_8bit[i].allocatable = 1;
		amd64_gprs_8bit[i].composed_of = NULL;
	}
	
	amd64_x86_gprs[6].name = NULL;
}

struct reg *
find_top_reg(struct reg *r) {
	int	i;

	for (i = 0; i < 6; ++i) {
		if (is_member_of_reg(&amd64_x86_gprs[i], r)) {
			return &amd64_x86_gprs[i];
		}
	}

	/*
	 * 10/30/07: Added this. I don't know yet why it is possible to
	 * get an r8-r15 sub register and to have to find the top, since
	 * the register allocator only uses rax - rdi for small items
	 * currently. It may be because conversion uses sub registers
	 */
	for (i = 8; i < 16; ++i) {
		if (is_member_of_reg(&amd64_gprs[i], r)) {
			return &amd64_gprs[i];
		}
	}
	fprintf(stderr, "Failed to find top preg for %s\n", r->name);
	abort();
	return NULL;
}


static void
do_invalidate(struct reg *r, struct icode_list *il, int save) {
	free_preg(r, il, 1, save);
}


/*
 * XXX this shouldn't be saving esi/edi/ebx and r12 - r15 when we're
 * invalidating because of a function call, because those regs are
 * callee-save
 */
static void
invalidate_gprs(struct icode_list *il, int saveregs, int for_fcall) {
	int	i;

	(void) for_fcall;
	for (i = 0; i < N_GPRS; ++i) {
		do_invalidate(&amd64_x86_gprs[i], il, saveregs);
	}
	for (i = 8; i < 16; ++i) {
		do_invalidate(&amd64_gprs[i], il, saveregs);
	}	

	/*
	 * 07/26/12: Dropped incomplete SSE usage check, could
	 * yield compiler crashes
	 */
	for (i = 0; i < 8; ++i) {
		do_invalidate(&x86_sse_regs[i], il, saveregs);
	}
}


static struct reg *
alloc_gpr(struct function *f, int size, struct icode_list *il,
struct reg *dontwipe, int line) {
	struct reg	*ret;

	if (size == 0) {
		/* 0 means GPR */
		size = 8;
	}

	if (size < 8) {
		ret = x86_backend.alloc_gpr(f, size, il, dontwipe, line);
	} else {
		/*
		 * Notice how only r8 - r15 are used for 64bit register
		 * allocations. This is because the x86 gpr extensions
		 * (rax, rbx, etc) are used for argument passing, so
		 * thrashing should be avoided. Note that emit_copystruct()
		 * will use those regs too, so it is absolutely critical	
		 * that struct pointers used by it are never stored in
		 * rdi/rsi/rdx
		 */
		ret = generic_alloc_gpr(f, size, il, dontwipe,
			&amd64_gprs[8], 8, callee_save_map, line);
	}

	return ret;
}


static struct reg *
alloc_fpr(struct function *f, int size, struct icode_list *il,
struct reg *dontwipe) {
	return alloc_sse_fpr(f, size, il, dontwipe);
}

static void
x86_free_preg(struct reg *r, struct icode_list *il) {
	x86_backend.free_preg(r, il);
}

/*
 * IMPORTANT: The x86 backend and the x86 emitter that corresponds to
 * this emitter (currently only yasm) also has to be initialized (init()),
 * because some code is shared between x86 and amd64
 */
static int 
init(FILE *fd, struct scope *s) {
	out = fd;
	tunit  = s;

	(void) use_nasm;
	if (asmflag == NULL
		|| strcmp(asmname, "gas") == 0
		|| strcmp(asmname, "as") == 0) {
		emit = &amd64_emit_gas;
		emit_x86 = &x86_emit_x86_gas;
		x86_backend.init(fd, s);
		x86_emit_gas.init(out, tunit);
		emit_amd64 = &emit_amd64_gas;
	} else if (strcmp(asmname, "yasm") == 0) {
		/* Default is yasm */
		emit = &amd64_emit_yasm;
		emit_x86 = &x86_emit_x86_nasm; /* XXX */
		x86_backend.init(fd, s);
		x86_emit_nasm.init(out, tunit);
		emit_amd64 = &emit_amd64_yasm;
	} else {
		(void) fprintf(stderr, "Unknown AMD64 assembler `%s'\n",
			asmflag);
		exit(EXIT_FAILURE);
	}

	init_regs();

	/* Setup code sharing between x86 and amd64 */
	amd64_backend.invalidate_except = x86_backend.invalidate_except;
	amd64_backend.name_to_reg = x86_backend.name_to_reg;
	amd64_backend.get_inlineasm_label = x86_backend.get_inlineasm_label;
	amd64_backend.asmvreg_to_reg = x86_backend.asmvreg_to_reg;
	amd64_backend.alloc_16_or_32bit_noesiedi =
		x86_backend.alloc_16_or_32bit_noesiedi;
	backend->emit = emit;
	return emit->init(out, tunit);
}

static int
get_ptr_size(void) {
	return 8;
}	

static struct type *
get_size_t(void) {
	return make_basic_type(TY_ULONG);
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
		return 4;
	case TY_LONG:
	case TY_ULONG:
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
		return 8; /* XXX contradicts abi */

	case TY_LDOUBLE:
		return /*10*/12;
	default:
	printf("err sizeof cannot cope w/ it, wuz %d\n", type); 
	abort();
		return 1; /* XXX */
	}
}

static struct vreg		saved_gprs[4]; /* r12 - r15 */
static struct stack_block	*saved_gprs_sb[4];

static void
do_ret(struct function *f, struct icode_instr *ip) {
	int	i;

	if (f->alloca_head != NULL) {
		struct stack_block	*sb;
		static struct vreg	rvr;

		rvr.stack_addr = f->alloca_regs;
		rvr.size = 8;
		backend_vreg_map_preg(&rvr, &amd64_x86_gprs[0]);
		emit->store(&rvr, &rvr);

		for (sb = f->alloca_head; sb != NULL; sb = sb->next) {
			emit->dealloca(sb, NULL);
		}

		emit->load(&amd64_x86_gprs[0], &rvr);
		backend_vreg_unmap_preg(&amd64_x86_gprs[0]);
	}
	if (f->vla_head != NULL) {
		struct stack_block	*sb;
		static struct vreg	rvr;

		rvr.stack_addr = f->alloca_regs;
		rvr.size = 8;
		backend_vreg_map_preg(&rvr, &amd64_x86_gprs[0]);
		emit->store(&rvr, &rvr);

		for (sb = f->vla_head; sb != NULL; sb = sb->next) {
			emit->dealloc_vla(sb, NULL);
		}

		emit->load(&amd64_x86_gprs[0], &rvr);
		backend_vreg_unmap_preg(&amd64_x86_gprs[0]);
	}
	if (f->callee_save_used & CSAVE_EBX) {
		emit->load(&amd64_x86_gprs[1], &csave_rbx);
	}
	for (i = 12; i < 16; ++i) {
		if (saved_gprs[i-12].stack_addr != NULL) {
			emit->load(&amd64_gprs[i], &saved_gprs[i-12]);
		}
	}

	if (saved_ret_addr) {
		emit->check_ret_addr(f, saved_ret_addr);
	}
	emit->freestack(f, NULL);
	emit->ret(ip);
}

static struct reg *
get_abi_reg(int index, struct type *ty) {
	if (index == 0
		&& (is_integral_type(ty)	
			|| ty->tlist != NULL)) {
		int	size = backend->get_sizeof_type(ty, NULL);
		if (size == 8) {
			return amd64_argregs[0];
		} else if (size == 4) {
			return amd64_argregs[0]->composed_of[0];
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
	if (is_integral_type(ty) || ty->tlist != NULL) {
		return &amd64_x86_gprs[0];
	} else {
		unimpl();
	}
	/* NOTREACHED */
	return NULL;
}

static void
map_parameters(struct function *f, struct ty_func *proto) {
	struct sym_entry	*se = proto->scope->slist;
	struct stack_block	*sb;
	int			i;
	long			offset = 16; /* rbp */
	int			gprs_used = 0;
	int			fprs_used = 0;
	struct reg		*curreg;
	int			stack_bytes_used = 0;

	if (f->fty->variadic) {
		/*
		 * Same story as usual - allocate space for argument
		 * registers; those are then followed by any possibly
		 * stack-passed variadic arguments.
		 * 6 arg regs * 8 = 48 bytes
		 * XXX floating point!! mmm..sse registers.mmm
		 */
		f->fty->lastarg = alloc_decl();

		/* Allocate 48 bytes for gprs, followed by 64 for fprs */
		f->fty->lastarg->stack_addr = stack_malloc(f, /*48*/112);
	}

	if (f->proto->dtype->tlist->next == NULL
		&& (f->proto->dtype->code == TY_STRUCT
		|| f->proto->dtype->code == TY_UNION)) {
		/*
		 * Function returns struct/union - accomodate for
		 * hidden pointer (passed as first argument)
		 * XXX duplicates mips code
		 */
		struct vreg	*hp;
		hp = vreg_alloc(NULL,NULL,NULL,NULL);
		hp->size = 8;
		hp->var_backed = alloc_decl();
		hp->var_backed->dtype =
			n_xmemdup(f->proto->dtype, sizeof(struct type));
		hp->var_backed->dtype->tlist = alloc_type_node();
		hp->var_backed->dtype->tlist->type = TN_POINTER_TO; 
		hp->var_backed->stack_addr = stack_malloc(f, 8);
		f->hidden_pointer = hp;
		++gprs_used;
	}	

	for (i = 0; i < proto->nargs; ++i, se = se->next) {
		size_t		size;
		long		last_offset = offset;

		size = backend->get_sizeof_type(se->dec->dtype, NULL);
		if (is_integral_type(se->dec->dtype)
			|| se->dec->dtype->tlist) {
			if (gprs_used < N_ARGREGS) {
				/* passed in register */
				curreg = amd64_argregs[gprs_used++];
				sb = stack_malloc(f, size);
				se->dec->stack_addr = sb;
				if (size == 4) {
					curreg = curreg->composed_of[0];
				} else if (size == 2) {
					curreg = curreg->composed_of[0]
						->composed_of[0];
				} else if (size == 1) {
					if (curreg->composed_of[0]
						->composed_of[0]
						->composed_of[1]) {
						curreg = curreg->composed_of[0]
							->composed_of[0]
							->composed_of[1];
					} else {
						curreg = curreg->composed_of[0]
							->composed_of[0]
							->composed_of[0];
					}
				}
				
				se->dec->stack_addr->from_reg = curreg;
			} else {
				/* passed on stack */
				/* XXX alignment */
/*assert(size == se->dec->vreg->size);*/


				se->dec->stack_addr =
					make_stack_block(offset,
						/*se->dec->vreg->size*/ size);
				se->dec->stack_addr->is_func_arg = 1;
				offset += size/*se->dec->vreg->size*/;
				while (offset % 8) {
					++offset;
				}
			}
		} else if (IS_FLOATING(se->dec->dtype->code)) {
			if (se->dec->dtype->code == TY_LDOUBLE) {
/* XXXX woah... what's the deal with size vs se->dec->vreg->size? */
/*assert(se->dec->vreg->size == size);*/
				if (offset % 16) {
					/* First align to 16-byte boundary */
					offset += 16 - (offset % 16);
				}
				sb = make_stack_block(offset, size);
				sb->is_func_arg = 1;
				offset += size;  /*se->dec->vreg->size;*/
				se->dec->stack_addr = sb;
				stack_bytes_used += /*16*/ offset - last_offset;
			} else {
				if (fprs_used < 8) {
					/* passed in register */
					curreg = &x86_sse_regs[fprs_used++];
					sb = stack_malloc(f, size);
					se->dec->stack_addr = sb;
					sb->from_reg = curreg;
				} else {
/*assert(size == se->dec->vreg->size);	*/
					/* passed on stack */
					/* XXX alignment */
					se->dec->stack_addr =
						make_stack_block(offset,
							/*se->dec->vreg->size*/ size);
					se->dec->stack_addr->is_func_arg = 1;
					offset += size /*se->dec->vreg->size*/;
					if (offset % 8) {
						offset += 8 - (offset % 8);
					}
				}
			}
		} else if (se->dec->dtype->code == TY_STRUCT
			|| se->dec->dtype->code == TY_UNION) {
			if (1 /*size > 16  || has_unaligned_members() */) {
				/*
				 * 07/26/12: Align for struct first. This may
				 * require 8 bytes of padding if the struct
				 * contains long double
				 */
				int	align = backend->get_align_type(se->dec->dtype);
				if (offset % align) {
					offset += align - (offset % align);
				}
				sb = make_stack_block(offset, size); 
				offset += size; /* was before makestackblock */
				if (offset % 8) {
					offset += 8 - (offset % 8);
				}
				sb->is_func_arg = 1;
				se->dec->stack_addr = sb;

#if 0
				if (size % 8) {
					stack_bytes_used += size + (8 - size % 8);
				} else {
					stack_bytes_used += size;
				}
				#endif
				stack_bytes_used += offset - last_offset;
			}
		} else {
			unimpl();
		}
	}
	if (f->fty->variadic) {
		/* Patch varargs block to real address */
		struct stack_block	*save_area;

		save_area = f->fty->lastarg->stack_addr;
		if (gprs_used == 6) {
			/* All variadic stuff passed on stack */
			f->fty->lastarg->stack_addr =
				make_stack_block(offset, 0);
			f->fty->lastarg->stack_addr->is_func_arg = 1;
		} else {
			f->fty->lastarg->stack_addr->from_reg =
				(void *)&amd64_argregs[gprs_used]; /* XXX */
		}
		if (f->patchme) {
			struct amd64_va_patches	*p = f->patchme;
			int			n;

			/*
			 * 08/07/08: Use a loop because there may be
			 * multiple items to be patched! (Multiple
			 * va_start() calls in the function)
			 */
			for (; p != NULL; p = p->next) {
				if (gprs_used == 6) {
					n = 48;
				} else {
					n = (&amd64_argregs[gprs_used] -
						amd64_argregs) * 8;
				}
				*p->gp_offset = n;
				if (fprs_used == 8) {
					n = 64+48;
				} else {
					n = (&x86_sse_regs[fprs_used] -
						x86_sse_regs) * 8;
					n += 48;
				}

				*p->reg_save_area = *save_area;
				if (gprs_used == 6) {
					/*
					 * The last argument is definitely passed
					 * on the stack so we can use that as
					 * base address
					 */
					*p->overflow_arg_area =
						*f->fty->lastarg->stack_addr;
				} else {
					/*
					 * 07/25/12: The stack area begins at [rbp + 16],
					 * but nwcc passes long double and struct
					 * arguments on the stack as well, such
					 * that we may have to advance the varargs
					 * start offset. Example:
					 *
					 * void foo(int x, struct foo f, char *fmt, ...);
					 *
					 * ... fmt and x are passed in registers, as
					 * are the first couple of varargs arguments,
					 * but since "f" is passed on the stack the
					 * last varargs arguments begin at
					 * [rbp + 16 + sizeof f] (with suitable
					 * alignment)
					 * Traditionally we always assumed excess
					 * args at rbp+16
					 */
					int	offset = 16 + stack_bytes_used;
					*p->overflow_arg_area =
						*make_stack_block(offset, 0);
					p->overflow_arg_area->is_func_arg = 1;
				}
			}
#if 0
printf("gp offset = %d\n", n);
printf("reg save area = %d\n", p->reg_save_area->offset);
printf("overflow area = %d\n", p->overflow_arg_area->offset);
#endif
		}
	}
}

void	store_preg_to_var(struct decl *, size_t, struct reg *);

static int
gen_function(struct function *f) {
	struct ty_func		*proto;
	struct scope		*scope;
	struct icode_instr	*lastret = NULL;
	struct stack_block	*sb;
	struct sym_entry	*se;
	size_t			size;
	size_t			alloca_bytes = 0;
	size_t			vla_bytes = 0;
	int			i;
	unsigned		mask;

	emit->setsection(SECTION_TEXT);
	proto = f->proto->dtype->tlist->tfunc;

	emit->func_header(f);
	emit->label(f->proto->dtype->name, 1);
	emit->intro(f);

	map_parameters(f, proto);

	/* Make local variables */
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
			struct decl	*alignfor;

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

			alignfor = get_next_auto_decl_in_scope(scope, i);
			if (alignfor != NULL) {
				align = calc_align_bytes(f->total_allocated,
					dec[i]->dtype,
					alignfor->dtype, 0);
			} else {
				align = 0;
			}	

			size = backend->
				get_sizeof_decl(dec[i], NULL);
			sb = stack_malloc(f, size+align);
			sb->nbytes = size;
			dec[i]->stack_addr = sb;
		}
	}
	stack_align(f, 8);

	/*
	 * Allocate storage for saving callee-saved registers (ebx/esi/edi)
	 * (but defer saving them until esp has been updated)
	 */
	f->total_allocated += 8;
	if (f->callee_save_used & CSAVE_EBX) {
		rbx_saved = 1;
		csave_rbx.stack_addr
			= make_stack_block(f->total_allocated, 8);
	}	

	for (i = 12, mask = 1 << 11; i < 16; ++i, mask <<= 1) {
		if (f->callee_save_used & mask) {
			if (saved_gprs_sb[i-12] == NULL) {
				saved_gprs_sb[i-12] = make_stack_block(0, 8);
			}	
			f->total_allocated += 8;
			saved_gprs[i-12].stack_addr = saved_gprs_sb[i-12];
			saved_gprs[i-12].size = 8;
			saved_gprs[i-12].stack_addr->offset =
				f->total_allocated;
		} else {
			saved_gprs[i-12].stack_addr = NULL;
		}	
	}
	f->callee_save_offset = f->total_allocated;

	if (stackprotectflag) {
		f->total_allocated += 4;
		/*
		 * 08/03/11: The save_ret_addr stack block was cached here,
		 * which caused the (later introduced) zone allocator to 
		 * trash the "frame pointer" flag while resetting memory
		 */
		saved_ret_addr
			= make_stack_block(f->total_allocated, 4);
	}

	/* Allocate storage for temporarily saving GPRs & patch offsets */
	for (sb = f->regs_head; sb != NULL; sb = sb->next) {
		stack_align(f, sb->nbytes);
		f->total_allocated += sb->nbytes;
		sb->offset = f->total_allocated;
	}
	/*
	 * Allocate storage for saving alloca() pointers, and initialize
	 * it to zero
	 */
	stack_align(f, 8);
	for (sb = f->alloca_head; sb != NULL; sb = sb->next) {
		f->total_allocated += sb->nbytes;
		alloca_bytes += sb->nbytes;
		sb->offset = f->total_allocated;
	}

	/*
	 * Allocate storage for saving VLA data, and initialize
	 * it to zero
	 */
	for (sb = f->vla_head; sb != NULL; sb = sb->next) {
		f->total_allocated += sb->nbytes;
		vla_bytes += sb->nbytes;
		sb->offset = f->total_allocated;
	}
	if (f->alloca_head != NULL || f->vla_head != NULL) {
		/*
		 * Get stack for saving return value register (rax)
		 * before performing free() on alloca()ted blocks
		 */
		f->alloca_regs = make_stack_block(0, 8);
		f->total_allocated += 8;
		f->alloca_regs->offset = f->total_allocated;
	}

	if (f->total_allocated > 0) {
		stack_align(f, 16);
		emit->allocstack(f, f->total_allocated);
		if (f->callee_save_used & CSAVE_EBX) {
			backend_vreg_map_preg(&csave_rbx, &amd64_x86_gprs[1]);
			emit->store(&csave_rbx, &csave_rbx);
			backend_vreg_unmap_preg(&amd64_x86_gprs[1]);
			x86_gprs[1].used = 0;
			amd64_gprs[1].used = 0;
		}
		for (i = 0; i < 4; ++i) {
			if (saved_gprs[i].stack_addr != NULL) {
				backend_vreg_map_preg(&saved_gprs[i],
					&amd64_gprs[12+i]);
				emit->store(&saved_gprs[i], &saved_gprs[i]);
				backend_vreg_unmap_preg(
					&amd64_gprs[12+i]);
				amd64_gprs[12+i].used = 0;
			}
		}	
		if (f->hidden_pointer) {
			backend_vreg_map_preg(f->hidden_pointer, &amd64_x86_gprs[5]);
			emit->store(f->hidden_pointer, f->hidden_pointer);
			backend_vreg_unmap_preg(&amd64_x86_gprs[5]);
		}
		se = proto->scope->slist;
		for (i = 0; i < proto->nargs; ++i, se = se->next) {
			if (se->dec->stack_addr->from_reg != NULL) {
				static struct vreg	tempvr;

				tempvr.var_backed = se->dec;
				tempvr.size = backend->get_sizeof_type(
					se->dec->dtype, NULL);
				tempvr.type = se->dec->dtype;

				backend_vreg_map_preg(&tempvr,
					se->dec->stack_addr->from_reg);
				emit->store(&tempvr, &tempvr);
				backend_vreg_unmap_preg(
					se->dec->stack_addr->from_reg);	
			}
		}
		if (f->fty->variadic
			&& f->fty->lastarg->stack_addr->from_reg != NULL) {
			struct reg	**r;
			size_t		saved_offset =
				f->fty->lastarg->stack_addr->offset;

			r = (struct reg **)f->fty->
				lastarg->stack_addr->from_reg; /* XXX */
			f->fty->lastarg->stack_addr->offset -=
				(r - amd64_argregs) * 8;
			for (i = r - amd64_argregs; i < N_ARGREGS; ++i) {
				store_preg_to_var(f->fty->lastarg, 8,
					amd64_argregs[i]);
				f->fty->lastarg->stack_addr->offset -= 8;
			}

			/* XXX ... */
			for (i = 0; i < 8; ++i) {
				f->fty->lastarg->dtype =
					make_basic_type(TY_DOUBLE);
				store_preg_to_var(f->fty->lastarg, 8,
					&x86_sse_regs[i]);
				f->fty->lastarg->stack_addr->offset -= 8;
			}

			f->fty->lastarg->stack_addr->offset = saved_offset;
		}
	}
	if (stackprotectflag) {
		emit->save_ret_addr(f, saved_ret_addr);
	}
	if (curfunc->alloca_head != NULL) {
		emit->zerostack(curfunc->alloca_tail, alloca_bytes);
	}
	if (curfunc->vla_head != NULL) {
		emit->zerostack(curfunc->vla_tail, vla_bytes);
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
	if (gflag) {
		/* Print file names */
		emit->dwarf2_files();
	}
	if (emit->support_decls) {
		emit->support_decls();
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
	emit->support_buffers();
	if (emit->finish_program) {
		emit->finish_program();
	}
	x_fflush(out);
	return 0;
}

#else

static int
gen_program(void) {
	struct function		*func;

	if (gflag) {
		/* Print file names */
		emit->dwarf2_files();
	}

	if (emit->support_decls) {
		emit->support_decls();
	}
	if (emit->extern_decls) {
		emit->extern_decls();
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
	emit->strings(str_const);
	emit->fp_constants(float_const);
	emit->support_buffers();
	emit->empty();

	if (emit->struct_defs) {
		emit->struct_defs();
	}

	emit->setsection(SECTION_TEXT);

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


/*
 * 10/30/07: This stuff was quite wrong because it did
 * not align correctly and did not count the long double
 * size properly (it may still not be right, but seems
 * better now)
 */
static void
pass_ldouble_stack(
	struct vreg *vr,
	unsigned long *allpushed,
	struct icode_list *il) {
	struct vreg	*dest;
	
	/* We will use at least 16 bytes to pass the long double itself */
	*allpushed += 16;

	dest = vreg_alloc(NULL, NULL, NULL, vr->type);
	dest->stack_addr = make_stack_block(vr->addr_offset, 16);
	dest->stack_addr->use_frame_pointer = 0;

	vreg_faultin_x87(NULL, NULL, vr, il, 0);
	vreg_map_preg(dest, vr->pregs[0]);
	icode_make_store(curfunc, dest, dest, il);
}

static unsigned long
pass_args_stack(struct vreg **vrs, int nvrs,
		unsigned long preceding_allpushed,
		unsigned long precalc,
		struct icode_list *il) {
	int		j;
	/*
	 * 07/26/12: The stack usage calculation already allocated 8 bytes of
	 * storage if necessary in order to ensure 16-byte-alignment for the
	 * callee. We failed to take that alignment into account by assuming
	 * we're starting at 0, thereby messing up alignment decisions for
	 * long double
	 */
	unsigned long	allpushed = preceding_allpushed;  /*0;*/
	int		ignore_integral = 0;
	int		ignore_floating = 0;

	/*
	 * 07/26/12: A bunch of clutter and highly dubious decisions have
	 * been removed. We now take the results of offset and alignment
	 * calculations that are performed prior to calling this function
	 * rather than duplication them here (error-prone)
	 */

	/*
	 * 07/26/12: The argument placement is done from right to left because
	 * we allocate storage as we go, with the stack growing "downward" and
	 * ending up to the leftmost argument.
	 * Because of this, the code used to handle padding improperly. Given
	 * for example a long double argument followed by a double, a traversal
	 * from left to right in the stack size calculation loop prior to
	 * this function will decide, correcly:
	 *
	 *                stack is 16-byte aligned, no changes necessary
	 * slot 1-2       place long double   (leftmost arg)
	 * slot 3         place double
	 * slot 4         final padding
	 *
	 * This right to left iteration here incorrectly did it like this
	 * instead:
	 *
	 * slot 4     place double    (alignment is uninteresting, will be 8 at least)
	 * slot 3     stack is not 16-byte aligned, allocate 8 bytes padding!!!
	 * slot 1-2   place long double
	 *
	 * This was incompatible with map_parameters(), which also decides
	 * the left to right way.
	 *
	 * So now, as a
	 *      XXX TEMPORARY UGLY KLUDGE XXX
	 * we keep passing over the arguments from right to left, but check
	 * whether the preceding would, if we were to store the current item
	 * at this location, require another 8 bytes of padding between itself
	 * and this item. If that's the case, we allocate 8 bytes of padding
	 * at the current slot instead and just move the current item 8 bytes
	 * ahead.
	 *
	 * A better solution would be to:
	 *
	 *    - Set all offset and alignment allocations in stone in the
	 * stack size calculation iteration that is performed prior to calling
	 * this function in order to determine alignment
	 *    - Allocate all storage in one block
	 *    - Use the precalcuated values here instead of reproducing them
	 * (which very error-prone anyway)
	 *    - Store items to their corresponding locations in that storage
	 * block
	 */


	if (precalc > 0) {
		/* Allocate all storage used for passing arguments (inc alignment) */
		icode_make_allocstack(NULL, precalc, il);
	}

	for (j = nvrs - 1; j >= 0; --j) {
		int		remaining = 0;
		struct vreg	*dest;
		size_t		tysize;
		size_t		align;
		int		is_struct = 0;
		int		is_ldouble = 0;

		if (vrs[j]->addr_offset == -1) {
			/*
			 * 07/26/12: This argument is not passed on the stack
			 */
			continue;
		}

		if ((IS_CHAR(vrs[j]->type->code)
			|| IS_SHORT(vrs[j]->type->code))
			&& vrs[j]->type->tlist == NULL) {
			vrs[j] = backend->
				icode_make_cast(vrs[j],
					make_basic_type(TY_INT), il);
		} else {
			if (vrs[j]->type->code == TY_LDOUBLE
				&& vrs[j]->type->tlist == NULL) {
				is_ldouble = 1;
			} else {
				if (!is_basic_agg_type(vrs[j]->type)) {
					vreg_faultin_x87(NULL, NULL, vrs[j], il, 0);
				} else {
					is_struct = 1;
				}
			}
		}


		if (is_ldouble) {
			/*
			 * 07/23/08: Do long double here as well instead of
			 * separately, since offsets were wrong
			 */
			pass_ldouble_stack(vrs[j], &allpushed, il);
		} else {
			dest = vreg_alloc(NULL, NULL, NULL, vrs[j]->type);
			dest->stack_addr = make_stack_block(vrs[j]->addr_offset, backend->get_sizeof_type(vrs[j]->type, NULL));
			dest->stack_addr->use_frame_pointer = 0;
			allpushed += dest->size;
		}

		if (is_struct) {
			/*
			 * 07/22/08: Invalidation was missing. There were
			 * no visible known bugs, but pass_struct_union()
			 * also called invalidate_gprs(), and it really
			 * should be done for copystruct
			 */
			backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
			vreg_faultin_ptr(vrs[j], il);

			/* 04/06/08: This was missing! */
			icode_make_copystruct(dest, vrs[j], il);
		} else if (is_ldouble) {
			; /* Already passed above */
		} else {
			/*
			 * 04/06/08: Note that the store frees the x87 reg, if used!
			 */
			vreg_map_preg(dest, vrs[j]->pregs[0]);
			icode_make_store(curfunc, dest, dest, il);
		}
	}

	return precalc;
}



static struct vreg *
icode_make_fcall(struct fcall_data *fcall, struct vreg **vrs, int nvrs,
struct icode_list *il)
{
	size_t			allpushed = 0;
	size_t			would_use_stack_bytes = 0;
	struct vreg		*tmpvr;
	struct vreg		*ret = NULL;
	struct type		*ty;
	struct icode_instr	*ii;
	struct type_node	*tn;
	struct vreg		*struct_lvalue;
	struct reg 		*fptr_reg = NULL;
	int			i;
	int			need_dap = 0;
	int			regs_used = 0;
	int			fp_regs_used = 0;
	int			ret_is_anon_struct = 0;
	int			saved_regs_used;
	int			saved_fp_regs_used;

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
		if (struct_lvalue == NULL || fcall->need_anon) {
			struct type_node	*tnsav;
			/*
			 * Result of function is not assigned so we need to
			 * allocate storage for the callee to store its
			 * result into
			 */

#if 1 /* XXX: This should go, use rettype! */ 
			tnsav = ty->tlist;
			ty->tlist = NULL;
#endif
			/*
			 * 08/05/08: Don't allocate anonymous struct return
			 * storage right here, but when creating the stack
			 * frame. This has already been done on MIPS, PPC
			 * and SPARC, but not on x86/AMD64. The reason is
			 * that it broke something that is long forgotten 
			 * now. So we'll re-enable this and fix any bugs 
			 * that may come up.
			 *
			 * The reason I ran into this again is that if we
			 * don't allocate the struct on the stack frame,
			 * then in
			 *
			 *     struct foo otherfunc() { return ...}
			 *     struct foo func() { return otherfunc(); }
			 *
			 * ... the anonymous storage is reclaimed before
			 * it can be copied as a return value, hence
			 * trashing it
			 */
			struct_lvalue = vreg_stack_alloc(ty, il, 1 /*0*/, NULL);

#if 1 /* XXX: This should go, use rettype! */ 
			ty->tlist = tnsav;
#endif
			/*
			 * 08/05/08: Don't add to allpushed since struct is
			 * created on frame
			 */
			/* allpushed += struct_lvalue->size;*/
			ret_is_anon_struct = 1;
		}	

		/* Hidden pointer is passed in first GPR! */
#if 0
		ii = icode_make_addrof(NULL, struct_lvalue, il);
		append_icode_list(il, ii);
#endif
		{
			struct reg	*r;
			/*ii*/ r = make_addrof_structret(struct_lvalue, il);

			free_preg(amd64_argregs[0], il, 1, 1);
			icode_make_copyreg(amd64_argregs[0], r /*ii->dat*/, NULL, NULL, il);
			++regs_used;
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


	/*
	 * 07/24/08: Now we make three passes over all arguments; The first
	 * part determines which integral and non-long-double arguments need
	 * to be passed on the stack (struct-by-value and long double always
	 * go there), the second pass performs the passing of the stack
	 * arguments, and the third pass passes all register arguments.
	 *
	 * By doing stack arguments first, we can minimize register saving
	 * problems (since struct-by-value may need to call memcpy(), which
	 * invalidates most GPRs)
	 */
	saved_regs_used = regs_used;
	saved_fp_regs_used = fp_regs_used;

	/*
	 * First determine the amount of stack usage
	 */
	for (i = 0; i < nvrs; ++i) {
		/* First mark the argument as not being passed on stack (may change later) */
		vrs[i]->addr_offset = -1;

		if (vrs[i]->type->tlist != NULL
			|| is_integral_type(vrs[i]->type)) {
			if (regs_used < N_ARGREGS) {
				++regs_used;
			} else {
				vrs[i]->addr_offset = would_use_stack_bytes;

				/*
				 * An integral or scalar type is always
				 * rounded up to 8 bytes if necessary
				 */
				would_use_stack_bytes += 8;
			}
		} else if (IS_FLOATING(vrs[i]->type->code)) {
			if (vrs[i]->type->code == TY_LDOUBLE) {
				/*
				 * long double is always passed on stack and
				 * takes up two quad-word argument slots
				 * 07/26/12: It might also require a slot of
				 * padding in order to ensure 16-byte
				 * alignment
				 */
				if (would_use_stack_bytes % 16) {
					would_use_stack_bytes += 8;
				}
				vrs[i]->addr_offset = would_use_stack_bytes;
				would_use_stack_bytes += 16;
			} else {
				/* float or double */
				if (fp_regs_used < 8) {
					++fp_regs_used;
				} else {
					vrs[i]->addr_offset = would_use_stack_bytes;
					/*
					 * A floating point type is always
					 * padded to 8 bytes if necessary
					 */
					would_use_stack_bytes += 8;
				}
			}
		} else if ((vrs[i]->type->code == TY_STRUCT
			|| vrs[i]->type->code == TY_UNION)
			&& vrs[i]->type->tlist == NULL) {
			int	size = backend->get_sizeof_type(vrs[i]->type, NULL);
			int	align = backend->get_align_type(vrs[i]->type);

			if (size % 8) {
				size += 8 - size % 8;
			}
			

			/*
			 * 07/26/12: Account for possibility of 16-byte alignment
			 * (long double members)
			 */
			if (would_use_stack_bytes % align) {
				would_use_stack_bytes += 8;
			}
			vrs[i]->addr_offset = would_use_stack_bytes;
			would_use_stack_bytes += size;
		}
	}

	/*
	 * Reset register counters (we have to use the saved vars since the
	 * values may not have started out as 0, e.g. if the function returns
	 * a struct, regs_used begins counting at 1)
	 */
	regs_used = saved_regs_used;
	fp_regs_used = saved_fp_regs_used;

	/*
	 * 07/27/08: As required by the ABI, ensure that the stack ends
	 * up being 16-byte-aligned eventually
	 */
	if (would_use_stack_bytes % 16) {
		size_t	align = 16 - would_use_stack_bytes % 16;

		allpushed += align;
		would_use_stack_bytes += align;
	}


	/*
	 * 07/23/08: Pass all struct args in one go here!
	 */
	allpushed = pass_args_stack(vrs, /*i*/ nvrs, allpushed, would_use_stack_bytes, il);

	for (i = 0; i < nvrs; ++i) {
		struct reg		*curreg;

		if (fcall->functype->variadic
			&& i >= fcall->functype->nargs) {
			need_dap = 1;
		}

		if (vrs[i]->type->tlist != NULL
			|| is_integral_type(vrs[i]->type)) {
			if (regs_used < N_ARGREGS) {
				curreg = amd64_argregs[regs_used];
			} else {
				curreg = NULL;
			}

			/*
			 * 07/23/08: Don't fault-in if we pass on the stack
			 * later
			 */
			if (curreg != NULL) {
				if ((IS_CHAR(vrs[i]->type->code)
					|| IS_SHORT(vrs[i]->type->code))
					&& vrs[i]->type->tlist == NULL) {
					vrs[i] = backend->
						icode_make_cast(vrs[i],
								make_basic_type(TY_INT), il);
				} else {
					vreg_faultin(NULL, NULL, vrs[i], il, 0);
				}
			}
			
			if (curreg != NULL) {
				struct reg	*topcurreg = curreg;

				if (curreg->size > vrs[i]->size) {
					if (vrs[i]->type != NULL
						&& vrs[i]->type->tlist != NULL
						&& vrs[i]->type->tlist->type == TN_VARARRAY_OF) {
						/*
						 * 02/23/09: The vreg size was 0 because
						 * we are passing a VLA - don't cut off
					 	 * the upper word! XXX Note that the real
						 * question is why we are not doing VLA
						 * array type to pointer decay when passing
						 * it to a function - maybe that would be
						 * the correct fix in expr_to_icode()?
						 */
						;
					} else {
						curreg = curreg->composed_of[0];
					}
				}
				if (vrs[i]->pregs[0] != curreg) {
					free_preg(topcurreg, il, 1, 1);
					icode_make_copyreg(curreg,
						vrs[i]->pregs[0],
						vrs[i]->type,
						vrs[i]->type, il);
				}
				reg_set_unallocatable(curreg);
				amd64_argregs[regs_used]->used = 0;
				++regs_used;
			} else {
				/* Pass remaining args on stack */
				/*
				 * 07/23/08: Don't pass now, and don't break,
				 * since there may be remaining FP args which
				 * can go into registers! Do all stack args
				 * in one go later
				 */
			}
		} else if (IS_FLOATING(vrs[i]->type->code)) {
			if (vrs[i]->type->code == TY_LDOUBLE) {
				/* long double is always passed on stack */
				;
			} else {
				/* float or double */
				struct reg	*curfpreg;

				if (vrs[i]->type->code == TY_FLOAT
					&& need_dap) {
					struct type	*ty =
						make_basic_type(TY_DOUBLE);
					vrs[i] = backend->icode_make_cast(
						vrs[i], ty, il);
				}

				if (fp_regs_used < 8) {
					curfpreg = &x86_sse_regs
						[fp_regs_used];
					if (vrs[i]->pregs[0] != curfpreg
						|| vrs[i]->pregs[0]->vreg
						!= vrs[i]) {
						free_preg(curfpreg,
							il, 1, 1);
					}
					vreg_faultin(curfpreg, NULL,
						vrs[i], il, 0);
					++fp_regs_used;
				} else {
					; /* Passed on stack */
				}
			}
		} else if (vrs[i]->type->code == TY_STRUCT
				|| vrs[i]->type->code == TY_UNION) {
			;
		} else {
			unimpl();
		}
	}

	/*
	 * In the x86 ABI, the caller is responsible for saving
	 * eax/ecx/edx (but not ebx, esi, edi), so that's what we 
	 * do here
	 */
	if (ty->tlist->type == TN_POINTER_TO) {
		/*
		 * Need to indirect thru function pointer.
		 * 07/10/15: This stuff used to come after the invalidate
		 * below. Thus it trashed an argument register
		 */
		vreg_faultin(NULL, NULL, tmpvr, il, 0);
		fptr_reg = tmpvr->pregs[0];
		tmpvr->pregs[0]->used = 0;
	}

	backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
	if (fcall->functype->variadic || need_dap) {
		/* rax = number of sse registers used for call */
		ii = icode_make_setreg(&amd64_x86_gprs[0], fp_regs_used);
		append_icode_list(il, ii);
		reg_set_unallocatable(&amd64_x86_gprs[0]);
		amd64_x86_gprs[0].used = 0;
	}


	if (ty->tlist->type == TN_POINTER_TO) {
		/* Need to indirect thru function pointer */
		ii = icode_make_call_indir(fptr_reg);
	} else {
		ii = icode_make_call(ty->name);
		if (IS_ASM_RENAMED(ty->flags)) {
			/*
			 * 02/21/09: Pass renaming as icode instr kludge
			 * to OSX AMD64 emitter
			 */
			ii->hints |= HINT_INSTR_RENAMED;
		}
	}	
	append_icode_list(il, ii);
	ii = icode_make_freestack(allpushed);
	append_icode_list(il, ii);

	for (i = 0; i < N_ARGREGS; ++i) {
		reg_set_allocatable(amd64_argregs[i]);
	}
	reg_set_allocatable(&amd64_x86_gprs[0]);

	ret = vreg_alloc(NULL, NULL, NULL, NULL);
	ret->type = ty;

	/* XXX man, this pointer stuff is painful and error prone */
	if ((ty->tlist->type == TN_POINTER_TO
		&& ty->tlist->next->next != NULL)
		|| (ty->tlist->type == TN_FUNCTION
		&& ty->tlist->next != NULL)) {
		/* Must be pointer */
		ret->pregs[0] = &amd64_x86_gprs[0];
	} else {
		if (IS_CHAR(ty->code)) {
			ret->pregs[0] = x86_gprs[0].composed_of[0]->
				composed_of[1];
		} else if (IS_SHORT(ty->code)) {
			ret->pregs[0] = x86_gprs[0].composed_of[0];
		} else if (IS_INT(ty->code)
			|| ty->code == TY_ENUM) { /* XXX */
			ret->pregs[0] = &x86_gprs[0];
		} else if (IS_LONG(ty->code) || IS_LLONG(ty->code)) {
			ret->pregs[0] = &amd64_x86_gprs[0];
		} else if (ty->code == TY_FLOAT
			|| ty->code == TY_DOUBLE) {
			ret->pregs[0] = &x86_sse_regs[0];
		} else if (ty->code == TY_LDOUBLE) {
			ret->pregs[0] = &x86_fprs[0];
		} else if (ty->code == TY_STRUCT
			|| ty->code == TY_UNION) {
			if (ret_is_anon_struct) {
				/*
				 * 08/16/07: Added this
				 */
				ret = struct_lvalue;
			}
			ret->struct_ret = 1;
		} else if (ty->code == TY_VOID) {
			; /* Nothing! */
		}
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

	if (ret->pregs[0] != NULL) {
		vreg_map_preg(ret, ret->pregs[0]);
	}	
	
	if (is_x87_trash(ret)) {
		/*
		 * Don't keep stuff in x87 registers, ever!!
		 */
		free_preg(ret->pregs[0], il, 1, 1);
	}
	return ret;
}

static int
icode_make_return(struct vreg *vr, struct icode_list *il) {
	struct icode_instr	*ii;
	struct type		*rtype = curfunc->rettype;  /*proto->dtype;*/

	/* 06/17/08: Use rettype instead of (wrongly) changing function type! */
#if 0
	oldtn = rtype->tlist;
	rtype->tlist = rtype->tlist->next;
#endif

	if (vr != NULL) {
		if (IS_CHAR(rtype->code)
			|| IS_SHORT(rtype->code)
			|| IS_INT(rtype->code)
			|| IS_LONG(rtype->code)
			|| IS_LLONG(rtype->code)
			|| rtype->code == TY_ENUM /* 06/15/09: Was missing?!? */
			|| rtype->tlist != NULL) {
			struct reg	*r = &amd64_x86_gprs[0];
			if (r->size > vr->size) {
				r = get_smaller_reg(r, vr->size);
			}
			vreg_faultin(r, NULL, vr, il, 0);
		} else if (rtype->code == TY_FLOAT
			|| rtype->code == TY_DOUBLE) {
			/* Return in xmm0 */
			vreg_faultin(&x86_sse_regs[0], NULL, vr, il, 0);
		} else if (rtype->code == TY_LDOUBLE) {
			/* Return in st0 */
			vreg_faultin_x87(NULL, NULL, vr, il, 0);
		} else if (rtype->code == TY_STRUCT
			|| rtype->code == TY_UNION) {

			/* vr may come from pointer */
			vreg_faultin_ptr(vr, il);
			icode_make_copystruct(/*dest*/NULL, vr, il); 
		}
	}
	ii = icode_make_ret(vr);
	append_icode_list(il, ii);

#if 0
	rtype->tlist = oldtn;
#endif

	return 0;
}

/*
 * Deal with preparations necessary to make things work with the terrible
 * x86 design
 */
static void 
icode_prepare_op(
	struct vreg **dest0,
	struct vreg **src0,
	int op,
	struct icode_list *il) {

	x86_backend.icode_prepare_op(dest0, src0, op, il);
}



/*
 * Most of the time, instructions give meaning to data. This function
 * generates code required to convert virtual register ``src'' to type
 * ``to'' where necessary
 */
static struct vreg *
icode_make_cast(struct vreg *src, struct type *to, struct icode_list *il) {
	return x86_backend.icode_make_cast(src, to, il);
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
	
	for (i = 0; i < 6; ++i) {
		printf("\t");
		do_print_gpr(&amd64_x86_gprs[i]);
		printf("\t");
		do_print_gpr(&x86_gprs[i]);
		putchar('\t');
		do_print_gpr(x86_gprs[i].composed_of[0]);
		if (i < 4) {
			putchar('\t');
			do_print_gpr(x86_gprs[i].composed_of[0]->
				composed_of[0]);
			putchar('\t');
			do_print_gpr(x86_gprs[i].composed_of[0]->
				composed_of[1]);
		}	
		putchar('\n');
	}
	for (i = 8; i < 16; i += 4) {
		printf("\t");
		do_print_gpr(&amd64_gprs[i]);
		printf("\t");
		do_print_gpr(&amd64_gprs[i+1]);
		printf("\t");
		do_print_gpr(&amd64_gprs[i+2]);
		printf("\t");
		do_print_gpr(&amd64_gprs[i+3]);
	}
}

static int
is_multi_reg_obj(struct type *t) {
	(void) t;
	return 0;
}


struct backend amd64_backend = {
	ARCH_AMD64,
	0, /* ABI */
	0, /* multi_gpr_object */
	4, /* structure alignment */
	0, /* need pic initialization? */
	0, /* emulate long double? */
	0, /* relax alloc gpr order */
	0, /* max displacement */
	0, /* min displacement */
	x86_have_immediate_op,
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
	/*invalidate_except*/NULL,
	alloc_gpr,
	/*alloc_16_or_32bit_noesiedi*/NULL,
	alloc_fpr,
	x86_free_preg,
	icode_make_fcall,
	icode_make_return,
	NULL,
	icode_prepare_op,
	NULL, /* prepare_load_addrlabel */
	icode_make_cast,
	NULL, /* icode_make_structreloc */
	NULL, /* icode_initialize_pic */
	NULL, /* icode_complete_func */
	make_null_block,
	make_init_name,
	debug_print_gprs,
	/*name_to_reg XXX */ NULL,
	/*asmvreg_to_reg*/ NULL,
	/*get_inlineasm_label*/NULL,
	do_ret,
	get_abi_reg,
	get_abi_ret_reg,
	generic_same_representation
};

