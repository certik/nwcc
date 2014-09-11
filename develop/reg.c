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
#include "reg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include "scope.h"
#include "decl.h"
#include "type.h"
#include "functions.h"
#include "control.h"
#include "debug.h"
#include "token.h"
#include "error.h"
#include "functions.h"
#include "icode.h"
#include "stack.h"
#include "zalloc.h"
#include "reg.h"
#include "backend.h"
#include "subexpr.h"
#include "cc1_main.h"
#include "features.h"
#include "n_libc.h"

int
reg_unused(struct reg *r) {
	struct reg	**comp;
	int		i;

	if (r->used) return 0;

	comp = r->composed_of;
	if (comp == NULL) {
		return 1;
	}
	for (i = 0; comp[i] != NULL; ++i) {
		if (!reg_unused(comp[i])) {
			return 0;
		}
	}
	return 1;
}

int
reg_allocatable(struct reg *r) {
	struct reg	**comp;
	int		i;

	if (!r->allocatable) return 0;
	comp = r->composed_of;
	if (comp == NULL) {
		return 1;
	}
	if (r->size == 8) {
		/* AMD64 */
		return reg_allocatable(r->composed_of[0]);
	}

	for (i = 0; comp[i] != NULL; ++i) {
		if (!reg_unused(comp[i])
			&& !reg_allocatable(comp[i])) {
			return 0;
		}
	}
	return 1;
}


void
reg_set_allocatable(struct reg *r) {
	/*
	 * XXX for some reason gpr0 always becomes allocatable
	 * somewhere :-(, and stuff like   stw foo, 0(0) breaks.
	 * Here's a kludge to fix this for now.
	 * To debug this, avoid calling free_preg() on gpr0 in
	 * change_preg_size(), and then throw in an abort() in
	 * free_preg() if the operand is ever gpr0
	 *
	 *          ---> To hell with PowerPC!!! <---
	 */
	if (r == &power_gprs[0]) {
		return;
	}
#ifdef DEBUG6
	debug_log_regstuff(r, r->vreg, DEBUG_LOG_ALLOCATABLE);
#endif
	r->allocatable = 1;
}

void
reg_set_unallocatable(struct reg *r) {
#ifdef DEBUG6
	debug_log_regstuff(r, r->vreg, DEBUG_LOG_UNALLOCATABLE);
#endif
	r->allocatable = 0;
}

/*
 * 11/26/08: Mark register as dedicated to avoid it ever being used for
 * register allocation and saving (e.g. for stack pointer/frame pointer
 * registers)
 */
void
reg_set_dedicated(struct reg *r) {
	reg_set_unallocatable(r);
	r->used = 0;
	r->dedicated = 1;
}

int
is_x87_trash(struct vreg *vr) {
	if (sysflag == OS_OSX) {
		if (vr->type->code != TY_LDOUBLE) {
			return 0;
		}
	}
	if (is_floating_type(vr->type)
		&& (backend->arch == ARCH_X86
			|| (vr->type->code == TY_LDOUBLE
				&& backend->arch == ARCH_AMD64))) {
		return 1;
	}
	return 0;
}

void
vreg_set_unallocatable(struct vreg *vr) {
	if (vr->pregs[0] && vr->pregs[0]->vreg == vr) {
		reg_set_unallocatable(vr->pregs[0]);
		if (vr->is_multi_reg_obj) {
			reg_set_unallocatable(vr->pregs[1]);
		}
	}
	if (vr->from_ptr != NULL
		&& vr->from_ptr->pregs[0] != NULL
		&& vr->from_ptr->pregs[0]->vreg == vr->from_ptr) {
		reg_set_unallocatable(vr->from_ptr->pregs[0]);
	} else if (vr->parent) {
		struct vreg	*vr2 = get_parent_struct(vr);

		if (vr2->from_ptr
			&& vr2->from_ptr->pregs[0]
			&& vr2->from_ptr->pregs[0]->vreg == vr2->from_ptr) {
			reg_set_unallocatable(vr2->from_ptr->pregs[0]);
		}
	}
}

void
vreg_set_allocatable(struct vreg *vr) {
	if (vr->pregs[0] && vr->pregs[0]->vreg == vr) {
		reg_set_allocatable(vr->pregs[0]);
		if (vr->is_multi_reg_obj) {
			reg_set_allocatable(vr->pregs[1]);
		}
	}	
	if (vr->from_ptr != NULL
		&& vr->from_ptr->pregs[0] != NULL
		&& vr->from_ptr->pregs[0]->vreg == vr->from_ptr) {
		reg_set_allocatable(vr->from_ptr->pregs[0]);
	} else if (vr->parent) {
		struct vreg	*vr2 = get_parent_struct(vr);

		if (vr2->from_ptr
			&& vr2->from_ptr->pregs[0]
			&& vr2->from_ptr->pregs[0]->vreg == vr2->from_ptr) {
			reg_set_allocatable(vr2->from_ptr->pregs[0]);
		}
	}
}	

static struct vreg *
alloc_vreg(void) {
	struct vreg	*res;

#if USE_ZONE_ALLOCATOR
	res = zalloc_buf(Z_VREG);
#else
	struct vreg	*ret = n_xmalloc(sizeof *ret);
	static struct vreg	nullvreg;
	*ret = nullvreg;
	res = ret;
#endif

#if VREG_SEQNO
	{
		static int	seqno;
		res->seqno = ++seqno;
	}
#endif
	return res;
}

