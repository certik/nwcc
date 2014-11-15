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
 * Emit NASM code from intermediate x86 code
 */
#include "x86_emit_nasm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <ctype.h>
#include "scope.h"
#include "type.h"
#include "decl.h"
#include "icode.h"
#include "subexpr.h"
#include "token.h"
#include "functions.h"
#include "typemap.h"
#include "symlist.h"
#include "dwarf.h"
#include "cc1_main.h"
#include "x86_gen.h"
#include "amd64_gen.h"
#include "amd64_emit_yasm.h"
#include "expr.h"
#include "inlineasm.h"
#include "error.h"
#include "n_libc.h"

static FILE	*out;
static size_t	data_segment_offset;
static size_t	bss_segment_offset;
static size_t	thread_data_segment_offset;
static size_t	thread_bss_segment_offset;

static int 
init(FILE *fd, struct scope *s) {
	(void) s;
	out = fd;
	return 0;
}

static void
print_mem_operand(struct vreg *vr, struct token *constant);
struct reg *
get_smaller_reg(struct reg *r, size_t size);

/*
 * Turns either a byte size into an assembler type string. If a type
 * argument is supplied (non-null), it will ensure that ``long long''
 * is mapped to ``dword'', as that type is really dealt with as two
 * dwords rather than a qword.
 */
static char *
size_to_asmtype(size_t size, struct type *type) {
	if (type != NULL) {
		if (type->tlist == NULL) {
			if (IS_LLONG(type->code)) {
				size = 4;
			}
		}
	}	
	/* XXXXXXX long double :( */
	if (size == /*10*/ 12 || size == 10) return "tword"; /* long double */
	else if (size == 8) return "qword"; /* double */
	else if (size == 4) return "dword";
	else if (size == 2) return "word";
	else if (size == 1) return "byte";
	else {
		printf("bad size for size_to_type(): %lu\n",
			(unsigned long)size);
		abort();
	}	
	return NULL;
}

void
print_nasm_offsets(struct vreg *vr) {
	/*
	 * XXX this should probably be selectable as command line
	 * flag, -verboseoffsets or somesuch
	 */
	x_fprintf(out, "+ %lu", calc_offsets(vr));
#if 0
	do {
		char	*tag;

		if (vr->parent->type->code == TY_STRUCT) {
			tag = vr->parent->type->tstruc->tag;
			x_fprintf(out, "+ %s%s.%s",
					vr->parent->type->tstruc->unnamed? "":
						"_Struct_",
					tag, vr->type->name);
		}
		if (vr->from_ptr) {
			break;
		}	
		vr = vr->parent;
	} while (vr->parent != NULL);	
#endif
}

#if 0
static void
emit_section(const char *name) {
	x_fprintf(out, "section .%s\n", name);
}
#endif

static int	cursect = 0;

static void
emit_setsection(int value) {
	char	*p = NULL;

	if (cursect == value) {
		/* We're already in that section */
		return;
	}
	p = generic_elf_section_name(value);

	if (p != NULL) {
		x_fprintf(out, "section .%s\n", p);
	}	
	cursect = value;
}

static void
print_init_list(struct decl *dec, struct initializer *init);
void	print_nasm_string_init(size_t howmany, struct ty_string *str);
static void
print_reg_assign(struct reg *dest,
	struct reg *srcreg, size_t src_size, struct type *src_type);

static void
print_init_expr(struct type *dt, struct expr *ex) {
	int		is_addr_as_int = 0;
	struct tyval	*cv;

	cv = ex->const_value;

	if (cv && (cv->str || cv->address)) {
		if (dt->tlist == NULL) {
			/*
			 * This must be a stupid construct like
			 *    static size_t foo = (size_t)&const_addr;
			 * because otherwise the address/string would
			 * have a pointer or array type node
			 */
			is_addr_as_int = 1;
		}
	}

	x_fprintf(out, "\t");
	if (dt->tlist == NULL && !is_addr_as_int) {
		switch (dt->code) {
		case TY_CHAR:
		case TY_SCHAR:
		case TY_UCHAR:
		case TY_BOOL:	
			x_fprintf(out, "db ");
			cross_print_value_by_type(out, ex->const_value->value,
				TY_UCHAR, 'd');
			break;
		case TY_SHORT:
			x_fprintf(out, "dw ");
			cross_print_value_by_type(out, ex->const_value->value,
				TY_SHORT, 'd');
			break;
		case TY_USHORT:
			x_fprintf(out, "dw ");
			cross_print_value_by_type(out, ex->const_value->value,
				TY_USHORT, 'd');
			break;
		case TY_INT:
		case TY_ENUM:	
			x_fprintf(out, "dd ");
			cross_print_value_by_type(out, ex->const_value->value,
				TY_INT, 'd');
			break;
		case TY_UINT:
			x_fprintf(out, "dd ");
			cross_print_value_by_type(out, ex->const_value->value,
				TY_UINT, 'd');
			break;

		case TY_LONG:
		case TY_LLONG:	
			if (backend->arch != ARCH_AMD64
				&& dt->code == TY_LLONG) {
				x_fprintf(out, "dd ");
				cross_print_value_chunk(out,
					ex->const_value->value,
					TY_LLONG, TY_UINT, 0, 0);
				x_fputc('\n', out);
				x_fprintf(out, "\tdd ");
				cross_print_value_chunk(out,
					ex->const_value->value,
					TY_LLONG, TY_UINT, 0, 1);
			} else if (backend->arch == ARCH_AMD64) {
				x_fprintf(out, "dq ");
				cross_print_value_by_type(out,
					ex->const_value->value,
					TY_LONG, 'd');
			} else {	
				x_fprintf(out, "dd ");
				cross_print_value_by_type(out,
					ex->const_value->value,
					TY_LONG, 'd');
			}
			break;
		case TY_ULONG:	
		case TY_ULLONG:
			if (backend->arch != ARCH_AMD64
				&& dt->code == TY_ULLONG) {	
				x_fprintf(out, "dd ");
				cross_print_value_chunk(out,
					ex->const_value->value,
					TY_ULLONG, TY_UINT, 0, 0);
				x_fputc('\n', out);
				x_fprintf(out, "\tdd ");
				cross_print_value_chunk(out,
					ex->const_value->value,
					TY_ULLONG, TY_UINT, 0, 1);
			} else if (backend->arch == ARCH_AMD64) {
				x_fprintf(out, "dq ");
				cross_print_value_by_type(out,
					ex->const_value->value,
					TY_ULONG, 'd');
			} else {	
				x_fprintf(out, "dd ");
				cross_print_value_by_type(out,
					ex->const_value->value,
					TY_ULONG, 'd');
			}	
			break;
		case TY_FLOAT:
			x_fprintf(out, "dd ");
			cross_print_value_by_type(out,
				ex->const_value->value,
				TY_UINT, 'd');
			break;
		case TY_DOUBLE:
			x_fprintf(out, "dd ");
			cross_print_value_chunk(out,
				ex->const_value->value,
				TY_DOUBLE, TY_UINT, 0, 0);
			x_fputc('\n', out);
			x_fprintf(out, "\tdd ");
			cross_print_value_chunk(out,
				ex->const_value->value,
				TY_DOUBLE, TY_UINT, 0, 1);
			break;
		case TY_LDOUBLE:
			x_fprintf(out, "dd ");
			cross_print_value_chunk(out,
				ex->const_value->value,
				TY_LDOUBLE, TY_UINT, 0, 0);
			x_fputc('\n', out);
			x_fprintf(out, "\tdd ");
			cross_print_value_chunk(out,
				ex->const_value->value,
				TY_LDOUBLE, TY_UINT, 0, 1);
			x_fputc('\n', out);
			x_fprintf(out, "dw ");
			cross_print_value_chunk(out,
				ex->const_value->value,
				TY_LDOUBLE, TY_UINT, TY_USHORT, 2);
			/* 02/23/09: Pad to 12 */
			if (sysflag == OS_OSX || backend->arch == ARCH_AMD64) {
				x_fprintf(out, "\n\ttimes 6 db 0\n");
			} else {
				x_fprintf(out, "\n\tdw 0\n");
			}
			break;
		default:	
			printf("print_init_expr: "
				"unsupported datatype %d\n",
				dt->code);
			unimpl();
		}
	} else {
		if (is_addr_as_int || dt->tlist->type == TN_POINTER_TO) {
			if (backend->arch == ARCH_AMD64) {
				x_fprintf(out, "dq ");
			} else {	
				x_fprintf(out, "dd ");
			}
			if (cv->is_nullptr_const) {
				x_fprintf(out, "0");
			} else if (cv->str) {
				x_fprintf(out, "_Str%lu", cv->str->count);
			} else if (cv->value) {
#if 0
				x_fprintf(out, "%lu",
					*(unsigned long *)cv->value);
#endif
				/* XXX hmm... realy use unsigned long?? */
				cross_print_value_by_type(out,
					ex->const_value->value,
					TY_LONG, 'd');
			} else if (cv->address) {
				/* XXX */
				char	*sign;

				if (cv->address->diff < 0) {
					/*
					 * 08/02/09: The address diff value
					 * is already negative, so we used
					 * to get   var - - val
					 */
					sign = "";
				} else {
					sign = "+";
				}
				if (cv->address->dec != NULL) {
					/* Static variable */
					x_fprintf(out, "%s %s %ld",
						cv->address->dec->dtype->name,
						sign, cv->address->diff);
				} else {
					/* Label */
					x_fprintf(out, "%s.%s %s %ld",
						cv->address->funcname,
						cv->address->labelname,
						sign,
						cv->address->diff);
				}
			}	
		} else if (dt->tlist->type == TN_ARRAY_OF) {
			size_t	arrsize;
			struct tyval	*cv;
			
			/*
			 * This has to be a string because only in
			 * char buf[] = "hello"; will an aggregate
			 * initializer ever be stored as INIT_EXPR
			 */

			cv = ex->const_value;
			arrsize = dt->tlist->arrarg_const;
			print_nasm_string_init(arrsize, cv->str);

			if (arrsize >= cv->str->size) {
				if (arrsize > cv->str->size) {
					/* XXX not totally host-independent */
					x_fprintf(out, "\n\ttimes %lu db 0\n",
						arrsize - cv->str->size);
				}	
			} else {
				/* Do not null-terminate */
				;
			}
		}
	}
	x_fputc('\n', out);
}

