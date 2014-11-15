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
#include "icode.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <sys/mman.h>
#include <unistd.h>
#include "misc.h"
#include "token.h"
#include "control.h"
#include "decl.h"
#include "type.h"
#include "debug.h"
#include "defs.h"
#include "expr.h"
#include "error.h"
#include "subexpr.h"
#include "icode.h"
#include "symlist.h"
#include "typemap.h"
#include "reg.h"
#include "functions.h"
#include "backend.h" /* for get_sizeof() */
#include "scope.h"
#include "zalloc.h"
#include "cc1_main.h" /* flags */
#include "inlineasm.h"
#include "n_libc.h"

struct icode_instr *
alloc_icode_instr(void) {
	static unsigned long seq;

#if USE_ZONE_ALLOCATOR
	struct icode_instr	*ret = zalloc_buf(Z_ICODE_INSTR);

	ret->seqno = ++seq;

	return ret;
#else
	struct icode_instr	*ret = n_xmalloc(sizeof *ret);
	static struct icode_instr	nullinstr;

	*ret = nullinstr;
	ret->seqno = ++seq;
	return ret;
#endif
}

struct icode_list *
alloc_icode_list(void) {
#if USE_ZONE_ALLOCATOR
	return zalloc_buf(Z_ICODE_LIST);
#else
	struct icode_list	*ret = n_xmalloc(sizeof *ret);
	static struct icode_list	nullil;
	*ret = nullil;
	return ret;
#endif
}

struct icode_instr *
copy_icode_instr(struct icode_instr *ii) {
	struct icode_instr	*ret = alloc_icode_instr();
	*ret = *ii;
	return ret;
}	

void
append_icode_list(struct icode_list *list, struct icode_instr *instr) {
	static unsigned long	append_seqno;

	instr->append_seqno = ++append_seqno;
	if (list->tail == NULL) {
		list->head = list->tail = instr;
	} else {
		list->tail->next = instr;
		list->tail = instr;
	}
	
	/*
	 * XXX stuff below is a kludge to ensure that pointer pregs are
	 * always recorded properly
	 */
	if (instr->src_vreg && instr->src_ptr_preg == NULL) {
		if (instr->src_vreg->from_ptr) {
			instr->src_ptr_preg
				= instr->src_vreg->from_ptr->pregs[0];
		} else if (instr->src_vreg->parent) {
			struct vreg	*vr2 =
				get_parent_struct(instr->src_vreg);

			/*
			 * 25/12/07: Always save parent struct, not
			 * just for pointers. Hmm, that raises the
			 * question of how reliable vr->parent is
			 * guaranteed to be in the backend anyway?
			 */
			instr->src_parent_struct = vr2;
			if (vr2->from_ptr) {
				instr->src_ptr_preg
					= vr2->from_ptr->pregs[0];
			}	
		}
	}
	if (instr->dest_vreg && instr->dest_ptr_preg == NULL) {
		if (instr->dest_vreg->from_ptr) {
			instr->dest_ptr_preg
				= instr->dest_vreg->from_ptr->pregs[0];
		} else if (instr->dest_vreg->parent) {
			struct vreg	*vr2 =
				get_parent_struct(instr->dest_vreg);
			if (vr2->from_ptr) {
				instr->dest_parent_struct = vr2;
				instr->dest_ptr_preg
					= vr2->from_ptr->pregs[0];
			}	
		}
	}
	if (instr->type == INSTR_COPYSTRUCT) {
		struct copystruct	*cs = instr->dat;
		struct vreg		*structtop;

		if (cs->src_vreg && cs->src_vreg->parent) {
			structtop = get_parent_struct(cs->src_vreg);

			/* 12/25/07: Always save, not just for pointers */
			instr->src_parent_struct = structtop;
			if (structtop->from_ptr) {
				instr->src_ptr_preg =
					structtop->from_ptr->pregs[0];
			}
		}
		if (cs->dest_vreg && cs->dest_vreg->parent) {
			structtop = get_parent_struct(cs->dest_vreg);
			if (structtop->from_ptr) {
				instr->dest_parent_struct = structtop;
				instr->dest_ptr_preg =
					structtop->from_ptr->pregs[0];
			}	
		}
	}
}

void
merge_icode_lists(struct icode_list *dest, struct icode_list *src) {
	if (src->head == NULL) {
		/* XXX ??? */
		dest->res = src->res;
		return;
	}	
	if (dest->head == NULL) {
		if (dest->tail != NULL) {
			puts("icode list corruption detected");
			exit(EXIT_FAILURE);
		}	
		dest->head = src->head;
		dest->tail = src->tail;
	} else {
		dest->tail->next = src->head;
		if (src->tail != NULL) {
			dest->tail = src->tail;
		}
	}
	dest->res = src->res;
}

struct reg **
make_icode_pregs(struct vreg *vr, struct reg *r) {
	struct reg	**ret;

	if (vr == NULL || !vr->is_multi_reg_obj) {
		ret = n_xmalloc(2 * sizeof *ret);
		ret[0] = vr? vr->pregs[0]: r;
		ret[1] = NULL;
	} else {
		ret = n_xmalloc(vr->is_multi_reg_obj * sizeof *ret);
		if (vr->is_multi_reg_obj != 2) abort();
		/* XXX hardcoded x86 */
		ret[0] = vr->pregs[0];
		ret[1] = vr->pregs[1];
	}

	return ret;
}	
	
struct icode_instr *
generic_icode_make_instr(struct vreg *dest, struct vreg *src, int type) {
	struct icode_instr	*ret = alloc_icode_instr();
	ret->type = type;
	ret->dest_vreg = dest;
	if (dest != NULL) {
		ret->dest_pregs = make_icode_pregs(dest, NULL);
	}	
	ret->src_vreg = src;
	if (src != NULL) {
		ret->src_pregs = make_icode_pregs(src, NULL);
	}	
	return ret;
}

static unsigned long	lnum = 0;

unsigned long
get_label_count(void) {
	return lnum;
}

struct icode_instr *
icode_make_label(const char *name) {
	struct icode_instr	*ret;
	char			buf[1024];

	ret = alloc_icode_instr();
	ret->type = INSTR_LABEL;

	if (name == NULL) {
		sprintf(buf, "L%lu", lnum++);
		ret->dat = n_xstrdup(buf);
	} else {
		ret->dat = n_xstrdup(name);
	}	

	return ret;
}

struct icode_instr *
icode_make_adj_allocated(int bytes) {
	struct icode_instr	*ii = alloc_icode_instr();

	ii->dat = n_xmemdup(&bytes, sizeof bytes);
	ii->type = INSTR_ADJ_ALLOCATED;
	return ii;
}	

struct icode_instr *
icode_make_setreg(struct reg *r, int value) {
	struct icode_instr	*ret = alloc_icode_instr();

	ret->type = INSTR_SETREG;
	ret->src_pregs = make_icode_pregs(NULL, r);
	ret->dat = n_xmemdup(&value, sizeof value); /* XXX :-( */
	return ret;
}	

struct icode_instr *
icode_make_freestack(size_t bytes) {
	struct icode_instr	*ret = alloc_icode_instr();
	ret->type = INSTR_FREESTACK;
	ret->dat = n_xmemdup(&bytes, sizeof bytes);
	return ret;
}


struct icode_instr *
icode_make_branch(struct icode_instr *dest, int btype, struct vreg *vr) {
	struct icode_instr	*ret = alloc_icode_instr();
	ret->type = btype;
	ret->dat = dest;
	ret->dest_vreg = vr;
	return ret;
}

struct icode_instr *
icode_make_call(const char *name) {
	struct icode_instr	*ret = alloc_icode_instr();
	ret->type = INSTR_CALL;
	ret->dat = (char *)name;
	return ret;
}

struct icode_instr *
icode_make_call_indir(struct reg *r) {
	struct icode_instr	*ret = alloc_icode_instr();
	ret->type = INSTR_CALLINDIR;
	ret->dat = r;
	return ret;
}	

void
icode_make_xchg(struct reg *r1, struct reg *r2, struct icode_list *il) {
	struct icode_instr	*ret = alloc_icode_instr();

	ret->type = INSTR_XCHG;
	ret->src_pregs = make_icode_pregs(NULL, r1);
	ret->dest_pregs = make_icode_pregs(NULL, r2);
	append_icode_list(il, ret);
}

void
icode_make_initialize_pic(struct function *f, struct icode_list *il) {
	struct icode_instr	*ret = alloc_icode_instr();

	ret->type = INSTR_INITIALIZE_PIC;
	ret->dat = f;
	append_icode_list(il, ret);
}