struct vreg *
dup_vreg(struct vreg *vr) {
	struct vreg	*ret = alloc_vreg();
#if VREG_SEQNO
	int		saved_seqno = vr->seqno;
#endif
	*ret = *vr;
#if VREG_SEQNO
	ret->seqno = saved_seqno;
#endif

	return ret;
}	


struct vreg *poi;


struct vreg *
vreg_alloc(struct decl *dec, struct token *constant, struct vreg *from_ptr,
	struct type *ty0) {

	struct vreg	*vreg;
	struct type	*ty;

	vreg = alloc_vreg();

	if (dec != NULL) {
		/* Variable-backed */
		vreg->var_backed = dec;
		/*
		 * 05/26/11: Don't set size member to 0 for pointers to VLAs
		 * (int (*p)[N]) because those do have a known size - the
		 * standard pointer size
		 */
		if (IS_VLA(dec->dtype->flags)
			&& is_immediate_vla_type(dec->dtype)) {
			vreg->size = 0;
		} else {	
			/* 07/09/10: Array has ptr size as far as vregs are
			 * concerned */  
			if (dec->dtype->tlist != NULL) {
				vreg->size = backend->get_ptr_size();
			} else {	
				vreg->size = backend->get_sizeof_type(dec->dtype, NULL);
			}
		}	
		vreg->type = dec->dtype;
	} else if (from_ptr != NULL) {	
		vreg->from_ptr = from_ptr;

		/* Get size of what pointer points to */
		ty = n_xmemdup(from_ptr->type, sizeof *ty);
		copy_tlist(&ty->tlist, ty->tlist->next);
		vreg->type = ty;

		if (IS_VLA(ty->flags) && is_immediate_vla_type(ty)) {
			vreg->size = 0;
		} else {	
			if (ty->tlist != NULL) {
				vreg->size = backend->get_ptr_size();
			} else {	
				vreg->size = backend->get_sizeof_type(ty, NULL);
			}
		}
	} else if (constant != NULL) {
		/* Const-backed */
		vreg->from_const = constant;
		vreg->size = backend->get_sizeof_const(constant);
		vreg->type = n_xmalloc(sizeof *vreg->type);
		if (constant->type == TOK_STRING_LITERAL) {
			struct ty_string	*ts = constant->data;
			copy_type(vreg->type, ts->ty, 0);
		} else {
			struct type	*ty =
				make_basic_type(constant->type);

			copy_type(vreg->type, ty, 0);
			if (vreg->type->code == TY_INT
				|| vreg->type->code == TY_LONG
				|| vreg->type->code == TY_LLONG) {
				vreg->type->sign = TOK_KEY_SIGNED;
			} else if (vreg->type->code == TY_UINT
				|| vreg->type->code == TY_ULONG
				|| vreg->type->code == TY_ULLONG) {
				vreg->type->sign = TOK_KEY_UNSIGNED;
			}
			vreg->is_nullptr_const = is_nullptr_const(constant, ty);
		}
	} else if (ty0 != NULL) {	
		vreg->type = ty0;
		if (ty0->code != TY_VOID || ty0->tlist != NULL) {
			if (IS_VLA(ty0->flags) && is_immediate_vla_type(ty0)) {
				vreg->size = 0;
			} else if (ty0->tlist != NULL) {
				/*
				 * 07/09/10: This was missing. When we are
				 * dealing with arrays, we probably want the
				 * pointer size instead of the array size. See
				 * do_struct_member()
				 */
				vreg->size = backend->get_ptr_size();
			} else {	
				vreg->size = backend->get_sizeof_type(ty0, NULL);
			}	
		} else {
			vreg->size = 0;
		}
	} else {
		/*
		 * Anonymous register. Will be saved on stack if it
		 * gets ``faulted in'', is modified, and then needs
		 * to be saved again
		 */
		;
		vreg->size = backend->get_ptr_size(); /* XXX hm */
	}	
	vreg->pregs[0] = NULL; /* No physical register yet */
	if (vreg->type != NULL) {
		vreg->is_multi_reg_obj
			= backend->is_multi_reg_obj(vreg->type);
	}	

	return vreg;
}


/*
 * 08/11/08: Basic routine to actually anonymify a vreg which is already
 * disconnected and register-resident, but may still be backed by something
 * else as well.
 *
 * vreg_disconnect() + vreg_do_anonymify() is probably what we had intended
 * for vreg_anonymify();
 */
void
vreg_do_anonymify(struct vreg *vr) {
	vr->var_backed = NULL;
	vr->from_const = NULL;
	vr->from_ptr = NULL;
	vr->parent = NULL;
	vr->stack_addr = NULL;
}