/* XXX may be adaptable for different platforms */
/* XXX duplicates gas print_init_list() :-( */
static void
print_init_list(struct decl *dec, struct initializer *init) {
	struct sym_entry	*se = NULL;
	struct sym_entry	*startse = NULL;

	if (dec
		&& dec->dtype->code == TY_STRUCT
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
				if (dec
					&& dec->dtype->code == TY_STRUCT) {
					nested_dec = alloc_decl();
					nested_dec->dtype = dec->dtype;
					saved_tlist = dec->dtype->tlist;
					dec->dtype->tlist = NULL;
				}
			} else {
				nested_dec = se->dec;
			}

			print_init_list(nested_dec, init->data);

			if (saved_tlist != NULL) {
				dec->dtype->tlist = saved_tlist;
				free(nested_dec);
			}	

			/*
			 * 10/08/08: If this is a bitfield initializer, match
			 * (skip) all affected bitfield declarations in this
			 * struct. This is important for alignment
			 *
			 * XXX This is degenerating into copy&paste coding! Should
			 * share with gas!!!!!!!!!!!!!!!!!!!
			 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
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
						 && se->dec->dtype->tbit->bitfield_storage_unit == storage_unit);

					if (se != NULL
						&& (se->dec->dtype->tbit == NULL
						|| se->dec->dtype->tbit->bitfield_storage_unit != storage_unit)) {
						 /* Move back to last BF member - so we can align for next member */
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
			if (init->varinit != NULL && init->left_type->tbit != NULL) {
				continue;
			} else {
				x_fprintf(out, "\ttimes %lu db 0\n",
					(unsigned long)*(size_t *)init->data);	
				/*
				 * If this is for a struct, skip all members covered
				 * by this initializer (but keep the last one so the
				 * code below pad the struct for alignment)
			 	 */
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
				 * is properly aligned
				 *
				 * Note that we have to use the whole struct
				 * alignment, not just first member alignment
				 */
				ty = dec->dtype;
				if (init->type == INIT_NULL) {
					/*
					 * 08/08/07: Now the alignment is 
					 * hopefully done correctly for zero
					 * initializers. Previously we called
					 * calc_align_bytes() for the last
					 * member of a zero initializer, which
					 * was wrong
					 */
					size_t	curoff = startse->dec->offset +
						*(size_t *)init->data;
					size_t	alignto = backend->get_align_type(ty);
					size_t	tmp = 0;

					while ((curoff + tmp) % alignto) {
						++tmp;
					}
					if (tmp > 0) {
						x_fprintf(out, "\ttimes %lu db 0\n",
							(unsigned long)tmp);
					}	
				} else {
					d = dec->dtype->tstruc->scope->slist->dec;
				}
			}


			if (d != NULL) {
				unsigned long   offset;


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
					x_fprintf(out, "\ttimes %lu db 0\n",
						(unsigned long)nbytes);
				}
			}
			se = se->next;
		}
	}	
}


static void
emit_support_decls(void) {
	if (1) {
		/* need memcpy() */
		x_fprintf(out, "extern memcpy\n");
	}
	if (/*curfunc->alloca_head != NULL*/ 1) {
		x_fprintf(out, "extern malloc\n");
		x_fprintf(out, "extern free\n");
		x_fprintf(out, "extern memset\n");
	}
	if (picflag) {
		x_fprintf(out, "extern _GLOBAL_OFFSET_TABLE_\n");
	}
}


static void
emit_extern_decls(void) {

#if 0 
	/* Generate external references */
	if (global_scope.extern_decls.ndecls > 0) {
		d = global_scope.extern_decls.data;
		for (i = 0; i < global_scope.extern_decls.ndecls; ++i) {
			if (d[i]->references == 0
				&& (!d[i]->dtype->is_func
				|| !d[i]->dtype->is_def)) {
				/* Unneeded declaration */
				continue;
			}
			
			if (d[i]->has_def) {
				/*
				 * extern declaration overriden by later
				 * definition
				 */
				continue;
			}	

			if (d[i]->dtype->is_func
				&& d[i]->dtype->is_def) {
				d[i]->has_symbol = 1;
				/* XXX hm what about extern/static inline? */
				if (!IS_INLINE(d[i]->dtype->flags)) {
					x_fprintf(out, "global $%s\n",
						d[i]->dtype->name,
						d[i]);
				}	
			}
		}
	}	
#endif
	{
		struct sym_entry	*se;

		for (se = extern_vars; se != NULL; se = se->next) {
			if (se->dec->invalid) {
				continue;
			}
			if (se->dec->has_symbol
				|| se->dec->dtype->is_def
				|| se->dec->has_def) {
				continue;
			}
			if (se->dec->references == 0
				&& (!se->dec->dtype->is_func
				|| !se->dec->dtype->is_def)) {
				/* Unneeded declaration */
				continue;
			}
			x_fprintf(out, "extern $%s\n",
				se->dec->dtype->name);
			x_fputc('\n', out);
			se->dec->has_symbol = 1;
		}
	}	
}

static void
emit_global_extern_decls(struct decl **d, int ndecls) {
	int		i;

	/* Generate external references */
	if (ndecls > 0) {
		for (i = 0; i < ndecls; ++i) {
			if (d[i]->references == 0
				&& (!d[i]->dtype->is_func
				|| !d[i]->dtype->is_def)) {
				/* Unneeded declaration */
				continue;
			}
			
			if (d[i]->has_def) {
				/*
				 * extern declaration overriden by later
				 * definition
				 */
				continue;
			}	
			if (d[i]->invalid) {
				continue;
			}

			if (d[i]->dtype->is_func
				&& d[i]->dtype->is_def) {
				d[i]->has_symbol = 1;
				/* XXX hm what about extern/static inline? */
				if (!IS_INLINE(d[i]->dtype->flags)) {
					x_fprintf(out, "global $%s\n",
						d[i]->dtype->name,
						d[i]);
				}	
			}
		}
	}
}

static void
emit_global_static_decls(struct decl **dv, int ndecls) {
	int	i;

	for (i = 0; i < ndecls; ++i) {
		if (dv[i]->dtype->storage != TOK_KEY_STATIC) {
			struct type_node	*tn = NULL;

			if (dv[i]->invalid) {
				continue;
			}
			if (dv[i]->dtype->is_func) {
				for (tn = dv[i]->dtype->tlist;
					tn != NULL;
					tn = tn->next) {
					if (tn->type == TN_FUNCTION) {
						if (tn->ptrarg) {
							continue;
						} else {
							break;
						}	
					}
				}
			}

			if (tn != NULL) {
				tn->ptrarg = 1;
			}	
			if (dv[i]->has_symbol) continue;
			x_fprintf(out, "global $%s\n",
				dv[i]->dtype->name, dv[i]);
			dv[i]->has_symbol = 1;
		}
	}
}

static void
emit_static_init_vars(struct decl *list) {
	struct decl	*d;
	size_t		size;

	if (list) emit_setsection(SECTION_INIT);
	for (d = list; d != NULL; d = d->next) {
		struct type	*dt = d->dtype;
		
		if (d->dtype->storage == TOK_KEY_STATIC
			&& d->references == 0) {
			continue;
		}
		if (d->invalid) {
			continue;
		}

		size = backend->get_sizeof_decl(d, NULL);

		/* Constant initializer expression */
		if (picflag) {
			x_fprintf(out, "\ttype %s object\n",
				dt->name);
			x_fprintf(out, "\tsize %s %lu\n", dt->name, size);
		}
		x_fprintf(out, "\t$%s:\n", dt->name);
		print_init_list(d, d->init);
		if (d->next != NULL) {
			unsigned long	align;

			align = calc_align_bytes(data_segment_offset,
				d->dtype, d->next->dtype, 0);
			if (align) {
				x_fprintf(out, "\ttimes %lu db 0\n", align);
				data_segment_offset += align;
			}	
		}	
		data_segment_offset += size;
	}
}

static void
emit_static_uninit_vars(struct decl *list) {
	struct decl	*d;
	size_t		size;

	if (list != NULL) {
		emit_setsection(SECTION_UNINIT);
		for (d = list; d != NULL; d = d->next) {
			if (d->dtype->storage == TOK_KEY_STATIC
				&& d->references == 0) {
				continue;
			}
			if (d->invalid) {
				continue;
			}

			size = backend->get_sizeof_decl(d, NULL);

			if (picflag) {
				x_fprintf(out, "\ttype %s object\n",
					d->dtype->name);
				x_fprintf(out, "\tsize %s %lu\n",
					d->dtype->name, size);
			}

			x_fprintf(out, "\t$%s resb %lu\n",
				d->dtype->name, size);
			if (d->next != NULL) {
				unsigned long	align;

				align = calc_align_bytes(bss_segment_offset,
					d->dtype, d->next->dtype, 0);
				if (align) {
					x_fprintf(out, "\tresb %lu\n", align);
					bss_segment_offset += align;
				}	
			}	
			bss_segment_offset += size;
		}
	}
}

static void
emit_static_init_thread_vars(struct decl *list) {
	struct decl	*d;
	size_t		size;

	if (list != NULL) {
		emit_setsection(SECTION_INIT_THREAD);
		thread_data_segment_offset = 0;
		for (d = list; d != NULL; d = d->next) {
			struct type	*dt = d->dtype;
		
			if (d->dtype->storage == TOK_KEY_STATIC
				&& d->references == 0) {
				continue;
			}
			if (d->invalid) {
				continue;
			}

			size = backend->get_sizeof_decl(d, NULL);

			/* Constant initializer expression */
			if (picflag) {
				x_fprintf(out, "\ttype %s object\n",
					dt->name);
				x_fprintf(out, "\tsize %s %lu\n", size);
			}
			x_fprintf(out, "\t$%s:\n", dt->name);
			print_init_list(d, d->init);
			if (d->next != NULL) {
				unsigned long	align;

				align = calc_align_bytes(thread_data_segment_offset,
					d->dtype, d->next->dtype, 0);
				if (align) {
					x_fprintf(out, "\ttimes %lu db 0\n", align);
					thread_data_segment_offset += align;
				}	
			}	
			thread_data_segment_offset += size;
		}
	}
}

static void
emit_static_uninit_thread_vars(struct decl *list) {
	struct decl	*d;
	size_t		size;

	if (list != NULL) {
		thread_bss_segment_offset = 0;
		emit_setsection(SECTION_UNINIT_THREAD);
		for (d = list; d != NULL; d = d->next) {
			if (d->dtype->storage == TOK_KEY_STATIC
				&& d->references == 0) {
				continue;
			}
			if (d->invalid) {
				continue;
			}

			size = backend->get_sizeof_decl(d, NULL);

			if (picflag) {
				x_fprintf(out, "\ttype %s object\n",
					d->dtype->name);
				x_fprintf(out, "\tsize %s %lu\n", size);
			}

			x_fprintf(out, "\t$%s resb %lu\n",
				d->dtype->name, size);
			if (d->next != NULL) {
				unsigned long	align;

				align = calc_align_bytes(thread_bss_segment_offset,
					d->dtype, d->next->dtype, 0);
				if (align) {
					x_fprintf(out, "\tresb %lu\n", align);
					thread_bss_segment_offset += align;
				}	
			}	
			thread_bss_segment_offset += size;
		}
	}
}	