static struct icode_instr *
icode_make_load_vla(struct reg *r, struct type *ty) {
	struct icode_instr	*ii = alloc_icode_instr();

	ii->type = INSTR_LOAD_VLA;
	ii->dest_pregs = make_icode_pregs(NULL, r);
	ii->dat = ty;
	return ii;
}



static struct reg *	
icode_prepare_loadstore(int is_load,
	struct reg *r,
	struct vreg *vr,
	struct vreg *parent_struct,
	struct vreg *protectme,
	struct icode_list *il,
	int *is_stack0) {

	struct reg		*ret = NULL;
	int			is_stack = 0;

	/*
	 * Before any preparations are done, mark all other needed
	 * source/target registers unallocatable so they do not get
	 * trashed
	 *
	 * NOTE!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	 * If we load a multi-GPR item, that is done by calling
	 * icode_make_load() multiple times! The caller must therefore
	 * mark subsequent registers unallocatable himself! Otherwise
	 * the first load will only protect the first register but
	 * may trash subsequent ones
	 */
	if (is_load) {
		reg_set_unallocatable(r);
	} else {
		reg_set_unallocatable(protectme->pregs[0]);
		if (protectme->is_multi_reg_obj) {
			reg_set_unallocatable(protectme->pregs[1]);
		}
	}

	if (IS_THREAD(vr->type->flags)) {
		ret = ALLOC_GPR(curfunc, backend->get_ptr_size(),
			il, NULL);
	} else if (picflag && vreg_needs_pic_reloc(vr)) {
		if (backend->need_pic_init) {
			backend->icode_initialize_pic(curfunc, il);
			curfunc->pic_initialized = 1;
		}
		ret = ALLOC_GPR(curfunc, backend->get_ptr_size(),
			il, NULL);
	} else {

		/*
	  	 * This is not a TLS- or PIC-load, so we should need
		 * no register at all, unless the offset is too large!
		 */
		if (arch_without_offset_limit()) {
			goto out;
		}

		/*
		 * OK, we're on an arch with limited offsets. What we have
		 * to handle:
		 *
		 *    - Stack variables. This is terrible because currently
		 * the final offsets are computed when it's too late. Therefore
		 * we always have to ensure that one register is allocated when
		 * we load/store a stack variable. This SUCKS and must be
		 * fixed!
		 *
		 *    - Anonymous stack items (usually registers) - like for
		 * stack variables
		 *
		 *    - Static struct members. Here we can check whether the
		 * offset is too large and, if necessary, compute the address
		 * in a register
		 *
		 *    - Indirect struct members. Here we can check the offset
		 * and, if necessary, add it to the pointer register (which
		 * must already be resident)
		 */
		if (vr->var_backed != NULL) {
			if (vr->var_backed->dtype->storage == TOK_KEY_AUTO
				|| vr->var_backed->dtype->storage == 0) {
				is_stack = 1;
			} else if (vr->var_backed->dtype->storage !=
				TOK_KEY_REGISTER) {
				/*
				 * 12/24/07: Static variable! On some archs
				 * this always requires an extra register
				 */ 
				if (backend->arch == ARCH_SPARC) {
					if (is_load) {
						/*
						 * The target load register can
						 * be used to hold the address,
						 * UNLESS it is an FPR!
						 */
						if (is_floating_type(vr->type)) {
							ret = ALLOC_GPR(curfunc,
								backend->get_ptr_size(),
								il, NULL);
						}
					} else {
						/* XXX hmm already seems to be handled */
						;
					}
				} else if (backend->arch == ARCH_POWER) {
				} else if (backend->arch == ARCH_MIPS) {
				} else {
					/*
					 * 02/11/08: Woah that unimpl() seems
					 * a bit strong, disallowing all static
					 * variable access regardless of offset!
					 */
				/*	unimpl();*/
				}
			}
		} else if (parent_struct != NULL) {
			struct decl	*d = parent_struct->var_backed;
			
			if (d != NULL
				&& vr->from_ptr == NULL
				&& (d->dtype->storage == TOK_KEY_AUTO
				|| d->dtype->storage == 0)) {
				is_stack = 1;
#if 0
			} else if (parent_struct->stack_addr != NULL) {
				is_stack = 1;
#endif
			} else if (vr->from_ptr || parent_struct->from_ptr || d != NULL) {
				/*
				 * Parent is pointer or static struct
				 */
				long	off;

				off = calc_offsets(vr);
				if (off > backend->max_displacement) {
					/* Offset too big */
					ret = ALLOC_GPR(curfunc, backend->get_ptr_size(),
						il, NULL);
				}
			} else {
				unimpl();
			}
		} else if (vr->stack_addr != NULL) {
			is_stack = 1;
		}

		if (is_stack) {
			ret = ALLOC_GPR(curfunc, backend->get_ptr_size(),
				il, NULL);
		}
	}

out:
	*is_stack0 = is_stack;

	/* Restore allocatability */
	if (is_load) {
		/*
		 * 07/13/09: This also restored allocatability for dedicated
		 * temp regs
		 */
		if (!r->dedicated) {
			reg_set_allocatable(r);
		}
	} else {
		if (!protectme->pregs[0]->dedicated) {
			reg_set_allocatable(protectme->pregs[0]);
		}
		if (protectme->is_multi_reg_obj) {
			if (!protectme->pregs[1]->dedicated) {
				reg_set_allocatable(protectme->pregs[1]);
			}
		}
	}
	return ret;
}

struct vreg *
promote_bitfield(struct vreg *vr, struct icode_list *il) {

	/*
	 * 08/09/08: If this is a signed bitfield member, perform sign
	 * extension on it! We can do this comfortably by shifting left,
	 * then right. This requires ``expected'' arithmetic shift right
	 * behavior, which is not standard but works on all supported
	 * architectures
	 */
	if (vr->type->tbit != NULL) {
		decode_bitfield(vr->type, vr, il);
		vr = vreg_disconnect(vr);
		vreg_do_anonymify(vr);
	}
	return vr;
}

/*
 * XXX pass vreg instead??
 * Note that this really only deals with one register at a time -
 * multi-register objects are dealt with in vreg_faultin()
 */