void
vreg_anonymify(struct vreg **vr, struct reg *r,
		struct reg *r2, struct icode_list *il) {
	struct vreg	*tmp;


	if (is_x87_trash(*vr)) {
	}


	if ((*vr)->var_backed
		|| (*vr)->from_const
		|| (*vr)->from_ptr
		|| (*vr)->parent
		|| (*vr)->stack_addr
		) {
#if VREG_SEQNO
		static int	saved_seqno;
#endif

		vreg_faultin_x87(r, r2, *vr, il, 0);

		tmp = vreg_alloc(NULL, NULL, NULL, (*vr)->type);



#if VREG_SEQNO
		saved_seqno = tmp->seqno;
#endif
		*tmp = **vr;
#if VREG_SEQNO
		tmp->seqno = saved_seqno;
#endif

		/*
		 * 11/02/07: The stuff below was missing. When
		 * anonymifying an array, it must decay into a
		 * pointer. Otherwise reloading it will go wrong.
		 * This is a VERY fundamental bug which I'm
		 * surprised to find, and which needs verification
		 * and testing
		 */
		if (tmp->type->tlist != NULL
			&& (tmp->type->tlist->type == TN_ARRAY_OF
			|| tmp->type->tlist->type == TN_VARARRAY_OF)) {
			int	is_vla;

			is_vla = tmp->type->tlist->type == TN_VARARRAY_OF;

			tmp->type = n_xmemdup(tmp->type,
				 sizeof *tmp->type);
			copy_tlist(&tmp->type->tlist, tmp->type->tlist);
			tmp->type->tlist->type = TN_POINTER_TO;

			if (is_vla) {
				/*
				 * 05/20/11: XXX
				 */
				tmp->size = backend->get_ptr_size();
			} else { 
				tmp->size = backend->get_sizeof_type(
					tmp->type, NULL);
			}
		} else if (IS_THREAD(tmp->type->flags)) {
			tmp->type = n_xmemdup(tmp->type, sizeof *tmp->type);
			tmp->type->flags &= ~FLAGS_THREAD;
		}

		vreg_map_preg(tmp, tmp->pregs[0]);
		if (tmp->pregs[1] != NULL) {
			vreg_map_preg2(tmp, tmp->pregs[1]);
		}	
		*vr = tmp;
#if 0
		tmp->var_backed = NULL;
		tmp->from_ptr = NULL;
		tmp->from_const = NULL;
		tmp->parent = NULL;
		tmp->stack_addr = NULL;
#endif
		vreg_do_anonymify(tmp);
		if (is_x87_trash(*vr)) {
			free_preg((*vr)->pregs[0], il, 1, 1);
			(*vr)->pregs[0] = NULL;
			tmp->pregs[0] = NULL;
		}
#if 0
	} else if ((*vr)->preg == NULL || (*vr)->preg->vreg != *vr) {
		/* Already anonymous but saved on stack */
		vreg_faultin(NULL, NULL, *vr, il, 0);
#endif
	}	
}

/*
 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
 * does not set var_backed, etc, to null! so it does not really
 * disconnect much at all
 */
struct vreg *
vreg_disconnect(struct vreg *vr) {
	struct vreg	*ret;
#if VREG_SEQNO
	int		saved_seqno = vr->seqno;
#endif


	/*
	 * 12/23/08: Is there a good reason not to use
	 * dup_vreg()? That would have done the seqno copying...
	 */
#if USE_ZONE_ALLOCATOR
	ret = alloc_vreg();
	*ret = *vr;
#else
	ret = n_xmemdup(vr, sizeof *vr);
#endif

#if VREG_SEQNO
	ret->seqno = saved_seqno;
#endif

	if (ret->pregs[0] && vr->pregs[0]->vreg == vr) {
		vreg_map_preg(ret, ret->pregs[0]);
	}
	if (ret->is_multi_reg_obj && vr->pregs[1]->vreg == vr) {
		vreg_map_preg2(ret, ret->pregs[1]);
	}
	return ret;
}	

static int	yes_really = 0;


struct reg *
vreg_faultin_x87(
	struct reg *r0,
	struct reg *r0_2,
	struct vreg *vr,
	struct icode_list *il,
	int whatfor) {

	struct reg	*ret;

	yes_really = 1;
	/*
	 * XXX sometimes x87 registers wrongly remain associated with 
	 * data items, which is never allowed. We probably aren't using
	 * free_preg() rigorously enough
	 */
	if (is_x87_trash(vr)) {
		vr->pregs[0] = NULL;
	}

	ret = vreg_faultin(r0, r0_2, vr, il, whatfor);
	yes_really = 0;
	return ret;
}
	
static int	doing_dedicated_mapping;



/*
 * Faults virtual register ``vr'' into a physical register, if necessary,
 * and returns a pointer to that register.
 * If a physical register needs to be saved before we can use it, it will
 * be saved on curfunc's stack.
 * The caller can supply physical register r0 to use instead of calling
 * alloc_gpr()
 *
 * XXX this assumes that every item will fit into two registers, which
 * is not be true for all cpus :(. In particular, SPARC supports quad-
 * precision floating point by combining four FPRs. Perhaps change to
 * ``struct reg **r0''.
 * I also note that only one GPR is returned, but that does not seem to
 * be a problem because currently the return value is only used for pointer
 * faultins (and pointers should occupy only one GPR on most every arch
 * that will ever be supported.)
 */
