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
 * x86 backend
 * (XXX much of this stuff can probably be adapted to different
 * architectures)
 */
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
#include "typemap.h"
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
#include "features.h"
#include "x87_nonsense.h"
/* #include "x86_emit_gas.h" */
#include "inlineasm.h"
#include "x86_emit_nasm.h"
#include "x86_emit_gas.h"
#include "amd64_gen.h"
#include "amd64_emit_gas.h"  /* XXX for SSE */
#include "cc1_main.h"
#include "n_libc.h"

static FILE			*out;
static struct scope		*tunit;
static int			use_nasm = 1; /* XXX */
struct emitter_x86		*emit_x86;

#if ! REMOVE_FLOATBUF
struct vreg			floatbuf;
#endif

struct vreg			x87cw_new;
struct vreg			x87cw_old;

static int			ebx_saved;
static int			esi_saved;
static int			edi_saved;
struct vreg			csave_ebx;
struct vreg			csave_esi;
struct vreg			csave_edi;
struct stack_block		*saved_ret_addr;


#define N_GPRS	6	

struct reg		x86_gprs[7];
static struct reg	x86_16bit_gprs[6];
static struct reg	x86_8bit_gprs[8];
static struct reg	x86_esp;
static struct reg	x86_ebp;
static struct reg	x86_esp_16bit;
static struct reg	x86_ebp_16bit;

struct reg		x86_fprs[8];

/* 02/09/08: Moved to x86 backend from AMD64 (for OSX) */
struct reg	x86_sse_regs[8];

int	sse_csave_map[] = {
	0, 0, 0, 0, 0, 0, 0, 0
};



static void
init_regs(void) {
	static struct reg	nullreg;
	int					i;
	static const struct {
		struct reg	*regs;
		char		*names[9];
	} rps[] = {
		{ x86_gprs,
			{"eax","ebx","ecx","edx","esi","edi",0,0,0}},
		{ x86_16bit_gprs,
			{"ax","bx","cx","dx","si","di",0,0,0 }},
		{ x86_8bit_gprs,
			{"ah","al","bh","bl","ch","cl","dh","dl",NULL}},
		{ x86_fprs,
			{ "st0", "st1", "st2", "st3", "st4", "st5",
			"st6", "st7", NULL}},	
		{ NULL, {0,0,0,0,0,0,0,0,0} }
	};
	
	for (i = 0; rps[i].regs != NULL; ++i) {
		int	j;
		int	size = i == 0? 4: i == 1? 2: 1;
		int	type;

		if (rps[i].regs == x86_fprs) {
			type = REG_FPR;
			/*
			 * size was for some reason set to 8, with a comment
			 * saying it should be 10, which is factually correct
			 * because those really are 10 bytes big.. but we use
			 * 12 bytes for alignment, and that seems to work
			 */
			size = 12;
		} else {
			type = REG_GPR;
		}	
		
		nullreg.type = type;
		nullreg.allocatable = 1;
		for (j = 0; rps[i].names[j] != NULL; ++j) {
			rps[i].regs[j] = nullreg;
			rps[i].regs[j].size = size;
			rps[i].regs[j].name = rps[i].names[j];
		}
	}
	
	x86_gprs[6].name = NULL;

	for (i = 0; i < 8; ++i) {
		static char     *names[] = {
			"xmm0", "xmm1", "xmm2", "xmm3",
			"xmm4", "xmm5", "xmm6", "xmm7",
		};
		x86_sse_regs[i].name = names[i];
		x86_sse_regs[i].type = REG_FPR;
		x86_sse_regs[i].size = 8; /* XXX */
		x86_sse_regs[i].allocatable = 1;
	}

}


static int
calc_total_refs(struct reg *r) {
	(void) r;
	return 0;
}

static void
do_invalidate(struct reg *r, struct icode_list *il, int save) {
#if FEAT_DEBUG_DUMP_BOGUS_STORES
	struct icode_instr	*tail = il? il->tail: NULL;
#endif	
	if (curfunc->pic_initialized
		&& r == &x86_gprs[1]) {
		/* ebx is used for PIC access */
		return;
	}

	free_preg(r, il, 1, save);
#if FEAT_DEBUG_DUMP_BOGUS_STORES
	if (backend_warn_inv && tail != NULL && tail != il->tail) {
		icode_make_debug(il, "previous save(s) may be unneeded");
	}	
#endif	
}
 
/*
 * XXX this shouldn't be saving esi/edi/ebx when we're invalidating
 * because of a function call
 */
static void
invalidate_gprs(struct icode_list *il, int saveregs, int for_fcall) {
	int	i;

	(void) for_fcall;
	for (i = 0; i < N_GPRS; ++i) {
		do_invalidate(&x86_gprs[i], il, saveregs);
	}

	/*
	 * 07/26/12: Dropped incomplete SSE usage check (could yield compiler
	 * crashes)
	 */
	for (i = 0; i < 8; ++i) {
		do_invalidate(&x86_sse_regs[i], il, saveregs);
	}
}

/*
 * AMD64 & x86
 */
static void
invalidate_except(struct icode_list *il, int save, int for_fcall, ...) {
	int			i;
	struct reg		*except[8];
	static struct reg	*gprset;
	struct reg		*arg;
	va_list			va;

	if (gprset == NULL) {
		if (backend->arch == ARCH_X86) {
			gprset = x86_gprs;
		} else {
			/* AMD64 */
			gprset = amd64_x86_gprs;
		}
	}

	va_start(va, for_fcall);
	for (i = 0; (arg = va_arg(va, struct reg *)) != NULL; ++i) {
		except[i] = arg;
	}
	va_end(va);
	except[i] = NULL;

	for (i = 0; i < N_GPRS; ++i) {
		int	j;

		for (j = 0; except[j] != NULL; ++j) {
			if (is_member_of_reg(&gprset[i], except[j])) {
				/*
				 * XXX perhaps we would want to save
				 * part of a GPR in some cases.
				 */
				break;
			}	
		}
		if (except[j] != NULL) {
			continue;
		}
		do_invalidate(&gprset[i], il, save);
	}

	if (backend->abi == ARCH_AMD64) {
		for (i = 1; i < 16; ++i) {
			int	j;

			for (j = 0; except[j] != NULL; ++j) {
				if (&amd64_gprs[i] == except[j]) {
					break;
				}
			}
			if (except[j] == NULL) {
				do_invalidate(&amd64_gprs[i], il, save);
			}	
		}
	}	
}	

static int is_noesiedi;