void
icode_make_load(struct reg *r, struct vreg *parent_struct,
	int is_not_first_load, struct icode_list *il) {

	struct icode_instr	*ret = alloc_icode_instr();
	struct vreg		*vr = r->vreg;
	static struct reg	*supportreg;
	int			is_stack = 0;

#if 0 
	if (vr->from_ptr == NULL
		&& vr->var_backed == NULL
		&& vr->stack_addr == NULL
		&& vr->parent == NULL) {
		printf("vr %p is unbacked!\n", vr);
		abort();
	}
#endif
	if (IS_VLA(vr->type->flags) && is_immediate_vla_type(vr->type)) {
		if (vr->from_ptr != NULL
			&& vr->from_ptr->type->tlist->type == TN_POINTER_TO) {
			/*
			 * 05/27/11: Loading VLA address through pointer to
			 * VLA;
			 *   char (*p)[expr] = ...
			 *   strcpy(*p, "hello");
			 * This is handled like an ordinary array load, i.e.
			 * the pointer already constitutes the address.
			 * XXX We should probably trash that icode_make_load_vla(0
			 * stuff and handle all VLA load instructions exactly
			 * like ordinary array load operations, and have the
			 * emitters sort out the access (accessing the metadata
			 * array address rather than a stack or static buffer).
			 * It's not clear (3 years after VLAs were introduced)
			 * why the distinction is so explicit in the frontend
			 * right now
			 */
			;
		} else {
			append_icode_list(il, icode_make_load_vla(r, vr->type));
			return;
		}
	}

	if (vr->parent != NULL && parent_struct == NULL) {
		parent_struct = get_parent_struct(vr);
	}

	/*
	 * 11/24/07: Time for some load preparations! In particular,
	 * build source address in a register if we are using large
	 * offsets on RISC, generating PIC, or accessing TLS. 
	 */
	if (is_not_first_load) {
		/*
		 * This is the second (or later) part of a multi-register
		 * load; All preparations have already been done at the
		 * first load (supportreg is static)
		 */
		;
	} else {	
		supportreg = icode_prepare_loadstore(1, r, vr,
			parent_struct, NULL, il, &is_stack);
	}

	if (supportreg != NULL) {
		if (is_stack) {
			/*
			 * It's not yet clear whether the register is needed;
			 * Only save it just in case
			 */
			ret->dat = supportreg;
		} else {
			/*
			 * We definitely have to indirect
			 */
/*			struct icode_instr	*ii;*/
			struct vreg		*ptrvr;

			/*ii =*/ (void) icode_make_addrof(supportreg, vr, il);
/*			append_icode_list(il, ii);*/
			ptrvr = vreg_back_by_ptr(vr, supportreg, 0);
			parent_struct = NULL;
			vr = ptrvr;

#if 1 
			if (sysflag == OS_OSX
				&& vr->type->tlist != NULL
				&& vr->type->tlist->type == TN_FUNCTION) {
				/*
				 * 02/08/09: When loading the address of a
				 * function on OSX, the initial load already
				 * computes the final function addres (i.e.
				 * it doesn't create a pointer through which
				 * we have to indirect to get the result
				 * pointer). So return that
				 *
				 * XXX This only applies to locally defined
				 * functions (_foo) and not nonlazy symbol
				 * references (L_foo$non_lazy_ptr).
				 * Currently we always create a non_lazy_ptr
			 	 * entry even for local functions. This may
				 * be undesirable
				 */
				icode_make_copyreg(r, supportreg, NULL, NULL, il);
				return;
			}
#endif
		}
	}

	if (vr->from_const && vr->from_const->data2) {
		/* Too big to be immediate */
		struct ty_llong	*tll;

		tll = vr->from_const->data2;
		tll->loaded = 1;
	}	
	if (vr->type->code == TY_STRUCT && vr->type->tlist == NULL) {
		puts("BUG: Attempt to load struct into register :-(");
		abort();
	}

	/*
	 * 12/07/07: Order matters!!!! In  foo->bar, the vreg has both
	 * from_ptr and parent set... Hmm.. why anyway?
	 */
	if (vr->from_ptr) {
		ret->src_ptr_preg = vr->from_ptr->pregs[0];
	} else if (vr->parent != NULL && parent_struct != NULL) {
		/* 12/25/07: Always save parent, not just for pointers */
		ret->src_parent_struct = parent_struct;
		if (parent_struct->from_ptr) {
			ret->src_ptr_preg =
				parent_struct->from_ptr->pregs[0];
		}
	}	

	ret->type = INSTR_LOAD;
	ret->src_vreg = vr;
	ret->src_pregs = make_icode_pregs(NULL, r);
	if (!vr->var_backed
		&& !vr->from_const
		&& !vr->from_ptr
		&& !vr->parent
		&& !vr->stack_addr) {
#if VREG_SEQNO
		printf("BUG: load from unbacked vreg %p [seq %d]\n", vr, vr->seqno);
#else
		printf("BUG: load from unbacked vreg %p\n", vr);
#endif
		printf("calling abort()\n");
		abort();
#if 0
	} else {
		debug_print_vreg_backing(vr);
#endif
	}

	append_icode_list(il, ret);

	if (supportreg != NULL) {
		if (!vr->is_multi_reg_obj || is_not_first_load) {
			free_preg(supportreg, il, 1, 0);
			supportreg = NULL;
		}
	}
}

void
icode_make_load_addrlabel(struct reg *r, struct icode_instr *label,
	struct icode_list *il) {

	struct icode_instr	*ii;

	ii = alloc_icode_instr();
	ii->dest_pregs = make_icode_pregs(NULL, r);
	ii->dat = label;
	ii->type = INSTR_LOAD_ADDRLABEL;
	append_icode_list(il, ii);
	if (backend->icode_prepare_load_addrlabel != NULL) {
		/*
		 * 12/25/08: For PPC: We have to create a TOC entry
		 * for the label
		 */
		backend->icode_prepare_load_addrlabel(label);
	}
}

void
icode_make_comp_goto(struct reg *addr, struct icode_list *il) {
	struct icode_instr	*ii;

	ii = alloc_icode_instr();
	ii->dat = addr;
	ii->type = INSTR_COMP_GOTO;
	append_icode_list(il, ii);
}


/*
 * 04/07/08: Ripped this out of icode_make_store()
 *
 * This allocates us a stack block for saving registers. The
 * list of blocks will be allocated later
 *
 * XXXXXXXXXXXXXXXXXXXXX Why do we have to go here to get
 * convenient stack allocation which gives us a stack_block
 * immediately (so it can be assigned to multiple vregs), and
 * why is there no other nice way?
 *
 * Is there something that could break here? (Alignment)
 *
 * Should we generalize the concept?
 */
struct stack_block *
icode_alloc_reg_stack_block(struct function *f, size_t size) {
	struct stack_block	*sb;

	sb = make_stack_block(0, size);

	if (f->regs_head == NULL) {
		f->regs_head = f->regs_tail = sb;
	} else {
		f->regs_tail->next = sb;
		f->regs_tail = f->regs_tail->next;
	}
	return sb;
}

/*
 * Store current item to virtual register vr 
 * XXX this is complete messed up
 *    Store current item (INSTR_SETITEM) to vr
 *
 */
void
icode_make_store(struct function *f,
	struct vreg *dest, struct vreg *src, /* XXX order is WRONG!! */
	struct icode_list *il) {

	struct icode_instr	*ret = alloc_icode_instr();
	static struct reg	*supportreg;
	struct vreg		*parent = NULL;
	int			is_stack = 0;

	if (src->parent != NULL) {
		parent = get_parent_struct(src);
		if (src->from_ptr || parent->from_ptr) {
			/*
			 * 12/12/07: This preparation of parent vreg and
			 * pointer was missing (but present in make_load!)
			 */
			ret->src_parent_struct = parent;
			if (src->from_ptr) {
				ret->src_ptr_preg = parent->pregs[0];
			} else {
				ret->src_ptr_preg = parent->from_ptr->pregs[0];	
			}
		}
	}

	/*
	 * 11/24/07: Time for some store preparations! In particular,
	 * build target address in a register if we are using large
	 * offsets on RISC, generating PIC, or accessing TLS. 
	 */
	supportreg = icode_prepare_loadstore(0, NULL, src, parent,
			dest, il, &is_stack);

	if (supportreg != NULL) {
		if (is_stack) {
			/*
			 * It's not yet clear whether the register is needed;
			 * Only save it just in case
			 */
			ret->dat = supportreg;
		} else {
			/*
			 * We definitely have to indirect
			 */
			/*struct icode_instr	*ii;*/
			struct vreg		*ptrvr;

			/*ii =*/ (void) icode_make_addrof(supportreg, src, il);
/*			append_icode_list(il, ii);*/
			ptrvr = vreg_back_by_ptr(src, supportreg, 0);
		/*	parent_struct = NULL;*/
			src = ptrvr;
		}
	}

	ret->type = INSTR_STORE;
	ret->dest_vreg = dest;
	ret->dest_pregs = make_icode_pregs(dest, NULL);
	ret->src_vreg = src;

	if (src != NULL) {
		ret->src_pregs = make_icode_pregs(src, NULL);
		if (src->type->code == TY_STRUCT
			&& src->type->tlist == NULL) {
			puts("BUG: Attempt to store register to struct :-((((");
			abort();
		}
	}

	/*
	 * 12/10/07: Added
	 */
	if (src->from_ptr) {
		ret->src_ptr_preg = src->from_ptr->pregs[0];
	} else if (src->parent != NULL && parent != NULL) {
		ret->src_parent_struct = parent;
		if (parent->from_ptr) {
			ret->src_ptr_preg = parent->from_ptr->pregs[0];
		}
	}
		
	if (src != NULL
		&& !src->var_backed
		&& !src->from_ptr
		&& !src->from_const
		&& !src->parent) {
		/*
		 * Need to allocate anonymous storage 
		 * for register
		 */
		if (src->stack_addr == NULL) {
			/* Offset will be patched later */
			/* XXX long long !?!?!!! */
			size_t	size = src->pregs[0]->size;
			
			if (src->is_multi_reg_obj) {
				size += src->pregs[1]->size;
			} else if (backend->arch == ARCH_SPARC) {
				/*
				 * XXX hmm this seems wrong and ugly
				 */
				if (src->pregs[0]->type == REG_FPR) {
					switch (src->type->code) {
					case TY_FLOAT:
						size = 4;
						break;
					case TY_DOUBLE:
						size = 8;
						break;
					case TY_LDOUBLE:
						size = 16;
						break;
					default: unimpl();
					}
				}
			}

			src->stack_addr = icode_alloc_reg_stack_block(f, size);
		}
	} else if (src->from_ptr) {
		vreg_faultin_protected(src, NULL, NULL, src->from_ptr, il, 0);
	}
	append_icode_list(il, ret);

	if (supportreg != NULL) {
		free_preg(supportreg, il, 1, 0);
		supportreg = NULL;
	}
}


struct icode_instr *
icode_make_sub(struct vreg *dest, struct vreg *src) {
	return generic_icode_make_instr(dest, src, INSTR_SUB);
}

struct icode_instr *
icode_make_neg(struct vreg *vr) {
	return generic_icode_make_instr(NULL, vr, INSTR_NEG);
}
	