struct reg *
vreg_faultin(
	struct reg *r0,
	struct reg *r0_2,
	struct vreg *vr,
	struct icode_list *il,
	int whatfor) {

	struct reg		*r = NULL;
	struct reg		*r2 = NULL;
	struct vreg		*vr2 = NULL;


	if (is_x87_trash(vr) && !yes_really) {
		puts("BUG: Attempt to treat x87 register like something");
		puts("     that is actually useful.");
		abort();
	}
	if (r0 != NULL && !doing_dedicated_mapping) {
		if (!r0->allocatable && !r0->used && r0->dedicated) {
			(void) fprintf(stderr, "BUG: Attempt to map vreg \n"
			"to dedicated register. Use vreg_faultin_dedicated()\n"
			"instead\n");
			abort();
		}
	}

	if (vr->pregs[0] != NULL && vr->pregs[0]->vreg == vr) {
		/* XXX needs work for long long .... */
		if (vr->is_multi_reg_obj) {
			if (vr->pregs[1] == NULL
				|| vr->pregs[1]->vreg != vr) {
				/* Second register isn't mapped! */
				free_preg(vr->pregs[0], il, 1, 1);
				goto dofault;
			}

			debug_log_regstuff(vr->pregs[1], vr, DEBUG_LOG_REVIVE);
		}
		debug_log_regstuff(vr->pregs[0], vr, DEBUG_LOG_REVIVE);
		if (r0 == NULL) {
			/* Already loaded */
			vreg_map_preg(vr, vr->pregs[0]);
			if (vr->is_multi_reg_obj) {
				vreg_map_preg2( vr, vr->pregs[1]);
			}	
			r = vr->pregs[0];
			goto out;
		} else {
			/*
			 * The item is already loaded, but the caller wishes
			 * it to be stored in a specific register, so it
			 * may have to be relocated. Note that this is tricky
			 * for multi-register objects. Consider a ``long long''
			 * loaded into ebx,eax that shall be relocated to
			 * eax,edx; We need to ensure that the copy from ebx
			 * to eax does not trash the other part of the object!
			 */
			if (vr->pregs[0] != r0) {
				if (vr->is_multi_reg_obj
					&& r0 == vr->pregs[1]) {
					icode_make_xchg(r0, vr->pregs[0], il);
					vr->pregs[1] = vr->pregs[0];
				} else {

					icode_make_copyreg(r0, vr->pregs[0],
						vr->type, vr->type, il);
					/* XXX shouldn't caller do this? */
					vr->pregs[0]->used = 0;
					/* Record new preg */
				}	
			}
			if (vr->is_multi_reg_obj) {
				/* XXX very broken if the regs overlap */
				if (vr->pregs[1] != r0_2) {
					icode_make_copyreg(r0_2, vr->pregs[1],
						vr->type, vr->type, il);
					/* XXX shouldn't caller do this? */
					vr->pregs[1]->used = 0;
				}	
				vreg_map_preg2(vr, r0_2);
			}	
			r = r0;
			vreg_map_preg(vr, r);
			goto out;
		}
	} else {
		/* Register is owned by someone else ... */
		;
	}
dofault:

	if (vr->from_ptr) {
		/* 
		 * We may need to fault in the pointer itself first before
		 * we can indirect through it
		 */
		if (r2 != NULL) {
			/*
			 * 11/01/07:
			 * This was missing... for a multi-reg
			 * item, the pointer may not be trashed
			 * when loading the first part, so we
			 * better separate pointer reg clearly
			 * from data regs
			 */
			reg_set_unallocatable(r);
			reg_set_unallocatable(r2);
		}
		
		if (vr->from_ptr->pregs[0] == NULL
			|| vr->from_ptr->pregs[0]->vreg != vr->from_ptr) {
			vreg_faultin(NULL, NULL, vr->from_ptr, il, 0);
		}

		if (r2 != NULL) {
			reg_set_allocatable(r);
			reg_set_allocatable(r2);
		}

		if ((vr->from_ptr->type->tlist->type == TN_ARRAY_OF
			|| vr->from_ptr->type->tlist->type == TN_VARARRAY_OF)
			&& vr->type->tlist
			&& (vr->type->tlist->type == TN_ARRAY_OF
			|| vr->type->tlist->type == TN_VARARRAY_OF)) {
			/*
			 * The ``pointer'' is really an array, i.e.
			 * the address of the first element, so there
			 * is no need to indirect
			 */
			vr->pregs[0] = r = vr->from_ptr->pregs[0];
			vr->pregs[0]->vreg = vr;
			goto out;
		}
	} else if (vr->parent) {
		vr2 = get_parent_struct(vr);
		if (vr2->from_ptr) {
			vreg_faultin(NULL, NULL, vr2->from_ptr, il, 0);
		}
	}
	
	if (r0 == NULL) {
		int	size = 0;
		int	is_floating;

		is_floating = is_floating_type(vr->type);

		if (vr->type->tlist != NULL
			&& (vr->type->tlist->type == TN_ARRAY_OF
			|| vr->type->tlist->type == TN_VARARRAY_OF)) { /* 05/26/11 */
			/* Array decays into pointer */
			size = backend->get_ptr_size();
			/*size = 4;*/
#if 0
		} else if (vr->size > 4
			&& !is_floating
			&& !IS_LLONG(vr->type->code)) {
			printf("BAD REGISTER LOAD OF SIZE %d\n", vr->size);
			printf("hm type is %d\n", vr->type->code);
			abort();
#endif
		} else {
			size = vr->size;
		}		

		/* XXX pass whatfor to alloc_gpr() */
		(void) whatfor;
		if (vr->from_ptr) {
			reg_set_unallocatable(vr->from_ptr->pregs[0]);
		}	
		if (is_floating) {
			/* XXX backend->multi_fpr_object ??? */
			r = backend->alloc_fpr(curfunc, size, il, NULL);
			if (vr->is_multi_reg_obj) {
				/* 11/18/08: New for PPC long double */
				r2 = backend->alloc_fpr(curfunc, size, il, NULL);
			}
		} else {
			r = ALLOC_GPR(curfunc, size, il, NULL);

			/*
			 * 06/20/08: This used to check the backend flag
			 * multi_gpr_object, which indicates whether the last
			 * ALLOC_GPR() requested a multi-gpr-sized item (size
			 * 8). Then it set vr->is_multi_reg_obj.
			 *
			 * The first one is bad because the state stuff is
			 * dangerous and easy to mess up (by omitting the
			 * second alloc, which did in fact happen. The second
			 * part - setting vr->is_multi_reg_obj - is also bad
			 * because it should be set at this point already
			 */
			if (backend->multi_gpr_object
				&& !vr->is_multi_reg_obj) {
				warningfl(NULL, "Compiler bug? Multi-GPR "
					"settings do not agree with each "
					"other");
			}

			if (vr->is_multi_reg_obj /*backend->multi_gpr_object*/) {
				/* XXX hardcoded x86 */
/*				vr->is_multi_reg_obj = 2;*/
				r2 = ALLOC_GPR(curfunc, size, il, NULL);	
			}
		}	
		if (vr->from_ptr) {
			reg_set_allocatable(vr->from_ptr->pregs[0]);
		}	
	} else {
		r = r0;
		r2 = r0_2;
	}

	debug_log_regstuff(r, vr, DEBUG_LOG_FAULTIN);
	vreg_map_preg(vr, r);
	/*
	 * 07/12/08: icode_make_load() may perform a register invalidation.
	 * This means that, if we are handling a multi-register object, then
	 * the vreg_map_preg() above may leave an inconsistent state (the
	 * vreg being mapped to only one register). So map both here already
	 *
	 * Also, set both registers to unused so they are not saved to the
	 * stack
	 */
	if (r2 != NULL) {
		vreg_map_preg2(vr, r2);
/*		r2->used = 0;*/
	}
/*	r->used = 0;*/

	/*
	 * 12/07/07: Protect r2 because icode_make_load() may have to
	 * allocate a register
	 */
	if (r2 != NULL) reg_set_unallocatable(r2);
	icode_make_load(r, vr2, 0, il);
	if (r2 != NULL) reg_set_allocatable(r2);

	if (r2 != NULL) {
		vreg_map_preg2(vr, r2);
		icode_make_load(r2, vr2, 1, il);
	}

	if (vr->type->tbit != NULL) {
	/*	extract_bitfield(vr);*/
	}
#if 0
	if (vr->from_ptr) {
		free_preg(vr->from_ptr->preg, il, 0, 0);
	} else if (vr->parent && vr2->from_ptr) {
		free_preg(vr2->from_ptr->preg, il, 0, 0);
	}	
#endif

out:	
	return r;
}