#if 0
static void 
emit_static_decls(void) {
	struct decl	*d;
	struct decl	**dv;
	size_t		size;
	int		i;

	bss_segment_offset = 0;
	if (static_uninit_vars != NULL) {
		x_fprintf(out, "section .bss\n");
		for (d = static_uninit_vars; d != NULL; d = d->next) {
			if (d->dtype->storage == TOK_KEY_STATIC
				&& d->references == 0) {
				continue;
			}

			size = backend->get_sizeof_decl(d, NULL);

			if (picflag) {
				x_fprintf(out, "\ttype %s object\n",
					d->dtype->name);
				x_fprintf(out, "\tsize %s %lu\n",
					d->dtype->name, size);
			}

			x_fprintf(out, "\t$%s resb %lu\n",
				d->dtype->name, size);
			if (d->next != NULL) {
				unsigned long	align;

				align = calc_align_bytes(bss_segment_offset,
					d->dtype, d->next->dtype);
				if (align) {
					x_fprintf(out, "\tresb %lu\n", align);
					bss_segment_offset += align;
				}	
			}	
			bss_segment_offset += size;
		}
	}

	if (static_init_vars) x_fprintf(out, "section .data\n");
	data_segment_offset = 0;
	for (d = static_init_vars; d != NULL; d = d->next) {
		struct type	*dt = d->dtype;
		
		if (d->dtype->storage == TOK_KEY_STATIC
			&& d->references == 0) {
			continue;
		}

		size = backend->get_sizeof_decl(d, NULL);

		/* Constant initializer expression */
		if (picflag) {
			x_fprintf(out, "\ttype %s object\n",
				dt->name);
			x_fprintf(out, "\tsize %s %lu\n", dt->name, size);
		}
		x_fprintf(out, "\t$%s:\n", dt->name);
		print_init_list(d, d->init);
		if (d->next != NULL) {
			unsigned long	align;

			align = calc_align_bytes(data_segment_offset,
				d->dtype, d->next->dtype);
			if (align) {
				x_fprintf(out, "\ttimes %lu db 0\n", align);
				data_segment_offset += align;
			}	
		}	
		data_segment_offset += size;
	}

	if (static_uninit_thread_vars != NULL) {
		thread_bss_segment_offset = 0;
		x_fprintf(out, "section .tbss\n");
		for (d = static_uninit_thread_vars; d != NULL; d = d->next) {
			if (d->dtype->storage == TOK_KEY_STATIC
				&& d->references == 0) {
				continue;
			}

			size = backend->get_sizeof_decl(d, NULL);

			if (picflag) {
				x_fprintf(out, "\ttype %s object\n",
					d->dtype->name);
				x_fprintf(out, "\tsize %s %lu\n", size);
			}

			x_fprintf(out, "\t$%s resb %lu\n",
				d->dtype->name, size);
			if (d->next != NULL) {
				unsigned long	align;

				align = calc_align_bytes(thread_bss_segment_offset,
					d->dtype, d->next->dtype);
				if (align) {
					x_fprintf(out, "\tresb %lu\n", align);
					thread_bss_segment_offset += align;
				}	
			}	
			thread_bss_segment_offset += size;
		}
	}

	if (static_init_thread_vars != NULL) {
		x_fprintf(out, "section .tdata\n");
		thread_data_segment_offset = 0;
		for (d = static_init_thread_vars; d != NULL; d = d->next) {
			struct type	*dt = d->dtype;
		
			if (d->dtype->storage == TOK_KEY_STATIC
				&& d->references == 0) {
				continue;
			}

			size = backend->get_sizeof_decl(d, NULL);

			/* Constant initializer expression */
			if (picflag) {
				x_fprintf(out, "\ttype %s object\n",
					dt->name);
				x_fprintf(out, "\tsize %s %lu\n", size);
			}
			x_fprintf(out, "\t$%s:\n", dt->name);
			print_init_list(d, d->init);
			if (d->next != NULL) {
				unsigned long	align;

				align = calc_align_bytes(thread_data_segment_offset,
					d->dtype, d->next->dtype);
				if (align) {
					x_fprintf(out, "\ttimes %lu db 0\n", align);
					thread_data_segment_offset += align;
				}	
			}	
			thread_data_segment_offset += size;
		}
	}
}
#endif


static void
emit_struct_inits(struct init_with_name *list) {
	struct init_with_name	*in;

	if (list) emit_setsection(SECTION_INIT);
	for (in = list; in != NULL; in = in->next) {
		x_fprintf(out, "%s:\n", in->name);
		print_init_list(in->dec, in->init);
	}	
}

void
print_nasm_string_init(size_t howmany, struct ty_string *str) {
	char	*p;
	int	wasprint = 0;
	size_t	i;

	if (str->is_wide_char) {
		(void) fprintf(out, "dd ");
	} else {
		(void) fprintf(out, "db ");
	}

	for (i = 0, p = str->str; i < str->size-1; ++p, ++i) {
		if (str->is_wide_char) {
			x_fprintf(out, "0x%x", (unsigned char)*p);
			if (/*p[1] != 0*/i+1 < str->size-1) {
				(void) fprintf(out, ", ");
			}	
		} else if (isprint((unsigned char)*p)) {
			if (!wasprint) {
				if (*p == '\'') {
					goto printval;
				} else {
					(void) fprintf(out, "'%c", *p);
				}
				wasprint = 1;
			} else {
				if (*p == '\'') {
					goto printval;
				} else {
					(void) fputc(*p, out);
				}
			}
		} else {
printval:
			if (wasprint) {
				(void) fputc('\'', out);
				(void) fputc(',', out);
				wasprint = 0;
			}	
			(void) fprintf(out, " %d", *p);
			if (/*p[1] != 0*/i+1 < str->size-1) {
				(void) fprintf(out, ", ");
			}	
		}
	}

	if (wasprint) {
		(void) fprintf(out, "'");
	}

	if (howmany >= str->size) {
		if (str->size > 1) {
			(void) fprintf(out, ", ");
		}	
		(void) fprintf(out, "0\n");
	}	
}

static void
emit_fp_constants(struct ty_float *list) {
	if (list != NULL) {
		struct ty_float	*tf;

		emit_setsection(SECTION_INIT);
		for (tf = list; tf != NULL; tf = tf->next) {
			x_fprintf(out, "\t_Float%lu ", tf->count);
			switch (tf->num->type) {
			case TY_FLOAT:
				x_fprintf(out, "dd ");
				cross_print_value_by_type(out,
					tf->num->value,
					TY_FLOAT, 0);
				break;
			case TY_DOUBLE:
				x_fprintf(out, "dq ");
				cross_print_value_by_type(out,
					tf->num->value,
					TY_DOUBLE, 0);
				break;
			case TY_LDOUBLE:
				x_fprintf(out, "dt ");
				cross_print_value_by_type(out,
					tf->num->value,
					TY_LDOUBLE, 0);
				break;
			default:
				printf("bad floating point constant - "
					"code %d\n", tf->num->type);
				abort();
			}
			x_fputc('\n', out);
		}
	}
}

static void
emit_support_buffers(void) {
	static int	have_floatbuf, have_x87cw;


	if (/*float_const != NULL
		||*/ x87cw_old.var_backed != NULL
#if ! REMOVE_FLOATBUF
		|| floatbuf.var_backed != NULL
#endif
		) {

		if (have_floatbuf && have_x87cw) {
			return;
		}
		emit_setsection(SECTION_INIT);
#if ! REMOVE_FLOATBUF
		if (floatbuf.var_backed != NULL && !have_floatbuf) {
			x_fprintf(out, "\t%s dq 0.0\n",
				floatbuf.type->name);
			have_floatbuf = 1;
		}
#endif
	
		if (x87cw_old.var_backed != NULL && !have_x87cw) {
			x_fprintf(out, "\t%s dw 0\n",
				x87cw_old.type->name);
			x_fprintf(out, "\t%s dw 0\n",
				x87cw_new.type->name);
			have_x87cw = 1;
		}
	}

	if (amd64_need_ulong_float_mask) {
		x86_emit_nasm.setsection(SECTION_INIT);
		x_fprintf(out, "_Ulong_float_mask:\n");
		x_fprintf(out, "\tdd 1602224128\n");
	}
}

/*
 * XXX Misleading name ;o
 */
static void
emit_strings(struct ty_string *list) {
	if (list != NULL) {
		struct ty_string	*str;

		emit_setsection(SECTION_RODATA);
		for (str = list; str != NULL; str = str->next) {
			x_fprintf(out, "\t_Str%lu ", str->count);
			print_nasm_string_init(str->size, str);
		}
	}

#if 0
	if (unimpl_instr) {
		x_fprintf(out,"\t_Unimpl_msg db 'ERROR: Use of unimplemented'\n"
			"\t            db 'compiler feature (probably)'\n"
			"\t            db 'floating point - check %d\n'\n");
	}	
#endif
}

static void
emit_comment(const char *fmt, ...) {
	int		rc;
	va_list	va;

	va_start(va, fmt);
	x_fprintf(out, "; ");
	rc = vfprintf(out, fmt, va);
	va_end(va);
	x_fputc('\n', out);

	if (rc == EOF || fflush(out) == EOF) {
		perror("vfprintf");
		exit(EXIT_FAILURE);
	}
}

static void
emit_dwarf2_line(struct token *tok) {
	(void) tok;
	unimpl();
#if 0
	x_fprintf(out, "\t[loc %d %d 0]\n",
		tok->fileid, tok->line);	
#endif
}

static void
emit_dwarf2_files(void) {
	struct dwarf_in_file	*inf;
	
	x_fprintf(out, "[file \"%s\"]\n",
		input_file);

	for (inf = dwarf_files; inf != NULL; inf = inf->next) {
		x_fprintf(out, "[file %d \"%s\"]\n",
			inf->id, inf->name);	
	}
}	

static void
emit_inlineasm(struct inline_asm_stmt *stmt) {
	x_fprintf(out, "; inline start\n");
	/*
	 * There may be an empty body for statements where only the side
	 * effect is desired;
	 * __asm__("" ::: "memory");
	 */
	if (stmt->code != NULL) {
		inline_instr_to_nasm(out, stmt->code);
	}	
	x_fprintf(out, "; inline end\n");
}	

static void
emit_unimpl(void) {
	static int	n;

	x_fprintf(out, "\tpush dword _Unimpl_msg\n");
	x_fprintf(out, "\tpush dword %d\n", n++);
	x_fprintf(out, "\tcall printf\n");
	x_fprintf(out, "\tadd esp, 8\n");
	x_fprintf(out, "\tpush dword 1\n");
	x_fprintf(out, "\tcall exit\n");
}	

static void
emit_empty(void) {
	x_fputc('\n', out);
}

static void
emit_label(const char *name, int is_func) {
	if (is_func) {
		x_fprintf(out, "$%s:\n", name);
	} else {
		x_fprintf(out, ".%s:\n", name);
	}
}

static void
emit_call(const char *name) {
	if (picflag) {
		x_fprintf(out, "\tcall $%s@PLT\n", name);
	} else {
		x_fprintf(out, "\tcall $%s\n", name);
	}
}

static void
emit_callindir(struct reg *r) {
	x_fprintf(out, "\tcall %s\n", r->name);
}	

static void
emit_func_header(struct function *f) {
	if (picflag) {
		x_fprintf(out, "\ttype %s function\n", f->proto->dtype->name);
	}
}

static void
emit_func_intro(struct function *f) {
	(void) f;
	x_fprintf(out, "\tpush dword ebp\n"); /* XXX */
	x_fprintf(out, "\tmov ebp, esp\n");
}

static void
emit_func_outro(struct function *f) {
	if (picflag) {
		x_fprintf(out, "._End_%s:\n", f->proto->dtype->name);
		x_fprintf(out, "\tsize %s %s._End_%s-%s\n",
			f->proto->dtype->name,
			f->proto->dtype->name,
			f->proto->dtype->name);
	}
}


static void
emit_define(const char *name, const char *fmt, ...) {
	va_list		va;
	const char	*p;

	va_start(va, fmt);
	x_fprintf(out, "%%define %s ", name);
	for (p = fmt; *p != 0; ++p) {
		switch (*p) {
		case '%':
			switch (*++p) {
			case 0:
			case '%':
				x_fputc('%', out);
				break;
			case 's': {
				char	*p = va_arg(va, char *);
				x_fprintf(out, "%s", p);
				}
				break;
			case 'l':
				if (*++p == 'd') {
					long	l;
					l = va_arg(va, long);
					x_fprintf(out, "%ld", l);
				}
				break;
			}
			break;
		default:
			x_fputc(*p, out);
		}
	}
	va_end(va);
}