struct icode_instr *
icode_make_add(struct vreg *dest, struct vreg *src) {
	return generic_icode_make_instr(dest, src, INSTR_ADD);
}

struct icode_instr *
icode_make_div(struct vreg *dest, struct vreg *src) {
	return generic_icode_make_instr(dest, src, INSTR_DIV);
}

struct icode_instr *
icode_make_mod(struct vreg *dest, struct vreg *src) {
	return generic_icode_make_instr(dest, src, INSTR_MOD);
}

struct icode_instr *
icode_make_mul(struct vreg *dest, struct vreg *src) {
	return generic_icode_make_instr(dest, src, INSTR_MUL);
}

struct icode_instr *
icode_make_shl(struct vreg *dest, struct vreg *src) {
	return generic_icode_make_instr(dest, src, INSTR_SHL);
}

struct icode_instr *
icode_make_shr(struct vreg *dest, struct vreg *src) {
	return generic_icode_make_instr(dest, src, INSTR_SHR);
}

struct icode_instr *
icode_make_and(struct vreg *dest, struct vreg *src) {
	return generic_icode_make_instr(dest, src, INSTR_AND);
}

struct icode_instr *
icode_make_xor(struct vreg *dest, struct vreg *src) {
	struct icode_instr	*ret = alloc_icode_instr();
	ret->type = INSTR_XOR;
	ret->dest_vreg = dest;
	ret->dest_pregs = make_icode_pregs(dest, NULL);
	if (src != NULL) {
		ret->src_pregs = make_icode_pregs(src, NULL);
	}	
	ret->src_vreg = src;
	return ret;
}	

struct icode_instr *
icode_make_not(struct vreg *vr) {
	return generic_icode_make_instr(NULL, vr, INSTR_NOT);
}	

struct icode_instr *
icode_make_or(struct vreg *dest, struct vreg *src) {
	return generic_icode_make_instr(dest, src, INSTR_OR);
}

void
icode_make_preg_or(struct reg *dest, struct reg *src, struct icode_list *il) {
	struct icode_instr	*ret = alloc_icode_instr();
	ret->type = INSTR_PREG_OR;
	ret->dest_pregs = make_icode_pregs(NULL, dest);
	ret->src_pregs = make_icode_pregs(NULL, src);
	append_icode_list(il, ret);
}

struct icode_instr *
icode_make_push(struct vreg *vr, struct icode_list *il) {
	struct icode_instr	*ret = alloc_icode_instr();

	ret->type = INSTR_PUSH;

	/*
	 * 12/06/07: Wow, the unconditional type->tlist->type check
	 * below crashed when passing ``ar[i]'' to printf(): The
	 * subscript operator removed the type node but kept the
	 * is_vla flag.
	 * XXX Maybe we should change that instead? 
	 */
	if (vr->type && IS_VLA(vr->type->flags) && vr->type->tlist != NULL) {
		if (vr->type->tlist->type == TN_ARRAY_OF
			|| vr->type->tlist->type == TN_VARARRAY_OF) {
			struct reg		*r;

			if (vr->pregs[0] == NULL
				|| vr->pregs[0]->vreg != vr) {
				r = ALLOC_GPR(curfunc, backend->get_ptr_size(),
					il, 0);	
				vreg_map_preg(vr, r);
				icode_make_load(r, NULL, 0, il);
				free_preg(r, il, 0, 0);
			}
		}
	}
	ret->src_vreg = vr;
	ret->src_pregs= make_icode_pregs(vr, NULL);

	return ret;
}

struct icode_instr *
icode_make_ret(struct vreg *vr) {
	struct icode_instr	*ret = alloc_icode_instr();
	ret->type = INSTR_RET;
#if 0
	if (vr != NULL) {
		/*ret->src_preg = vr->preg; seems never used*/
		ret->src_vreg = vr;
	}
#endif
	ret->src_vreg = vr;
	return ret;
}

struct icode_instr *
icode_make_indir(struct reg *r) {
	struct icode_instr	*ret = alloc_icode_instr();
	ret->type = INSTR_INDIR;
	ret->dat = r;
	return ret;
}

/*
 * vr = NULL means take address of next argument on stack in
 * variadic function
 */
struct reg * /*icode_instr **/
icode_make_addrof(struct reg *r, struct vreg *vr, struct icode_list *il) {
	struct icode_instr	*ret;
	
	ret = alloc_icode_instr();
	ret->type = INSTR_ADDROF;
	ret->src_vreg = vr;

	/*
	 * 06/15/09: This was needed for stale parent struct pointers
	 * and such
	 */
	if (vr != NULL) {
		(void) vreg_faultin_ptr(vr, il);

		/*
		 * 02/01/10: This ``from pointer'' load does not
		 * work to load the address of a function in PIC
		 * mode! That type of load has to load a base
		 * address, then load the final address
		 * indirectly. This fails for functions because
		 * vreg_faultin_ptr() will recurse to
		 * icode_make_addrof(), and then things are
		 * going to get trashed because the address is
		 * already assumed to have been loaded, which it
		 * hasn't.
		 *
		 * The solution is to avoid icode_make_addrof()
		 * for that particular case (the address-of
		 * handling for functions in subexpr.c just
		 * uses vreg_faultin() instead). There may be
		 * other cases as well
		 */
	}
#if 0
	ret->dat = backend->alloc_gpr(curfunc, 4, il, NULL); /* XXX */
#endif
	if (r != NULL) {
		ret->dat = r;
	} else {	
		ret->dat = ALLOC_GPR(curfunc, backend->get_ptr_size(),
			il, NULL); /* XXX */
	}

	append_icode_list(il, ret);
	return ret->dat;
}

struct icode_instr *
icode_make_inc(struct vreg *vr) {
	return generic_icode_make_instr(NULL, vr, INSTR_INC);
}

struct icode_instr *
icode_make_dec(struct vreg *vr) {
	return generic_icode_make_instr(NULL, vr, INSTR_DEC);
}

/*
 * =====================================================
 * x86-specific instructions
 * =====================================================
 */
void
icode_make_x86_fxch(struct reg *r, struct reg *r2, struct icode_list *il) {
	struct icode_instr	*ret = alloc_icode_instr();
	struct vreg		*tmp = r2->vreg;

	ret->type = INSTR_X86_FXCH;
	ret->src_pregs = make_icode_pregs(NULL, r);
	ret->dest_pregs = make_icode_pregs(NULL, r2);
	append_icode_list(il, ret);
	if (r->vreg != NULL) {
		r->vreg->pregs[0] = r2;
		r2->vreg = r->vreg;
	}
	if (tmp != NULL) {
		r->vreg = tmp;
		r->vreg->pregs[0] = r;
	}	
}

void
icode_make_asm(struct inline_asm_stmt *inl, struct icode_list *il) {
	struct icode_instr	*ret = alloc_icode_instr();
	ret->type = INSTR_ASM;
	ret->dat = inl;
	append_icode_list(il, ret);
}	

void
icode_make_x86_cdq(struct icode_list *il) {
	struct icode_instr	*ret = alloc_icode_instr();
	ret->type = INSTR_X86_CDQ;
	append_icode_list(il, ret);
}	

void
icode_make_x86_ffree(struct reg *r, struct icode_list *il) {
	struct icode_instr	*ret = alloc_icode_instr();
	ret->type = INSTR_X86_FFREE;
	ret->src_pregs = make_icode_pregs(NULL, r);
	append_icode_list(il, ret);
}

void
icode_make_x86_store_x87cw(struct vreg *vr, struct icode_list *il) {
	struct icode_instr	*ii;
	
	ii = generic_icode_make_instr(NULL, vr, INSTR_X86_FNSTCW);
	append_icode_list(il, ii);
}

void
icode_make_x86_load_x87cw(struct vreg *vr, struct icode_list *il) {
	struct icode_instr	*ii;
	
	ii = generic_icode_make_instr(NULL, vr, INSTR_X86_FLDCW);
	append_icode_list(il, ii);
}

void
icode_make_x86_fild(struct reg *r,
	struct vreg *vr, struct icode_list *il) {

	struct icode_instr	*ii;
	struct filddata		*fdat;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_X86_FILD);
	fdat = n_xmalloc(sizeof *fdat);
	fdat->r = r;
	fdat->vr = vr;
	ii->dat = fdat;
	append_icode_list(il, ii);
}

void
icode_make_x86_fist(struct reg *r,
	struct vreg *vr, struct type *ty, struct icode_list *il) {
	
	struct icode_instr	*ii;
	struct fistdata		*fdat;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_X86_FIST);
	fdat = n_xmalloc(sizeof *fdat);
	fdat->r = r;
	fdat->vr = vr;
	fdat->target_type = ty;
	ii->dat = fdat;
	append_icode_list(il, ii);
}