/*
 * 12/27/08: New function to fault to ``dedicated'' registers
 * which are not generally allocable, and for which the ``used''
 * flag may not be set (e.g. temp GPRS)
 */
struct reg *
vreg_faultin_dedicated(
	struct reg *r0,
	struct reg *r0_2,
	struct vreg *vr,
	struct icode_list *il,
	int whatfor) {

	struct reg	*ret;

	if (r0 == NULL || !r0->dedicated /*r0->allocatable || r0->used*/) {
		if (r0_2 != NULL && r0_2->dedicated) {
			/* OK other register is dedicated */
			;
		} else {
			(void) fprintf(stderr,
				"BUG: vreg_faultin_dedicated() used for\n"
				"non-dedicated register\n");
			abort();
		}
	}
	doing_dedicated_mapping = 1;
	ret = vreg_faultin(r0, r0_2, vr, il, whatfor);
	if (r0 != NULL) {
		r0->used = 0;
		r0->allocatable = 0;
	}
	doing_dedicated_mapping = 0;
	return ret;
}


struct reg *
vreg_faultin_protected(
	struct vreg *protectme,
	struct reg *r0,
	struct reg *r0_2,
	struct vreg *vr,
	struct icode_list *il,
	int whatfor) {

	struct reg	*preg = NULL;
	struct reg	*preg2 = NULL;
	struct reg	*ret;

	if (protectme->pregs[0] != NULL
		&& protectme->pregs[0]->vreg == protectme) {
		preg = protectme->pregs[0];
	} else if (protectme->from_ptr != NULL
		&& protectme->from_ptr->pregs[0] != NULL	
		&& protectme->from_ptr->pregs[0]->vreg == protectme->from_ptr) {
		preg = protectme->from_ptr->pregs[0];
	}
	
	if (preg != NULL) {
		reg_set_unallocatable(preg);
		if (protectme->is_multi_reg_obj) {
			if (protectme->pregs[1]
				&& protectme->pregs[1]->vreg == protectme) {	
				/*
				 * 04/15/08: This additional check above was
				 * needed for long long accessed through
				 * pointers, where the pointers need
				 * protection but the multi-reg values them-
				 * selves don't
				 */
				reg_set_unallocatable(preg2 = protectme->pregs[1]);
			}
		}	
	}

	ret = vreg_faultin(r0, r0_2, vr, il, whatfor);

	if (preg != NULL) {
		reg_set_allocatable(preg);
		if (preg2 != NULL) {
			reg_set_allocatable(preg2);
		}	
	}
	return ret;
}	


/*
 * If the vreg comes from a pointer, load that pointer into a register. The
 * interface is kept simple because it is not likely to be used in many
 * places. Extend as necessary.
 */
struct reg *
vreg_faultin_ptr(struct vreg *vr, struct icode_list *il) {
	struct vreg	*faultme = NULL;

	if (vr->from_ptr) {
		faultme = vr->from_ptr;
	} else if (vr->parent != NULL) {
		struct vreg	*vr2 = get_parent_struct(vr);

		if (vr2->from_ptr) {
			faultme = vr2->from_ptr;
		}
	} else if (vr->type->tlist != NULL
		&& (vr->type->tlist->type == TN_ARRAY_OF
			|| vr->type->tlist->type == TN_VARARRAY_OF
			|| vr->type->tlist->type == TN_FUNCTION)) {
		/*
		 * 07/01/07: Extended for arrays! Loading an array or
		 * a function ``into a register'' loads its address
		 *
		 * 10/31/07: This appears to be nonsense. It was
		 * introduced for variable-length arrays, so at the
		 * very least the tlist->type check was missing. I
		 * don't know whether this ever makes any sense for
		 * functions
		 *
		 * 01/05/10: This 
		 */
		faultme = vr;
	}
	if (faultme != NULL) {
		vreg_faultin(NULL, NULL, faultme, il, 0);
		return faultme->pregs[0];
	}
	return NULL;
}