static struct reg *
alloc_16_or_32bit_reg(
	struct function *f, 
	int size, 
	struct icode_list *il,
	struct reg *dontwipe) {

	int			i;
	int			save = 0;
	int			least = INT_MAX;
	int			least_idx = -1;
	static int		last_alloc;
	struct reg		*ret = NULL;
	struct reg		*aset;
	struct reg		*topreg = NULL;
	int			old_relax = backend->relax_alloc_gpr_order;

	/*
	 * 05/31/09: Now we always relax the GPR order when allocating
	 * non-ESI/non-EDI registers! This means that we allow this call
	 * to allocate the same register as the last successful call.
	 * This is probably a necessity when generating PIC code, because
	 * that limits us to only 3 registers that are usable for 8bit
	 * and 16bit allocations (ebx is taken as PIC pointer, so only
	 * eax, ecx, edx are allowed).
	 *
	 * If we then have a construct such as
	 *
	 *    ptr->member |= ptr2->value;
	 *
	 * ... then that will easily cause two registers to become
	 * unallocatable to hold the pointers, and with PIC ebx is
	 * taken anyway, so there is only one potential register left
	 * which we don't want to filter through the allocation ordering
	 * constraint
	 *
	 * Relaxing the constraint may work if we perform a sequence of:
	 *
	 *     - allocating register N
	 *     - looking to allocate register M, but finding that it is
	 *       already loaded with our desired value, so it can be
	 *       skipped
	 *     - allocating register N 
	 *
	 * In this case, it will seem like we are allocating register N
	 * twice in a row, but there was effectively another allocation
	 * in between. If the first allocation is not needed anymore and
	 * we will work with M and second N, then it will work.
	 *
	 * It will generally if we really only have one register available
	 * but need two at once for an operation.
	 *
	 * XXX Can this happen? Are there any implicit register alloc
	 * ordering assumptions left?
	 */
	if (is_noesiedi) {
		backend->relax_alloc_gpr_order = 1;
	}

	if (backend->arch == ARCH_AMD64) {
		aset = amd64_x86_gprs;
	} else {
		aset = x86_gprs;
	}
	(void) f;
	for (i = 0; x86_gprs[i].name != NULL; ++i) {
		if (reg_unused( /*&x86_gprs[i]*/  &aset[i])
			&& reg_allocatable(/*&x86_gprs*/ &aset[i])) {
			ret = &x86_gprs[i];
			last_alloc = i;
			break;
		} else {
			int	total;

			if (!optimizing /* || !reg_allocatable(...)*/) {
				continue;
			}
			total = calc_total_refs(&x86_gprs[i]);
			if (total < least) {
				least = total;
				least_idx = i;
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

			do {
				if (cur == N_GPRS) cur = 0;
				if (cur == last_alloc) {
					/*
					 * Ensure two successive allocs always
					 * use different registers
					 */
					if (backend->relax_alloc_gpr_order
						&& iterations != 0) {
						/*
						 * 02/09/09: Lift the constraint
						 * that successive uses of the
						 * same GPR aren't allowed, but
						 * only do so in the second
						 * iteration (i.e. try other regs
						 * first and fall back if all
						 * fails)
						 */
						;
					} else {
						cur = (cur + 1) % N_GPRS;
					}
				}

				ret = &x86_gprs[cur /*++*/];
				topreg = &aset[cur++];
				/*
				 * 02/09/09: N_GPRS + 1 to allow for an extra
				 * iteration in case relax_alloc_gpr_order is
				 * set
				 */
				if (++iterations >= N_GPRS + 1) {
					/*
					 * Ouch, no register can be allocated.
					 * This will probably only ever happen
					 * with inline asm statements using too
					 * many registers .... HOPEFULLY!!
					 */
					if (is_noesiedi) {
						backend->relax_alloc_gpr_order = old_relax;
					}
					return NULL;
				}
			} while ((dontwipe != NULL && ret == dontwipe)
				|| !reg_allocatable(/*ret*/topreg));
			last_alloc = cur - 1;
		} else {
			int	idx;

unimpl(); /* XXX doesn;t work with amd64 */
			idx = least_idx == -1? 0: least_idx;
			if (idx == last_alloc) {
				idx = (idx + 1) % N_GPRS;
			}	
			ret = &x86_gprs[idx];
			last_alloc = idx;
		}
	}

	if (ret == &x86_gprs[1]) {
		f->callee_save_used |= CSAVE_EBX;
	} else if (ret == &x86_gprs[4]) {
		f->callee_save_used |= CSAVE_ESI;
	} else if (ret == &x86_gprs[5]) {
		f->callee_save_used |= CSAVE_EDI;
	}	
	
	if (save) {
		struct reg	*freeme = ret;

		/*
		 * IMPORTANT: It is assumed that an allocatable register
		 * has a vreg, hence no ret->vreg != NULL check here.
		 * Reusing a preg without a vreg is obviously a bug
		 * because without a vreg, it cannot be saved anywhere.
		 * See reg_set_unallocatable()/vreg_faultin_protected()
		 */
		if (backend->arch == ARCH_AMD64) {
			/*
			 * 05/20/11: This ALWAYS attempted to free the
			 * surrounding register, so if we're allocating
			 * eax, it always tried to free rax. Instead we
			 * have to check whether the outer one is
			 * actually used!
			 */
			freeme = topreg;
		}
		free_preg(freeme, il, 1, 1);
	}
	if (size == 2) {
		/* 16bit reg */
		ret = ret->composed_of[0];
		if (ret->composed_of) {
			ret->composed_of[0]->vreg = NULL;
			if (ret == x86_gprs[4].composed_of[0]
				|| ret == x86_gprs[5].composed_of[0]) {
				/*
				 * This means we're allocating si or di. It
				 * follows that there can only be one sub-
				 * register, namely sil or dil (on AMD64!)
				 * Hence composed_of[1] does not exist
				 */
				;
			} else {	
				ret->composed_of[1]->vreg = NULL;
			}
		}
	} else {
		/* 32bit */
		ret->composed_of[0]->vreg = NULL;
		if (ret->composed_of[0]->composed_of) {
			/* eax - edx */
			ret->composed_of[0]->composed_of[0]->vreg = NULL;
			if (ret->composed_of[0]->composed_of[1]) {
				ret->composed_of[0]->composed_of[1]->vreg
					= NULL;
			}
		}
	}	

	if (is_noesiedi) {
		backend->relax_alloc_gpr_order = old_relax;
	}

	ret->used = ret->allocatable = 1;
	return ret;
}

static struct reg *
alloc_16_or_32bit_noesiedi(struct function *f, size_t size,
struct icode_list *il, struct reg *dontwipe) {
	int		esi_allocatable /*= x86_gprs[4].allocatable*/;
	int		edi_allocatable /* = x86_gprs[5].allocatable */;
	struct reg	*ret;
	struct reg	*esi_reg;
	struct reg	*edi_reg;

	esi_reg = backend->arch == ARCH_AMD64? &amd64_x86_gprs[4]: &x86_gprs[4];
	edi_reg = backend->arch == ARCH_AMD64? &amd64_x86_gprs[5]: &x86_gprs[5];

	esi_allocatable = reg_allocatable(esi_reg);
	edi_allocatable = reg_allocatable(edi_reg);

	reg_set_unallocatable(esi_reg);
	reg_set_unallocatable(edi_reg);

	is_noesiedi = 1;
	ret = ALLOC_GPR(f, size, il, dontwipe);
	is_noesiedi = 0;

	if (esi_allocatable) {
		reg_set_allocatable(/*&x86_gprs[4]*/esi_reg);
	}	
	if (edi_allocatable) {
		reg_set_allocatable(/*&x86_gprs[5]*/edi_reg);
	}
	return ret;
}

static struct reg *
alloc_8bit_reg(struct function *f, struct icode_list *il,
struct reg *dontwipe) {
	int	i;
	int		least8 = INT_MAX;
	int		least32 = INT_MAX;
	int		total;
	struct reg	*ret = NULL;
	struct reg	*aset;

	if (backend->arch == ARCH_AMD64) {
		aset = amd64_x86_gprs;
	} else {
		aset = x86_gprs;
	}

	(void) dontwipe;

	for (i = 0; i < 4; ++i) {
		int		j;
		struct reg	**r16bit;

		if (!reg_allocatable(&aset[i])) {
			continue;
		}
		if (!aset[i].used
			&& !x86_gprs[i].used
			&& !((r16bit = x86_gprs[i].composed_of)[0])->used) {
			struct reg	**r8bit;

			r8bit = r16bit[0]->composed_of;

			/*
			 * Beware - mov ah, byte [r8] doesn't work,
			 * but does with al on amd64! So never use
			 * ah on amd64. Oh, and also, composed_of[0]
			 * of ax is actually ah, not al. I'm afraid
			 * of reversing this because I think it will
			 * break other stuff that depends on it
			 */
			if (backend->arch == ARCH_AMD64) {
				j = 1;
			} else {
				j = 0;
			}
			for (; j < 2; ++j) {
				if (!r8bit[j]->used) {
					ret = r8bit[j];
					break;
				} else {
					total = calc_total_refs(r8bit[j]);
					if (total < least8) {
						least8 = total;
					}
				}
			}
		} else {
			/* in use */
			total = calc_total_refs(/*&x86_gprs*/&aset[i]);
			if (total < least32) {
				least32 = total;
			}
		}
		if (ret != NULL) {
			break;
		}
	}

	if (ret == NULL) {
		ret = alloc_16_or_32bit_noesiedi(f, 2, il, NULL);

		if (ret == NULL) {
			return NULL;
		}

		ret->used = 0;
		ret->allocatable = 1;
		if (backend->arch == ARCH_AMD64) {
			ret = ret->composed_of[1];
		} else {
			ret = ret->composed_of[0];
		}
		ret->used = 1;
	}	
	return ret;
}


static struct reg *
alloc_gpr(struct function *f, int size, struct icode_list *il,
struct reg *dontwipe, int line) {
	struct reg	*ret;

	(void) line;
	if (f == NULL) abort(); /* using invalidate_gprs() now */

	if (size == 0) {
		/* 0 means GPR */
		size = 4;
	}

	if (backend->multi_gpr_object) {
		/* Previous gpr allocation request remains to be finished */
		ret = alloc_16_or_32bit_reg(f, 4, il, dontwipe);
		backend->multi_gpr_object = 0;
	} else if (size == 8) {
		/* long long ... ouch */
		ret = alloc_16_or_32bit_reg(f, 4, il, dontwipe);
		backend->multi_gpr_object = 1;
	} else if (size == 4 || size == 2) {
		ret = alloc_16_or_32bit_reg(f, size, il, dontwipe);
	} else if (size == 1) {
		ret = alloc_8bit_reg(f, il, dontwipe);
	} else {
		printf("REGISTER LOAD WITH BAD SIZE %d\n", size);
		abort();
	}

	if (ret == NULL) {
		debug_log_regstuff(ret, NULL, DEBUG_LOG_FAILEDALLOC);
#ifdef DEBUG6
		printf("(alloc size was %d)\n", size);
#endif
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

#if 0
static int	fpr_bos = 0; /* fpr bottom of stack */
#endif

struct reg *
alloc_sse_fpr(struct function *f, int size, struct icode_list *il,
struct reg *dontwipe) {

	/*
	 * 06/18/09: Is this size trickery for long double still needed?
	 */
        if (size == 10 || size == 12 || size == 16) {
		if (backend->arch == ARCH_AMD64) {
			return x86_backend.alloc_fpr(f, size, il, dontwipe);
		} else {
	                return backend->alloc_fpr(f, size, il, dontwipe);
		}
        } else {
                return generic_alloc_gpr(f, size, il, dontwipe,
                        x86_sse_regs, 8, sse_csave_map, 0);
        }
}



static struct reg *
alloc_fpr(struct function *f, int size, struct icode_list *il,
struct reg *dontwipe) {
	(void) f; (void) size; (void) il; (void) dontwipe;

	if (sysflag == OS_OSX && (size == 4 || size == 8)) {
		return alloc_sse_fpr(f, size, il, dontwipe);
	}
	x86_fprs[0].used = 1;
	return &x86_fprs[0];
#if 0
	(void) f; (void) size; (void) il; (void) dontwipe;

	if (fpr_bos == 7) {	
		(void) fprintf(stderr,
			"x87 register stack overflow\n");
		abort();
	}

	/*
	 * Allocated register is top of stack - relocate all allocated
	 * registers by one
	 */
	for (i = 6; i >= 0; --i) {
		
		if (x86_fprs[i].used) {
			vreg_map_preg(x86_fprs[i].vreg, &x86_fprs[i+1]);
		}
	}

	x86_fprs[0].used = 1;
	return &x86_fprs[0];
#endif
}

static void
x86_free_preg(struct reg *r, struct icode_list *il) {
	(void) r; (void) il;
	
	return ;
#if 0
	if (r->type == REG_FPR) {
		assert(r == &x86_fprs[0]);
	}
	r->vreg = NULL;
	r->used = 0;
#endif
#if 0
	if (r->type != REG_FPR || !STUPID_X87(r)) {
		return;
	}
	--fpr_bos; /* :-( */

#if 0
	if (r != &x86_fprs[0]) {
		/*
		 * XXX st0 will be popped if the result is assigned to
		 * something ... but this cannot be relied on when the
		 * result is not use; (void) f1 * f2;
		 * perhaps icode.c should generate an additional
		 * ffree if needed
		 */
		icode_make_x86_ffree(r, il);
	}	
#endif
	if (r != &x86_fprs[0]) {
		icode_make_x86_ffree(r, il);
	}
	r->vreg=  NULL;
	r->used = 0;
#endif
}	

static int 
init(FILE *fd, struct scope *s) {
	int	i;
	int	j;

	out = fd;
	tunit  = s;

	(void) use_nasm;

	if (sysflag == OS_OSX) {
		/*
		 * 02/09/09: Make AMD64 emitter available so that we
		 * can emit SSE instructions (required by OSX even on
		 * x86).
		 *
		 * XXX These instructions should be moved to the x86
		 * backend, like all other SSE things already have
		 * (GPR allocator, etc)
		 */
		emit_amd64 = &emit_amd64_gas;
		/*
		 * Also initialize FILE handle
		 */
		amd64_emit_gas.init(out, s);
	}

	/*
	 * Initialize registers and function pointer tables.
	 * It is important not to trash ``emit'' if this is
	 * called from AMD64 init()!!!
	 */
	if (asmflag == NULL) {
		/* Default is nasm */
		if (backend->arch != ARCH_AMD64) {
#if 0
			emit = &x86_emit_nasm;
#endif
			emit = &x86_emit_gas;
		}	
		emit_x86 = &x86_emit_x86_gas;
	} else if (strcmp(asmname, "nasm") == 0
		|| strcmp(asmname, "nwasm") == 0	
		|| strcmp(asmname, "yasm") == 0) {
		if (backend->arch != ARCH_AMD64) {
			emit = &x86_emit_nasm;
		}	
		emit_x86 = &x86_emit_x86_nasm;
	} else if (strcmp(asmname, "as") == 0
		|| strcmp(asmname, "gas") == 0) {
		if (backend->arch != ARCH_AMD64) {
			emit = &x86_emit_gas;
		}	
		emit_x86=  &x86_emit_x86_gas;
	} else {
		(void) fprintf(stderr, "Unknown x86 assembler `%s'\n",
			asmflag);
		exit(EXIT_FAILURE);
	}
	
#if 0 
	if (use_nasm) {
		emit = &x86_emit_nasm;
	} else {
		emit = &x86_emit_gas;
	}
	emit = &x86_emit_nasm;
	emit_x86 = &x86_emit_x86_nasm;
#endif

	init_regs();
	for (i = 0, j = 0; i < N_GPRS; ++i) {
		struct reg	*r16bit;

		x86_gprs[i].composed_of = n_xmalloc(2 * sizeof(struct reg *)); 
		r16bit = &x86_16bit_gprs[i];
		x86_gprs[i].composed_of[0] = r16bit;
		x86_gprs[i].composed_of[1] = NULL;
		if (i < 4) {
			r16bit->composed_of =
				n_xmalloc(3 * sizeof(struct reg *));
			r16bit->composed_of[0] = &x86_8bit_gprs[j++];
			r16bit->composed_of[1] = &x86_8bit_gprs[j++];
			r16bit->composed_of[2] = NULL;
		}	
	}

	x86_esp.type = REG_SP;
	x86_esp.size = 4;
	x86_esp.name = "esp";
	x86_esp.composed_of = n_xmalloc(2 * sizeof(struct reg *));
	*x86_esp.composed_of = &x86_esp_16bit;
	x86_esp.composed_of[1] = NULL;
	x86_esp_16bit.size = 2;
	x86_esp_16bit.name = "sp";
	x86_ebp.type = REG_BP;
	x86_ebp.size = 4;
	x86_ebp.name = "ebp";
	x86_ebp.composed_of = n_xmalloc(2 * sizeof(struct reg *)); 
	*x86_ebp.composed_of = &x86_ebp_16bit;
	x86_ebp.composed_of[1] = NULL;
	x86_ebp_16bit.size = 2;
	x86_ebp_16bit.name = "bp";

	if (backend->arch != ARCH_AMD64) {
		backend->emit = emit;
		return emit->init(out, tunit);
	}
	return 0;
}

static int
get_ptr_size(void) {
	return 4;
}	

static struct type *
get_size_t(void) {
	return make_basic_type(TY_UINT);
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
		if (sysflag == OS_OSX) {
			return 16;
		} else {
			if (backend->arch == ARCH_AMD64) {
				return /*10 XXX */10;
			} else {
				return 10;
			}
		}
	default:
	printf("err sizeof cannot cope w/ it, wuz %d\n", type); 
	abort();
		return 1; /* XXX */
	}
}


static void
do_ret(struct function *f, struct icode_instr *ip) {
	if (f->callee_save_used & CSAVE_EBX) {
		emit->load(&x86_gprs[1], &csave_ebx);
	}
	if (f->callee_save_used & CSAVE_ESI) {
		emit->load(&x86_gprs[4], &csave_esi);
	}
	if (f->callee_save_used & CSAVE_EDI) { 
		emit->load(&x86_gprs[5], &csave_edi);
	}
	if (saved_ret_addr) {
		emit->check_ret_addr(f, saved_ret_addr);
	}	
	if (f->alloca_head != NULL) {
		struct stack_block	*sb;
		static struct vreg	rvr;

		rvr.stack_addr = f->alloca_regs;
		rvr.size = 4;
		backend_vreg_map_preg(&rvr, &x86_gprs[0]);
		emit->store(&rvr, &rvr);
		backend_vreg_unmap_preg(&x86_gprs[0]);
		if (ip && ip->src_vreg && ip->src_vreg->is_multi_reg_obj) {
			rvr.stack_addr = f->alloca_regs->next;
			backend_vreg_map_preg(&rvr, &x86_gprs[3]);
			emit->store(&rvr, &rvr);
			backend_vreg_unmap_preg(&x86_gprs[3]);
		}

		for (sb = f->alloca_head; sb != NULL; sb = sb->next) {
			emit->dealloca(sb, NULL);
		}

		rvr.stack_addr = f->alloca_regs;
		backend_vreg_map_preg(&rvr, &x86_gprs[0]);
		emit->load(&x86_gprs[0], &rvr);
		backend_vreg_unmap_preg(&x86_gprs[0]);
		if (ip && ip->src_vreg && ip->src_vreg->is_multi_reg_obj) {
			rvr.stack_addr = f->alloca_regs->next;
			backend_vreg_map_preg(&rvr, &x86_gprs[3]);
			emit->load(&x86_gprs[3], &rvr);
			backend_vreg_unmap_preg(&x86_gprs[3]);
		}
	}
	if (f->vla_head != NULL) {
		struct stack_block	*sb;
		static struct vreg	rvr;

		rvr.stack_addr = f->alloca_regs;
		rvr.size = 4;
		backend_vreg_map_preg(&rvr, &x86_gprs[0]);
		emit->store(&rvr, &rvr);
		backend_vreg_unmap_preg(&x86_gprs[0]);
		if (ip && ip->src_vreg && ip->src_vreg->is_multi_reg_obj) {
			rvr.stack_addr = f->alloca_regs->next;
			backend_vreg_map_preg(&rvr, &x86_gprs[3]);
			emit->store(&rvr, &rvr);
			backend_vreg_unmap_preg(&x86_gprs[3]);
		}

		for (sb = f->alloca_head; sb != NULL; sb = sb->next) {
			emit->dealloc_vla(sb, NULL);
		}

		rvr.stack_addr = f->alloca_regs;
		backend_vreg_map_preg(&rvr, &x86_gprs[0]);
		emit->load(&x86_gprs[0], &rvr);
		backend_vreg_unmap_preg(&x86_gprs[0]);
		if (ip && ip->src_vreg && ip->src_vreg->is_multi_reg_obj) {
			rvr.stack_addr = f->alloca_regs->next;
			backend_vreg_map_preg(&rvr, &x86_gprs[3]);
			emit->load(&x86_gprs[3], &rvr);
			backend_vreg_unmap_preg(&x86_gprs[3]);
		}
	}
	emit->freestack(f, NULL);
	emit->ret(ip);
}

static struct reg *
get_abi_reg(int index, struct type *ty) {
	(void) index; (void) ty;
	/* x86 passes all stuff on the stack */
	return NULL;
}

static struct reg *
get_abi_ret_reg(struct type *ty) {
	if (is_integral_type(ty) || ty->tlist != NULL) {
		return &x86_gprs[0];
	} else {
		unimpl();
	}
	/* NOTREACHED */
	return NULL;
}

static int
gen_function(struct function *f) {
	struct ty_func		*proto;
	struct scope		*scope;
	struct icode_instr	*lastret = NULL;
	struct stack_block	*sb;
	size_t			size;
	size_t			alloca_bytes = 0;
	size_t			vla_bytes = 0;
	int			i;
	struct stupidtrace_entry	*traceentry = NULL;

	emit->setsection(SECTION_TEXT);
	proto = f->proto->dtype->tlist->tfunc;

	emit->func_header(f); /* XXX */
	emit->label(f->proto->dtype->name, 1);
	emit->intro(f);

	if (proto->nargs > 0) {
		struct sym_entry	*se = proto->scope->slist;
		int			i;
		long			offset = 8; /* ebp, ret, was 0 */

		if (f->proto->dtype->tlist->next == NULL
			&& (f->proto->dtype->code == TY_STRUCT
			|| f->proto->dtype->code == TY_UNION)) {
			/*
			 * Function returns struct/union - accomodate for
			 * hidden pointer (passed as first argument)
			 */
			offset += 4;
		}

		for (i = 0; i < proto->nargs; ++i, se = se->next) {
			size_t		size;

			size = backend->get_sizeof_type(se->dec->dtype, NULL);
			if (size < 4) {
				/*
				 * 07/21/08: Ouch, this was missing! char and
				 * short are passed as dwords, so make sure the
				 * corresponding stack block is also 4 bytes
				 *
				 * Otherwise emit_addrof() will skip the wrong
				 * byte count to get to the start of the
				 * ellipsis in variadic functions
				 */
				/*
				 * 05/22/11: Account for empty structs (a GNU
				 * C silliness) being passed
				 */
				if (size > 0) {
					size = 4;
				}
			} else if ((size % 4) != 0) {
				/*
				 * 08/09/08: Pad to boundary of 4! This was
				 * already done for long double below, but not
				 * for structs and unions
				 */
				size += 4 - size % 4;
			}

			sb = make_stack_block(offset, size); 
			offset += size; /* was before makestackblock */

			sb->is_func_arg = 1;
			se->dec->stack_addr = sb;
		}
	}

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

#if 0
			if (i+1 < scope->automatic_decls.ndecls
				&& !IS_VLA(dec[i+1]->dtype->flags)) {
				align = calc_align_bytes(f->total_allocated,
					dec[i]->dtype,
					dec[i+1]->dtype);	
			} else {
				align = 0;
			}	
#endif

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
		stack_align(f, 4);
	}

	/*
	 * Allocate storage for saving callee-saved registers (ebx/esi/edi)
	 * (but defer saving them until esp has been updated)
	 *
	 * 11/26/07: This unconditionally allocated storage for all regs
	 * regardless of whether they were saved! Bad.
	 */
#if 0
	f->total_allocated += 12;
	f->callee_save_offset = f->total_allocated;
#endif

	if (f->callee_save_used & CSAVE_EBX) {
		ebx_saved = 1;
		f->total_allocated += 4;
		csave_ebx.stack_addr
			= make_stack_block(f->total_allocated /*callee_save_offset*/, 4);
	}	
	if (f->callee_save_used & CSAVE_ESI) {
		esi_saved = 1;
		f->total_allocated += 4;
		csave_esi.stack_addr
			= make_stack_block(f->total_allocated /*callee_save_offset  - 4 */, 4);
	}
	if (f->callee_save_used & CSAVE_EDI) {
		edi_saved = 1;
		f->total_allocated += 4;
		csave_edi.stack_addr
			= make_stack_block(f->total_allocated /*callee_save_offset  - 8*/, 4);
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
	for (sb = f->alloca_head; sb != NULL; sb = sb->next) {
		f->total_allocated += sb->nbytes;
		alloca_bytes += sb->nbytes;
		sb->offset = f->total_allocated;
	}
	if (f->alloca_head != NULL || f->vla_head != NULL) {
		/*
		 * Get stack for saving return value registers before
		 * performing free() and alloca()ted blocks
		 */
		f->alloca_regs = make_stack_block(0, 4);
		f->total_allocated += 4;
		f->alloca_regs->offset = f->total_allocated; 
		f->alloca_regs->next = make_stack_block(0, 4);
		f->total_allocated += 4;
		f->alloca_regs->next->offset = f->total_allocated; 
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

	if (sysflag == OS_OSX) {
		stack_align(f, 16);
	} else {
		stack_align(f, 4);
	}
	if (f->total_allocated > 0) {
		emit->allocstack(f, f->total_allocated);
		if (f->callee_save_used & CSAVE_EBX) {
			backend_vreg_map_preg(&csave_ebx, &x86_gprs[1]);
			emit->store(&csave_ebx, &csave_ebx);
			backend_vreg_unmap_preg(&x86_gprs[1]);
			x86_gprs[1].used = 0; /* unneeded now?!?! */
		}	
		if (f->callee_save_used & CSAVE_ESI) {
			backend_vreg_map_preg(&csave_esi, &x86_gprs[4]);
			emit->store(&csave_esi, &csave_esi);
			backend_vreg_unmap_preg(&x86_gprs[4]);
			x86_gprs[4].used = 0; /* unneeded now!?!? */
		}	
		if (f->callee_save_used & CSAVE_EDI) {
			backend_vreg_map_preg(&csave_edi, &x86_gprs[5]);
			emit->store(&csave_edi, &csave_edi);
			backend_vreg_unmap_preg(&x86_gprs[5]);
			x86_gprs[5].used = 0; /* unneded now?!?! */
		}
	}
	if (stackprotectflag) {
		emit->save_ret_addr(f, saved_ret_addr);
	}
	if (f->alloca_head) {
		/* 08/19/07: This wrongly used alloca_head! */
		emit->zerostack(f->alloca_tail, alloca_bytes);
	}
	if (f->vla_head) {
		/* 08/19/07: This wrongly used vla_head! */
		emit->zerostack(f->vla_tail, vla_bytes);
	}

	if (stupidtraceflag && emit->stupidtrace != NULL) {
		traceentry = put_stupidtrace_list(f);
		emit->stupidtrace(traceentry);
	}

	if (xlate_icode(f, f->icode, &lastret) != 0) {
		return -1;
	}
	if (lastret != NULL) {
		struct icode_instr	*tmp;

		for (tmp = lastret->next; tmp != NULL; tmp = tmp->next) {
			if (tmp->type != INSTR_SETITEM) {
				lastret = NULL;
				break;
			}
		}
	}

	emit->outro(f);

	if (traceentry != NULL) {
		emit->finish_stupidtrace(traceentry);
	}

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
	/*
	 * Emit remaining static initializd variables. Currently this
	 * should only handle function name identifiers (__func__).
	 */
	if (sysflag == OS_OSX) {
		emit->global_static_decls(global_scope.static_decls.data,
				global_scope.static_decls.ndecls);
	}
/*	emit->static_init_vars(static_init_vars);
	emit->static_init_thread_vars(static_init_thread_vars);*/
	emit->static_init_vars(static_init_vars);
	emit->static_init_thread_vars(static_init_thread_vars);

	emit->static_uninit_vars(static_uninit_vars);
	emit->static_uninit_thread_vars(static_uninit_thread_vars);
	emit->global_extern_decls(global_scope.extern_decls.data,
			global_scope.extern_decls.ndecls);
	if (emit->extern_decls) {
		emit->extern_decls();
	}

	/*
	 * Support buffers at end because we may only now know
	 * whether they are needed (used at all)
	 */
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
#if 0
	if (emit->llong_constants) {
		emit->llong_constants();
	}
#endif
	emit->support_buffers();
#if 0
	if (emit->pic_support) {
		emit->pic_support();
	}
#endif
	emit->empty();


#if 0
	if (emit->struct_defs) {
		emit->struct_defs();
	}	
#endif

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

static int
calc_x86_stack_bytes(struct fcall_data *fcall,
	struct vreg **vrs, int nvrs, int start_value) {
	int	bytes = start_value + 8; /* ebp and return address */
	int	i;
	int	need_dap = 0;


	if (fcall->functype->nargs == -1
		/*|| ty->implicit*/) {
		/* Need default argument promotions */
		need_dap = 1;
	}	
	for (i = nvrs - 1; i >= 0; --i) {
		if (fcall->functype->variadic
			&& i >= fcall->functype->nargs) {
			need_dap = 1;
		}
		if (vrs[i]->type->tlist != NULL
			|| is_integral_type(vrs[i]->type)) {
			bytes += vrs[i]->size < 4? 4: vrs[i]->size;
		} else if (is_floating_type(vrs[i]->type)) {
			if (vrs[i]->type->code == TY_FLOAT) {
				if (need_dap) {
					bytes += 8;
				} else {
					bytes += 4;
				}
			} else if (vrs[i]->type->code == TY_LDOUBLE) {
				bytes += 16; /* XXXXXXXXXXXXX 16 */
			} else {
				bytes += vrs[i]->size;
			}
		} else if (vrs[i]->type->code == TY_STRUCT
			|| vrs[i]->type->code == TY_UNION) {
			/* 07/21/08: (left-)Align to boundary of 4 */
			if (vrs[i]->size & 3) {
				bytes += 4 - (vrs[i]->size % 4);
			}
			bytes += vrs[i]->size;
		}
	}
	return bytes;
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
	int			i;
	int			need_dap = 0;
	int			was_struct;
	int			was_float;
	int			was_llong;
	int			struct_return = 0;
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
		if (struct_lvalue == NULL || fcall->need_anon) {
			struct type_node	*tnsav;
			/*
			 * Result of function is not assigned so we need to
			 * allocate storage for the callee to store its
			 * result into
			 */

			tnsav = ty->tlist;
			ty->tlist = NULL;

			/*
			 * 08/05/08: Don't allocate anonymous struct return
			 * storage right here, but when creating the stack
			 * frame. This has already been done on MIPS, PPC
			 * and SPARC, but not on x86/AMD64. The reason is
			 * that it broke something that is long fogotten
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

			ty->tlist = tnsav;
			/*
			 * 08/05/08: Don't add to allpushed since struct is
			 * created on frame
			 */
	/*		allpushed += struct_lvalue->size;*/
			ret_is_anon_struct = 1;
		}	
	}	

	if (sysflag == OS_OSX) {
		int	count;

		count = calc_x86_stack_bytes(fcall, vrs, nvrs, struct_return? 4: 0);
		if (count % 16 != 0) {
			unsigned long align = 16 - count % 16;
#if 0
			printf("aligning %lu\n", align);
#endif
			icode_make_allocstack(NULL, align, il);
			allpushed += align;
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

	for (i = nvrs - 1; i >= 0; --i) {
		struct vreg		*dest;

		if (fcall->functype->variadic
			&& i >= fcall->functype->nargs) {
			need_dap = 1;
		}

		/* 
		 * May have to be moved into 
		 * register if we're dealing with
		 * pointer stuff, otherwise we may 
		 * push with memory operand
		 */

		was_struct = was_float = was_llong = 0;
		if (vrs[i]->parent) {
			vr2 = get_parent_struct(vrs[i]);
		} else {
			vr2 = NULL;
		}	

		if (vrs[i]->type->tlist != NULL) {
			vreg_faultin(NULL, NULL, vrs[i], il, 0);
		} else {
			if (vrs[i]->from_ptr) {
				/* XXX not needed?! */
				vreg_faultin(NULL, NULL,
					vrs[i]->from_ptr, il, 0);
			}	
			if (IS_CHAR(vrs[i]->type->code)
				|| IS_SHORT(vrs[i]->type->code)) {
				struct type	*ty
					= make_basic_type(TY_INT);

				/*
				 * Bytes and halfwords are pushed as words  
				 */
				vrs[i] = backend->
					icode_make_cast(vrs[i], ty, il);
			} else if (IS_LLONG(vrs[i]->type->code)) {
				vreg_faultin(NULL, NULL, vrs[i], il, 0);
				allpushed += 8;
				ii = icode_make_push(vrs[i], il);
				append_icode_list(il, ii);
				was_llong = 1;
			} else if (vrs[i]->type->code == TY_STRUCT
				|| vrs[i]->type->code == TY_UNION) {
				/*
				 * struct/union - memcpy() it onto stack,
				 * allocate storage manually (no push!)
				 */
				/*
				 * 05/22/11: Account for empty structs (a GNU
				 * C silliness) being passed
				 */
				if (vrs[i]->size > 0) {
					/* 07/21/08: (left-)Align to boundary of 4 */
					if (vrs[i]->size & 3) {
						icode_make_allocstack(NULL, 4 - (vrs[i]->size % 4), il);
						allpushed += 4 - (vrs[i]->size % 4);
					}

					dest = vreg_stack_alloc(vrs[i]->type, il, 0, NULL);
					allpushed += dest->size;
					backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
					vreg_faultin_ptr(vrs[i], il);
					icode_make_copystruct(dest, vrs[i], il);
				}
				was_struct = 1;
			} else {
				vreg_faultin_x87(NULL, NULL, vrs[i], il, 0);
				if (IS_FLOATING(vrs[i]->type->code)) {
					was_float = 1;
					if (need_dap
						&& vrs[i]->type->code
						== TY_FLOAT) {
#if 0
						struct type	*ty
						= make_basic_type(TY_DOUBLE);

						vrs[i] = backend->
						icode_make_cast(vrs[i],ty,il);
#endif
						if (sysflag == OS_OSX
							&& vrs[i]->type->code == TY_FLOAT) {
							struct type	*ty
								= make_basic_type(TY_DOUBLE);

							vrs[i] = backend->
								icode_make_cast(vrs[i],ty,il);
						} else {
							vrs[i] = n_xmemdup(vrs[i],
								sizeof *vrs[i]);
							vrs[i]->type = make_basic_type(
								TY_DOUBLE);
							vrs[i]->size = backend->
								get_sizeof_type(vrs[i]->
									type, NULL);	
						}
					}
					if (vrs[i]->type->code == TY_LDOUBLE) {
						if (sysflag == OS_OSX) {
#if 0
							/* 6 bytes of padding */
							icode_make_allocstack(NULL, 6, il);
							allpushed += 6;
#endif
						} else {
#if 0
							/* 2 bytes of padding */
							icode_make_allocstack(NULL, 2, il);
							allpushed += 2;
#endif
						}
					}
					dest = vreg_stack_alloc(vrs[i]->type,
						il, 0, NULL);
					vreg_map_preg(dest, vrs[i]->pregs[0]);
					icode_make_store(NULL, dest, dest, il);

					if (vrs[i]->type->code == TY_DOUBLE) {
						allpushed += 8;
					} else if (vrs[i]->type->code == TY_FLOAT) {
						allpushed += 4;
					} else {
						allpushed += vrs[i]->size;
					/*	allpushed += 10;*/
					}
				}
			}
		}
		
		if (!was_struct && !was_float && !was_llong) {
			ii = icode_make_push(vrs[i], il);
			if (vrs[i]->size < 4) {
				/* bytes and shorts are passed as words */
				allpushed += 4;
			} else if (vrs[i]->type->tlist != NULL
				&& vrs[i]->type->tlist->type == TN_ARRAY_OF) {
				allpushed += 4;
			} else {
				allpushed += vrs[i]->size;
			}	
			append_icode_list(il, ii);
		}	
		
		free_pregs_vreg(vrs[i], il, 0, 0);
		if (vr2 && vr2->from_ptr && vr2->from_ptr->pregs[0]
			&& vr2->from_ptr->pregs[0]->vreg == vr2->from_ptr) {
			free_preg(vr2->from_ptr->pregs[0], il, 0, 0);
		}	
	}

	if (struct_return) {
		struct vreg	*addr = vreg_alloc(NULL, NULL, NULL, NULL);
		
		/*
		 * 06/15/09: icode_make_addrof() apparently happily used stale
		 * registers for parent struct pointers. Such invalid
		 * registers can happen if memcpy() is used to pass a struct
		 * by value. alloc_gpr() used by icode_make_addrof() requires
		 * a struct type rather than ``function returning struct'', so
		 * we temporarily set the type list to NULL.
		 * XXX Can this break in the backend?
		 */
		{
			struct reg	*r;
			/*ii*/ r = make_addrof_structret(struct_lvalue, il);

			addr->pregs[0] = r /*ii->dat*/;
			addr->size = 4;
		}

		ii = icode_make_push(addr, il);
		append_icode_list(il, ii);

		/*
		 * Adjust amount of bytes allocated; the push above adds
		 * 4 to it but it's the callee that cleans up the hidden
		 * pointer, so the count needs to be fixed manually (as
		 * opposed to having emit_freestack do it.) 
		 * XXX this is very ugly
		 */
		ii = icode_make_adj_allocated(-4);
		append_icode_list(il, ii);
		free_preg(addr->pregs[0], il, 0, 0);
	}


	/*
	 * In the x86 ABI, the caller is responsible for saving
	 * eax/ecx/edx (but not ebx, esi, edi), so that's what we 
	 * do here
	 */
	backend->invalidate_gprs(il, 1, INV_FOR_FCALL);

	if (ty->tlist->type == TN_POINTER_TO) {
		/* Need to indirect thru function pointer */
		vreg_faultin(NULL, NULL, tmpvr, il, 0);
		ii = icode_make_call_indir(tmpvr->pregs[0]);
		tmpvr->pregs[0]->used = 0;
		tmpvr->pregs[0]->vreg = NULL;
	} else {
		ii = icode_make_call(ty->name);
		if (IS_ASM_RENAMED(ty->flags)) {
			ii->hints |= HINT_INSTR_RENAMED;
		}
	}	
	append_icode_list(il, ii);
	ii = icode_make_freestack(allpushed);
	append_icode_list(il, ii);

	ret = vreg_alloc(NULL, NULL, NULL, NULL);
	ret->type = ty;

	/*
	 * 07/06/2007 What the HELL!??!?! This stuff still did
	 *    if (ty->tlist->next != NULL) {
	 * to check if the function returns a pointer, not
	 * taking into account that this could be a call thru
	 * a function pointer. I thought I had this fixed
	 * everywhere but apparently it was only done in AMD64
	 * and all other backends were broken :-(
	 */
#if 0
	if (ty->tlist->next != NULL) {
#endif
		
	if ((ty->tlist->type == TN_POINTER_TO
		&& ty->tlist->next->next != NULL)
		|| (ty->tlist->type == TN_FUNCTION
		&& ty->tlist->next != NULL)) {	
		/* Must be pointer */
		ret->pregs[0] = &x86_gprs[0];
	} else {
		if (IS_CHAR(ty->code)) {
			ret->pregs[0] = x86_gprs[0].composed_of[0]->
				composed_of[1];
		} else if (IS_SHORT(ty->code)) {
			ret->pregs[0] = x86_gprs[0].composed_of[0];
		} else if (IS_INT(ty->code)
			|| IS_LONG(ty->code)
			|| ty->code == TY_ENUM) { /* XXX */
			ret->pregs[0] = &x86_gprs[0];
		} else if (IS_LLONG(ty->code)) {
			ret->pregs[0] = &x86_gprs[0];
			ret->is_multi_reg_obj = 2;
		} else if (ty->code == TY_FLOAT
			|| ty->code == TY_DOUBLE
			|| ty->code == TY_LDOUBLE) {
			if (sysflag == OS_OSX
				&& ty->code != TY_LDOUBLE) {
				ret->pregs[0] = &x86_sse_regs[0];
			} else {
				ret->pregs[0] = &x86_fprs[0];
			}
		} else if (ty->code == TY_STRUCT
			|| ty->code == TY_UNION) {
			/*
			 * 08/16/07: Added this
			 */
			if (ret_is_anon_struct) {
				ret = struct_lvalue;
			}
			ret->struct_ret = 1;
		} else if (ty->code == TY_VOID) {
			; /* Nothing! */
		}
	}

	if (ret->pregs[0] != NULL) {
		vreg_map_preg(ret, ret->pregs[0]);
		if (ret->is_multi_reg_obj) {
			vreg_map_preg2(ret, &x86_gprs[3]);
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

	if (is_x87_trash(ret)) {
		/*
		 * Don't keep stuff in x87 registers, ever!!!
		 */
		free_preg(ret->pregs[0], il, 1, 1);
	}	
	return ret;
}

static int
icode_make_return(struct vreg *vr, struct icode_list *il) {
	struct icode_instr	*ii;
#if 0
	struct type		*rtype = curfunc->proto->dtype;

#endif
	struct type		*rtype = curfunc->rettype;

#if 0 
	oldtn = curfunc->proto->dtype->tlist;
	rtype->tlist = rtype->tlist->next;
#endif
	/*
	 * 08/06/17: We were removing the first typenode, then performed
	 * the return, then restored the typenode. This is wrong because
	 * the generated icode may rightly depend on the type being stable
	 * instead of having it changed behind its back!
	 */
#if 0
	rtype = func_to_return_type(rtype);
#endif

	if (vr != NULL) {
		if (IS_CHAR(rtype->code)
			|| IS_SHORT(rtype->code)
			|| IS_INT(rtype->code)
			|| IS_LONG(rtype->code)
			|| rtype->code == TY_ENUM /* 06/15/09: Was missing?!? */
			|| rtype->tlist != NULL) {
			struct reg	*r = &x86_gprs[0];
			int		size = backend->get_sizeof_type(rtype,0);
			
			if (r->size > (unsigned long)size) {
				r = get_smaller_reg(r, size);
			}	
			vreg_faultin(r, NULL, vr, il, 0);
		} else if (IS_LLONG(rtype->code)) {
			vreg_faultin(&x86_gprs[0], &x86_gprs[3],
				vr, il, 0);
		} else if (rtype->code == TY_FLOAT
			|| rtype->code == TY_DOUBLE
			|| rtype->code == TY_LDOUBLE) {
			/* Return in st0 */
			vreg_faultin_x87(NULL, NULL, vr, il, 0);
		} else if (rtype->code == TY_STRUCT
			|| rtype->code == TY_UNION) {
			struct stack_block	*sb;
			struct vreg		*dest;
			struct vreg		*from_ptr;
			static struct decl	dec;
			struct decl		*decp;
			unsigned long		offset;
			static struct type_node	tn;

			/* Get hidden struct pointer for storing return */
			offset = 8; /* Move past ebp,eip */
			sb = make_stack_block(offset, 4);
			sb->is_func_arg = 1;
			dec.stack_addr = sb;
			dec.dtype = n_xmemdup(rtype, sizeof *rtype);
			decp = n_xmemdup(&dec, sizeof dec);
			from_ptr = vreg_alloc(decp, NULL, NULL, NULL);
			
			tn.type = TN_POINTER_TO;
			from_ptr->type->tlist = &tn;
			from_ptr->size = 4;
			vreg_faultin(NULL, NULL, from_ptr, il, 0);
			
			dest = vreg_alloc(NULL, NULL, NULL, NULL);
			dest->from_ptr = from_ptr;

			/* vr may come from pointer */
			vreg_faultin_ptr(vr, il);
			icode_make_copystruct(dest, vr, il); 
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

	struct vreg	*dest = *dest0;
	struct vreg	*src = *src0;

	/*
	 * 05/30/11: This was missing! This function implicitly assumed both
	 * operands to be register-resident already (e.g. see the eax checks
	 * below which do not verify that eax is really mapped to the vreg)-
	 * which was true in most but not all cases. This broke compound
	 * assignment operators for VLAs, and may have caused bad code
	 * generation in other cases as well
	 */
	if (!is_floating_type(dest->type)) {
		vreg_faultin_protected(dest, NULL, NULL, src, il, 0);
		vreg_faultin_protected(src, NULL, NULL, dest, il, 0);
	}

	/*
	 * For long long, the preparations below only apply to shifting
	 */
	if (dest->is_multi_reg_obj && op != TOK_OP_BSHL && op != TOK_OP_BSHR) {
		return;
	}	
	if (is_floating_type(dest->type)) {
		if (backend->arch == ARCH_X86
			|| dest->type->code == TY_LDOUBLE) {
#if 0
			/*
		 	 * As we can only write to memory from st0, it is
		 	 * desirable to store all results there
		 	 */
			if (dest->pregs[0] != &x86_fprs[/*0*/ 1]) {
				icode_make_x86_fxch(dest->pregs[0],
					&x86_fprs[0], il);
			}
#endif
			return;
		} else {
			/* Has to be SSE (AMD64) */
			return;
		}
	}	

	if (op == TOK_OP_DIVIDE || op == TOK_OP_MOD || op == TOK_OP_MULTI) {
		/* Destination must be in eax, or rax */

		if (backend->arch == ARCH_AMD64 && dest->size == 8) {
			if (dest->pregs[0] != &amd64_x86_gprs[0]) {
				free_preg(&amd64_x86_gprs[0], il, 1, 1);
				vreg_faultin(&amd64_x86_gprs[0], NULL, dest, il,
					0);
			}
			reg_set_unallocatable(&amd64_x86_gprs[3]);
			vreg_faultin_protected(dest, /*NULL*/
				NULL, NULL, src, il, 0);
			reg_set_allocatable(&amd64_x86_gprs[3]);
			return;
		}
		if (dest->pregs[0] != &x86_gprs[0]) {
			/*
			 * 05/20/11: This unconditionally freed eax for AMD64
			 * too, such that if rax had been in use, it was not
			 * saved but still marked as in use - which could lead
			 * to problems later on when both rax and eax were
			 * in use
			 */
			if (backend->arch == ARCH_AMD64) {
				free_preg(&amd64_x86_gprs[0], il, 1, 1);
			} else {
				free_preg(&x86_gprs[0], il, 1, 1);
			}
			vreg_faultin(&x86_gprs[0], NULL, dest, il, 0);
		}

		/*
		 * 04/13/08: Only load immediate value if there is no
		 * immediate instruction available!
		 */
		if (src->from_const == NULL
			|| !backend->have_immediate_op(dest->type, op)) {
			/* may not be edx for div */
			struct reg	*srcreg = NULL;

			if (src->pregs[0] && src->pregs[0]->vreg == src) {
				if (src->pregs[0] == &x86_gprs[3]) {
					/* Have to move it elsewhere */

					/*
					 * 10/31/07: Pass size instead of 0.
					 * We want a 4 byte reg, but 0 gives
					 * us a full GPR. That breaks on
					 * AMD64, where we'll be getting an
					 * 8 byte reg instead
					 *
					 * 08/10/08: This was missing the
					 * possibility that the source could
					 * be loaded to eax, which is
					 * obviously wrong because that's
					 * the target location! Thus set eax 
					 * unallocatable
					 * XXX what if it was unallocatable
					 * before?
				 	 */
					reg_set_unallocatable(&x86_gprs[0]);
					srcreg = ALLOC_GPR(curfunc, /*0*/4,
						il, NULL);
					reg_set_allocatable(&x86_gprs[0]);
				}
			}	
			reg_set_unallocatable(&x86_gprs[3]);
			vreg_faultin_protected(dest, /*NULL*/
				srcreg, NULL, src, il, 0);
			reg_set_allocatable(&x86_gprs[3]);
		}

		/* edx is trashed in any case - save it */
		/*
		 * 05/20/11: This unconditionally freed edx for AMD64
		 * too, such that if rdx had been in use, it was not
		 * saved but still marked as in use - which could lead
		 * to problems later on when both rdx and edx were
		 * in use
		 */
		if (backend->arch == ARCH_AMD64) {
			free_preg(&amd64_x86_gprs[3], il, 1, 1);
		} else {
			free_preg(&x86_gprs[3], il, 1, 1);
		}
	} else if ((op == TOK_OP_BSHL || op == TOK_OP_BSHR)
		&& (src->from_const == NULL
		|| !backend->have_immediate_op(dest->type, op))) {
		/*
		 * Source must be in cl
		 *
		 * 04/13/08: Only load immediate value if there is no
		 * immediate instruction available!
		 */
		struct reg	*reg_cl;

		reg_cl = x86_gprs[2]
			.composed_of[0]
			->composed_of[1];
		if (src->pregs[0] != reg_cl
			|| reg_cl->vreg != src) {
		 	/*
			 * 05/20/11: This unconditionally freed ecx for
			 * AMD64
			 */
			if (backend->arch == ARCH_AMD64) {
				free_preg(&amd64_x86_gprs[2], il, 1, 1);
			} else {
				free_preg(&x86_gprs[2], il, 1, 1);
			}

			if (src->is_multi_reg_obj) {
				reg_set_unallocatable(&x86_gprs[2]);
				src = backend->icode_make_cast(src,
					make_basic_type(TY_CHAR), il);	
				*src0 = src;
				reg_set_allocatable(&x86_gprs[2]);
			}

			/*
			 * Need to ensure that the operand is loaded
			 * correctly regardless of its size.
			 * XXX this is really nasty, perhasp we should
			 * demand that callers guarantee a byte-sized
			 * or word-sized vreg?!
			 */
			if (src->size == 1) {
				vreg_faultin(reg_cl, NULL, src, il, 0);
			} else if (src->size == 2) {
				vreg_faultin(x86_gprs[2].composed_of[0],
					NULL, src, il, 0);
			} else {
				/*
				 * 05/20/11: This did not distinguish
				 * between x86 and AMD64, such that
				 * ecx was always used even for 64bit
				 * integers (resulting in assembler
				 * errors)
				 */
				if (backend->arch == ARCH_AMD64
					&& src->size == 8) {
					vreg_faultin(&amd64_x86_gprs[2],
						NULL, src, il, 0);
				} else {
					vreg_faultin(&x86_gprs[2],
						NULL, src, il, 0);
				}
			}
			vreg_faultin_protected(src, NULL, NULL,
				dest, il, 0);
		}
	}
}


static void 
change_preg_size(
	struct vreg *vr,
	struct icode_list *il,
	struct type *to,
	struct type *from);

/*
 * ,==x87==x87====x87=======x87===============x87===========,
 * |~~*,.,*~{ 80x87 FLOATING POINT KLUDGERY DELUXE }~*,.,*~~|
 * `======87=87=87===========87=======87==87==========87===='
 *
 * ``struct vreg floatbuf'' is used as buffer to convert between integers
 * and floats because:
 *    - fild cannot take a GPR or immediate operand
 *    - fstp can only write to memory too
 *
 * ``struct vreg x87cw_new'' and ``struct vreg x87cw_old'' are used as buffers
 * for storing the x87 status control word. Converting a floating point value
 * to an integer with x87 by default rounds mathematically. However, in C,
 * ``(int)fp_value'' is required to *truncate* the fractional part. The x87
 * control word therefore has to be changed before and restored after
 * performing the fp-to-integer conversion in order to make the thing behave as
 * desired. (I couldn't believe it when I first saw the code gcc generated for
 * this exercise.)
 */
#if ! REMOVE_FLOATBUF
static void
#else
static struct vreg *
#endif
load_floatbuf(struct vreg *data,
#if REMOVE_FLOATBUF
	struct type *from,
#endif
	struct icode_list *il
#if REMOVE_FLOATBUF
	, int is_int
#endif
	) {


#if ! REMOVE_FLOATBUF
	if (floatbuf.var_backed == NULL) {
		/* Not allocated yet */
		static struct decl	dec;
		static struct type	ty;

		ty = *make_basic_type(/*TY_INT*/TY_LLONG);
		dec.dtype = &ty;
		ty.name = "_Floatbuf";
		floatbuf.var_backed = &dec;
		floatbuf.type = &ty;
		if (backend->arch == ARCH_AMD64) {
			/*
			 * Wow this used data->pregs[0]->size, which for an
			 * x87 fpr was 12... So stores to it did
			 *    movt val, _Floatbuf
			 * I guess 8 is invalid too? So use 4 always
			 */
			floatbuf.size = 4 ;   /*data->pregs[0]->size;*/
		} else {
			floatbuf.size = 4; /* XXX long long :( */
		}
	}
	vreg_map_preg(&floatbuf, data->pregs[0]);
	tmp = n_xmemdup(&floatbuf, sizeof floatbuf);
	icode_make_store(curfunc, &floatbuf, &floatbuf, il);
#else
	/* REMOVE_FLOATBUF is set */
	static struct vreg	vr;
	struct vreg		*resvr;
	int			res_type_changed_to_64bit = 0; /* 06/15/08: Was 1!! */

	if (from->code < /*TY_INT*/ TY_LLONG) {
		/*
		 * Smaller than int isn't possible - must have been
		 * promoted
		 */
		from = make_basic_type(  /*TY_INT*/ TY_LLONG);
	}

	vr.type = from;
	vr.size = backend->get_sizeof_type(from, NULL);

	vr.is_multi_reg_obj = data->is_multi_reg_obj;
	vr.pregs[0] = data->pregs[0];
	vr.pregs[1] = data->pregs[1];

	resvr = vreg_alloc(NULL,NULL,NULL,NULL);
	*resvr = vr;

	if ((IS_INT(data->type->code) || IS_LONG(data->type->code))
		&& data->type->sign == TOK_KEY_UNSIGNED) {
		/*
		 * 06/08/08: Unsigned integers require storing as 64bit
		 */
		vreg_set_new_type(resvr, make_basic_type(TY_LLONG));
		res_type_changed_to_64bit = 1;
	}	

	vreg_map_preg(resvr, data->pregs[0]);

	/*
	 * 06/15/08: Multi-register mapping was incorrectly done for
	 * fp-to-int conversion, but is only correct the other way
	 * around!
	 */
	if (resvr->is_multi_reg_obj
		&& !res_type_changed_to_64bit
		&& is_int) {
		vreg_map_preg2(resvr, data->pregs[1]);
	}

	if (IS_FLOATING(from->code)) {
		/* Save and convert */
/*	
	vreg_stack_alloc() doesn't work because it doesn't
	immediately give us a stack_block which can be
	assigned to other vregs too
	resvr = vreg_stack_alloc(from, il, 1, NULL);*/
resvr->stack_addr = icode_alloc_reg_stack_block(curfunc, resvr->size);
		/*
		 * 06/15/08: Always use data->type instead of resvr->type!
		 * resvr->type is the source type...?
		 */

		icode_make_x86_fist(resvr->pregs[0], resvr,
			(res_type_changed_to_64bit
			/*&& is_integral_type(resvr->type)*/)?
			
			resvr->type: data->type, il);
	} else {
/*	free_preg(resvr->pregs[0], il, 1, 1);*/
		icode_make_store(curfunc, resvr, resvr, il);
	}

	/* Yawn, another duplication to ensure the multi gpr flag is
	 * preserved for the stores above
	 *
	 * 06/08/08: This is now actually beneficial because if we stored
	 * an unsigned 32bit integer to a 64bit storage block (which is
	 * necessary to convert large values such as UINT_MAX correctly),
	 * then we can now set the type of that block to ``unsigned int'',
	 * thus ensuring that the subsequent load only looks at the lower
	 * double-word
	 */
/*	return dup_vreg(resvr);*/
	resvr = dup_vreg(resvr);
	vreg_set_new_type(resvr, data->type);
	return resvr;
#endif /* REMOVE_FLOATBUF */
}

#if REMOVE_FLOATBUF

static struct vreg *
load_integer_floatbuf(struct vreg *data, struct type *from,
	struct icode_list *il) {

	return load_floatbuf(data, from, il, 1);
}	


static struct vreg *
load_floatval_floatbuf(struct vreg *data, struct type *from,
	struct icode_list *il) {	

	return load_floatbuf(data, from, il, 0);
}

#endif


/* Save FPU CW to memory */
static void
store_x87cw(struct icode_list *il) {
	if (x87cw_old.var_backed == NULL) {
		/* Not allocated yet */
		static struct decl	dec_old;
		static struct decl	dec_new;
		static struct type	ty_old;
		static struct type	ty_new;

		ty_old = *make_basic_type(TY_SHORT);
		ty_old.name = "_X87CW_old";
		dec_old.dtype = &ty_old;
		x87cw_old.var_backed = &dec_old;
		x87cw_old.type = &ty_old;
		x87cw_old.size = 2;

		ty_new = *make_basic_type(TY_SHORT);
		ty_new.name = "_X87CW_new";
		dec_new.dtype = &ty_new;
		x87cw_new.var_backed = &dec_new;
		x87cw_new.type = &ty_new;
		x87cw_new.size = 2;
	}
	icode_make_x86_store_x87cw(&x87cw_old, il);
}

/* Create modified copy of in-memory CW */
static void
modify_x87cw(struct icode_list *il) {
	struct reg		*r;
	struct icode_instr	*ii;

	r = alloc_16_or_32bit_noesiedi(curfunc, 2, il, NULL);
	vreg_faultin(r, NULL, &x87cw_old, il, 0);
	vreg_map_preg(&x87cw_new, r);
	ii = icode_make_setreg(r->composed_of[0], 12);
	append_icode_list(il, ii);
	icode_make_store(curfunc, &x87cw_new, &x87cw_new, il);
	r->used = 0;
}

/* Load CW from memory */
static void
load_x87cw(struct vreg *which, struct icode_list *il) {
	icode_make_x86_load_x87cw(which, il);
}



#define AMD64_OR_X86_REG(idx) \
	(backend->arch == ARCH_AMD64? &amd64_x86_gprs[idx]: &x86_gprs[idx])

static void 
change_preg_size(
	struct vreg *vr,
	struct icode_list *il,
	struct type *to,
	struct type *from) {

	int			i;
	struct reg		*extreg = NULL;
	struct icode_instr	*ii;
	size_t			from_size;
	int			amd64_reg = 0;

	from_size = backend->get_sizeof_type(from, NULL);

	for (i = 0; i < N_GPRS; ++i) {
		if (is_member_of_reg(AMD64_OR_X86_REG(i), vr->pregs[0])) {
			break;
		}
	}
	if (i == N_GPRS) {
		if (backend->arch == ARCH_AMD64) {
			amd64_reg = 1;
		} else {
			printf("FATAL ERROR: %s is not member of any gpr\n",
				vr->pregs[0]->name);
			abort();
		}
	}


	if (vr->size > from_size
		&& (!IS_LLONG(to->code) || from_size != 4)) {
		/*
		 * A sub register is extended to a bigger register
		 */
		vr->pregs[0]->used = 0;
		if (i < N_GPRS && reg_unused(AMD64_OR_X86_REG(i))) {
			/* Use parent reg, e.g. movsx ax, al */
			if (backend->arch == ARCH_AMD64
				&& vr->size == 8) {
				;
			} else {
				extreg = vr->size == 4 || vr->size == 8?
				&x86_gprs[i]: x86_gprs[i].composed_of[0];
			}
		} else {
			/* Use unrelated reg */
			size_t	size;

			if (vr->size == 8 && backend->arch != ARCH_AMD64) {
				size = 4;
			} else {
				size = vr->size;
			}
			if (from_size == 1) {
				extreg = backend->alloc_16_or_32bit_noesiedi
					(curfunc, size, il, NULL);
			} else {	
				extreg = ALLOC_GPR(curfunc, size, il, NULL);
			}
		}	
	}	

	if (vr->size == 2) {
		if (from_size == 1) {
			free_preg(vr->pregs[0], il, 1, 0);
			icode_make_copyreg(extreg, vr->pregs[0], to, from, il);
			vreg_map_preg(vr, extreg);
		} else if (from_size == 4) {
			/* 4 - truncate */
			free_preg(vr->pregs[0], il, 1, 0);
			vreg_map_preg(vr, vr->pregs[0]->composed_of[0]);
		} else if (from_size == 8) {
			/* long long or long on amd64 */
			if (backend->arch == ARCH_X86) {
				free_preg(vr->pregs[0], il, 1, 0);
				free_preg(vr->pregs[1], il, 1, 0);
				vreg_map_preg(vr, vr->pregs[0]->composed_of[0]);
			} else {
				free_preg(vr->pregs[0], il, 1, 0);
				vreg_map_preg(vr,
					vr->pregs[0]
						->composed_of[0]
						->composed_of[0]);
			}
		}	
	} else if (vr->size == 4) {
		if (from_size == 8) {
			/*
			 * long long! Truncate - low-order 32bits are in
			 * first preg, on x86
			 */
			if (backend->arch == ARCH_X86) {
				free_preg(vr->pregs[1], il, 1, 0);
				vreg_map_preg(vr, vr->pregs[0]);
			} else {
				free_preg(vr->pregs[0], il, 1, 0);
				vreg_map_preg(vr, vr->pregs[0]->
					composed_of[0]);
			}
		} else {
			/* extend */
			/*
			 * Is this sub register the only used one? If not,
			 * the other one must be saved
			 */
			icode_make_copyreg(extreg, vr->pregs[0], to, from, il);
			free_preg(vr->pregs[0], il, 1, 0);
			vreg_map_preg(vr, extreg);
		}	
	} else if (vr->size == 8) {
		/* long long! */
		if (backend->arch == ARCH_AMD64) {
			if (extreg == NULL) {
				extreg = ALLOC_GPR(curfunc, 0, il, NULL);
			}
			free_preg(vr->pregs[0], il, 1, 0);
			icode_make_copyreg(extreg, vr->pregs[0], to, from, il);
			vreg_map_preg(vr, extreg);
			return;
		}
 
#if 0
		if (to->code == TY_ULLONG) {
			if (extreg != NULL) {
				icode_make_copyreg(extreg, vr->pregs[0],
					to, from, il);
				vreg_map_preg(vr, extreg);
			} else {
				/*
				 * dword being converted to long long -
				 * keep mapping
				 */
				vreg_map_preg(vr, vr->pregs[0]);
			}	
			reg_set_unallocatable(vr->pregs[0]);
			r = ALLOC_GPR(curfunc, 4, il, NULL);
			reg_set_allocatable(vr->pregs[0]);
			vreg_map_preg2(vr, r);
			ii = icode_make_setreg(r, 0);
			append_icode_list(il, ii);
		} else {
#endif
			/* signed long long */
			if (vr->pregs[0] != &x86_gprs[0]) {
				if (!reg_unused(&x86_gprs[0])) {
					free_preg(&x86_gprs[0], il, 1, 1);
				}

				/* Source may be associated with a variable?! */
				free_preg(vr->pregs[0], il, 1, 0);
				icode_make_copyreg(&x86_gprs[0], vr->pregs[0],
					from, from, il);
			}	
			if (!reg_unused(&x86_gprs[3])) {
				free_preg(&x86_gprs[3], il, 1, 1);
			}
			if (from->sign == TOK_KEY_SIGNED) {
				icode_make_x86_cdq(il);
			} else {
				ii = icode_make_setreg(&x86_gprs[3], 0);
				append_icode_list(il, ii);
			}	
			vreg_map_preg(vr, &x86_gprs[0]);
			vreg_map_preg2(vr, &x86_gprs[3]);
#if 0
		}	
#endif
	} else {
		/* Must be 1 - truncate */
		struct reg	*r;


		if (backend->arch == ARCH_AMD64
			&& from_size == 8) {
			free_preg(vr->pregs[0], il, 1, 0);
			vreg_map_preg(vr, vr->pregs[0]->
				composed_of[0]-> /* 32bit */
				composed_of[0]-> /* 16bit */
				composed_of[amd64_reg? 0: 1]);
			return; 
		}

		/*
		 * 08/18/08: This was missing the check for amd64_reg, so it
		 * would fail for i = 6
		 */
		if (i >= 4 && !amd64_reg) {
			/*
			 * Whoops - source resides in esi/edi, which
			 * do not have 8bit sub registers
			 */
			free_preg(&x86_gprs[i], il, 1, 0);
			/*
			 * 06/20/08: This used to pass the source size, i.e.
			 * possibly 8 for long long! This was wrong because
			 * it ended up setting multi-reg state in the backend
			 * and expecting a second alloc_*() for the second
			 * dword. This is wrong because we only want a single
			 * 32bit part register
			 */
			r = alloc_16_or_32bit_noesiedi(curfunc,
				/*from_size*/4, il, NULL);
			icode_make_copyreg(r, &x86_gprs[i], from, from, il);
			free_preg(r, il, 1, 0);
		} else if (!amd64_reg) {
			r = &x86_gprs[i];
			free_preg(r, il, 1, 0);
		} else {
			/*
			 * 08/18/08: This was missing?!!?!?!??
			 */
			r = vr->pregs[0];
		}
#if 0
		free_pregs_vreg(vr, il, 1, 0);
#endif

		/*
		 * 09/30/07: Wow, this unconditionally assumed that
		 * r is a 32bit register! That broke short-to-char
		 * conversion, but apparently only in some cases
		 *
		 * XXX we have to use r->size instead of from_size
		 * here... otherwise e.g. on AMD64 a
		 *
		 *     *charp++ = *ushortp;
		 *
		 * ... assignment gives the short source value in
		 * eax, which may be a conversion/promotion issue
		 */
		if (r->size >= 4) {
			if (r->size == 8 && from_size == 8) {
				/* AMD64 */
				vreg_map_preg(vr,
					r->	
					composed_of[0]->
					composed_of[0]->
					/* was missing amd64_reg case */
					composed_of[amd64_reg? 0: 1]);
			} else {	
				vreg_map_preg(vr,
					r->	
					composed_of[0]->
					/* was missing amd64_reg case */
					composed_of[amd64_reg? 0: 1]);
			}
		} else {
			/* was missing amd64_reg case */
			vreg_map_preg(vr,
				r->composed_of[amd64_reg? 0: 1]);
		}
	}
}	


static int
convert_amd64_fp(
	struct type *to,
 	struct type *from, 
	struct vreg *ret,
	struct icode_list *il) {

	struct vreg	*fbvr;
	int		rc = 0;

	if (from->code == to->code) {
		/*
		 * 07/29/08: This didn't return the ``is long double''
		 * indicator, so the x87 register was not freed and the
		 * register stack filled up
		 *
		 * XXX This raises the question of why can a conversion
		 * of type T to itself (no-op) get this far and doesn't
		 * cause a very early return in icode_make_cast()?
		 */
		if (to->code == TY_LDOUBLE) {
			return 1;
		}
		return 0;
	}

	if (from->code == TY_LDOUBLE) {
		/*
		 * long double, resident in an x87 register, to
		 * float or double
		 */


#if ! REMOVE_FLOATBUF
		fbvr = n_xmemdup(&floatbuf, sizeof floatbuf);
#else
		fbvr = vreg_alloc(NULL,NULL,NULL,NULL);
#endif

		if (to->code == TY_DOUBLE) {
			fbvr->size = 8;
			fbvr->type = make_basic_type(TY_DOUBLE);
		} else {
			/* float */
			fbvr->size = 4;
			fbvr->type = make_basic_type(TY_FLOAT);
		}
		vreg_map_preg(fbvr, ret->pregs[0]);
#if ! REMOVE_FLOATBUF
		icode_make_store(curfunc, fbvr, fbvr, il);
		free_preg(fbvr->pregs[0], il, 1, 0);
#else
		free_preg(fbvr->pregs[0], il, 1, 1);
#endif

		/* Now into SSE register */
		vreg_faultin(NULL, NULL, fbvr, il, 0); 
		vreg_map_preg(ret, fbvr->pregs[0]);
	} else if (to->code == TY_LDOUBLE) {
		/*
		 * float or double, resident in an SSE register, to
		 * long double
		 */
		struct reg	*r;
		struct vreg	*tmp;

#if 0
		r = backend->alloc_fpr(curfunc, 12, il, NULL);
#endif
		r = &x86_fprs[0];

		/*
		 * 04/12/08: Fixed this
		 */
		tmp = dup_vreg(ret);
		tmp->type = from;
		tmp->size = backend->get_sizeof_type(from, NULL);
		vreg_map_preg(tmp, ret->pregs[0]);
		
		free_preg(/*ret->pregs[0]*/tmp->pregs[0], il, 1, 1); /* causes store */
		/*
		 * XXX hmm another temp adhoc vars :(
		 * There are lots of problems because we always
		 * work with ``ret'' which already has the target
		 * type set. We should use the source vreg more,
		 * which ahs the correct ype for loading
		 */
		tmp = dup_vreg(tmp /*, sizeof *ret*/);
		tmp->type = from;
		tmp->size = backend->get_sizeof_type(from, NULL);
 
		vreg_faultin_x87(r, NULL, tmp, il, 0);
		vreg_map_preg(ret, r);
		rc = 1;
	} else if (to->code == TY_DOUBLE) {
		icode_make_amd64_cvtss2sd(ret->pregs[0], il);
	} else { /* double to float */
		icode_make_amd64_cvtsd2ss(ret->pregs[0], il);
	}
	return rc;
}

/*
 * Most of the time, instructions give meaning to data. This function
 * generates code required to convert virtual register ``src'' to type
 * ``to'' where necessary
 */
static struct vreg *
icode_make_cast(struct vreg *src, struct type *to, struct icode_list *il) {
	struct reg		*r;
	struct reg		*r2;
	struct vreg		*ret;
	struct type		*from = src->type;
	struct type		*orig_to = to;
	size_t			size;
	int			res_is_x87_reg = 0;

	ret = src;
	if (ret->pregs[0] != NULL
		&& ret->pregs[0]->vreg == ret) {
		/* Item is already resident in a register */
		r = NULL;
	} else {
		/*
		 * Item is not resident yet so we get to choose
		 * a suitable register
		 */
#if 0
		if (IS_FLOATING(to->code)) {
			r = backend->alloc_fpr(curfunc, 0, il, NULL);
		} else {
			size = backend->get_sizeof_type(to, NULL);
			r = backend->alloc_gpr(curfunc, size, il, NULL);
		}	
#endif
		r = 0;
	}	

	if (is_x87_trash(ret)) {
		ret = x87_anonymify(ret, il);
		if (ret == src) {
			ret = n_xmemdup(ret, sizeof *ret);
		}	
	} else {
		if (ret->type->tlist != NULL
			|| (ret->type->code != TY_STRUCT
			&& ret->type->code != TY_UNION)) {
			vreg_anonymify(&ret, NULL, NULL /*r*/, il);
		}	

		if (ret == src) {
			/* XXX anonymify is broken */
			ret = vreg_disconnect(src);
		}	
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

	
	/*
	 * We may have to move the item to a different
	 * register as a result of the conversion
	 */
	if (is_floating_type(to)) {
		if (!is_floating_type(from)) {
			int	from_size;

			from_size = backend->get_sizeof_type(from, NULL);
			/*
			 * 04/17/08: Convert to 64bit integer, so that
			 * 64bit fildq is used instead of 32bit fild!
			 * This is necessary for large (unsigned) 32bit
			 * values that are otherwise not converted
			 * properly
			 */
			if (from_size < 8) {
				/* Need to sign-extend first*/
				struct vreg	*tmp =
					n_xmemdup(ret, sizeof *ret);
				tmp->size = 8;
				change_preg_size(tmp, il, /*to*/
					make_basic_type(TY_LLONG), from);
				ret = n_xmemdup(ret, sizeof *ret);
				vreg_map_preg(ret, tmp->pregs[0]);
				if (backend->arch == ARCH_X86) {
					vreg_map_preg2(ret, tmp->pregs[1]);
				}
				ret->type = make_basic_type(TY_LLONG);
				ret->size = 8;

				/*
				 * 07/24/08: This wrongly set the multi-reg
				 * flag for AMD64 as well
				 */
				if (backend->arch != ARCH_AMD64) {
					ret->is_multi_reg_obj = 2; 
				}
				from = ret->type;
			}

			/*
			 * 08/04/08: Don't perform x86-like u-integer to long
			 * double conversion for 64bit integers on AMD64
			 * anymore
			 */
			if (backend->arch == ARCH_X86
				&& from->code == TY_ULLONG) {
				/*
				 * 08/05/09: Request 4 bytes instead of 8.
				 * 8 byte requests are always treated as
				 * multi-register requests, but we only
				 * want to allocate a single register
				 * (since we already have ret->pregs[0]).
				 * So the next ALLOC_GPR() - which may be
				 * for a 16bit or 8bit item - would
				 * wrongly return a 32bit GPR
				 */
				struct reg	*temp =
					ALLOC_GPR(curfunc, /*8*/4, il, NULL);
				struct vreg	*tempfb;

				r = backend->alloc_fpr(curfunc,
					0, il, NULL);

				tempfb = dup_vreg(ret);
				vreg_map_preg(tempfb, ret->pregs[0]);
				vreg_map_preg2(tempfb, ret->pregs[1]);
				vreg_set_new_type(tempfb, from);
				free_preg(ret->pregs[0], il, 1, 1);
				icode_make_x86_fild(r, tempfb, il);

				icode_make_amd64_ulong_to_float(
					ret->pregs[1], /* pass upper dword as source reg */
					temp,
					r,
					to->code, /* is float */
					il);
				free_preg(temp, il, 1, 0);
				free_preg(ret->pregs[0], il, 1, 0);
				vreg_map_preg(ret, r);
				res_is_x87_reg = 1;
			} else if (backend->arch == ARCH_X86
				|| (to->code == TY_LDOUBLE
					&& (ret->pregs[0]->size <= 4
					|| from->sign != TOK_KEY_UNSIGNED))) {
				/* x87 kludgery */
#if ! REMOVE_FLOATBUF
				load_floatbuf(ret, il);
				free_preg(ret->pregs[0], il, 1, 0);
				if (ret->is_multi_reg_obj) {
					free_preg(ret->pregs[1], il, 1, 0);
				}	
#else
				struct vreg	*tempfb =
					load_integer_floatbuf(ret,from,il);

				free_preg(tempfb->pregs[0], il, 1, 0);
				if (tempfb->is_multi_reg_obj) {
					free_preg(tempfb->pregs[1], il, 1, 0);
				}	

tempfb->is_multi_reg_obj = 0;
#endif
#if 0
				r = backend->alloc_fpr(curfunc, 0, il, NULL);
#endif
				r = &x86_fprs[0];
#if ! REMOVE_FLOATBUF
				floatbuf.pregs[0] = NULL;
				vreg_faultin_x87(r, NULL, &floatbuf, il, 0);
				free_preg(ret->pregs[0], il, 1, 0);
#else
				tempfb->pregs[0] = NULL;
/*				vreg_faultin_x87(r, NULL, tempfb, il, 0);*/
		tempfb = dup_vreg(tempfb);
		vreg_set_new_type(tempfb, from);
				icode_make_x86_fild(r, tempfb, il);

#endif
ret = dup_vreg(ret);
				vreg_map_preg(ret, r);
				ret->size = backend->get_sizeof_type(to, NULL);
				res_is_x87_reg = 1;
ret->stack_addr = NULL;				
			} else {
				/*
				 * SSE (AMD64) integer to floating point
				 * conversion
				 */ 
				if (ret->pregs[0]->size > 4) {
					/*
					 * 64bit int to fp conversion.
					 *
					 * 04/11/08: Use qword SSE
					 * instructions instead of the
					 * utter x87 nonsense. There was
					 * a comment here that said 64bit
					 * conv instructions don't exist,
					 * maybe they were overlooked?
					 */
					if (to->code == TY_LDOUBLE) {
						struct reg	*temp =
							ALLOC_GPR(curfunc, 8, il, NULL);
						struct vreg *tempfb;

						/*
						 * Note that we can only get
						 * here for unsigned 64bit
						 * integers
						 */
						r = backend->alloc_fpr(curfunc,
							16, il, NULL);
						/*
						 * 08/02/08: Unsigned long to
						 * float is a bit more 
						 * complicated than we made it
						 * out to be
						 */
/*						free_preg(ret->pregs[0], il, 1, 1);*/
						tempfb = dup_vreg(ret);
						vreg_map_preg(tempfb, ret->pregs[0]);
						vreg_set_new_type(tempfb, from);
						free_preg(ret->pregs[0], il, 1, 1);
						icode_make_x86_fild(r, tempfb, il);

						icode_make_amd64_ulong_to_float(
							ret->pregs[0],
							temp,
							r,
							TY_LDOUBLE, /* is double */
							il);
						free_preg(temp, il, 1, 0);
						vreg_map_preg(ret, r);
						ret->size = backend->get_sizeof_type(to, NULL);
						res_is_x87_reg = 1;
					} else if (to->code == TY_DOUBLE) {
						r = backend->alloc_fpr(curfunc,
							8, il, NULL);
						if (from->sign == TOK_KEY_UNSIGNED) {
							/*
							 * 08/02/08: Unsigned long to
							 * float is a bit more 
							 * complicated than we made it
							 * out to be
							 */
							struct reg	*temp =
								ALLOC_GPR(curfunc, 8, il, NULL);
							icode_make_amd64_ulong_to_float(
								ret->pregs[0],
								temp,
								r,
								TY_DOUBLE, /* is double */
								il);
							free_preg(temp, il, 1, 0);
						} else {
							icode_make_amd64_cvtsi2sdq(
								r, ret, il);
						}
					} else {
						/* Has to be float */
						/*
						 * 08/02/08: Unsigned long to
						 * float is a bit more 
						 * complicated than we made it
						 * out to be
						 */
						r = backend->alloc_fpr(curfunc,
							4, il, NULL);


						if (from->sign == TOK_KEY_UNSIGNED) {
							struct reg	*temp =
								ALLOC_GPR(curfunc, 8, il, NULL);
							icode_make_amd64_ulong_to_float(
								ret->pregs[0],
								temp,
								r,
								TY_FLOAT, /* is float */
								il);
							free_preg(temp, il, 1, 0);
						} else {
							icode_make_amd64_cvtsi2ssq(
								r, ret, il);
						}
					}
					free_preg(ret->pregs[0], il, 1, 0);
					vreg_map_preg(ret, r);
				} else {
					if (to->code == TY_DOUBLE) {
						r = backend->alloc_fpr(curfunc,
							backend->get_sizeof_type
							(to, NULL), il, NULL);
						icode_make_amd64_cvtsi2sd(
							r, ret, il);
						res_is_x87_reg = 1;
					} else {
						/* Has to be float */
						r = backend->alloc_fpr(curfunc,
							ret->size, il, 0);
						icode_make_amd64_cvtsi2ss(
							r, ret, il);
					}
					free_preg(ret->pregs[0], il, 1, 0);
					vreg_map_preg(ret, r);
				}
			}
		} else if (backend->arch == ARCH_AMD64
			|| sysflag == OS_OSX) {
			/*
			 * On AMD64, the item may be in an x87 or
			 * SSE register, and has to be moved into
			 * SSE or x87, respectively
			 */
			if (is_x87_trash(src)) {
				vreg_faultin_x87(NULL, NULL, src, il, 0);
				vreg_map_preg(ret, src->pregs[0]);
#if 0
				free_preg(vrtmp->pregs[0], il, 1, 1);
#endif
			}
			res_is_x87_reg = convert_amd64_fp(to, from, ret, il);
		} else {
			/*
			 * x87 to x87... this is not a no-op anymore! Because:
			 * the source fp value is stored on the stack, so we
			 * have to load it to a register and create a new
			 * stack buffer of different size to store it
			 * (remember we never want to keep stuff in x87 regs)
			 */
			struct vreg	*vrtmp;

			vreg_faultin_x87(NULL, NULL, src, il, 0);
			vrtmp = vreg_alloc(NULL,NULL,NULL,NULL);
			vrtmp->type = to;
			vrtmp->size = backend->get_sizeof_type(to, NULL);
			vreg_map_preg(vrtmp, src->pregs[0]);
			free_preg(vrtmp->pregs[0], il, 1, 1);
			ret = vrtmp;
		}
	} else if (is_floating_type(from)) {
		if (!is_floating_type(to)) {
			if ((backend->arch == ARCH_X86 && sysflag != OS_OSX)
				|| from->code == TY_LDOUBLE) {
				/*
			 	 * We have to change the status control word,
			 	 * perform the conversion by writing the value
			 	 * to the float buffer, then save it in a GPR,
			 	 * then reset the CW
				  */
#if REMOVE_FLOATBUF
				struct vreg		*tempfb;
				struct stack_block	*sb;
#endif
				store_x87cw(il);
				modify_x87cw(il);
				load_x87cw(&x87cw_new, il);
				size = backend->get_sizeof_type(to, NULL);

				vreg_faultin_x87(NULL, NULL, src, il, 0);
				vreg_map_preg(ret, src->pregs[0]);
				src->pregs[0] = NULL;
#if ! REMOVE_FLOATBUF
				load_floatbuf(ret, il);
				free_preg(floatbuf.pregs[0], il, 1, 0);
				if (ret->is_multi_reg_obj) {
					free_preg(floatbuf.pregs[1], il, 1, 0);
				}
#else
			ret  = dup_vreg(ret);
			vreg_set_new_type(ret, to);
				tempfb = load_floatval_floatbuf(ret, from, il);
#if 0
				free_preg(tempfb->pregs[0], il, 1, 0);
				if (ret->is_multi_reg_obj) {
					free_preg(tempfb->pregs[1], il, 1, 0);
				}
#endif
#endif

#if 0
				floatbuf.pregs[0] = NULL;
#endif

				if (size < 4) {
					/*
					 * fistp cannot output shorts or chars -
					 * so get an int and convert it
				 	 */

					r = alloc_16_or_32bit_noesiedi(curfunc,
						4, il, NULL);
				} else {
					r = ALLOC_GPR(curfunc, size, il, NULL);
				}

				if (backend->arch == ARCH_X86
					&& IS_LLONG(to->code)
					&& to->tlist == NULL) {
					r2 = ALLOC_GPR(curfunc, size, il, NULL);
				} else {
					r2 = NULL;
				}	
#if ! REMOVE_FLOATBUF
				fbvr = n_xmemdup(&floatbuf, sizeof floatbuf);
				if (size > 4 && backend->arch == ARCH_AMD64) {
					fbvr->size = 8;
					fbvr->type = make_basic_type(TY_LONG);
				} else {
					fbvr->size = 4;
					fbvr->type = make_basic_type(TY_INT);
				}

				vreg_faultin(r, r2, fbvr, il, 0);
#else
				sb = tempfb->stack_addr;

				tempfb = vreg_alloc(NULL,NULL,NULL,NULL);
				if (size > 4 && backend->arch == ARCH_AMD64) {
					tempfb->size = 8;
					tempfb->type = make_basic_type(TY_LONG);
				} else if (size == 8) {
					/*
					 * 06/04/08: This was missing - why? It
					 * broke double to long long conversion
					 * since the long was treated as two
					 * individual ints instead of one llong
					 */
					tempfb->type = make_basic_type(TY_LLONG);
					tempfb->size = 8;
					tempfb->is_multi_reg_obj = 2;
				} else {
					tempfb->size = 4;
					tempfb->type = make_basic_type(TY_INT);
				}
				tempfb->stack_addr = sb;

				vreg_faultin(r, r2, tempfb, il, 0);
#endif

				if (size < 4) {
					if (size == 1) {
						free_preg(r, il, 0, 0);
						r = r->composed_of[0]
							->composed_of[/*0*/1];
					} else {	
						/* 2 */
						free_preg(r, il, 0, 0);
						r = r->composed_of[0];
					}
				}	
				vreg_map_preg(ret, r);
				if (r2 != NULL) {
					vreg_map_preg2(ret, r2);
				}	
				load_x87cw(&x87cw_old, il);
			} else {
				/*
				 * SSE (AMD64) floating point to integer
				 * conversion
				 */
				int	siz;
				int	to_quad = 0;
				int	is_64bit = 0;


				/*
				 * 08/01/08: When converting to unsigned
				 * 32bit integers, we first have to convert
				 * to a 64bit integer, then chop off the
				 * desired part!
				 */
				if (backend->arch == ARCH_X86) {
					/*
					 * 02/15/09: SSE on x86 (for OSX) cannot use
					 * 64bit GPRs, so for now we just always use
					 * 32bit results
					 */
					is_64bit = 0;
					to_quad = 0;
				} else {
					if (!IS_LONG(to->code) && !IS_LLONG(to->code)) {
						is_64bit = 0;
						if (to->sign == TOK_KEY_UNSIGNED) {
							to_quad = 1;
						}
					} else {
						is_64bit = 1;
						to_quad = 1;
					}
				}

				r = ALLOC_GPR(curfunc, to_quad? 8: 4, il, NULL);
				if (from->code == TY_DOUBLE) {
					if (to_quad) {
						icode_make_amd64_cvttsd2siq(
							r, ret->pregs[0], il);
					} else {
						icode_make_amd64_cvttsd2si(
							r, ret->pregs[0], il);
					}
				} else {
					if (to_quad) {
						icode_make_amd64_cvttss2siq(
							r, ret->pregs[0], il);
					} else {
						icode_make_amd64_cvttss2si(
							r, ret->pregs[0], il);
					}
				}
				siz = backend->get_sizeof_type(to, NULL);
				/*
				 * 08/01/08: < 4 instead of == 4
				 */
				if (siz < 4 || (to_quad && !is_64bit)) {
					struct reg	*r2;

					r2 = ALLOC_GPR(curfunc, siz, il, NULL);
					icode_make_copyreg(r2, r, to,
						to->sign !=
							TOK_KEY_UNSIGNED?
							make_basic_type(TY_INT)
							: make_basic_type(TY_UINT),
						il);
					free_preg(r, il, 0, 0);
					r = r2;
				}
				
				vreg_map_preg(ret, r);
				if (backend->arch == ARCH_X86 && IS_LLONG(to->code)) {
					/*
					 * The result is 32bit, so sign- or zero-extend
					 * it if we are converting to long long
					 */
					change_preg_size(ret, il, to, make_basic_type(TY_INT));
				}
			}
		} else if (backend->arch == ARCH_AMD64) {
			/*
			 * x87 vs SSE maybe?
			 */
			res_is_x87_reg = convert_amd64_fp(to, from, ret, il);
		}
	} else if (ret->pregs[0]->size != ret->size && to->code != from->code) {
		/*
		 * XXX change_preg_size() was being called for ``long long''
		 * versus ``unsigned long long'' because the preg size check
		 * above yields 4 for those types!
		 * Thus only call the function if one or both types are not
		 * llong
		 */
		if ( (!IS_LLONG(from->code) || from->tlist != NULL)
			|| (!IS_LLONG(to->code) || to->tlist != NULL)  ) {	
			change_preg_size(ret, il, to, from);
		}	
	}

	to = orig_to; /* because of uintptr_t stuff */
	ret->type = to;
	ret->size = backend->get_sizeof_type(to, NULL);

	if (res_is_x87_reg) {
		/*
		 * Save to stack so that the god awful  x87 regs are
		 * all free
		 */
		ret->is_multi_reg_obj = 0;
		vreg_map_preg(ret, ret->pregs[0]);
		free_preg(ret->pregs[0], il, 1, 1);
		ret->pregs[0] = NULL;
	} else if (ret->pregs[0] != NULL) {
		/*
		 * The non-null check is to avoid mapping to a null
		 * pointer register, which can happen if source and
		 * target type are x87 fp types, such that no
		 * conversion is actually performed and no register
		 * is ever loaded
		 */
		vreg_map_preg(ret, ret->pregs[0]);
	}	

	/* Update multi-register information */
	if (backend->arch == ARCH_X86
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
icode_initialize_pic(struct function *f, struct icode_list *il) {
	/*
	 * We only have to do the first initialization, because ebx is
	 * callee-save, and so even after function calls it remains
	 * loaded with the GOT address
	 */
	if (!f->pic_initialized) {
		free_preg(&x86_gprs[1], il, 1, 1);
		reg_set_unallocatable(&x86_gprs[1]);
		f->callee_save_used |= CSAVE_EBX;
		icode_make_initialize_pic(f, il);
	}
}	

static void
icode_complete_func(struct function *f, struct icode_list *il) {
	(void) il;

	if (f->pic_initialized) {
		/* PIC register ebx was used - free it again */
		reg_set_allocatable(&x86_gprs[1]);
		x86_gprs[1].used = 0;
	}
}

static void
do_print_gpr(struct reg *r) {
	printf("%s=%d(%d) ", r->name, r->used, reg_allocatable(r));
	if (r->vreg && r->vreg->pregs[0] == r) {
		printf("<-> %p", r->vreg);
	}
}	

static void
debug_print_gprs(void) {
	int	i;
	
	for (i = 0; i < 6; ++i) {
		printf("\t\t");
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
}

static int
is_multi_reg_obj(struct type *t) {
	return (t->tlist == NULL && IS_LLONG(t->code))? 2: 0;
}	

static struct reg *
name_to_reg(const char *name) {
	int		i;
	size_t		len;

	if (*name == '%') ++name;

	if (strncmp(name, "st", 2) == 0) {
		/* Floating point registers */
		if (name[2] == 0) {
			/* st = st(0) */
			return &x86_fprs[0];
		} else if (name[2] != '(' || name[4] != ')'
			|| name[5] != 0 || !isdigit((unsigned char)name[3])
			|| name[3] > '7') {
			return NULL;
		} else {
			return &x86_fprs[name[3] - '0'];
		}
	} else if ((len = strlen(name)) == 2) {
		if (name[1] == 'i') {
			if (strcmp(x86_gprs[4].name, name) == 0) {
				return &x86_gprs[4];
			} else if (strcmp(x86_gprs[5].name, name) == 0) {
				return &x86_gprs[5];
			}
		}
		for (i = 0; i < 4; ++i) {
			if (name[1] == 'x') {
				/* Must be 16bit */
				if (strcmp(x86_gprs[i].composed_of[0]->name,
					name) == 0) {
					return x86_gprs[i].composed_of[0];
				}	
			} else {
				/* Must be 8bit */
				if (strcmp(x86_gprs[i].composed_of[0]->
					composed_of[0]->name, name) == 0) {
					return x86_gprs[i].composed_of[0]
						->composed_of[0];
				}
				if (strcmp(x86_gprs[i].composed_of[0]->
					composed_of[0]->name, name) == 0) {
					return x86_gprs[i].composed_of[0]
						->composed_of[0];
				}
			}	
		}
		if (strcmp(x86_esp.composed_of[0]->name, name) == 0) {
			return x86_esp.composed_of[0];
		}
		if (strcmp(x86_ebp.composed_of[0]->name, name) == 0) {
			return x86_esp.composed_of[0];
		}
	} else if (len == 3) {
		for (i = 0; i < N_GPRS; ++i) {
			if (strcmp(x86_gprs[i].name, name) == 0) {
				return &x86_gprs[i];
			}
		}
		if (strcmp(x86_esp.name, name) == 0) {
			return &x86_esp;
		} else if (strcmp(x86_ebp.name, name) == 0) {
			return &x86_ebp;
		}
		if (backend->arch == ARCH_AMD64) {
			for (i = 0; i < N_GPRS; ++i) {
				if (strcmp(amd64_x86_gprs[i].name, name) == 0) {
					return &amd64_x86_gprs[i];
				}
			}
			for (i = 0; i < 8; ++i) {
				if (strcmp(amd64_gprs[i].name, name) == 0) {
					return &amd64_gprs[i];
				}
			}
		}	
	}	
	return NULL;
}

/*
 * Get suitably sized register for storing item vr, where ch dictates which
 * 32bit register to choose from. For use with inline asm constraints
 *
 * XXX this does handle the amd64, but not completely
 */
static struct reg *
asmvreg_to_reg(
	struct vreg **vr0,
	int ch,
	struct inline_asm_io *io,
	struct icode_list *il,
	int faultin) {

	struct reg      *r = NULL;
	struct vreg	*vr = *vr0;
	size_t          size = vr->size;
	struct vreg	*newvr;

	if ((vr->type->code == TY_STRUCT || vr->type->code == TY_UNION)
		&& vr->type->tlist == NULL) {
		errorfl(io->expr->tok,
			"Cannot load struct/union into register");
		return NULL;
	} else if (IS_LLONG(vr->type->code)
		&& vr->type->tlist == NULL
		&& backend->arch == ARCH_X86) {
		errorfl(io->expr->tok,
			"Cannot load long long into register");
		return NULL;
	} else if (vr->type->tlist != NULL) {
		size = backend->arch == ARCH_AMD64? 8: 4;
	}	

	/*
	 * For a/b/c/d/S/D input must be moved to a specific register. For
	 * q more or less as well, and for r to any GPR
	 */
	if (ch == 'b') {
		curfunc->callee_save_used |= CSAVE_EBX;
	} else if (ch == 'S') {
		curfunc->callee_save_used |= CSAVE_ESI;
	} else if (ch == 'D') {
		curfunc->callee_save_used |= CSAVE_EDI;
	}
	switch (ch) {
	case 'a': /* eax */
		if (backend->arch == ARCH_AMD64) r = &amd64_x86_gprs[0];
		else r = &x86_gprs[0];
		break;
	case 'b': /* ebx */
		if (backend->arch == ARCH_AMD64) r = &amd64_x86_gprs[1];
		else r = &x86_gprs[1];
		break;
	case 'c': /* ecx */
		if (backend->arch == ARCH_AMD64) r = &amd64_x86_gprs[2];
		else r = &x86_gprs[2];
		break;
	case 'd': /* edx */
		if (backend->arch == ARCH_AMD64) r = &amd64_x86_gprs[3];
		else r = &x86_gprs[3];
		break;
	case 'S': /* esi */
		if (backend->arch == ARCH_AMD64) r = &amd64_x86_gprs[4];
		else r = &x86_gprs[4];
		break;
	case 'D': /* edi */
		if (backend->arch == ARCH_AMD64) r = &amd64_x86_gprs[5];
		else r = &x86_gprs[5];
		break;
	case 'q': 
	case 'Q':
		/* XXX amd64 */
		/* Must be any of eax/ebx/ecx/edx - exclude esi/edi */
		if (backend->arch == ARCH_X86) {
			r = alloc_16_or_32bit_noesiedi(curfunc, 0, il, NULL);
		} else {
			/* XXX maybe need 64bit x86 allocator :-( */
			r = x86_backend.alloc_gpr(curfunc, 0, il, NULL, 0);
			if (is_member_of_reg(&amd64_x86_gprs[0], r)) {
				r = &amd64_x86_gprs[0];
			} else if (is_member_of_reg(&amd64_x86_gprs[1], r)) {
				r = &amd64_x86_gprs[1];
			} else if (is_member_of_reg(&amd64_x86_gprs[2], r)) {
				r = &amd64_x86_gprs[2];
			} else if (is_member_of_reg(&amd64_x86_gprs[3], r)) {
				r = &amd64_x86_gprs[3];
			}
		}
		break;
	case 'r':
		if (size == 1) {
			if (backend->arch == ARCH_X86) {
				/* esi/edi have no 1byte sub registers ... */
				r = alloc_16_or_32bit_noesiedi(curfunc, 1,
					il, NULL);
			} else {
				/* amd64 */
				r = ALLOC_GPR(curfunc, 1, il, NULL);
			}
		} else {
			if (backend->arch == ARCH_X86) {
				r = alloc_16_or_32bit_reg(curfunc, size,
					il, NULL);
			} else {
				r = ALLOC_GPR(curfunc, size, il, NULL);
			}
		}
		break;
	default:
		printf("BAD CHAR FOR asmvreg_to_reg(): %c(%d)\n", ch, ch);
		abort();
	}
	
	if (r == NULL) {
		errorfl(io->expr->tok, "Too many inline asm operands - "
			"cannot allocate register");
		return NULL;
	} else if (faultin && !reg_allocatable(r)) {
		/*
		 * XXX this isn't quite correct... use of ``faultin'' above
		 * causes output registers to be assigned even if those are
		 * used for input, which is good. Problem is that clobbered
		 * registers should not be used for output.
		 */
		errorfl(io->expr->tok, "Cannot allocate %s (in clobber list?)",
			r->name);
		return NULL;
	}	
	free_preg(r, il, 1, 1);
	if (size == 1 && (ch == 'S' || ch == 'D')
		&& backend->arch == ARCH_X86) {
		errorfl(io->expr->tok,
			"Cannot store 1-byte item to "
			"%s", r->name);
		return NULL;
	} else if (size != r->size) {
		if (r->size == 8) {
			/* amd64 */
			r = r->composed_of[0];
		}

		if (size == 1) {
			r = r->composed_of[0]->composed_of[0];
		} else if (size == 2) {
			r = r->composed_of[0];
		} else if (size == 4) {
			/* amd64 - 64 to 32 bit, already done above */
			;
		}
	}

	newvr = vreg_disconnect(vr);
	
	if (faultin) {
		vreg_faultin(r, NULL, newvr, il, 0);
		reg_set_unallocatable(r);
	}
	*vr0 = newvr;
	return r;
}

static char *
get_inlineasm_label(const char *tmpl) {
	char	*ret = n_xmalloc(strlen(tmpl) + sizeof "inlasm");
	sprintf(ret, "inlasm%s", tmpl);
	return ret;
}	

/*
 * Print inline asm instruction operand
 */
void
print_asmitem_x86(FILE *out, void *item, int item_type, int postfix, int a) {
	char			*p = NULL;
	struct reg		*r = NULL;
	struct gas_token	*gt;
	struct inline_asm_io	*io;
	int			idx;
	int			applied_constraint = 0;

	switch (item_type) {
	case ITEM_NUMBER:
		if (a == TO_GAS) x_fputc('$', out); 
		gt = item;
		p = gt->data;
		break;
	case ITEM_REG:
		if (a == TO_GAS) x_fputc('%', out); 
		gt = item;
		p = gt->data;
		break;
	case ITEM_SUBREG_B:
	case ITEM_SUBREG_H:
	case ITEM_SUBREG_W:
		io = item;
		if (io->outreg) {
			r = io->outreg;
		} else if (io->inreg) {
			r = io->inreg;
		} else {
			r = io->vreg->pregs[0];
		}	
		if (r == NULL/* || r->vreg != io->vreg*/) { /* XXX!!! */
			errorfl(io->expr->tok,
			"Operand not in register but used with %h or %b");
			return;
		}

		if (backend->arch == ARCH_X86) {
			if (!is_member_of_reg(&x86_gprs[0], r)
				&& !is_member_of_reg(&x86_gprs[1], r)
				&& !is_member_of_reg(&x86_gprs[2], r)
				&& !is_member_of_reg(&x86_gprs[3], r)) {
				errorfl(io->expr->tok,
		"`%s' does not have a 8bit register for use with %%h or %%b",
					r->name);
				return;
			}
		} else {
			/* AMD64 */
			int	i;

			for (i = 0; i < 4; ++i) {
				if (is_member_of_reg(&amd64_x86_gprs[i], r)) {
					break;
				}
			}
			if (i == 4) {
				for (i = 8; i < 16; ++i) {
					if (is_member_of_reg(&amd64_gprs[i],
						r)) {
						errorfl(io->expr->tok,
				"`%s' doesn't make sense with %%h or %%b",
					r->name);
						return;
					}
				}
				if (i == 16) {
					errorfl(io->expr->tok,
		"`%s' does not have a 8bit register for use with %h or %b",
						r->name);
					return;
				}
			}
		}
		if (item_type == ITEM_SUBREG_B) {
			idx = 1;
		} else {
			idx = 0;
		}	
		if (r->size == 2) {
			if (item_type == ITEM_SUBREG_W) {
				; /* OK - already 16bit */
			} else {
				r = r->composed_of[idx];
			}
		} else if (r->size == 1) {
			/*
			 * XXX this unimpl() was probably here because
			 * I didn't know what this means if used with
			 * 8 bit regs!
			 */
#if 0
			unimpl();
#endif
		} else {
			/* Must be 4 */
			if (item_type == ITEM_SUBREG_W) {
				r = r->composed_of[0];
			} else {
				r = r->composed_of[0]->composed_of[idx];
			}
		}
		if (a == TO_GAS) x_fputc('%', out); 
		x_fprintf(out, "%s", r->name);
		break;
	case ITEM_VARIABLE:
		gt = item;
		if (a == TO_NASM) {
			x_fputc('$', out);
		}	
		p = gt->data;
		break;
	case ITEM_LABEL:
		x_fprintf(out, ".%s", item);
		break;
	case ITEM_INPUT:
	case ITEM_OUTPUT:
		io = item;
		for (p = io->constraints; *p != 0; ++p) {
			struct vreg	*vr = io->vreg;
			

			r = NULL;
			/*
			 * If this constraint uses a register (even with
			 * "m" we may have a register holding a pointer
			 * value), map it to the vreg
			 */
			if (strchr("qrabcdSDm", *p) != 0) {
				if (item_type == ITEM_INPUT) {
					r = io->inreg;
				} else {
					/* Output */
					r = io->outreg;
				}
				if (vr->from_ptr != NULL) {
					/*
					 * Register is pointer value
					 */
					backend_vreg_map_preg(vr->from_ptr, r);
				} else {
					/* Register references vreg */
					backend_vreg_map_preg(vr, r);
				}
			}
			
			if (*p == '+' || *p == '=' || *p == '&') {
				continue;
			} else if (applied_constraint) {
				/*
				 * 05/17/09: For things like "rm", after
				 * having chosen r, we do not want to print
				 * an m item as well. Because it's just one
				 * operand.
				 * XXX Here we always use the first one,
				 * i.e. r in "rm" and m in "mr". Probably
				 * should pick it depending on the other
				 * instruction operands
				 */
				continue;
			} else if (strchr("qrabcdSD", *p) != 0) {
				if (a == TO_GAS) x_fputc('%', out); 
				if (item_type == ITEM_INPUT) {
					r = io->inreg;
					x_fprintf(out, "%s",
						r->name);
				} else {
					/* output */
					r = io->outreg;
					x_fprintf(out, "%s", r->name);
				}	
			} else if (*p == 'm') {
				if (postfix != 0 && a == TO_NASM) {
					char	*p = NULL;

					switch (postfix) {
					/*
					 * XXX what about floating point
					 * l and t :(
					 */
					case 'b': p = "byte"; break;
					case 'w': p = "word"; break;
					case 'l': p = "dword"; break;
					case 'q': p = "qword"; break;
					default:
						  unimpl();
					}	
					x_fprintf(out, "%s ", p);
				}
				emit->print_mem_operand(io->vreg, NULL);
			} else if (*p == 'i') {
				if (eval_const_expr(io->expr, 0, NULL) != 0) {
					return;
				}
				if (io->vreg->type->sign != TOK_KEY_UNSIGNED) {
#if 0
					x_fprintf(out, "%ld",
						*(long *)io->expr->const_value
						->value);
#endif
					cross_print_value_by_type(out,
						io->expr->const_value->value,
						TY_LONG, 'd');
				} else {
#if 0
					x_fprintf(out, "%lu",
						*(long *)io->expr->const_value
						->value);
#endif
					cross_print_value_by_type(out,
						io->expr->const_value->value,
						TY_ULONG, 'd');
				}
			} else if (*p == 'o') {
				unimpl();
			} else if (*p == 'v') {
				unimpl();
			} else {
				printf("WHA?? %c\n", *p);
				unimpl();
			}
			applied_constraint = 1;

			if (r != NULL) {
				backend_vreg_unmap_preg(r);
			}
		}
		p = NULL;
	}

	if (p != NULL) {
		x_fprintf(out, "%s", p);
	}
}

int
x86_have_immediate_op(struct type *ty, int op) {
	if (Oflag == -1) { /* XXX really want this here? */
		return 0;
	}
	if (op == TOK_OP_BSHL
		|| op == TOK_OP_BSHR
		|| op == TOK_OP_BAND
		|| op == TOK_OP_BOR
		|| op == TOK_OP_BXOR
		|| op == TOK_OP_COBSHL
		|| op == TOK_OP_COBSHR
		|| op == TOK_OP_COBOR
		|| op == TOK_OP_COBAND
		|| op == TOK_OP_COBXOR) {
		if (backend->arch == ARCH_X86
			&& IS_LLONG(ty->code)) {
			return 0;
		}
		return 1;
	}
	return 0;
}

struct backend x86_backend = {
	ARCH_X86,
	0, /* ABI */
	0, /* multi_gpr_object */
	4, /* structure alignment */
	1, /* need pic initialization (ebx) */
	0, /* emulate long double */
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
	&x86_esp,
	invalidate_gprs,
	invalidate_except,
	alloc_gpr,
	alloc_16_or_32bit_noesiedi,
	alloc_fpr,
	x86_free_preg,
	icode_make_fcall,
	icode_make_return,
	NULL,
	icode_prepare_op,
	NULL, /* prepare_load_addrlabel */
	icode_make_cast,
	NULL, /* icode_make_structreloc */
	icode_initialize_pic,
	icode_complete_func,
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