/*
 * =====================================================
 * MIPS-specific instructions
 * =====================================================
 */

void
icode_make_mips_mtc1(
	struct reg *dest,
	struct vreg *src,
	struct icode_list *il) {
	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, src, INSTR_MIPS_MTC1);
	ii->dat = dest;
	append_icode_list(il, ii);
}

void
icode_make_mips_mfc1(
	struct reg *dest,
	struct vreg *src,
	struct icode_list *il) {
	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, src, INSTR_MIPS_MFC1);
	ii->dat = dest;
	append_icode_list(il, ii);
}

void
icode_make_mips_cvt(
	struct vreg *dest,
	struct vreg *src,
	struct icode_list *il) {

	struct icode_instr	*ii;

	ii = generic_icode_make_instr(dest, src, INSTR_MIPS_CVT);
	append_icode_list(il, ii);
}

void
icode_make_mips_trunc(
	struct vreg *dest,
	struct vreg *src,
	struct icode_list *il) {

	struct icode_instr	*ii;

	ii = generic_icode_make_instr(dest, src, INSTR_MIPS_TRUNC);
	append_icode_list(il, ii);
}

void
icode_make_mips_make_32bit_mask(struct reg *r, struct icode_list *il) {
	struct icode_instr	*ii = alloc_icode_instr();

	ii->type = INSTR_MIPS_MAKE_32BIT_MASK;
	ii->dat = r;
	append_icode_list(il, ii);
}

/*
 * =====================================================
 * PowerPC-specific instructions
 * =====================================================
 */
void
icode_make_power_srawi(struct reg *dest, struct reg *src,
	int bits, struct icode_list *il) {

	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_POWER_SRAWI);
	ii->dest_pregs = make_icode_pregs(NULL, dest);
	ii->src_pregs = make_icode_pregs(NULL, src);
	ii->dat = n_xmemdup(&bits, sizeof(int));
	append_icode_list(il, ii);
}

void
icode_make_power_rldicl(struct reg *dest, struct reg *src,
	int bits, struct icode_list *il) {

	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_POWER_RLDICL);
	ii->dest_pregs = make_icode_pregs(NULL, dest);
	ii->src_pregs = make_icode_pregs(NULL, src);
	ii->dat = n_xmemdup(&bits, sizeof(int));
	append_icode_list(il, ii);
}

void
icode_make_power_fcfid(struct reg *dest, struct reg *src,
	struct icode_list *il) {

	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_POWER_FCFID);
	ii->dest_pregs = make_icode_pregs(NULL, dest);
	ii->src_pregs = make_icode_pregs(NULL, src);
	append_icode_list(il, ii);
}

void
icode_make_power_frsp(struct reg *dest, struct icode_list *il) {
	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_POWER_FRSP);
	ii->dat = dest;
	append_icode_list(il, ii);
}


void
icode_make_power_slwi(struct reg *dest, struct reg *src,
	int bits, struct icode_list *il) {
	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_POWER_SLWI);
	ii->dest_pregs = make_icode_pregs(NULL, dest);
	ii->src_pregs = make_icode_pregs(NULL, src);
	ii->dat = n_xmemdup(&bits, sizeof(int));
	append_icode_list(il, ii);
}

void
icode_make_power_rlwinm(struct reg *dest, struct reg *src, int
	value, struct icode_list *il) {

	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_POWER_RLWINM);
	ii->dest_pregs = make_icode_pregs(NULL, dest);
	ii->src_pregs = make_icode_pregs(NULL, src);
	ii->dat = n_xmemdup(&value, sizeof(int));
	append_icode_list(il, ii);
}

void
icode_make_power_extsb(struct reg *r, struct icode_list *il) {
	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_POWER_EXTSB);
	ii->dat = r;
	append_icode_list(il, ii);
}

void
icode_make_power_extsh(struct reg *r, struct icode_list *il) {
	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_POWER_EXTSH);
	ii->dat = r;
	append_icode_list(il, ii);
}

void
icode_make_power_extsw(struct reg *r, struct icode_list *il) {
	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_POWER_EXTSW);
	ii->dat = r;
	append_icode_list(il, ii);
}

 
void
icode_make_power_xoris(struct reg *dest, int val, struct icode_list *il) {
	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_POWER_XORIS);
	ii->dest_pregs = make_icode_pregs(NULL, dest);
	ii->dat = n_xmemdup(&val, sizeof(int));
	append_icode_list(il, ii);
}

void
icode_make_power_lis(struct reg *dest, int val, struct icode_list *il) {
	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_POWER_LIS);
	ii->dest_pregs = make_icode_pregs(NULL, dest);
	ii->dat = n_xmemdup(&val, sizeof(int));
	append_icode_list(il, ii);
}

void
icode_make_power_loadup4(struct reg *dest, struct vreg *src,
	struct icode_list *il) {

	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, src, INSTR_POWER_LOADUP4);
	ii->dest_pregs = make_icode_pregs(NULL, dest);
	append_icode_list(il, ii);
}

void
icode_make_power_fctiwz(struct reg *r, int for_unsigned, struct icode_list *il) {
	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_POWER_FCTIWZ);
	ii->dat = r;
	if (for_unsigned) {
		ii->hints |= HINT_INSTR_GENERIC_MODIFIER;
	}
	append_icode_list(il, ii);
}

/*
 * =====================================================
 * SPARC-specific instructions
 * =====================================================
 */

void
icode_make_sparc_load_int_from_ldouble(struct reg *dest,
	struct vreg *src,
	struct icode_list *il) {

	struct icode_instr		*ii;

	ii = generic_icode_make_instr(NULL, NULL,
			INSTR_SPARC_LOAD_INT_FROM_LDOUBLE);
	ii->src_vreg = src;
	ii->dat = dest;
	append_icode_list(il, ii);
}

/*
 * =====================================================
 * AMD64-specific instructions
 * =====================================================
 */
void
icode_make_amd64_cvtsi2sd(
	struct reg *dest,
	struct vreg *src,
	struct icode_list *il) {

	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, src, INSTR_AMD64_CVTSI2SD);
	ii->dest_pregs = make_icode_pregs(NULL, dest);
	append_icode_list(il, ii);
}

void
icode_make_amd64_cvtsi2sdq(
	struct reg *dest,
	struct vreg *src,
	struct icode_list *il) {

	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, src, INSTR_AMD64_CVTSI2SDQ);
	ii->dest_pregs = make_icode_pregs(NULL, dest);
	append_icode_list(il, ii);
}

void
icode_make_amd64_cvtsi2ssq(
	struct reg *dest,
	struct vreg *src,
	struct icode_list *il) {

	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, src, INSTR_AMD64_CVTSI2SSQ);
	ii->dest_pregs = make_icode_pregs(NULL, dest);
	append_icode_list(il, ii);
}

void
icode_make_amd64_cvtsi2ss(
	struct reg *dest,
	struct vreg *src,
	struct icode_list *il) {

	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, src, INSTR_AMD64_CVTSI2SS);
	ii->dest_pregs = make_icode_pregs(NULL, dest);
	append_icode_list(il, ii);
}

void
icode_make_amd64_cvttss2si(
	struct reg *dest,
	struct reg *src,
	struct icode_list *il) {

	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_AMD64_CVTTSS2SI);
	ii->dest_pregs = make_icode_pregs(NULL, dest);
	ii->src_pregs = make_icode_pregs(NULL, src);
	append_icode_list(il, ii);
}

void
icode_make_amd64_cvttsd2si(
	struct reg *dest,
	struct reg *src,
	struct icode_list *il) {

	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_AMD64_CVTTSD2SI);
	ii->dest_pregs = make_icode_pregs(NULL, dest);
	ii->src_pregs = make_icode_pregs(NULL, src);
	append_icode_list(il, ii);
}

void
icode_make_amd64_cvttss2siq(
        struct reg *dest,
        struct reg *src,
        struct icode_list *il) {

        struct icode_instr      *ii;

        ii = generic_icode_make_instr(NULL, NULL, INSTR_AMD64_CVTTSS2SIQ);
        ii->dest_pregs = make_icode_pregs(NULL, dest);
        ii->src_pregs = make_icode_pregs(NULL, src);
        append_icode_list(il, ii);
}

void
icode_make_amd64_cvtss2sd(struct reg *r, struct icode_list *il) {
	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_AMD64_CVTSS2SD);
	ii->dat = r;
	append_icode_list(il, ii);
}