/*
 * Variable and functions to forbid vreg_map_preg() - See
 * comment on backend_vreg_map_preg()
 */
static int	vreg_map_preg_forbidden = 1;

void
forbid_vreg_map_preg(void) {
	vreg_map_preg_forbidden = 1;
}

void
allow_vreg_map_preg(void) {
	vreg_map_preg_forbidden = 0;
}

static struct reg *trapreg;

int
trap_vreg_map_preg(struct reg *r) {
	trapreg = r;
	return 0;
}

/*
 * Use this instead of manual assignments to better track
 * the creation of mappings for debugging
 */
void
vreg_map_preg(struct vreg *vr, struct reg *preg) {
	if (preg == trapreg) abort();
#if XLATE_IMMEDIATELY
	if (vreg_map_preg_forbidden) {
		(void) fprintf(stderr,
			"BUG: vreg_map_preg() called from backend!\n\n"
			"Please use backend_vreg_map_preg() with a\n"
			"corresponding backend_vreg_unmap_preg()\n"
			"instead. Calling abort() ...\n");
		abort();
	}
#endif
	if (!doing_dedicated_mapping) {
		/* 12/27/08: Added this */
		if (!preg->allocatable && !preg->used && preg->dedicated) {
			(void) fprintf(stderr,
				"BUG: vreg_map_preg() used for dedicated \n"
				"(reserved) register. Use vreg_map_preg_dedicated \n"
				"if that was really intended (does not set used\n"
				"flag of register)\n");
			abort();
		}
	}
	debug_log_regstuff(preg, vr, DEBUG_LOG_MAP);

	vr->pregs[0] = preg;
	if (!doing_dedicated_mapping) {
		/*
		 * 27/12/08: Don't set used flag for dedicated
		 * register
		 */
		preg->used = 1;
	}
	if (preg != NULL) {
		vr->pregs[0]->vreg = vr;
	}	
}

/*
 * 12/27/08: New function to map a temp GPR to a vreg. This is
 * forbidden for vreg_map_preg() now because it sets the ``used''
 * flag of the register (which in turn trashes the ``dedicated''
 * property and causes the nexdt invalidate_gprs() to make the
 * register allocatable, which will cause all sorts of conflicts)
 */
void
vreg_map_preg_dedicated(struct vreg *vr, struct reg *preg) {
	if (preg->allocatable || preg->used) {
		(void) fprintf(stderr,
			"BUG: vreg_map_preg_dedicated() applied to\n"
			"register which is not dedicated. What are\n"
			"you really trying to do?\n");
		abort();
	}
	doing_dedicated_mapping = 1;
	vreg_map_preg(vr, preg);
	doing_dedicated_mapping = 0;
}

/*
 * 03/24/08: New functions to map/unmap registers in emission-related
 * parts of backend.
 *
 * Background: The old design (before XLATE_IMMEDIATELY was introduced)
 * worked such that all code was first translated, and then emitted.
 *
 * WIth the new design, however, one function at a time is translated
 * and emitted. This has raised the problem that EVEN the emission-
 * related backend functions (i.e. those functions that are indirectly
 * called by the old gen_program()) used vreg_map_preg()!
 *
 * This caused the following problem:
 *
 * The backend only calls vreg_map_preg() but not free_preg(), so the
 * registers end up being ``untrusted'' because a SINGLE register may
 * end up being mapped to a vreg that has the multi-GPR flag set, so
 * the invalidation routine will be misguided by this
 */
void
backend_vreg_map_preg(struct vreg *vr, struct reg *preg) {
	vr->pregs[0] = preg;
	if (preg != NULL) {
		vr->pregs[0]->vreg = vr;
	}	
}

void
backend_vreg_unmap_preg(struct reg *r) {
	if (r->vreg) {
		r->vreg->pregs[0] = NULL;
		r->vreg = NULL;
	}
}

void
backend_vreg_map_preg2(struct vreg *vr, struct reg *preg) {
	vr->pregs[1] = preg;
	if (preg != NULL) {
		vr->pregs[1]->vreg = vr;
	}	
}

void
backend_vreg_unmap_preg2(struct reg *r) {
	if (r->vreg) {
		r->vreg->pregs[1] = NULL;
		r->vreg = NULL;
	}
}


void
reg_set_unused(struct reg *r) {
	r->used = 0;
}

void
vreg_map_preg2(struct vreg *vr, struct reg *preg) {
	vr->pregs[1] = preg;
	preg->used = 1;
	if (preg != NULL) {
		vr->pregs[1]->vreg = vr;
	}	
}	
	

static void
invalidate_subregs(struct reg *r) {
	r->vreg = NULL;
	if (r->size == 8 && r->composed_of) {
		/* XXX kludge */
		invalidate_subregs(r->composed_of[0]);
		return;
	}	
		
	if (r->composed_of) {
		if (r->composed_of[0]) {
			r->composed_of[0]->vreg = NULL;
		}
		if (r->composed_of[1]) {
			r->composed_of[1]->vreg = NULL;
		}
		if (r->composed_of[0]->composed_of) {
			r->composed_of[0]->composed_of[0]->vreg = NULL;
			if (r->composed_of[0]->composed_of[1]) {
				r->composed_of[0]->composed_of[1]->vreg = NULL;
			}
		}	
	}	
}