static void
emit_push(struct function *f, struct icode_instr *ii) {
	struct vreg	*vr = ii->src_vreg;
	struct reg	*r = ii->src_pregs?
				(void *)ii->src_pregs[0]: (void *)NULL;

	(void) f;

	/*
	 * XXX 07/26/07: Hmmm ascii_type not used anymore??
	 */
	if (ii->src_vreg->type
		&& ii->src_vreg->type->tlist
		&& ii->src_vreg->type->tlist->type == TN_ARRAY_OF) {
	} else {
#if 0
		if (ii->src_vreg->type && ii->src_vreg->type->is_vla) {
#endif
	}

	if (ii->src_pregs) {
		if (ii->src_vreg->is_multi_reg_obj) {
			x_fprintf(out, "\tpush dword %s\n",
				ii->src_pregs[1]->name);
			f->total_allocated += 4;
		}	
		x_fprintf(out, "\tpush dword %s\n", /*ascii_type*/ r->name);
	} else if (vr->var_backed) {
		struct decl	*d = vr->var_backed;

		if (vr->parent != NULL) {
			/* Structure or union member */
			struct decl	*d2/* = d*/;
			struct vreg	*vr2;

			vr2 = get_parent_struct(vr);
			d2 = vr2->var_backed;
			if (vr2->from_ptr) {
				x_fprintf(out, "\tpush dword [%s",
					vr2->from_ptr->pregs[0]->name);
				if (vr->parent->type->code == TY_STRUCT) {
					x_fprintf(out, " + %s.%s",
					vr2->from_ptr->type->tstruc->tag,
					d2->dtype->name);
				}
				x_fprintf(out, "]\n");
			} else if (d2 && d2->stack_addr != NULL) {
				x_fprintf(out, "\tpush dword [ebp - %ld",
					d2->stack_addr->offset);
				if (vr->parent->type->code == TY_STRUCT) {
					x_fputc(' ', out);
					print_nasm_offsets(/*d*/vr2);
				}	
				x_fprintf(out, "]\n"); 
			} else if (d2 != NULL) {	
				/* Must be static */
				x_fprintf(out, "\tpush dword [$%s",
					d2->dtype->name);
				if (vr->parent->type->code == TY_STRUCT) {
					x_fprintf(out, " + ");
					print_nasm_offsets(/*d*/vr2);
				}	
				x_fprintf(out, "]\n"); 
			} else {
				unimpl();
			}	
		} else {
			if (d->stack_addr != NULL) {
				/* Stack */
				x_fprintf(out, "\tpush dword [ebp - %ld]\n",
					/*ascii_type*/ d->stack_addr->offset);
			} else {
				/* Static or register variable */
				if (d->dtype->storage == TOK_KEY_REGISTER) {
					unimpl();
				} else {
					x_fprintf(out, "\tpush dword [$%s]\n",
						/*ascii_type*/ d->dtype->name);
				}
			}
		}
	} else if (vr->from_const) {
		struct token	*t = vr->from_const;

		if (t->type == TOK_STRING_LITERAL) {
			struct ty_string	*s = t->data;
			x_fprintf(out, "\tpush dword _Str%lu\n", s->count);
		} else if (IS_INT(t->type) || IS_LONG(t->type)) {
			/* 
			 * There are only forms of ``int'' and ``long''
			 * constants
			 */
#if 0
			if (t->type == TY_INT || t->type == TY_LONG) {
				x_fprintf(out, "\tpush dword %d\n",
					*(int *) t->data);
			} else {
				/* UINT/ULONG */
				x_fprintf(out, "\tpush dword %u\n",
					*(unsigned *) t->data);
			}
#endif
			x_fprintf(out, "\tpush dword ");
			cross_print_value_by_type(out,
				t->data,
				t->type, 0);
			x_fputc('\n', out);
		} else if (IS_LLONG(t->type)) {	
			unimpl();
		} else {
			puts("BUG in NASM emit_push()");
			exit(EXIT_FAILURE);
		}
	} else if (vr->from_ptr) {	
		x_fprintf(out, "\tpush dword [%s]\n", /*ascii_type,*/
				vr->from_ptr->pregs[0]->name);
	} else {
		unimpl();
	}

	f->total_allocated += 4;
}

static void
emit_allocstack(struct function *f, size_t nbytes) {
	(void) f;
	x_fprintf(out, "\tsub esp, %lu\n", (unsigned long)nbytes);
}


static void
emit_freestack(struct function *f, size_t *nbytes) {
	if (nbytes == NULL) {
		/* Procedure outro */
		if (f->total_allocated != 0) {
			x_fprintf(out, "\tadd esp, %lu\n",
				(unsigned long)f->total_allocated);
		}	
		x_fprintf(out, "\tpop ebp\n");
	} else {
		if (*nbytes != 0) {
			x_fprintf(out, "\tadd esp, %lu\n",
				(unsigned long)*nbytes);
			f->total_allocated -= *nbytes;
		}
	}
}

static void
emit_adj_allocated(struct function *f, int *nbytes) {
	f->total_allocated += *nbytes; 
}	

static void emit_struct_defs(void) { return; }


static void
emit_alloc(size_t nbytes) {
	unimpl();
	(void) nbytes;
	if (cursect == SECTION_INIT) {
	} else if (cursect == SECTION_UNINIT) {
	} else if (cursect == SECTION_STACK) {
	} else if (cursect == SECTION_TEXT) {
	}
}


static void
print_mem_or_reg(struct reg *r, struct vreg *vr) {
	if (vr->on_var) {
		struct decl	*d = vr->var_backed;
		char		*p = size_to_asmtype(
			backend->get_sizeof_decl(d, NULL),
			d->dtype);

		if (d->stack_addr) {
			x_fprintf(out, "%s [ebp - %ld]",
				p, d->stack_addr->offset);
		} else {
			x_fprintf(out, "%s %s", p, d->dtype->name);
		}
	} else if (r != NULL) {
		/*
		 * 04/13/08: Note: If a register is available, use that one.
		 * Because even if it's a constant, a register is more likely
		 * to be correct if there's no immediate instruction
		 */
		x_fprintf(out, "%s", r->name);
	} else if (vr->from_const) {
		if (backend->arch == ARCH_AMD64) {
			amd64_print_mem_operand_yasm(vr, NULL);
		} else {
			print_mem_operand(vr, NULL);
		}
	} else {
		unimpl();
	}
}



static void
emit_inc(struct icode_instr *ii) {
	x_fprintf(out, "\tinc ");
	print_mem_or_reg(ii->src_pregs[0], ii->src_vreg);
	x_fputc('\n', out);
}

static void
emit_dec(struct icode_instr *ii) {
	x_fprintf(out, "\tdec ");
	print_mem_or_reg(ii->src_pregs[0], ii->src_vreg);
	x_fputc('\n', out);
}

static void
emit_load(struct reg *r, struct vreg *vr) {
	char		*p;
	int		needsize = 1;

	if (r->type == REG_FPR) {
		if (!IS_FLOATING(vr->type->code)) {
#if ! REMOVE_FLOATBUF
			p = "fild";
#else
			buggypath();
#endif
		} else {	
			p = "fld";
		}	
		x_fprintf(out, "\t%s ", p);
	} else {
		if (vr->stack_addr != NULL) {
			p = "mov";
		} else if (vr->type
			&& vr->type->tlist != NULL
			&& (vr->type->tlist->type == TN_ARRAY_OF
			|| vr->type->tlist->type == TN_VARARRAY_OF)) {
			if (vr->from_const == NULL) {
				p = "lea";
			} else {
				p = "mov";
			}	
			needsize = 0;
		} else {
			if (r->size == vr->size
				|| vr->size == 8) {
				/* == 8 for long long, SA for anonymous */
				p = "mov";
			} else {
				if (vr->type == NULL
					|| vr->type->sign == TOK_KEY_UNSIGNED) {
					p = "movzx";
				} else {
					p = "movsx";
				}	
			}
		}	
		x_fprintf(out, "\t%s %s, ", p, r->name);
	}

	if (vr->from_const != NULL && vr->size == 0) {
		needsize = 0;
	}

	if (needsize) {
		struct type	*ty = vr->type;
		size_t		size = vr->size;

		if (vr->stack_addr != NULL && r->type != REG_FPR) {
			size = r->size;
			ty = NULL;
		}
		x_fprintf(out, "%s ",
			size_to_asmtype(size, ty));
	}
	print_mem_operand(vr, NULL);
	x_fputc('\n', out);
}

static void
emit_load_addrlabel(struct reg *r, struct icode_instr *ii) {
	(void) r;
	x_fprintf(out, "\tmov %s, .%s\n", r->name, ii->dat);
}

static void
emit_comp_goto(struct reg *r) {
	x_fprintf(out, "\tjmp %s\n", r->name);
}


/*
 * Takes vreg source arg - not preg - so that it can be either a preg
 * or immediate (where that makes sense!)
 */
static void
emit_store(struct vreg *dest, struct vreg *src) {
	char		*p = NULL;
	int		floating = 0;
	static int	was_llong;

	if (src->pregs[0] && src->pregs[0]->type == REG_FPR) {	
		if (!IS_FLOATING(dest->type->code)) {
#if ! REMOVE_FLOATBUF
			p = "fistp";
#else
			buggypath();
#endif
		} else {
			p = "fstp";
		}
		floating = 1;
	} else {
		if (dest->stack_addr != NULL || dest->from_const != NULL) {
			p = "mov";
		} else {
			if (dest->size == 0) puts("?WHAT???"), unimpl();
			p = "mov";
		}
	}
	x_fprintf(out, "\t%s ", p);
	if (floating) {
		x_fprintf(out, "%s ", size_to_asmtype(dest->size, dest->type));
	}
	print_mem_operand(dest, NULL);
	if (floating) {
		/* Already done - floating stores only ever come from st0 */
		x_fputc('\n', out);
		return;
	}
	if (src->from_const) {
		print_mem_operand(src, NULL);
	} else {
		/* Must be register */
		if (was_llong) {
			x_fprintf(out, ", %s\n", src->pregs[1]->name);
			was_llong = 0;
		} else {
			x_fprintf(out, ", %s\n", src->pregs[0]->name);
			if (src->is_multi_reg_obj) {
				was_llong = 1;
			}
		}
	}
}


static void
emit_neg(struct reg **dest, struct icode_instr *src) {
	(void) src;
	if (dest[0]->type == REG_FPR) {
		/* fchs only works with TOS! */
		x_fprintf(out, "\tfchs\n");
	} else {
		x_fprintf(out, "\tneg %s\n", dest[0]->name);
		if (src->src_vreg->is_multi_reg_obj) {
			x_fprintf(out, "\tadc %s, 0\n", dest[1]->name);
			x_fprintf(out, "\tneg %s\n", dest[1]->name);
		}	
	}	
}	


static void
emit_sub(struct reg **dest, struct icode_instr *src) {
	if (dest[0]->type == REG_FPR) {
		/*
		 * 05/27/08: Use fsubrp instead of fsubp.. THAT'S WHAT
		 * nasm had been generating anyway!!! (XXX why?)
		 */
		x_fprintf(out, "\tfsubrp %s, ", dest[0]->name);
	} else {
		x_fprintf(out, "\tsub %s, ", dest[0]->name);
	}
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	if (src->src_vreg->is_multi_reg_obj
		&& src->dest_vreg->is_multi_reg_obj) { /* for ptr arit */
		/* long long */
		x_fprintf(out, "\n\tsbb %s, %s\n",
			dest[1]->name, src->src_pregs[1]->name);
	}	
	x_fputc('\n', out);
}

static void
emit_add(struct reg **dest, struct icode_instr *src) {
	if (dest[0]->type == REG_FPR) {
		x_fprintf(out, "\tfaddp %s, ", dest[0]->name);
	} else {	
		x_fprintf(out, "\tadd %s, ", dest[0]->name);
	}	
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	if (src->src_vreg->is_multi_reg_obj
		&& src->dest_vreg->is_multi_reg_obj) { /* for ptr arit */
		/* long long */
		x_fprintf(out, "\n\tadc %s, %s\n",
			dest[1]->name, src->src_pregs[1]->name);
	}	
	x_fputc('\n', out);
}

static void
make_divmul_call(struct icode_instr *src,
	const char *func,
	int want_remainder) {

	int	extra_pushed = 4;
	struct decl *d;

	x_fprintf(out, "\tsub esp, 4\n");
	x_fprintf(out, "\tmov [esp], %s\n", src->dest_pregs[1]->name);
	x_fprintf(out, "\tsub esp, 4\n");
	x_fprintf(out, "\tmov [esp], %s\n", src->dest_pregs[0]->name);
	x_fprintf(out, "\tsub esp, 4\n");
	x_fprintf(out, "\tmov [esp], %s\n", src->src_pregs[1]->name);
	x_fprintf(out, "\tsub esp, 4\n");
	x_fprintf(out, "\tmov [esp], %s\n", src->src_pregs[0]->name);
	x_fprintf(out, "\tlea eax, [esp + 8]\n");
	x_fprintf(out, "\tlea ecx, [esp]\n");

	/* Push data size */
	x_fprintf(out, "\tpush dword 64\n");

	if (want_remainder != -1) {
		x_fprintf(out, "\tpush dword %d\n", want_remainder);
		extra_pushed += 4;
	}	
	/* Push src address */
	x_fprintf(out, "\tpush dword ecx\n");
	/* Push dest address */
	x_fprintf(out, "\tpush dword eax\n");

	/*
	 * The lookup below ensures that __nwcc_ullmul, etc,
	 * is not redeclared incompatibly in libnwcc.c, which
	 * defines these functions and declares them ``global''.
	 * This stuff is not necessary for gas!
	 */
	if ((d = lookup_symbol(&global_scope, func, 0)) == NULL
		|| !d->has_symbol) {
		x_fprintf(out, "\textern $%s\n", func);
		if (d != NULL) {
			d->has_symbol = 1; 
		}
	}
	/*x_fprintf(out, "\tcall %s\n", func);*/
	emit_call(func);

	/*
	 * Result is saved in destination stack buffer - let's move it
	 * to eax:edx
	 */
	x_fprintf(out, "\tmov eax, [esp + %d]\n", 16+extra_pushed);
	x_fprintf(out, "\tmov edx, [esp + %d]\n", 20+extra_pushed);
	x_fprintf(out, "\tadd esp, %d\n", 24+extra_pushed);
}

static void
emit_div(struct reg **dest, struct icode_instr *src, int formod) {
	struct type	*ty = src->src_vreg->type;

	(void) dest;
	if (IS_LLONG(ty->code)) {
		char	*func;

		if (ty->code == TY_ULLONG) {
			func = "__nwcc_ulldiv";
		} else {
			func = "__nwcc_lldiv";
		}
		make_divmul_call(src, func, formod);
		return;
	} else if (!IS_FLOATING(ty->code)) {
		if (ty->sign != TOK_KEY_UNSIGNED) {
			/* sign-extend eax to edx:eax */
			x_fprintf(out, "\tcdq\n");
		} else {
			x_fprintf(out, "\txor edx, edx\n");
		}
	}

	if (IS_FLOATING(ty->code)) {
		x_fprintf(out, "\tfdivp ");
	} else if (ty->sign == TOK_KEY_UNSIGNED) {
		x_fprintf(out, "\tdiv ");
	} else {
		/* signed integer division */
		x_fprintf(out, "\tidiv ");
	}
	if (IS_FLOATING(ty->code)) {
		print_mem_or_reg(src->dest_pregs[0], src->dest_vreg);
	} else {
		print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	}
	x_fputc('\n', out);
}


static void
emit_mod(struct reg **dest, struct icode_instr *src) {
	emit_div(dest, src, 1);
	if (!IS_LLONG(src->dest_vreg->type->code)) {
		x_fprintf(out, "\tmov %s, edx\n", dest[0]->name);
	}	
}


static void
emit_mul(struct reg **dest, struct icode_instr *src) {
	struct type	*ty = src->src_vreg->type;

	(void) dest;

	if (IS_LLONG(ty->code)) {
		char	*func;

		if (ty->code == TY_ULLONG) {
			func = "__nwcc_ullmul";
		} else {
			func = "__nwcc_llmul";
		}	
		make_divmul_call(src, func, -1);
		return;
	} else if (IS_FLOATING(ty->code)) {
		x_fprintf(out, "\tfmulp ");
	} else if (ty->sign == TOK_KEY_UNSIGNED) {
		x_fprintf(out, "\tmul ");
	} else {
		/* signed integer multiplication */
		/* XXX should use mul for pointer arithmetic :( */
		x_fprintf(out, "\timul eax, ");
	}
	if (IS_FLOATING(ty->code)) {
		print_mem_or_reg(src->dest_pregs[0], src->dest_vreg);
	} else {	
		print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	}
	x_fputc('\n', out);
}

static unsigned long	shift_idx;

/* XXX sal for signed values!!!!! */
static void
emit_shl(struct reg **dest, struct icode_instr *src) {
	int	is_signed = src->dest_vreg->type->sign != TOK_KEY_UNSIGNED;

	if (src->dest_vreg->is_multi_reg_obj) {
		if (src->src_vreg->from_const) {
			/* XXX lazy way */
			x_fprintf(out, "\tpush ecx\n");
			x_fprintf(out, "\tmov ecx, ");
			cross_print_value_by_type(out,
				src->src_vreg->from_const->data,
				TY_INT, 0);
			x_fputc('\n', out);
		}	
		x_fprintf(out, "\tshld %s, %s, cl\n",
			dest[1]->name, dest[0]->name);	
		x_fprintf(out, "\t%s %s, cl\n",
			is_signed? "sal": "shl", dest[0]->name);
/*		if (!is_signed) {*/
			x_fprintf(out, "\ttest cl, 32\n");
			x_fprintf(out, "\tje .shftdone%lu\n", shift_idx);
			x_fprintf(out, "\tmov %s, %s\n",
				dest[1]->name, dest[0]->name);
			x_fprintf(out, "\txor %s, %s\n", dest[0]->name, dest[0]->name);
			x_fprintf(out, ".shftdone%lu:\n", shift_idx++);
/*		}	*/
		if (src->src_vreg->from_const) {
			x_fprintf(out, "\tpop ecx\n");
		}	
	} else {	
		if (src->src_vreg->from_const) {
			x_fprintf(out, "\t%s %s, ",
				is_signed? "sal": "shl", dest[0]->name);
			cross_print_value_by_type(out,
				src->src_vreg->from_const->data,
				TY_INT, 0);
			x_fputc('\n', out);
		} else {	
			x_fprintf(out, "\t%s %s, cl\n",
				is_signed? "sal": "shl", dest[0]->name);
		}
	}	
}


/* XXX sar for signed values !!!!!!!! */
static void
emit_shr(struct reg **dest, struct icode_instr *src) {
	int	is_signed = src->dest_vreg->type->sign != TOK_KEY_UNSIGNED;

	if (src->dest_vreg->is_multi_reg_obj) {
		x_fprintf(out, "\tshrd %s, %s, cl\n",
			dest[0]->name, dest[1]->name);	
		x_fprintf(out, "\t%s %s, cl\n",
			is_signed? "sar": "shr", dest[1]->name);
/*		if (!is_signed) {*/
			x_fprintf(out, "\tand ecx, 32\n");
			x_fprintf(out, "\tje .shftdone%lu\n", shift_idx);
			x_fprintf(out, "\tmov %s, %s\n",
				dest[0]->name, dest[1]->name);

			/*
			 * 06/21/08: This was always sign-extending! Zero-
			 * extend for unsigned values
			 */
			if (is_signed) { 
				x_fprintf(out, "\t%s %s, 31\n",
					is_signed? "sar": "shr", dest[1]->name);
			} else {
				x_fprintf(out, "\tmov %s, 0\n", dest[1]->name);
			}

			x_fprintf(out, ".shftdone%lu:\n", shift_idx++);
/*		}	*/
	} else {	
		if (src->src_vreg->from_const) {
			x_fprintf(out, "\t%s %s, ",
				is_signed? "sar": "shr", dest[0]->name);
			cross_print_value_by_type(out,
				src->src_vreg->from_const->data,
				TY_INT, 0);
			x_fputc('\n', out);
		} else {	
			x_fprintf(out, "\t%s %s, cl\n",
				is_signed? "sar": "shr", dest[0]->name);
		}	
	}	
}

static void
emit_or(struct reg **dest, struct icode_instr *src) {
	x_fprintf(out, "\tor %s, ", dest[0]->name);
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fputc('\n', out);
	if (src->src_vreg->is_multi_reg_obj) {
		x_fprintf(out, "\tor %s, %s\n",
			dest[1]->name, src->src_pregs[1]->name);
	}	
}

static void
emit_and(struct reg **dest, struct icode_instr *src) {
	x_fprintf(out, "\tand %s, ", dest[0]->name);
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fputc('\n', out);
	if (src->src_vreg->is_multi_reg_obj) {
		x_fprintf(out, "\tand %s, %s\n",
			dest[1]->name, src->src_pregs[1]->name);
	}	
}

static void
emit_xor(struct reg **dest, struct icode_instr *src) {
	x_fprintf(out, "\txor %s, ", dest[0]->name);
	if (src->src_vreg == NULL) {
		x_fprintf(out, "%s\n", dest[0]->name);
		if (src->src_vreg->is_multi_reg_obj) {
			x_fprintf(out, "\txor %s, %s\n",
				dest[1]->name, dest[1]->name);
		}	
	} else {	
		print_mem_or_reg(src->src_pregs[0], src->src_vreg);
		x_fputc('\n', out);
		if (src->src_vreg->is_multi_reg_obj) {
			x_fprintf(out, "\txor %s, %s\n",
				dest[1]->name, src->src_pregs[1]->name);	
		}	
	}
}

static void
emit_not(struct reg **dest, struct icode_instr *src) {
	(void) src;
	x_fprintf(out, "\tnot %s\n", dest[0]->name);
	if (src->src_vreg->is_multi_reg_obj) {
		x_fprintf(out, "\tnot %s\n", dest[1]->name);
	}	
}	

static void
emit_ret(struct icode_instr *ii) {
	(void) ii;

	if (curfunc->proto->dtype->tlist->next == NULL
		&& (curfunc->proto->dtype->code == TY_STRUCT
		|| curfunc->proto->dtype->code == TY_UNION)) {
		/*
		 * The hidden pointer used for structure returns
		 * is cleaned up by the callee. This NONSENSE has
		 * cost me a long time to track down ...
		 */
		x_fprintf(out, "\tret 4\n");
	} else {		
		x_fprintf(out, "\tret\n"); /* XXX */
	}	
}

struct icode_instr	*last_x87_cmp;
struct icode_instr	*last_sse_cmp;

static void
emit_cmp(struct reg **dest, struct icode_instr *src) {
	static int	was_llong;
	int		reg_idx = 0;
	int		need_ffree = 0;

	if (dest[0]->type == REG_FPR) {
		if (is_x87_trash(src->dest_vreg)) {
			last_x87_cmp = src;
		} else {
			/* Must be SSE */
			last_sse_cmp = src;
		}
		return;
	} else {
		if (was_llong) {
			reg_idx = 0;
			was_llong = 0;
		} else {	
			if (src->dest_vreg->is_multi_reg_obj) {
				if (!(src->hints &
				HINT_INSTR_NEXT_NOT_SECOND_LLONG_WORD)) {
					/*
					 * 06/02/08: Conditional for repeated
					 * cmps on same dword!
					 */
					was_llong = 1;
				}
				reg_idx = 1;
			} else {	
				reg_idx = 0;
			}
		}
		fprintf(out, "\tcmp %s, ", dest[reg_idx]->name);
	}	
	if (src->src_pregs == NULL || src->src_vreg == NULL) {
		fputc('0', out);
	} else {
		print_mem_or_reg(src->src_pregs[/*0*/reg_idx], src->src_vreg);
	}
	x_fputc('\n', out);
	if (need_ffree) {
		x_fprintf(out, "\tffree st0\n");
	}	
}

static void
emit_branch(struct icode_instr *ii) {
	char		*lname;
	char		*opcode = NULL;
	int		i;
	int		is_signed;
	static const struct {
		int	type;
		char	*for_signed;
		char	*for_unsigned;
	}		 instructions[] = {
		{ INSTR_BR_EQUAL, "je", "je" },
		{ INSTR_BR_SMALLER, "jl", "jb" }, /* less/below */
		{ INSTR_BR_SMALLEREQ, "jle", "jbe" }, /* less/below or equal */
		{ INSTR_BR_GREATER, "jg", "ja" }, /* greater/above */
		{ INSTR_BR_GREATEREQ, "jge", "jae" }, /* greater/above or eq */
		{ INSTR_BR_NEQUAL, "jne", "jne" },
		{ INSTR_JUMP, "jmp", "jmp" },
		{ -1, NULL, NULL }
	};

	lname = ((struct icode_instr *)ii->dat)->dat;
	if (ii->dest_vreg
		&& is_floating_type(ii->dest_vreg->type)) {
		int	cmp_with = 0;
		int	branch_if_equals = 0;
		int	is_sse = 0;
		
		if (is_x87_trash(ii->dest_vreg)) {
			fprintf(out, "\tfucomip st1\n");
		} else {
			/*
			 * Since this is fp but not x87, it must be SSE.
			 * This means we have to generate a ucomisd/ucomiss
			 */
			is_sse = 1;

			if (last_sse_cmp->dest_vreg->type->code == TY_FLOAT) {
				x_fprintf(out, "\tucomiss %s, %s\n",
					last_sse_cmp->src_pregs[0]->name,
					last_sse_cmp->dest_pregs[0]->name);
			} else {
				/* double */
				x_fprintf(out, "\tucomisd %s, %s\n",
					last_sse_cmp->src_pregs[0]->name,
					last_sse_cmp->dest_pregs[0]->name);
			}
		}

		/*
		 * Now we kludge our way around the x87/SSE way of
		 * signaling (in-)equality (both set eflags in the
		 * same way)...
		 * The flags register has the following meaningful flags:
		 *
		 *    ZF=0, PF=0, CF=0    means    st(0) > st(1)
		 *    ZF=0, PF=0, CF=1    means    st(0) < st(1)
		 *    ZF=1, PF=0, CF=0    means    st(0) = st(1)
		 *
		 * CF = bit 0 (lowest)
		 * PF = bit 2
		 * ZF = bit 6
		 *
		 * On x86 we can use the lahf instruction to get that
		 * flag register byte, however this is not available on
		 * AMD64, so we have to set* and or those flags together. 
		 */
#define FP_EQU_MASK	(1 /*CF*/  |  (1 << 2)  /*PF*/  |  (1 << 6) /*ZF*/)
#define ST1_SMALLER	(0)
#define ST1_GREATER	(1)
#define ST1_EQUAL	(1 << 6)
		if (backend->arch == ARCH_AMD64) {
			x_fprintf(out, "\tpush rax\n"); /* save ah/al */
			x_fprintf(out, "\tpush rbx\n"); /* save bl */

			/*
			 * Now painfully construct flags in ah like
			 * lahf plus masking does on x86. It is VERY
			 * important to do the set stuff before or'ing
			 * because or also sets flags! (sadly I got
			 * this wrong first, which cost me a good hour
			 * :-()
			 */
			x_fprintf(out, "\tsetc ah\n");
			x_fprintf(out, "\tsetp al\n");
			x_fprintf(out, "\tsetz bl\n");
			x_fprintf(out, "\tshl al, 2\n");
			x_fprintf(out, "\tshl bl, 6\n");
			x_fprintf(out, "\tor ah, al\n");
			x_fprintf(out, "\tor ah, bl\n");
		} else {
			x_fprintf(out, "\tpush eax\n"); /* save ah */
			x_fprintf(out, "\tlahf\n"); /* get flags */

			/* Mask off unused flags */
			x_fprintf(out, "\tand ah, %d\n", FP_EQU_MASK);
		}

		switch (ii->type) {
		case INSTR_BR_EQUAL:
			cmp_with = ST1_EQUAL;
			branch_if_equals = 1;
			break;
		case INSTR_BR_SMALLER:
			cmp_with = ST1_SMALLER;
			branch_if_equals = 1;
			break;
		case INSTR_BR_SMALLEREQ:
			cmp_with = ST1_GREATER;
			branch_if_equals = 0;
			break;
		case INSTR_BR_GREATER:
			cmp_with = ST1_GREATER;
			branch_if_equals = 1;
			break;
		case INSTR_BR_GREATEREQ:
			cmp_with = ST1_SMALLER;
			branch_if_equals = 0;
			break;
		case INSTR_BR_NEQUAL:
			cmp_with = ST1_EQUAL;
			branch_if_equals = 0;
			break;
		default:
			unimpl();
		}
		x_fprintf(out, "\tcmp ah, %d\n", cmp_with);

		if (backend->arch == ARCH_AMD64) {
			x_fprintf(out, "\tpop rbx\n");  /* restore bl */
			x_fprintf(out, "\tpop rax\n");  /* restore ah/al */
		} else {
			x_fprintf(out, "\tpop eax\n");  /* restore ah */
		}

		if (!is_sse) {
			/* Now free second used fp reg */
			x_fprintf(out, "\tffree st0\n");
		}

		/* Finally branch! */
		x_fprintf(out, "\t%s near .%s\n",
			branch_if_equals? "je": "jne", lname);

		return;
	}


	if (ii->dest_vreg == NULL) {
		/* Signedness doesn't matter */
		is_signed = 1;
	} else {
		/*
		 * 06/29/08: This was missing the check whether we
		 * are comparing pointers, and thus need unsigned
		 * comparison
		 */
		if (ii->dest_vreg->type->sign == TOK_KEY_UNSIGNED
			|| ii->dest_vreg->type->tlist != NULL) {
			is_signed = 0;
		} else {
			is_signed = 1;
		}
	}

	for (i = 0; instructions[i].type != -1; ++i) {
		if (instructions[i].type == ii->type) {
			if (is_signed) {
				opcode = instructions[i].for_signed;
			} else {
				opcode = instructions[i].for_unsigned;
			}
			break;
		}
	}
	if (instructions[i].type == -1) {
		printf("BUG: bad branch instruction - %d\n",
			ii->type);
		abort();
	}	

	/* XXX the near part may not always be necessary */
	x_fprintf(out, "\t%s near .%s\n", opcode, lname);
}

struct reg *
get_smaller_reg(struct reg *r, size_t size) {
	if (r->size == 8 && r->composed_of) {
		return get_smaller_reg(r->composed_of[0], size);
	}
	if (r->size == size) return r;
	if (r->size == 4) {
		if (size == 2) {
			return r->composed_of[0];
		} else { /* 1 */
			if (r->composed_of[0]->composed_of[1]) {
				return r->composed_of[0]->composed_of[1];
			} else {
				/* amd64 gpr */
				return r->composed_of[0]->composed_of[0];
			}
		}
	} else {
		/* 2 */
		return r->composed_of[1];
	}
}

static void
print_reg_assign(
	struct reg *dest,
	struct reg *srcreg,
	size_t src_size,
	struct type *src_type) {

	if (dest->type == REG_FPR) {
		if (!IS_FLOATING(src_type->code)) {
			x_fprintf(out, "\tfild ");
		} else {
			x_fprintf(out, "\tfld ");
		}
		return;
	}

	if (src_type->tlist != NULL
		&& (src_type->tlist->type == TN_ARRAY_OF
		|| src_type->tlist->type == TN_VARARRAY_OF)) {
		x_fprintf(out, "\tlea %s, ", dest->name);
		return;
	}	

	if (dest->size == src_size || src_size == 8) {
		/* == 8 for long long on x86 */
		x_fprintf(out, "\tmov %s, ", dest->name);
	} else {
		/* dest > src */
		if (src_type->sign == TOK_KEY_UNSIGNED) {	
			if (backend->arch == ARCH_AMD64
				&& dest->size == 8
				&& src_size == 4) {
				if (!isdigit(dest->name[1])) {
					/*
					 * e.g. mov eax, edx zero extends
					 * upper 32bits of rax
					 */
					x_fprintf(out, "\tmov %s, ",
						dest->composed_of[0]->name);
				} else {
					/* XXX there must be a better way :( */
					x_fprintf(out, "\tpush rax\n");
					x_fprintf(out, "\tmov eax, %s\n",
						srcreg->name);
					x_fprintf(out, "\tmov %s, rax\n",
						dest->name);
					x_fprintf(out, "\tpop rax\n");
					return;
				}
			} else {	
				x_fprintf(out, "\tmovzx %s, ", dest->name);
			}	
		} else {
			if (backend->arch == ARCH_AMD64
				&& dest->size == 8
				&& src_size == 4) {
				x_fprintf(out, "\tmovsxd %s, ", dest->name);
			} else {	
				x_fprintf(out, "\tmovsx %s, ", dest->name);
			}	
		}	
	}	

	if (srcreg != NULL) {
		x_fprintf(out, "%s\n", srcreg->name);
	}	
}


static void
emit_mov(struct copyreg *cr) {
	struct reg	*dest = cr->dest_preg;
	struct reg	*src = cr->src_preg;
	struct type	*src_type = cr->src_type;

	if (src == NULL) {
		/* Move null to register (XXX fp?)  */
		x_fprintf(out, "\tmov %s, 0\n",
			dest->name /*, size_to_asmtype(dest->size, NULL)*/);
	} else if (dest->type == REG_FPR) {
		/* XXX ... */
		if (STUPID_X87(dest)) {
			x_fprintf(out, "\tfxch %s, %s\n",
				dest->name, src->name);
		} else {
			x_fprintf(out, "\tmovs%c %s, %s\n",
				src_type->code == TY_FLOAT? 's': 'd',
				dest->name, src->name);
		}
	} else if (dest->size == src->size) {
		x_fprintf(out, "\tmov %s, %s\n", dest->name, src->name);
	} else if (dest->size > src->size) {
		print_reg_assign(dest, src, src->size, src_type);
		/*x_fprintf(out, "%s\n", src->name);*/
	} else {
		/* source larger than dest */
		src = get_smaller_reg(src, dest->size);
		x_fprintf(out, "\tmov %s, %s\n", dest->name, src->name);
	}	
}


static void
emit_setreg(struct reg *dest, int *value) {
	x_fprintf(out, "\tmov %s, %d\n", dest->name, *(int *)value);
}

static void
emit_xchg(struct reg *r1, struct reg *r2) {
	x_fprintf(out, "\txchg %s, %s\n", r1->name, r2->name);
}

static void
emit_initialize_pic(struct function *f) {
	char			buf[128];
	static unsigned long	count;
	
	(void) f;
	sprintf(buf, ".Piclab%lu", count++);
	x_fprintf(out, "\tcall %s\n", buf);
	x_fprintf(out, "%s:\n", buf);
	x_fprintf(out, "\tpop ebx\n");
	x_fprintf(out, "\tadd ebx, _GLOBAL_OFFSET_TABLE_+$$-%s wrt ..gotpc\n",
		buf);
}

static void
emit_addrof(struct reg *dest, struct vreg *src, struct vreg *structtop) {
	struct decl	*d;
	long		offset = 0;
	char		*sign = NULL;
	char		*base_pointer;
	
	if (backend->arch == ARCH_AMD64) {
		base_pointer = "rbp";
	} else {
		base_pointer = "ebp";
	}

	if (src == NULL) {
		d = curfunc->proto->dtype->tlist->tfunc->lastarg;
	} else {
		d = src->var_backed;
	}

	if (structtop != NULL) {
		d = structtop->var_backed;
	}

	if (d != NULL) {
		if (d->stack_addr != NULL) {
			if (d->stack_addr->is_func_arg) {
				sign = "+";
			} else {
				sign = "-";
			}	
			offset = d->stack_addr->offset;
		} else if (IS_THREAD(d->dtype->flags)) {
			x_fprintf(out, "\tmov %s, gs\n", dest->name);
			x_fprintf(out, "\tmov %s, [%s + %s wrt ..tpoff]\n",
				dest->name, dest->name, d->dtype->name);
			return;
		} else if (picflag) {
			if (d->dtype->is_func && d->dtype->tlist->type ==
				TN_FUNCTION) {
				x_fprintf(out, "\tlea %s, [ebx+%s "
					"wrt ..got]\n",
					dest->name, d->dtype->name);
			} else if (d->dtype->storage == TOK_KEY_STATIC
				|| d->dtype->storage == TOK_KEY_EXTERN) {
				if (backend->arch == ARCH_AMD64) {
					x_fprintf(out, "\tmov %s, [rel %s wrt ..gotpcrel]\n",
						dest->name, d->dtype->name);
				} else {
					x_fprintf(out, "\tmov %s, [ebx+%s "
						"wrt ..got]\n",
						dest->name, d->dtype->name);
				}
				if (src->parent != NULL) {
					x_fprintf(out, "\tadd %s, %ld\n",
						dest->name, calc_offsets(src));
				}
			}
			return;
		}
	} else if (picflag && src->from_const != NULL) {
		/*
		 * Not variable, but may be constant that needs a PIC
		 * relocation too. Most likely a string constant, but may
		 * also be an FP constant
		 */
		char	*name = NULL;
		char	buf[128];

		if (src->from_const->type == TOK_STRING_LITERAL) {
			struct ty_string	*ts = src->from_const->data;
			sprintf(buf, "_Str%lu", ts->count);
			name = buf;
		} else if (IS_FLOATING(src->from_const->type)) {
			struct ty_float		*tf = src->from_const->data;
			sprintf(buf, "_Float%lu", tf->count); /* XXX preceding . ? */
			name = buf;
		}
		if (name != NULL) {
			if (backend->arch == ARCH_AMD64) {
				x_fprintf(out, "\tmov %s, [rel %s wrt ..gotpcrel]\n",
					dest->name, name);
			} else {
				x_fprintf(out, "\tmov %s, [ebx + %s wrt ..got]\n",
					dest->name, name);
			}
			return;
		}
	}

	if (src && src->parent != NULL) {
		/* Structure or union type */
		if (d != NULL) {
			if (d->stack_addr != NULL) {
				x_fprintf(out, "\tlea %s, [%s %s %ld",
					dest->name, base_pointer,
					 sign, offset);
			} else {
				/* Static */
				x_fprintf(out, "\tlea %s, [$%s",
					dest->name, d->dtype->name);
			}	
		} else if (structtop->from_ptr) {
			x_fprintf(out, "\tlea %s, [%s",
				dest->name,
				structtop->from_ptr->pregs[0]->name);
		} else  {	
			printf("hm attempt to take address of %s\n",
				src->type->name);	
			unimpl();
		}	
		x_fputc(' ', out);
		print_nasm_offsets(src);
		x_fprintf(out, "]\n");
	} else if (src && src->from_ptr) {	
		x_fprintf(out, "\tmov %s, %s\n",
			dest->name, src->from_ptr->pregs[0]->name);
	} else if (src && src->from_const && src->from_const->type == TOK_STRING_LITERAL) {
		/* 08/21/08: This was missing */
		emit_load(dest, src);
	} else {		
		if (d && d->stack_addr) {
			if (src == NULL) {
				/* Move past object */
				offset += d->stack_addr->nbytes;
			}	

			x_fprintf(out, "\tlea %s, [%s %s %ld]\n",
				dest->name, base_pointer, sign, offset);
		} else if (d) {
			/*
			 * Must be static variable - symbol itself is
			 * address
			 */
			x_fprintf(out, "\tmov %s, $%s\n",
				dest->name, d->dtype->name);
		} else {
			printf("BUG: Cannot take address of item! preg=%p\n",
				src->pregs[0]);
			abort();
		}
	}	
}


static void
emit_fxch(struct reg *r, struct reg *r2) {
	x_fprintf(out, "\tfxch %s, %s\n", r->name, r2->name);
}

static void
emit_ffree(struct reg *r) {
	x_fprintf(out, "\tffree %s\n", r->name);
}	


static void
emit_fnstcw(struct vreg *vr) {
	x_fprintf(out, "\tfnstcw [$%s]\n", vr->type->name);
}

static void
emit_fldcw(struct vreg *vr) {
	x_fprintf(out, "\tfldcw [$%s]\n", vr->type->name);
}

/*
 * Copy initializer to automatic variable of aggregate type
 */
static void
emit_copyinit(struct decl *d) {
	x_fprintf(out, "\tpush dword %lu\n",
		(unsigned long)backend->get_sizeof_type(d->dtype, NULL) /*d->vreg->size*/);
	x_fprintf(out, "\tpush dword %s\n", d->init_name->name);
	x_fprintf(out, "\tlea eax, [ebp - %lu]\n", d->stack_addr->offset);
	x_fprintf(out, "\tpush dword eax\n");
	x_fprintf(out, "\tcall memcpy\n");
	x_fprintf(out, "\tadd esp, 12\n");
}


/*
 * Assign one struct to another (may be any of automatic or static or
 * addressed thru pointer)
 */
static void
emit_copystruct(struct copystruct *cs) {
	struct vreg	*stop;
	struct reg	*tmpreg = NULL;
	int		i;

	/* Get temporary register not used by our pointer(s), if any */
	for (i = 0; i < 4; ++i) {
		if (i == 1) {
			/* Don't use ebx as it's callee-saved */
			continue;
		}

		if (&x86_gprs[i] != cs->dest_from_ptr
			&& &x86_gprs[i] != cs->src_from_ptr
			&& &x86_gprs[i] != cs->dest_from_ptr_struct
			&& &x86_gprs[i] != cs->src_from_ptr_struct) {
			tmpreg = &x86_gprs[i];
			break;
		}
	}	

	x_fprintf(out, "\tpush dword %lu\n", (unsigned long)cs->src_vreg->size);
	if (cs->src_from_ptr == NULL) {
		if (cs->src_vreg->parent) {
			stop = get_parent_struct(cs->src_vreg);
		} else {
			stop = NULL;
		}	
		emit_addrof(tmpreg, cs->src_vreg, stop); 
		x_fprintf(out, "\tpush dword %s\n", tmpreg->name);
	} else {
		if (cs->src_vreg->parent) {
			x_fprintf(out, "\tadd %s, %lu\n",
				cs->src_from_ptr->name,
				calc_offsets(cs->src_vreg));
/*				cs->src_vreg->memberdecl->offset);	*/
		}	
		x_fprintf(out, "\tpush dword %s\n", cs->src_from_ptr->name);
	}

	if (cs->dest_from_ptr == NULL) {
		if (cs->dest_vreg->parent) {
			stop = get_parent_struct(cs->dest_vreg);
		} else {
			stop = NULL;
		}	
		emit_addrof(tmpreg, cs->dest_vreg, stop); 
		x_fprintf(out, "\tpush dword %s\n", tmpreg->name);
	} else {	
		if (cs->dest_vreg->parent) {
			x_fprintf(out, "\tadd %s, %lu\n",
				cs->dest_from_ptr->name,
				calc_offsets(cs->dest_vreg));
/*				cs->dest_vreg->memberdecl->offset);	*/
		}	
		x_fprintf(out, "\tpush dword %s\n", cs->dest_from_ptr->name);
	}	
	x_fprintf(out, "\tcall memcpy\n");
	x_fprintf(out, "\tadd esp, 12\n");
}

static void
emit_intrinsic_memcpy(struct int_memcpy_data *data) {
	struct reg	*dest = data->dest_addr;
	struct reg	*src = data->src_addr;
	struct reg	*nbytes = data->nbytes;
	struct reg	*temp = data->temp_reg;
	static int	labelcount;
	
	if (data->type == BUILTIN_MEMSET && src != temp) {
		x_fprintf(out, "\tmov %s, %s\n", temp->name, src->name);
	}
	x_fprintf(out, "\tcmp %s, 0\n", nbytes->name);
	x_fprintf(out, "\tje .Memcpy_done%d\n", labelcount);
	x_fprintf(out, ".Memcpy_start%d:\n", labelcount);
	if (data->type == BUILTIN_MEMCPY) {
		x_fprintf(out, "\tmov %s, [%s]\n", temp->name, src->name);
	}
	x_fprintf(out, "\tmov [%s], %s\n", dest->name, temp->name);
	x_fprintf(out, "\tinc %s\n", dest->name);
	if (data->type == BUILTIN_MEMCPY) {
		x_fprintf(out, "\tinc %s\n", src->name);
	}
	x_fprintf(out, "\tdec %s\n", nbytes->name);
	x_fprintf(out, "\tcmp %s, 0\n", nbytes->name);
	x_fprintf(out, "\tjne .Memcpy_start%d\n", labelcount);
	x_fprintf(out, ".Memcpy_done%d:\n", labelcount);
	++labelcount;
}

static void
emit_zerostack(struct stack_block *sb, size_t nbytes) {
	x_fprintf(out, "\tpush dword %lu\n", (unsigned long)nbytes);
	x_fprintf(out, "\tpush dword 0\n");
	x_fprintf(out, "\tlea ecx, [ebp - %lu]\n",
		(unsigned long)sb->offset);
	x_fprintf(out, "\tpush dword ecx\n");
	x_fprintf(out, "\tcall memset\n");
	x_fprintf(out, "\tadd esp, 12\n");
}

static void
emit_alloca(struct allocadata *ad) {
	x_fprintf(out, "\tpush dword %s\n", ad->size_reg->name);
	x_fprintf(out, "\tcall malloc\n");
	x_fprintf(out, "\tadd esp, 4\n");
	if (ad->result_reg != &x86_gprs[0]) {
		x_fprintf(out, "\tmov %s, eax\n",
			ad->result_reg->name);
	}
}


static void
emit_dealloca(struct stack_block *sb, struct reg *r) {
	char	*regname = r? r->name: "ecx";

	x_fprintf(out, "\tmov %s, [ebp - %lu]\n",
		regname,	
		(unsigned long)sb->offset);
	x_fprintf(out, "\tpush dword %s\n", regname);
	x_fprintf(out, "\tcall free\n");
	x_fprintf(out, "\tadd esp, 4\n");
}

static void
emit_alloc_vla(struct stack_block *sb) {
	x_fprintf(out, "\tmov ecx, [ebp - %lu]\n",
		(unsigned long)sb->offset - backend->get_ptr_size());
	x_fprintf(out, "\tpush dword ecx\n");
	x_fprintf(out, "\tcall malloc\n");
	x_fprintf(out, "\tadd esp, 4\n");
	x_fprintf(out, "\tmov [ebp - %lu], eax\n",
		(unsigned long)sb->offset);
}


static void
emit_dealloc_vla(struct stack_block *sb, struct reg *r) {
	char	*regname = r? r->name: "ecx";

	x_fprintf(out, "\tmov %s, [ebp - %lu]\n",
		regname,	
		(unsigned long)sb->offset);
	x_fprintf(out, "\tpush dword %s\n", regname);
	x_fprintf(out, "\tcall free\n");
	x_fprintf(out, "\tadd esp, 4\n");
}

static void
emit_put_vla_size(struct vlasizedata *data) {
	x_fprintf(out, "\tmov [ebp - %lu], %s\n",
		(unsigned long)data->blockaddr->offset - data->offset,
		data->size->name);
}

static void
emit_retr_vla_size(struct vlasizedata *data) {
	x_fprintf(out, "\tmov %s, [ebp - %lu]\n",
		data->size->name,
		(unsigned long)data->blockaddr->offset - data->offset);
}

static void
emit_load_vla(struct reg *r, struct stack_block *sb) {
	x_fprintf(out, "\tmov %s, [ebp - %lu]\n",
		r->name,
		(unsigned long)sb->offset);
}

static void
emit_frame_address(struct builtinframeaddressdata *dat) {
	x_fprintf(out, "\tmov %s, ebp\n", dat->result_reg->name);
}
			

static void
emit_cdq(void) {
	x_fprintf(out, "\tcdq\n");
}

static void
emit_fist(struct fistdata *dat) {
	int	size = backend->get_sizeof_type(dat->target_type, NULL);

	x_fprintf(out, "\tfistp %s ",
		/*dat->vr->*/size == 4? "dword": "qword");
	if (backend->arch == ARCH_AMD64) {
		amd64_print_mem_operand_yasm(dat->vr, NULL);
	} else {
		print_mem_operand(dat->vr, NULL);
		print_mem_operand(NULL, NULL);
	}
	x_fputc('\n', out);
}

static void
emit_fild(struct filddata *dat) {
	x_fprintf(out, "\tfild %s ",
		dat->vr->size == 4? "dword": "qword");
	if (backend->arch == ARCH_AMD64) {
		amd64_print_mem_operand_yasm(dat->vr, NULL);
	} else {
		print_mem_operand(dat->vr, NULL);
		print_mem_operand(NULL, NULL);
	}
	x_fputc('\n', out);
}

static void
emit_x86_ulong_to_float(struct icode_instr *ii) {
	struct amd64_ulong_to_float	*data = ii->dat;
	static unsigned long		count;

	x_fprintf(out, "\ttest %s, %s\n", data->src_gpr->name, data->src_gpr->name);
	x_fprintf(out, "\tjs ._Ulong_float%lu\n", count);
	x_fprintf(out, "\tjmp ._Ulong_float%lu\n", count+1);
	x_fprintf(out, "._Ulong_float%lu:\n", count);
	x_fprintf(out, "\tfadd dword [_Ulong_float_mask]\n");
	x_fprintf(out, "._Ulong_float%lu:\n", count+1);
	count += 2;
}

static void
emit_save_ret_addr(struct function *f, struct stack_block *sb) {
	(void) f;

	x_fprintf(out, "\tmov eax, [ebp + 4]\n");
	x_fprintf(out, "\tmov [ebp - %lu], eax\n", sb->offset);
}

static void
emit_check_ret_addr(struct function *f, struct stack_block *saved) {
	static unsigned long	labval = 0;

	(void) f;
	
	x_fprintf(out, "\tmov ecx, [ebp - %lu]\n", saved->offset);
	x_fprintf(out, "\tcmp ecx, dword [ebp + 4]\n");
	x_fprintf(out, "\tje .doret%lu\n", labval);
	x_fprintf(out, "\textern __nwcc_stack_corrupt\n");
/*	x_fprintf(out, "\tcall __nwcc_stack_corrupt\n");*/
	emit_call("__nwcc_stack_corrupt");
	x_fprintf(out, ".doret%lu:\n", labval++);
}

static void
do_stack(FILE *out, struct decl *d) {
	char	*sign;

	if (d->stack_addr->is_func_arg) {
		sign = "+";
	} else {
		sign = "-";
	}	
	x_fprintf(out, "[ebp %s %lu", sign, d->stack_addr->offset);
}

static void
print_mem_operand(struct vreg *vr, struct token *constant) {
	static int	was_llong;
	int		needbracket = 1;

	if (vr == NULL && constant == NULL) {
		/*
		 * 06/12/08: Reset long long flag! The problem was
		 * storing an fp value to a long long buffer, using
		 * fistpq. This is the single place where we don't
		 * need two store operations, so we can reset the
		 * llong flag here
		 */
		was_llong = 0;
		return;
	}

	/* 04/11/08: See comment in nasm print_mem_operand() */
	assert(backend->arch == ARCH_X86);

	if (vr && vr->from_const != NULL) {
		constant = vr->from_const;
	}
	if (constant != NULL) {
		struct token	*t = vr->from_const;


		if (IS_INT(t->type) || IS_LONG(t->type)) {
			cross_print_value_by_type(out,
				t->data,
				t->type, 'd');
#if ALLOW_CHAR_SHORT_CONSTANTS
		} else if (IS_CHAR(t->type) || IS_SHORT(t->type)) {	
			cross_print_value_by_type(out,
				t->data,
				t->type, 'd');
#endif
		} else if (IS_LLONG(t->type)) {
			static int	was_llong = 0;
			void		*p;
	
			if (was_llong) {
				/* Loading second part of long long */
				p = (char *)t->data + 4;
				was_llong = 0;
			} else {
				p = t->data;
				was_llong = 1;
			}	
			if (t->type == TY_LLONG) {
				x_fprintf(out, "%u",
					*(unsigned int *)p);
			} else {
				/* ULLONG */
				x_fprintf(out, "%u",
					*(unsigned int *)p);
			}
		} else if (t->type == TOK_STRING_LITERAL) {
			struct ty_string	*ts = t->data;
			x_fprintf(out, "_Str%ld",
				ts->count);
		} else if (t->type == TY_FLOAT
			|| t->type == TY_DOUBLE
			|| t->type == TY_LDOUBLE) {
			struct ty_float	*tf = t->data;
	
			x_fprintf(out, "[_Float%lu]",
				tf->count);
		} else {
			printf("loadimm: Bad data type %d\n", t->type);
			abort();
		}
	} else if (vr->parent != NULL) {
		struct vreg	*vr2;
		struct decl	*d2;

		vr2 = get_parent_struct(vr);
		if ((d2 = vr2->var_backed) != NULL) {
			if (d2->stack_addr) {
				do_stack(out, vr2->var_backed);
			} else {
				/* static */
				x_fprintf(out, "[$%s", d2->dtype->name);
			}
		} else if (vr2->from_ptr) {
			/* Struct comes from pointer */
			x_fprintf(out, "[%s",
				vr2->from_ptr->pregs[0]->name);	
		} else {
			printf("BUG: Bad load for %s\n",
				vr->type->name? vr->type->name: "structure");
			abort();
		}
		x_fputc(' ', out);
		print_nasm_offsets(vr);
	} else if (vr->var_backed) {
		struct decl	*d = vr->var_backed;

		if (d->stack_addr != NULL) {
			do_stack(out, d);
		} else {
			/*
			 * Static or register variable
			 */
			if (d->dtype->storage == TOK_KEY_REGISTER) {
				unimpl();
			} else {
				if (d->dtype->tlist != NULL
					&& d->dtype->tlist->type
					== TN_FUNCTION) {
					needbracket = 0;
				} else {
					x_fputc('[', out);
				}	
				x_fprintf(out, "$%s", d->dtype->name);
			}	
		}
	} else if (vr->stack_addr) {
		x_fprintf(out, "[ebp - %lu", vr->stack_addr->offset);
	} else if (vr->from_ptr) {
		x_fprintf(out, "[%s", vr->from_ptr->pregs[0]->name);
	} else {
		abort();
	}
	if (constant == NULL) {
		if (was_llong) {
			x_fprintf(out, " + 4 ");
			was_llong = 0;
		} else if (vr->is_multi_reg_obj) {
			was_llong = 1;
		}	
		if (needbracket) { 
			x_fputc(']', out);
		}	
	}	
}	


/*
 * Print inline asm instruction operand
 */
void
print_item_nasm(FILE *out, void *item, int item_type, int postfix) {
	print_asmitem_x86(out, item, item_type, postfix, TO_NASM);
}

struct emitter x86_emit_nasm = {
	1, /* need_explicit_extern_decls */
	init,
	emit_strings,
	emit_fp_constants,
	NULL, /* llong_constants */
	emit_support_buffers,
	NULL, /* pic_support */

	emit_support_decls,
	emit_extern_decls,
	emit_global_extern_decls,
	emit_global_static_decls,
#if 0
	emit_global_decls,
	emit_static_decls,
#endif
	emit_static_init_vars,
	emit_static_uninit_vars,
	emit_static_init_thread_vars,
	emit_static_uninit_thread_vars,
	emit_struct_defs,
	emit_comment,
	NULL, /* emit_debug */
	emit_dwarf2_line,
	emit_dwarf2_files,
	emit_inlineasm,
	emit_unimpl,
	emit_empty,
	emit_label,
	emit_call,
	emit_callindir,
	emit_func_header,
	emit_func_intro,
	emit_func_outro,
	emit_define,
	emit_push,
	emit_allocstack,
	emit_freestack,
	emit_adj_allocated,
	emit_inc,
	emit_dec,
	emit_load,
	emit_load_addrlabel,
	emit_comp_goto,
	emit_store,
	emit_setsection,
	emit_alloc,
	emit_neg,
	emit_sub,
	emit_add,
	emit_div,
	emit_mod,
	emit_mul,
	emit_shl,
	emit_shr,
	emit_or,
	NULL, /* emit_preg_or */
	emit_and,
	emit_xor,
	emit_not,
	emit_ret,
	emit_cmp,
	NULL, /* extend_sign */
	NULL, /* conv_fp */
	NULL, /* from_ldouble */
	NULL, /* to_ldouble */
	emit_branch,
	emit_mov,
	emit_setreg,
	emit_xchg,
	emit_addrof,
	emit_initialize_pic,
	emit_copyinit,
	NULL, /* putstructregs */
	emit_copystruct,
	emit_intrinsic_memcpy,
	emit_zerostack,
	emit_alloca,
	emit_dealloca,
	emit_alloc_vla,
	emit_dealloc_vla,
	emit_put_vla_size,
	emit_retr_vla_size,
	emit_load_vla,
	emit_frame_address,
	emit_struct_inits,
	emit_save_ret_addr,
	emit_check_ret_addr,
	print_mem_operand,
	NULL, /* finish_program */
	NULL, /* stupidtrace */
	NULL /* finish_stupidtrace */
};


struct emitter_x86 x86_emit_x86_nasm = {
	emit_fxch,
	emit_ffree,
	emit_fnstcw,
	emit_fldcw,
	emit_cdq,
	emit_fist,
	emit_fild,
	emit_x86_ulong_to_float
};		