void
icode_make_amd64_cvttsd2siq(
        struct reg *dest,
        struct reg *src,
        struct icode_list *il) {

        struct icode_instr      *ii;

        ii = generic_icode_make_instr(NULL, NULL, INSTR_AMD64_CVTTSD2SIQ);
        ii->dest_pregs = make_icode_pregs(NULL, dest);
        ii->src_pregs = make_icode_pregs(NULL, src);
        append_icode_list(il, ii);
}


void
icode_make_amd64_cvtsd2ss(struct reg *r, struct icode_list *il) {
	struct icode_instr	*ii;

	ii = generic_icode_make_instr(NULL, NULL, INSTR_AMD64_CVTSD2SS);
	ii->dat = r;
	append_icode_list(il, ii);
}

void
icode_make_amd64_load_negmask(struct reg *dest, struct reg *support, int for_double,
	struct icode_list *il) {

	struct icode_instr		*ii;
	static struct vreg		dummyvr;
	struct amd64_negmask_data	*dat = n_xmalloc(sizeof *dat);

	ii = generic_icode_make_instr(NULL, NULL, INSTR_AMD64_LOAD_NEGMASK);
/*	ii->dat = r;*/
	dat->target_fpr = dest;
	dat->support_gpr = support; 
	ii->dat = dat;

	if (for_double) {
		/* XXX ugly kludge to distinguish between float and double */
		ii->src_vreg = &dummyvr;
		amd64_need_negmask |= 2;
	} else {
		amd64_need_negmask |= 1;
	}

	append_icode_list(il, ii);
}

void
icode_make_amd64_xorps(struct vreg *dest, struct reg *src, struct icode_list *il) {
	struct icode_instr	*ii;

	ii = generic_icode_make_instr(dest, NULL, INSTR_AMD64_XORPS);
	ii->dat = src;
	append_icode_list(il, ii);
}

void
icode_make_amd64_xorpd(struct vreg *dest, struct reg *src, struct icode_list *il) {
	struct icode_instr	*ii;

	ii = generic_icode_make_instr(dest, NULL, INSTR_AMD64_XORPD);
	ii->dat = src;
	append_icode_list(il, ii);
}


void
icode_make_amd64_ulong_to_float(struct reg *src_gpr, struct reg *temp,
	struct reg *dest_sse_reg, int code, struct icode_list *il) {

	struct icode_instr		*ii;
	struct amd64_ulong_to_float	*data = n_xmalloc(sizeof *data);

	if (code == TY_LDOUBLE /* AMD64 */
		|| backend->arch == ARCH_X86) {
		amd64_need_ulong_float_mask = 1;
	}

	ii = generic_icode_make_instr(NULL, NULL, INSTR_AMD64_ULONG_TO_FLOAT);
	data->src_gpr = src_gpr;
	data->temp_gpr = temp;
	data->dest_sse_reg = dest_sse_reg;
	data->code = code;
	ii->dat = data;
	append_icode_list(il, ii);
}

struct icode_instr *
icode_make_seqpoint(struct var_access *stores) {
	struct icode_instr	*ret = alloc_icode_instr();
	ret->type = INSTR_SEQPOINT;
	ret->dat = stores;
	return ret;
}

extern int lastmapseq;
extern struct vreg *poi;

void
icode_make_debug(struct icode_list *il, const char *fmt, ...) {
	struct icode_instr	*ret = alloc_icode_instr();
	char			buf[2048];
	struct reg		*trashed_reg = NULL;
	va_list			va;

#define EXTRAMSG 0 
#if EXTRAMSG 
	char 			extramsg[128];

	*extramsg = 0;

	if (poi)
	sprintf(extramsg, " eax %p / poi %p  ->  %p   %p   ", x86_gprs[0].vreg,poi,poi->pregs[0],poi->pregs[1]);
#endif

	va_start(va, fmt);
	vsprintf(buf, fmt, va);
	va_end(va);

#if EXTRAMSG
	strcat(buf, extramsg);
#endif

	if (0  &&   emit->debug != NULL) {
		/*
		 * 2014/09/28: We generate code to actually emit the message at runtime. This
		 * involves constructing a message and loading it into a register, passing on
		 * the debug instruction to the emitter, and restoring the register we used - if
		 * necessary. It is assumed that the emitter will not touch any other registers.
	 	 *
		 * This is a quick kludge to get this functionality up and running on AMD64.
		 * The debug instruction emitter must also use pop to restore the register.
		 */
		struct token		*tok = const_from_string(buf);
		struct vreg		*msg_vreg = vreg_alloc(NULL, tok, NULL, NULL);
		
		if (amd64_argregs[0]->vreg != NULL && amd64_argregs[0]->vreg->pregs[0] == amd64_argregs[0] && amd64_argregs[0]->used) {
			struct icode_instr	*ii = icode_make_push(amd64_argregs[0]->vreg, il);
			trashed_reg = amd64_argregs[0];
			append_icode_list(il, ii);
			ii = icode_make_push(amd64_argregs[0]->vreg, il);
			append_icode_list(il, ii);
		}

	        vreg_faultin(amd64_argregs[0], NULL, msg_vreg, il, 0);
	}

	ret->type = INSTR_DEBUG;
	ret->dat = n_xstrdup(buf);
	if (trashed_reg != NULL) {
		ret->hints |= HINT_INSTR_GENERIC_MODIFIER;
	}

	append_icode_list(il, ret);
}

void
icode_make_dbginfo_line(struct statement *stmt, struct icode_list *il) {
	struct icode_instr	*ii = NULL;
	struct decl		*dec;
	struct expr		*ex;

	if (!gflag) return;

	switch (stmt->type) {
	case ST_CODE:
		ii = alloc_icode_instr();
		ii->type = INSTR_DBGINFO_LINE;
		ex = stmt->data;
		ii->dat = ex->tok;
		break;
	case ST_DECL:
		/* A declaration with initializer has ``code'' */
		dec = stmt->data;
		if (dec->init != NULL) {
			ii = alloc_icode_instr();
			ii->type = INSTR_DBGINFO_LINE;
			ex = dec->init->data;
			ii->dat = ex->tok; 
		}
		break;
	default:
		abort();
	}
	if (ii != NULL) { /* XXX */
		append_icode_list(il, ii);
	}	
}



int	unimpl_instr;

void
icode_make_unimpl(struct icode_list *il) {
	struct icode_instr	*ii = alloc_icode_instr();

	unimpl_instr = 1;
	ii->type = INSTR_UNIMPL;
	unimpl();
	append_icode_list(il, ii);
}	

void
icode_make_copyinit(struct decl *d, struct icode_list *il) {
	struct icode_instr	*ii = alloc_icode_instr();

	ii->type = INSTR_COPYINIT;
	ii->dat = d; 
	append_icode_list(il, ii);
}


void
icode_make_putstructregs(struct reg *firstreg,
	struct reg *ptrreg,
	struct vreg *vr,
	struct icode_list *il) {

	struct icode_instr	*ii = alloc_icode_instr();
	struct putstructregs	*ps = n_xmalloc(sizeof *ps);

	ps->ptrreg = ptrreg;
	ps->destreg = firstreg;
	ps->src_vreg = vr;

	ii->type = INSTR_PUTSTRUCTREGS;
	ii->dat = ps;
	append_icode_list(il, ii);
}

/*
 * Copy struct src to struct dest. Both may be automatic, static,  
 * indirect, etc. If dest is a null pointer, then it is assumed that
 * the current function returns a structure type, and src will be
 * copied to the hidden pointer for struct returns (hidden_pointer
 * member of ``struct function''). This only works with the MIPS
 * backend right now.
 *
 * If any argument comes from a pointer, that pointer will be loaded
 * into a register as necessary. Note that most backends currently
 * call memcpy() to copy structs, so it is essential to save all GPRs
 * as necessary (backend->invalidate_gprs().) Perhaps it would make
 * sense to add a backend->prepare_copystruct() for register saving.
 *
 * XXX seems dest_preg and src_preg are never used and conceptually
 * totally nonsense because a struct is never gpr-resident
 */