static int 
do_free_preg(struct reg *r, struct icode_list *il,
	int invalidate, int savereg) {
	struct vreg	*vr;

	if (r == NULL) return 0;
	vr = r->vreg;


	reg_set_allocatable(r);
	if (!r->used) {
		if (invalidate) r->vreg = NULL;
		return 0;
	}
	debug_log_regstuff(r, vr, DEBUG_LOG_FREEGPR);

	if (savereg
		&& vr && !vr->var_backed && !vr->from_const && !vr->from_ptr
		&& !vr->parent) {
		/*
		 * Must be saved. This doesn't interact well with
		 * multi-register objects
		 * XXX for some reason, is_multi_reg_obj sometimes
		 * isn't set even for a long long x86 object :(
		 * this is triggered in change_preg_size(), where
		 * a long long-associated register is freed
		 * hence the workaround below
		 */
		if (backend->arch == ARCH_X86
			&& IS_LLONG(vr->type->code)
			&& vr->type->tlist == NULL) {
			struct vreg	*copyvr;

			if (vr->pregs[1] == NULL) {
				/*
				 * This apparently happens when freeing
				 * a preg holding part of multi-gpr object.
				 */
				vr->pregs[1] = r;
			}	

#if 0
			copyvr = n_xmemdup(vr, sizeof *vr);
#endif
			copyvr = copy_vreg(vr);
			copyvr->is_multi_reg_obj = 2;
			copyvr->size = 8;
			icode_make_store(curfunc, copyvr,
				copyvr, il);
			if (copyvr->stack_addr != NULL) {
				vr->stack_addr = copyvr->stack_addr;
			}
#if 0
			icode_make_store(curfunc, copyvr,
				copyvr, il);
#endif
		} else {
			icode_make_store(curfunc, vr, vr, il);
		}
	}	

	if (backend->free_preg != NULL) {
		backend->free_preg(r, il);
	}	
	
	r->used = 0;
	if (invalidate) r->vreg = NULL;

	return 1;
}


/*
 * 07/25/09: Allow freeing dedicated registers (freeing generally resets
 * special properties like allocatability, so we reset it here)
 */
void
free_preg_dedicated(struct reg *r, struct icode_list *il, int invalidate, int savereg) {
	assert(r->dedicated);

	r->dedicated = 0;
	free_preg(r, il, invalidate, savereg);
	reg_set_dedicated(r);
}

/*
 * Note that if savereg is specified and the register is subsequently used,
 * invalidate also needs to be set in order to avoid bad stale references
 */
void
free_preg(struct reg *r, struct icode_list *il, int invalidate, int savereg) {
	static int	level;

	if (r->dedicated) {
		(void) fprintf(stderr, "BUG: freeing dedicated register %s\n", r->name);
		abort();
	}

	/*
	 * XXX The stuff below ensures that if a GPR holding a ``long long''
	 * is ever freed, the GPR for the other part will also be freed. This
	 * makes it easier to deal with long longs because that way a long
	 * long is always either resident or non-resident, never half-resident.
	 * Of course this approach is less than optimal speedwise, so it
	 * should be changed some time
	 */
	if (r->vreg && r->vreg->is_multi_reg_obj && level == 0) {
		struct reg	*r2;

		invalidate = 1; /* cached multi-reg objects cause problems */
		if (r->vreg->pregs[0] == r) {
			r2 = r;
			r = r->vreg->pregs[1];
		} else {
			r2 = r->vreg->pregs[0];
		}
		++level;
		/*
		 * 04/18/08: Don't call free_preg() recursively! The problem
		 * here was that do_free_preg() uses icode_make_store() to
		 * save the register, and this will write both registers, so
		 * we save the entire multi-reg object twice.
		 * However, only the invalidation is needed
		 */
#if ! AVOID_DUPED_MULTI_REG_SAVES
		free_preg(r2, il, invalidate, savereg);
#else
		free_preg(r2, il, invalidate, 0); /* Don't save! */
#endif
		--level;
	}

	if (do_free_preg(r, il, invalidate, savereg)) {
		if (invalidate) {
			invalidate_subregs(r);
		}	
		return;
	}
	if (r->size == 8 && r->composed_of) {
		/* XXX ... */
		free_preg(r->composed_of[0], il, invalidate, savereg);
		return;
	} 

	if (r->composed_of) {
		int	rc = 0;
		if (r->composed_of[0]) {
			rc |= do_free_preg(r->composed_of[0], il,
				invalidate, savereg);
		}
		if (r->composed_of[1]) {
			rc |= do_free_preg(r->composed_of[1], il,
				invalidate, savereg);
		}
		if (rc) {
			if (invalidate) {
				invalidate_subregs(r);
			}	
			return;
		}
		if (r->composed_of[0]->composed_of) {
			(void) do_free_preg(r->composed_of[0]->
				composed_of[0], il, invalidate, savereg);
			(void) do_free_preg(r->composed_of[0]->
				composed_of[1], il, invalidate, savereg);
		}
	}
}

/*
 * Free all pregs assigned to a vreg. This should be used for all vregs so
 * that it is transparent whether we are dealing with a vreg that occupies
 * more than preg (case in point: ``long long'' on x86 and objects that
 * were loaded through a pointer)
 */