void
icode_make_copystruct(
	struct vreg *dest,
	struct vreg *src,
	struct icode_list *il) {

	struct icode_instr	*ii = alloc_icode_instr();
	struct copystruct		*cs;
	struct vreg			*stop;
	static struct copystruct	nullcs;


	/*
	 * 05/22/11: Zero-sized arrays are possible in GNU C;
	 *   struct foo { char x[0]; }
	 * A gcc test suite case uses this and causes a crash
	 * when passing such a struct to a function
	 */
	if (src->size == 0) {
		/* Empty struct - nothing to do */
		return;
	}

	cs = n_xmalloc(sizeof *cs);
	*cs = nullcs;

	/* If source is indirect, do not trash pointer register */
	vreg_set_unallocatable(src);

	cs->dest_vreg = dest;
	if (dest != NULL) {
		vreg_faultin_ptr(dest, il);
		cs->dest_preg = dest->pregs[0];
		if (dest->from_ptr) {
			cs->dest_from_ptr = dest->from_ptr->pregs[0];
		} else if (dest->parent
			&& (stop = get_parent_struct(dest))->from_ptr) {
			cs->dest_from_ptr = NULL;
			cs->dest_from_ptr_struct = stop->from_ptr->pregs[0];
		} else {
			cs->dest_from_ptr = NULL;
		}	
		vreg_set_unallocatable(dest);
	}
	cs->src_vreg = src;
	cs->src_preg = src->pregs[0];

	vreg_faultin_ptr(src, il);
	vreg_set_allocatable(src);
	if (dest != NULL) {
		vreg_set_allocatable(dest);
	}
	if (src->from_ptr) {
		cs->src_from_ptr = src->from_ptr->pregs[0];
	} else if (src->parent
		&& (stop = get_parent_struct(src))->from_ptr) {
		cs->src_from_ptr = NULL;
		cs->src_from_ptr_struct = stop->from_ptr->pregs[0];
	} else {
		cs->src_from_ptr = NULL;
	}

	if (backend->icode_make_structreloc) {
		backend->icode_make_structreloc(cs, il);
	}

	ii->type = INSTR_COPYSTRUCT;
	ii->dat = cs;
	append_icode_list(il, ii);
}

/*
 * Intrinsically copy nbytes of src to dest. Currently dest and src must
 * be pointers or arrays. This is currently only used by __builtin_memcpy,
 * but should eventually replace INSTR_COPYSTRUCT and INSTR_COPYINIT.
 * If may_call_lib is set, the library memcpy() may be called, which
 * makes sense for huge data items (the primary point of __builtin_memcpy
 * is probably to avoid library dependencies.)
 */
void
icode_make_intrinsic_memcpy_or_memset(int type,
	struct vreg *dest,
	struct vreg *src,
	struct vreg *nbytes,
	int may_call_lib,
	struct icode_list *il) {

	struct icode_instr	*ii = alloc_icode_instr();
	struct int_memcpy_data	*imdata = n_xmalloc(sizeof *imdata);
	struct reg		*dest_reg;
	struct reg		*src_reg;
	struct reg		*temp_reg;

	ii->type = INSTR_INTRINSIC_MEMCPY;

	imdata->type = type;
	if (src->type->tlist == NULL
		|| src->type->tlist == NULL) {
		/*
		 * In the future we should allow copying non-
		 * pointer objects too (take the address here)
		 * for convenient struct and initializer copying
		 */
		if (type == BUILTIN_MEMCPY) {
			unimpl();
		}
	}


	vreg_faultin_ptr(dest, il);
	if (dest->from_ptr) {
		dest_reg = dest->from_ptr->pregs[0];
	} else {
		/* Array or function */
		dest_reg = dest->pregs[0];
	}
	reg_set_unallocatable(dest_reg);

	if (type == BUILTIN_MEMCPY) {
		vreg_faultin_ptr(src, il);
		if (src->from_ptr) {
			src_reg = src->from_ptr->pregs[0];
		} else {
			/* Array or function */
			src_reg = src->pregs[0];
		}
	} else {
		vreg_faultin(NULL, NULL, src, il, 0);
		src_reg = src->pregs[0];
	}
	reg_set_unallocatable(src_reg);

	/*
	 * Now allocate temporary register for holding one byte. This
	 * will be used for byte-wise simplistic copying by all
	 * backends until more sophisticated implementations are
	 * written. Then it will be time to have some sort of backend
	 * flag which tells us whether such a temp reg is needed or
	 * not, and what else may have to be done here
	 *
	 * 02/09/09: Allocation moved up before nbytes allocation.
	 * This was needed on OSX with PIC code because otherwise we
	 * may run out of GPRs. This is because ebx is reserved as PIC
	 * register, eax to ecx may be allocated for the memcpy()
	 * arguments, and then we have no registers left for the 1
	 * byte temp reg because esi and edi are unusable for that
	 * purpose! (Since they do not have 8bit sub registers)
	 *
	 * 02/09/09: OK this STILL failed in case there is only one
	 * allocatable GPR, and that GPR was allocated the last time
	 * alloc_gpr() got called.
	 * So we have to relax the alloc_gpr() constraint which
	 * prevents it from allocating the same GPR as during the
	 * last invocation
	 */
	backend->relax_alloc_gpr_order = 1;
	temp_reg = ALLOC_GPR(curfunc, 1, il, NULL);
	backend->relax_alloc_gpr_order = 0;
	if (temp_reg == NULL) {
		(void) fprintf(stderr, "BUG: Cannot allocate GPR\n");
		abort();
	}

	reg_set_unallocatable(temp_reg);

	vreg_faultin(NULL, NULL, nbytes, il, 0);
	reg_set_unallocatable(nbytes->pregs[0]);

#if 0
	/* 02/09/09: Allocation moved up */
	/*
	 * Now allocate temporary register for holding one byte. This
	 * will be used for byte-wise simplistic copying by all
	 * backends until more sophisticated implementations are
	 * written. Then it will be time to have some sort of backend
	 * flag which tells us whether such a temp reg is needed or
	 * not, and what else may have to be done here
	 */
	temp_reg = ALLOC_GPR(curfunc, 1, il, NULL);
#endif

	reg_set_allocatable(dest_reg);
	reg_set_allocatable(src_reg);
	reg_set_allocatable(nbytes->pregs[0]);
	reg_set_allocatable(temp_reg);

	imdata->dest_addr = dest_reg;
	imdata->src_addr = src_reg;
	imdata->nbytes = nbytes->pregs[0];
	imdata->temp_reg = temp_reg;
	if (may_call_lib /*  && makes_sense_to_call_lib */) {
		imdata->dest_addr->used = 0;
		imdata->src_addr->used = 0;
		imdata->nbytes->used = 0;
		imdata->temp_reg->used = 0;
		backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
	}
	ii->dat = imdata;
	append_icode_list(il, ii);
}

void
icode_make_dealloca(struct stack_block *sb, struct icode_list *il) {
	struct icode_instr	*ii = alloc_icode_instr();
	struct reg		*r;
	struct type		*st;

	st = backend->get_size_t();

	r = backend->get_abi_reg(0, st);
	if (r == NULL) {
		r = ALLOC_GPR(curfunc, backend->get_sizeof_type(st, 0), il, 0); 
	}

	ii->type = INSTR_DEALLOCA;
	ii->src_pregs = make_icode_pregs(NULL, r);
	ii->dat = sb;
	append_icode_list(il, ii);
}

void
icode_make_alloca(struct reg *r, struct vreg *size_vr,
	struct stack_block *sb,
	struct icode_list *il) {

	struct allocadata	*a = n_xmalloc(sizeof *a);
	struct icode_instr	*ii = alloc_icode_instr();

	/*
	 * First we unconditionally free the hidden pointer slot  
	 * for this allocation. Such that
	 *
	 *   for (i = 0; i < 10; ++i) {
	 *      void *p = __builtin_alloca(20);
	 *
	 * ... always performs a free() before overwriting the
	 * saved pointer with a newly allocated one. This is OK
	 * even at the first call because the save area is
	 * initialized to all-null-pointers, and free(NULL) is ok
	 *
	 * XXX 08/09/07: This is completely wrong. Removed the
	 * free, thus repeated allocas leak memory now. But better
	 * than not having it work at all
	 */
	backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
#if 0
	icode_make_dealloca(sb, il);
#endif

	reg_set_unallocatable(r);
	vreg_faultin(NULL, NULL, size_vr, il, 0);
	a->result_reg = r;
	a->size_reg = size_vr->pregs[0];
	a->addr = sb;

	ii->dat = a;
	ii->type = INSTR_ALLOCA;

	/*
	 * Because the current implementation is carried out
	 * using malloc() and free(), we have to save all
	 * registers except for the size argument register!
	 */
	a->result_reg->used = 0;
	a->size_reg->used = 0;
	reg_set_allocatable(r);
	backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
	append_icode_list(il, ii);
#if 0
	store_reg_to_stack_block(a->result_reg, a->addr);
#endif
}

void
icode_make_dealloc_vla(struct stack_block *sb, struct icode_list *il) {
	struct icode_instr	*ii = alloc_icode_instr();

#if 0
	r = backend->get_abi_reg(0, st);
	if (r == NULL) {
		r = ALLOC_GPR(curfunc, backend->get_sizeof_type(st, 0), il, 0); 
	}
#endif

	ii->type = INSTR_DEALLOC_VLA;
#if 0
	ii->src_pregs = make_icode_pregs(NULL, r);
#endif
	ii->dat = sb;
	append_icode_list(il, ii);
}

void
icode_make_alloc_vla(
	struct stack_block *sb,
	struct icode_list *il) {

	struct allocadata	*a = n_xmalloc(sizeof *a);
	struct icode_instr	*ii = alloc_icode_instr();

	/*
	 * First we unconditionally free the hidden pointer slot  
	 * for this allocation. Such that
	 *
	 *   for (i = 0; i < 10; ++i) {
	 *      void *p = __builtin_alloca(20);
	 *
	 * ... always performs a free() before overwriting the
	 * saved pointer with a newly allocated one. This is OK
	 * even at the first call because the save area is
	 * initialized to all-null-pointers, and free(NULL) is ok
	 */
	backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
	icode_make_dealloc_vla(sb, il);

	ii->dat = sb;
	ii->type = INSTR_ALLOC_VLA;

	/*
	 * Because the current implementation is carried out
	 * using malloc() and free(), we have to save all
	 * registers
	 */
	backend->invalidate_gprs(il, 1, INV_FOR_FCALL);
	append_icode_list(il, ii);
}

void
icode_make_builtin_frame_address(struct reg *r, struct reg *r2,
	size_t *n, struct icode_list *il) {

	struct icode_instr		*ii = alloc_icode_instr();
	struct builtinframeaddressdata	*dat = n_xmalloc(sizeof *dat);

	ii->type = INSTR_BUILTIN_FRAME_ADDRESS;
	dat->result_reg = r;
	dat->temp_reg = r2;
	dat->count = n;
	
	ii->dat = dat;
	append_icode_list(il, ii);
}


void
icode_make_put_vla_whole_size(struct reg *size, struct stack_block *sb,
	struct icode_list *il) {

	struct icode_instr	*ii = alloc_icode_instr();
	struct vlasizedata	*dat = n_xmalloc(sizeof *dat);

	ii->type = INSTR_PUT_VLA_SIZE;
	dat->size = size;
	dat->offset = backend->get_ptr_size();
	dat->blockaddr = sb;
	ii->dat = dat;
	append_icode_list(il, ii);
}


void
icode_make_put_vla_size(struct reg *size, struct stack_block *sb,
	int idx, struct icode_list *il) {

	struct icode_instr	*ii = alloc_icode_instr();
	struct vlasizedata	*dat = n_xmalloc(sizeof *dat);
	int			offset;

	offset = backend->get_ptr_size()
		+ (1 + idx) * backend->get_sizeof_type(
				make_basic_type(TY_ULONG), NULL);
	
	ii->type = INSTR_PUT_VLA_SIZE;
	dat->size = size;
	dat->blockaddr = sb;
	dat->offset = offset;
	ii->dat = dat;
	append_icode_list(il, ii);
}

struct vreg *
icode_make_retr_vla_size(struct reg *size, struct stack_block *sb,
	int idx, struct icode_list *il) {

	struct icode_instr	*ii = alloc_icode_instr();
	struct vlasizedata	*dat = n_xmalloc(sizeof *dat);
	struct vreg		*ret;
	int			offset;

	offset = backend->get_ptr_size()
		+ (1 + idx) * backend->get_sizeof_type(
				make_basic_type(TY_ULONG), NULL);
	
	
	ii->type = INSTR_RETR_VLA_SIZE;
	dat->size = size;
	dat->blockaddr = sb;
	dat->offset = offset;
	ii->dat = dat;
	append_icode_list(il, ii);
	ret = vreg_alloc(NULL, NULL, NULL, NULL);
	vreg_set_new_type(ret, make_basic_type(TY_ULONG));
	vreg_map_preg(ret, size);
	return ret;
}

void
icode_make_allocstack(struct vreg *vr, size_t size, struct icode_list *il) {
	struct icode_instr	*ii = alloc_icode_instr();
	struct allocstack	*as = n_xmalloc(sizeof *as);

	as->nbytes = size;
	as->patchme = vr;

	ii->type = INSTR_ALLOCSTACK;
	ii->dat = as;
	append_icode_list(il, ii);
}

/*
 * XXX should this really deal with multi-gpr objects?!  
 */
struct icode_instr *
icode_make_cmp(struct vreg *dest, struct vreg *src) {
	struct icode_instr	*ret = alloc_icode_instr();

	ret->type = INSTR_CMP;
	ret->dest_vreg = dest;
	ret->dest_pregs = make_icode_pregs(dest, NULL);
	if (src != NULL) {
		ret->src_pregs = make_icode_pregs(src, NULL);
		ret->src_vreg = src;
	}
	return ret;
}

void
icode_make_extend_sign(struct vreg *vr, struct type *to, struct type *from,
	struct icode_list *il) {

	struct icode_instr	*ret = alloc_icode_instr();
	struct extendsign	*data = n_xmalloc(sizeof *data);

	data->dest_preg = vr->pregs[0];
	data->dest_type = to;
	data->src_type = from;
	ret->dat = data;
	ret->type = INSTR_EXTEND_SIGN;
	append_icode_list(il, ret);
}

/* XXX misleading order of to/from :/ */
void
icode_make_conv_fp(struct reg *destr, struct reg *srcr,
	struct type *to, struct type *from,
	struct icode_list *il) {

	struct icode_instr	*ret = alloc_icode_instr();
	struct extendsign	*data = n_xmalloc(sizeof *data);

	data->dest_preg = destr;
	data->src_preg = srcr;
	data->dest_type = to;
	data->src_type = from;
	ret->dat = data;
	ret->type = INSTR_CONV_FP;
	append_icode_list(il, ret);
}

void
icode_make_conv_to_ldouble(struct vreg *dest, struct vreg *src,
	struct icode_list *il) {

	struct icode_instr	*ii;

	ii = generic_icode_make_instr(dest, src, INSTR_CONV_TO_LDOUBLE);
	append_icode_list(il, ii);
}

void
icode_make_conv_from_ldouble(struct vreg *dest, struct vreg *src,
	struct icode_list *il) {

	struct icode_instr	*ii;

	ii = generic_icode_make_instr(dest, src, INSTR_CONV_FROM_LDOUBLE);
	append_icode_list(il, ii);
}


struct icode_instr *
icode_make_jump(struct icode_instr *label) {
	struct icode_instr	*ret = alloc_icode_instr();
	ret->type = INSTR_JUMP;
	ret->dat = label;
	return ret;
}

void
put_label_scope(struct label *l) {
	struct statement	*stmt;

	stmt = alloc_statement();
	stmt->type = ST_LABEL;
	stmt->data = l;
	append_statement(
		&curscope->code, &curscope->code_tail, stmt);
	append_label_list(&curfunc->labels_head, &curfunc->labels_tail, l);
}


void
put_expr_scope(struct expr *e) {
	struct statement	*stmt;

	stmt = alloc_statement();
	stmt->type = ST_CODE;
	stmt->data = e;
	append_statement(&curscope->code, &curscope->code_tail, stmt);
}

void
put_ctrl_scope(struct control *c) {
	struct statement	*stmt;

	stmt = alloc_statement();
	stmt->type = ST_CTRL;
	stmt->data = c;
	append_statement(&curscope->code, &curscope->code_tail, stmt);
}

/*
 * XXX acts as fxch for x86 fp...is this ok?
 */
void
icode_make_copyreg(
        struct reg *dest,
        struct reg *src,
        struct type *desttype,
        struct type *srctype,
        struct icode_list *il) {

        struct icode_instr      *ii = alloc_icode_instr();
        struct copyreg          *cr = n_xmalloc(sizeof *cr);

if (dest->type != src->type) abort();
        cr->src_preg = src;
        cr->dest_preg = dest;
        cr->src_type = srctype;
        cr->dest_type = desttype;

        ii->type = INSTR_MOV;
        ii->dat = cr;

        append_icode_list(il, ii);
}

void
add_const_to_vreg(struct vreg *valist_vr, int size, struct icode_list *il) {
	struct token		*c;
	struct vreg		*tmpvr;
	struct icode_instr	*ii;

	c = const_from_value(&size, NULL);
	tmpvr = vreg_alloc(NULL, c, NULL, NULL);
	vreg_faultin(NULL, NULL, tmpvr, il, 0);
	vreg_faultin_protected(tmpvr, NULL, NULL, /*valist->vreg*/valist_vr, il, 0);
	ii = icode_make_add(  /*valist->vreg*/valist_vr, tmpvr);
	append_icode_list(il, ii);
	icode_make_store(curfunc, /*valist->vreg*/valist_vr,
		/*valist->vreg*/valist_vr, il);
}