void
free_pregs_vreg(
	struct vreg *vr,
	struct icode_list *il,
	int invalidate,
	int savereg) {

	if (vr->from_ptr != NULL) {
		if (vr->from_ptr->pregs[0] != NULL
			&& vr->from_ptr->pregs[0]->vreg == vr->from_ptr) {
			free_preg(vr->from_ptr->pregs[0], il,
				invalidate, savereg);
		}
	}
	if (vr->pregs[0] != NULL && vr->pregs[0]->vreg == vr) {
		free_preg(vr->pregs[0], il, invalidate, savereg);
	}
	if (vr->pregs[1] != NULL && vr->pregs[1]->vreg == vr) {
		free_preg(vr->pregs[1], il, invalidate, savereg);
	}
}	

int
is_member_of_reg(struct reg *r, struct reg *member) {
	if (r->composed_of == NULL) return 0;
	if (member == r) {
		return 1;
	}	
	if (r->size == 8) { /* XXX */
		return is_member_of_reg(r->composed_of[0], member);
	}
	if (r->composed_of[0] == member
		|| (r->composed_of[0]->composed_of
		&& (r->composed_of[0]->composed_of[0] == member
		|| r->composed_of[0]->composed_of[1] == member))) { 
		return 1;
	}	
	return 0;
}

void
vreg_set_new_type(struct vreg *vr, struct type *ty) {
	vr->type = ty;
	if (IS_VLA(ty->flags) && is_immediate_vla_type(ty)) {
		vr->size = 0;
		vr->is_multi_reg_obj = 0;
	} else {	
		vr->size = backend->get_sizeof_type(ty, NULL);
		vr->is_multi_reg_obj = backend->is_multi_reg_obj(ty);
	}	
}	

/*
 * Save a scalar item to the stack, loading it into a register
 * to do so if necessary.
 * The vreg is copied, and *vr is set to point to the copy
 * (because in most cases we want to save a vreg that has already
 * been used in other icode instructions.)
 */
void
vreg_save_to_stack(struct vreg **vr0, struct type *ty, struct icode_list *il) {
	struct vreg	*vr = *vr0;

	vr = vreg_disconnect(vr);
	vr->type = ty;
	vr->size = backend->get_sizeof_type(ty, NULL);
	vreg_faultin(NULL, NULL, vr, il, 0);

	/*
	 * 07/14/09: Anonymify was missing! Otherwise, if we have an FP
	 * constant which we want to reinterpret as an integer, then if
	 * from_const still points to the constant (which it did without
	 * anonymify), then the load may bypass our stack indirection and
	 * just load the constant directly, which is wrong
	 */
	vreg_anonymify(&vr, NULL, NULL, il);
	free_preg(vr->pregs[0], il, 1, 1);
	*vr0 = vr;
}


void
vreg_reinterpret_as(struct vreg **vr0, struct type *from, struct type *to,
	struct icode_list *il) { 

	struct vreg	*vr;

	vreg_save_to_stack(vr0, from, il);
	vr = vreg_disconnect(*vr0);
	vr->type = to;
	vr->size = backend->get_sizeof_type(to, NULL);
	vreg_faultin(NULL, NULL, vr, il, 0); 
	*vr0 = vr;
}


struct vreg *
copy_vreg(struct vreg *vr) {
	struct vreg	*ret = alloc_vreg();
	*ret = *vr;
	return ret;
}

struct vreg *
vreg_back_by_ptr(struct vreg *vr, struct reg *ptrreg, int is_backend) {
	struct vreg	*ptrvr;

	ptrvr = vreg_alloc(NULL, NULL, NULL, addrofify_type(vr->type));
	if (is_backend) {
		backend_vreg_map_preg(ptrvr, ptrreg);
	} else {	
		vreg_map_preg(ptrvr, ptrreg);
	}
#if 0
	vr = /*vreg_disconnect(vr)*/ n_xmemdup(vr, sizeof *vr);
#endif
	vr = copy_vreg(vr);
			
	vr->parent = NULL;
	vr->from_ptr = ptrvr;
	vr->stack_addr = NULL;  /* 12/29/07: This was missing!!!! */
	vr->from_const = NULL; /* 02/03/08: was missing.. */
	if (vr->parent != NULL) {
		vr->parent = ptrvr;
	}
	if (vr->var_backed != NULL) {
		vr->var_backed = NULL;
	}
	return vr;
}


int
vreg_needs_pic_reloc(struct vreg *vr) {
	/*
	 * 02/08/09: Wow this didn't take static parent structs
	 * into account
	 */
	if (vr->parent != NULL) {
		vr = get_parent_struct(vr);
	}
	if (vr->var_backed != NULL) {
		struct decl	*d = vr->var_backed;

		/*
		 * 02/10/09: XXXXXXXX: This fragment:
		 *   printf("%Lf\n", getlongdouble());
		 * ... causes the printf() argument to get the is_func flag
		 * set. Presumably because it is duplicated from the function
		 * declaration, and the is_func flag is not reset
		 * Thus the extra tlist != NULL check. However, we should
		 * investigate the cause and implications of this event.
		 */
		if (d->dtype->is_func
			&& d->dtype->tlist != NULL
			&& d->dtype->tlist->type == TN_FUNCTION) {
			return 1;
		} else if (vr->var_backed->dtype->storage == TOK_KEY_STATIC
			|| vr->var_backed->dtype->storage == TOK_KEY_EXTERN) {
			return 1;
		}
	} else if (vr->from_const) {
		if (vr->from_const->type == TOK_STRING_LITERAL) {
			return 1;
		} else if (IS_FLOATING(vr->from_const->type)) {
			return 1;
		}
	}
	return 0;
}

