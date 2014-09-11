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
 * SGI assembler emitter
 */
#include "sparc_emit_as.h"
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
#include "debug.h"
#include "backend.h"
#include "token.h"
#include "functions.h"
#include "symlist.h"
#include "sparc_gen.h"
#include "typemap.h"
#include "cc_main.h"
#include "expr.h"
#include "reg.h"
#include "libnwcc.h"
#include "inlineasm.h"
#include "error.h"
#include "n_libc.h"

static FILE	*out;
static size_t	data_segment_offset;
static size_t	data_thread_segment_offset;

static int 
init(FILE *fd, struct scope *s) {
	(void) s;
	out = fd;
	return 0;
}

static int	cursect = 0;

static void
emit_setsection(int value) {
	char	*p;
	
	if (cursect == value) {
		return;
	}
	cursect = value;
	p = generic_elf_section_name(value);
	x_fprintf(out, ".section \".%s\"\n", p);
}

static void
print_mem_operand(struct vreg *vr, struct token *constant,
struct vreg *vreg_parent);


void	as_print_string_init(FILE *out, size_t howmany, struct ty_string *str);

static void
print_init_expr(struct type *dt, struct expr *ex) {
	struct tyval		*cv;
	int			is_addr_as_int = 0;

	cv = ex->const_value;
#if 0
	as_align_for_type(out, dt);
#endif
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
			x_fprintf(out, ".byte ");
			cross_print_value_by_type(out,
				ex->const_value->value, TY_UCHAR, 'x');	
			break;
		case TY_SHORT:
		case TY_USHORT:
			x_fprintf(out, ".half ");
			cross_print_value_by_type(out,
				ex->const_value->value, TY_USHORT, 'x');	
			break;
		case TY_INT:
		case TY_UINT:	
		case TY_ENUM:	
			x_fprintf(out, ".word ");
			cross_print_value_by_type(out,
				ex->const_value->value, TY_UINT, 'x');	
			break;
		case TY_LONG:
			if (backend->abi == ABI_SPARC64) {
				x_fprintf(out, ".xword ");
				cross_print_value_by_type(out,
					ex->const_value->value, TY_ULONG, 'x');
			} else {	
				x_fprintf(out, ".word ");
				cross_print_value_by_type(out,
					ex->const_value->value, TY_ULONG, 'x');
			}
			break;
		case TY_ULONG:
			if (backend->abi == ABI_SPARC64) {
				x_fprintf(out, ".xword ");
				cross_print_value_by_type(out,
					ex->const_value->value, TY_ULONG, 'x');
			} else {	
				x_fprintf(out, ".word ");
				cross_print_value_by_type(out,
					ex->const_value->value, TY_ULONG, 'x');
			}	
			break;
		case TY_LLONG:
		case TY_ULLONG:	
			x_fprintf(out, ".word ");
			cross_print_value_chunk(out, ex->const_value->value,
				dt->code, TY_UINT, 0, 0);	
			x_fprintf(out, "\n\t.word ");
			cross_print_value_chunk(out, ex->const_value->value,
				dt->code, TY_UINT, 0, 1);
			break;
		case TY_FLOAT:
			x_fprintf(out, ".word ");
			cross_print_value_by_type(out,
				ex->const_value->value, TY_UINT, 'x');	
			break;
		case TY_DOUBLE:
			x_fprintf(out, ".word ");
			cross_print_value_chunk(out, ex->const_value->value,
				dt->code, TY_UINT, 0, 0);	
			x_fprintf(out, "\n\t.word ");
			cross_print_value_chunk(out, ex->const_value->value,
				dt->code, TY_UINT, 0, 1);
			break;
		case TY_LDOUBLE:
			x_fprintf(out, ".word ");
			cross_print_value_chunk(out, ex->const_value->value,
				dt->code, TY_UINT, 0, 0);	
			x_fprintf(out, "\n\t.word ");
			cross_print_value_chunk(out, ex->const_value->value,
				dt->code, TY_UINT, 0, 1);
			x_fprintf(out, "\n\t.word ");
			cross_print_value_chunk(out, ex->const_value->value,
				dt->code, TY_UINT, 0, 2);
			x_fprintf(out, "\n\t.word ");
			cross_print_value_chunk(out, ex->const_value->value,
				dt->code, TY_UINT, 0, 2);
			break;
		default:	
			printf("print_init_expr: "
				"unsupported datatype %d\n",
				dt->code);
			unimpl();
		}
	} else {
		if (is_addr_as_int || dt->tlist->type == TN_POINTER_TO) {
			if (backend->abi == ABI_SPARC64) {
				x_fprintf(out, ".xword ");
			} else {
				x_fprintf(out, ".word ");
			}	
			if (ex->const_value->is_nullptr_const) {
				x_fprintf(out, "0x0");
			} else if (cv->str) {
				x_fprintf(out, "_Str%lu", cv->str->count);
			} else if (cv->value) {
#if 0
				x_fprintf(out, "%lu",
					*(unsigned long *)cv->value);
#endif
				cross_print_value_by_type(out,
					cv->value, TY_ULONG, 'x');
			} else if (cv->address) {
				/* XXX */
				char	*sign;

				if (cv->address->diff < 0) {
					/*
					 * 08/02/09: diff is already negative,
					 * the extra sign is not required
					 */
					sign = "";
				} else {
					sign = "+";
				}
				if (cv->address->dec != NULL) {
					/* Identifier */
					x_fprintf(out, "%s%s%ld",
					cv->address->dec->dtype->name, sign,
					cv->address->diff);
				} else {
					/* Label */
					x_fprintf(out, ".%s%s%ld",
					cv->address->labelname, sign,
					cv->address->diff);
				}
			}
		} else if (dt->tlist->type == TN_ARRAY_OF) {
			size_t	arrsize;
			
			/*
			 * This has to be a string because only in
			 * char buf[] = "hello"; will an aggregate
			 * initializer ever be stored as INIT_EXPR
			 */

			arrsize = dt->tlist->arrarg_const;
			as_print_string_init(out, arrsize, cv->str);

			if (arrsize >= cv->str->size) {
				if (arrsize > cv->str->size) {
					x_fprintf(out, "\n\t.skip %lu\n",
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

static void
emit_global_extern_decls(struct decl **d, int ndecls) {
	/*
	  * 03/24/08: Like in MIPS and PPC: why is this case not handled?
	  */
	(void) d; (void) ndecls;
}

static void
emit_global_static_decls(struct decl **dv, int ndecls) {
	size_t		size;
	int		i;

	(void) size;
	for (i = 0; i < ndecls; ++i) {
		if (dv[i]->dtype->storage != TOK_KEY_STATIC) {
			struct type_node	*tn = NULL;
			
			if (dv[i]->invalid) {
				continue;
			}

			if (dv[i]->dtype->is_func) {
				continue;
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

			x_fprintf(out, ".globl %s\n", dv[i]->dtype->name);
			if (tn != NULL) {
				tn->ptrarg = 1;
			}
		}
	}
}


static void
emit_static_init_vars(struct decl *list) {
	struct decl	*d;

	if (list != NULL) {
		emit_setsection(SECTION_INIT);
		x_fprintf(out, "\t.align 16\n");
		for (d = list; d != NULL; d = d->next) {
			if (DECL_UNUSED(d)) {
				/*
				 * 02/16/08: This is needed because otherwise
				 * there will be lots of multiple type/size
				 * directives for unused __func__ declarations.
			 	 * I don't know why this happens (mmultiple
				 * entries per functiton) yet
				 *
				 * 03/24/08: It's aliases :-)
				 * (__func__, __PRETTY_FUNCTION__, ...)
				 */
				continue;
			}
			if (d->invalid) {
				continue;
			}
			if (picflag) {
				x_fprintf(out, "\t.type %s, #object\n",
					d->dtype->name);
				x_fprintf(out, "\t.size %s, %lu\n",
					d->dtype->name,
					backend->get_sizeof_type(d->dtype, NULL));
			}
			data_segment_offset += generic_print_init_var(
				out, d, data_segment_offset, print_init_expr, 0);
		}
	}

}

static void
emit_static_uninit_vars(struct decl *list) {
	struct decl	*d;
	size_t		size;

	if (list != NULL) {
		emit_setsection(SECTION_UNINIT);
		x_fprintf(out, "\t.align 16\n");
		for (d = list; d != NULL; d = d->next) {
			if (d->dtype->storage == TOK_KEY_STATIC
				&& d->references == 0) {
				continue;
			}
			if (d->invalid) {
				continue;
			}

			as_align_for_type(out, d->dtype, 0);
			size = backend->get_sizeof_decl(d, NULL);
			if (picflag) {
				x_fprintf(out, "\t.type %s, #object\n", d->dtype->name);
				x_fprintf(out, "\t.size %s, %lu\n", d->dtype->name, size);
			}
			x_fprintf(out, "%s:\n", d->dtype->name);
			x_fprintf(out, "\t.skip %lu\n", size);
		}
	}
}

static void
emit_static_init_thread_vars(struct decl *list) {
	struct decl	*d;

	if (list != NULL) {
		emit_setsection(SECTION_INIT_THREAD);
		x_fprintf(out, "\t.align 16\n");
		for (d = list; d != NULL; d = d->next) {
			if (d->invalid) {
				continue;
			}
			if (picflag) {
				x_fprintf(out, "\t.type %s, #object\n",
					d->dtype->name);
				x_fprintf(out, "\t.size %s, %lu\n",
					d->dtype->name,
					backend->get_sizeof_type(d->dtype, NULL));
			}
			data_thread_segment_offset += generic_print_init_var(
				out, d, data_thread_segment_offset, print_init_expr, 0);
		}
	}
}

static void
emit_static_uninit_thread_vars(struct decl *list) {
	struct decl	*d;
	size_t		size;

	if (list != NULL) {
		emit_setsection(SECTION_UNINIT_THREAD);
		x_fprintf(out, "\t.align 16\n");
		for (d = list; d != NULL; d = d->next) {
			if (d->dtype->storage == TOK_KEY_STATIC
				&& d->references == 0) {
				continue;
			}
			if (d->invalid) {
				continue;
			}

			as_align_for_type(out, d->dtype, 0);
			size = backend->get_sizeof_decl(d, NULL);
			if (picflag) {
				x_fprintf(out, "\t.type %s, #object\n",
					d->dtype->name);
				x_fprintf(out, "\t.size %s, %lu\n",
					d->dtype->name, size);
			}
			x_fprintf(out, "%s:\n", d->dtype->name);

			x_fprintf(out, "\t.skip %lu\n", size);
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
		x_fprintf(out, ".section \".bss\"\n");
		x_fprintf(out, "\t.align 16\n");
		for (d = static_uninit_vars; d != NULL; d = d->next) {
			if (d->dtype->storage == TOK_KEY_STATIC
				&& d->references == 0) {
				continue;
			}

			as_align_for_type(out, d->dtype);
			size = backend->get_sizeof_decl(d, NULL);
			if (picflag) {
				x_fprintf(out, "\t.type %s, #object\n", d->dtype->name);
				x_fprintf(out, "\t.size %s, %lu\n", d->dtype->name, size);
			}
			x_fprintf(out, "%s:\n", d->dtype->name);
			x_fprintf(out, "\t.skip %lu\n", size);
		}
	}

	data_segment_offset = 0;
	if (static_init_vars != NULL) {
		x_fprintf(out, ".section \".data\"\n");
		x_fprintf(out, "\t.align 16\n");
		for (d = static_init_vars; d != NULL; d = d->next) {
			if (DECL_UNUSED(d)) {
				/*
				 * 02/16/08: This is needed because otherwise
				 * there will be lots of multiple type/size
				 * directives for unused __func__ declarations.
			 	 * I don't know why this happens (mmultiple
				 * entries per functiton) yet
				 */
				continue;
			}
			if (picflag) {
				x_fprintf(out, "\t.type %s, #object\n",
					d->dtype->name);
				x_fprintf(out, "\t.size %s, %lu\n",
					d->dtype->name,
					backend->get_sizeof_type(d->dtype, NULL));
			}
			data_segment_offset += generic_print_init_var(
				out, d, data_segment_offset, print_init_expr, 0);
		}
	}

	bss_thread_segment_offset = 0;
	if (static_uninit_thread_vars != NULL) {
		x_fprintf(out, ".section \".tbss\"\n");
		x_fprintf(out, "\t.align 16\n");
		for (d = static_uninit_vars; d != NULL; d = d->next) {
			if (d->dtype->storage == TOK_KEY_STATIC
				&& d->references == 0) {
				continue;
			}

			as_align_for_type(out, d->dtype);
			size = backend->get_sizeof_decl(d, NULL);
			if (picflag) {
				x_fprintf(out, "\t.type %s, #object\n",
					d->dtype->name);
				x_fprintf(out, "\t.size %s, %lu\n",
					d->dtype->name, size);
			}
			x_fprintf(out, "%s:\n", d->dtype->name);

			x_fprintf(out, "\t.skip %lu\n", size);
		}
	}

	data_thread_segment_offset = 0;
	if (static_init_thread_vars != NULL) {
		x_fprintf(out, ".section \".tdata\"\n");
		x_fprintf(out, "\t.align 16\n");
		for (d = static_init_vars; d != NULL; d = d->next) {
			if (picflag) {
				x_fprintf(out, "\t.type %s, #object\n",
					d->dtype->name);
				x_fprintf(out, "\t.size %s, %lu\n",
					d->dtype->name,
					backend->get_sizeof_type(d->dtype, NULL));
			}
			data_thread_segment_offset += generic_print_init_var(
				out, d, data_thread_segment_offset, print_init_expr, 0);
		}
	}
}
#endif

static void
emit_struct_inits(struct init_with_name *list) {
	struct init_with_name	*in;

	if (list != NULL) {
		emit_setsection(SECTION_INIT);
		for (in = list; in != NULL; in = in->next) {
			x_fprintf(out, "%s:\n", in->name);
			new_generic_print_init_list(out, in->dec,
				in->init, print_init_expr);
		}
	}
}

static void
emit_pic_support(void) {
	if (picflag) {
		/* Generate function to get program counter for PIC offsets */
		emit_setsection(SECTION_TEXT);
		x_fprintf(out, "\t.align 4\n");
		x_fprintf(out, "_GetPC:\n");
		x_fprintf(out, "\tretl\n");
		x_fprintf(out, "\tadd %%%s, %%%s, %%%s\n",
			o_regs[7].name, pic_reg->name, pic_reg->name);
	}
}

static void
emit_fp_constants(struct ty_float *list) {
	if (list != NULL) {
		struct ty_float	*tf;

		emit_setsection(SECTION_INIT);
		for (tf = list; tf != NULL; tf = tf->next) {
			/* XXX cross-compilation ... */
			switch (tf->num->type) {
			case TY_FLOAT:
				x_fprintf(out, "\t.align 4\n");
				break;
			case TY_DOUBLE:
			case TY_LDOUBLE:
				x_fprintf(out, "\t.align 8\n");
			}

			x_fprintf(out, "_Float%lu:\n", tf->count);
			switch (tf->num->type) {
			case TY_FLOAT:
				x_fprintf(out, "\t.word ");
				cross_print_value_by_type(out,
					tf->num->value, TY_UINT, 'x');
				break;
			case TY_DOUBLE:
			case TY_LDOUBLE:	
				x_fprintf(out, "\t.xword ");
				cross_print_value_chunk(out,
					tf->num->value,
					tf->num->type, TY_ULLONG, 0, 0);	
				x_fprintf(out, "LL");
#if 0
				x_fprintf(out, "\n\t.word ");
				cross_print_value_chunk(out,
					tf->num->value,
					tf->num->type, TY_UINT, 0, 1);
#endif
				break;
#if 0
				/* XXX our double is not 16 bytes yet.. */
			case TY_LDOUBLE:
				x_fprintf(out, "\t.word %u\n",
					*(unsigned *)tf->num->value);
				x_fprintf(out, "\t.word %u\n",
					((unsigned *)tf->num->value)[1]);
				x_fprintf(out, "\t.word %u\n",
					((unsigned *)tf->num->value)[2]);
				x_fprintf(out, "\t.word %u\n",
					((unsigned *)tf->num->value)[3]);
				break;
#endif
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
emit_strings(struct ty_string *list) {
	if (list != NULL) {
		struct ty_string	*str;

		emit_setsection(SECTION_RODATA);
		for (str = list; str != NULL; str = str->next) {
			x_fprintf(out, "_Str%lu:\n", str->count);
			as_print_string_init(out, str->size, str);
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
	x_fprintf(out, "! ");
	rc = vfprintf(out, fmt, va);
	va_end(va);
	x_fputc('\n', out);

	if (rc == EOF || fflush(out) == EOF) {
		perror("vfprintf");
		exit(EXIT_FAILURE);
	}
}

static void
emit_inlineasm(struct inline_asm_stmt *stmt) {
	unimpl();
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
	/* Not implemented!! HA HA HA HOW IRONIC!!!!!!!!!!!!!! */
	unimpl();
}	

static void
emit_empty(void) {
	x_fputc('\n', out);
}

static void
emit_label(const char *name, int is_func) {
	if (is_func) {
		x_fprintf(out, "%s:\n", name);
	} else {
		x_fprintf(out, ".%s:\n", name);
	}
}

static void
emit_call(const char *name) {
	x_fprintf(out, "\tcall %s, 0\n", name);
	x_fprintf(out, "\tnop\n");
}

static void
emit_callindir(struct reg *r) {
	x_fprintf(out, "\tcall %%%s, 0\n", r->name);
	x_fprintf(out, "\tnop\n");
}	

static void
emit_func_header(struct function *f) {
	if (picflag) {
		x_fprintf(out, "\t.type %s, #function\n",
			f->proto->dtype->name);
	}
}

static void
emit_func_intro(struct function *f) {
	(void) f;
}

static void
emit_func_outro(struct function *f) {
	if (picflag) {
		x_fprintf(out, "\t.size %s, .-%s\n",
			f->proto->dtype->name, f->proto->dtype->name);	
	}
}


static void
load_32bit_imm(struct reg *dest, unsigned int *value);

static void
emit_allocstack(struct function *f, size_t nbytes) {
	(void) f;
	if (!f->gotframe) {
		/* Creating stack frame */
		if (nbytes < 4096) {
			x_fprintf(out, "\tsave %%sp, -%lu, %%sp\n",
				(unsigned long)nbytes);	
		} else {
			/*
			 * Size is too big to be used as an immediate
			 * operand to ``save''; We have to load the
			 * value into a register and use that instead
			 */
			unsigned int	ui = nbytes;
			load_32bit_imm(tmpgpr, &ui);
			x_fprintf(out, "\tneg %%%s\n", tmpgpr->name);
			x_fprintf(out, "\tsave %%sp, %%%s, %%sp\n",
				tmpgpr->name);
		}
		f->gotframe = 1;
	}
}


static void
emit_freestack(struct function *f, size_t *nbytes) {
#if 0
	if (backend->abi == ABI_SPARC64) {
		instr = "daddiu";
	} else {
		instr = "addiu";
	}	
#endif
	if (nbytes == NULL) {
		/* Procedure outro */
		if (f->total_allocated != 0) {
			/* XXX never needed? */
#if 0
			x_fprintf(out, "\t%s $sp, $sp, %lu\n",
				instr, (unsigned long)f->total_allocated);
#endif
		}	
	} else {
		if (*nbytes != 0) {
#if 0
			x_fprintf(out, "\t%s $sp, $sp, %lu\n",
				instr, (unsigned long)*nbytes);	
#endif
			f->total_allocated -= *nbytes;
		}
	}
}

static void
emit_adj_allocated(struct function *f, int *nbytes) {
	f->total_allocated += *nbytes; 
}	

static void
emit_struct_defs(void) {
	struct scope	*s;

	for (s = &global_scope; s != NULL; s = s->next) {
		struct ty_struct	*ts;

		for (ts = s->struct_defs.head; ts != NULL; ts = ts->next) {
			if (!ts->is_union) {
				/*do_print_struct(ts);*/
			}	
		}
	}
}


static void
emit_alloc(size_t nbytes) {
	(void) nbytes;
	unimpl();
}


static void
print_mem_or_reg(struct reg *r, struct vreg *vr) {
	if (vr->on_var) {
		struct decl	*d = vr->var_backed;

		unimpl();
		if (d->stack_addr) {
			x_fprintf(out, "[ebp - %ld]",
				d->stack_addr->offset);
		} else {
			x_fprintf(out, "%s", d->dtype->name);
		}
	} else {
		x_fprintf(out, "%%%s", r->name);
	}
}



static void
emit_inc(struct icode_instr *ii) {
	x_fprintf(out, "\t%s %%%s, 1, %%%s\n",
		ii->src_vreg->size == 8? "add": "add",
		ii->src_pregs[0]->name, ii->src_pregs[0]->name);
}

static void
emit_dec(struct icode_instr *ii) {
	x_fprintf(out, "\t%s %%%s, 1, %%%s\n",
		ii->src_vreg->size == 8? "sub": "sub",
		ii->src_pregs[0]->name, ii->src_pregs[0]->name,
		tmpgpr->name);
}

static void
load_64bit_sym(struct reg *r, const char *name) {
	struct reg	*tempreg;

	if (r == tmpgpr) {
		tempreg = tmpgpr2;
	} else {
		tempreg = tmpgpr;
	}
	x_fprintf(out, "\tsethi %%hh(%s), %%%s\n",
		name, tempreg->name);
	x_fprintf(out, "\tsethi %%lm(%s), %%%s\n",
		name, r->name);
	x_fprintf(out, "\tor %%%s, %%hm(%s), %%%s\n",
		tempreg->name, name, tempreg->name);	
	x_fprintf(out, "\tsllx %%%s, 32, %%%s\n",
		tempreg->name, tempreg->name);
	x_fprintf(out, "\tadd %%%s, %%%s, %%%s\n",
		tempreg->name, r->name, tempreg->name);
	x_fprintf(out, "\tor %%%s, %%lo(%s), %%%s\n",
		tempreg->name, name, r->name);	
}

/*
 * Load 32bit or 64bit symbol into register. This is sort of
 * difficult because it's done in multiple instructions, and
 * unlike MIPS with ``la'', SPARC does not seem to have a
 * synthetic instruction which does this for us :-(
 */
static void
load_sym_addr(struct reg *r, struct vreg *vr, const char *name0) {
	char	*name;
	char	buf[128];

	if (vr == NULL) {
		name = (char *)name0;
	} else if (vr->from_const) {
		if (vr->from_const->type == TOK_STRING_LITERAL) {
			struct ty_string	*ts = vr->from_const->data;
		
			sprintf(buf, "_Str%ld", ts->count);
		} else if (IS_FLOATING(vr->from_const->type)) {
			struct ty_float	*tf = vr->from_const->data;

			sprintf(buf, "_Float%lu", tf->count);
		} else {
			unimpl();
		}
		name = buf;
	} else if (vr->var_backed && vr->parent == NULL) {
		name = vr->var_backed->dtype->name;
	} else {
		name = NULL;
		debug_print_vreg_backing(vr, NULL);
		unimpl();
	}

	if (backend->abi == ABI_SPARC64) {
		load_64bit_sym(r, name);
	} else {
		x_fprintf(out, "\tsethi %%hi(%s), %%%s\n",
			name, r->name);
		x_fprintf(out, "\tor %%%s, %%lo(%s), %%%s\n",
			r->name, name, r->name);	
	}	
}

static void
load_32bit_imm(struct reg *dest, unsigned int *value) {
	x_fprintf(out, "\tsethi %%hi(%u), %%%s\n",
		*value, dest->name);
	x_fprintf(out, "\tor %%%s, %%lo(%u), %%%s\n",
		dest->name, *value, dest->name);
}

/*
 * Perform ``add src, off, dest'', where off may be above 4096
 */
static void
do_add(const char *src, unsigned long off, const char *dest) {
	if (off >= 4096) {
		unsigned int	ui = off;
#if 0
		assert(strcmp(tmpgpr->name, dest) != 0);
#endif
		assert(strcmp(tmpgpr->name, src) != 0);
		load_32bit_imm(tmpgpr, &ui);
		x_fprintf(out, "\tadd %%%s, ", src);
		x_fprintf(out, "%%%s", tmpgpr->name);
	} else {
		x_fprintf(out, "\tadd %%%s, ", src);
		x_fprintf(out, "%lu", off);
	}
	x_fprintf(out, ", %%%s\n", dest);
}

/*
 * Perform ``sub src, off, dest'', where off may be above 4096
 */
static void
do_sub(const char *src, unsigned long off, const char *dest) {
	if (off >= 4096) {
		unsigned int	ui = off;
		assert(strcmp(tmpgpr->name, src) != 0);
		load_32bit_imm(tmpgpr, &ui);
		x_fprintf(out, "\tsub %%%s, ", src);
		x_fprintf(out, "%%%s", tmpgpr->name);
	} else {
		x_fprintf(out, "\tsub %%%s, ", src);
		x_fprintf(out, "%lu", off);
	}
	x_fprintf(out, ", %%%s\n", dest);
}


static void
emit_setreg(struct reg *dest, int *value);

static void 
load_immediate(struct reg *r, struct vreg *vr) {
	struct token	*t = vr->from_const;

	/* XXX cross-compilation :-( :-( :-( */
	if (t->type == TOK_STRING_LITERAL || IS_FLOATING(t->type)) {
		load_sym_addr(r, vr, NULL);
	} else if (IS_INT(t->type)) {
		emit_setreg(r, t->data);
#if ALLOW_CHAR_SHORT_CONSTANTS
	} else if (IS_CHAR(t->type) || IS_SHORT(t->type)) {
		int	val;

		/* XXXXXXXXXXXXXXXX again, cross .... */
		if (IS_CHAR(t->type)) {
			val = *(signed char *)t->data;
		} else {
			val = *(signed short *)t->data;
		}
		emit_setreg(r, &val);
#endif
	} else if (IS_LONG(t->type) || IS_LLONG(t->type)) {
		if (backend->abi == ABI_SPARC64
			|| IS_LLONG(t->type)) {
			char	buf[128];
			sprintf(buf, "%llu",
				*(unsigned long long *)t->data);
			load_64bit_sym(r, buf);
		} else {
			emit_setreg(r, t->data);
		}
	} else {
		unimpl();
	}
}

static void
emit_addrof(struct reg *dest, struct vreg *src, struct vreg *stop);
static int
do_stack(FILE *out, struct reg *r, struct stack_block *sb, unsigned long off);


static int	dont_kludge_ldouble = 0;


static void
emit_load(struct reg *r, struct vreg *vr) {
	char		*p = NULL;
	struct vreg	*vr2 = NULL;
	int		is_static = 0;
	int		is_bigstackoff = 0;
	static int	was_int_ldouble;


	/*
	 * If we need to do displacement addressing with a static
	 * variable, its address must be moved to a GPR first...
	 */
	if (vr->parent) {
		vr2 = get_parent_struct(vr);
		if (vr2->var_backed
			&& vr2->var_backed->stack_addr == NULL) {
			emit_addrof(tmpgpr, vr  /*2*/, /*NULL*/ vr2);
			is_static = 1;
			goto getisn;
		}
#if 0
	} else if (vr->var_backed
		&& vr->var_backed->stack_addr == NULL
		&& vr->var_backed->dtype->storage != TOK_KEY_REGISTER) {
		x_fprintf(out, "\tla $%s, %s\n",
			tmpgpr->name, vr->var_backed->dtype->name);
#endif
	}

	if (r->type == REG_FPR) {
		/* No conversion between int/fp is handle here! */
		if (vr->size == 4) {
			p = "ld";
		} else if (vr->size == 8) {
			p = "ldd";
		} else if (vr->size == 16) {
			/* long double */
			p = "ldq";
		} else {
			/* ?!?!? */
			unimpl();
		}
		if (vr->from_const) {
			load_immediate(tmpgpr, vr);
			x_fprintf(out, "\t%s [%%%s], %%%s\n",
				p, tmpgpr->name, r->name);
			return;
		}
	} else {
		if (vr->from_const) {
			load_immediate(r, vr);
			if (IS_FLOATING(vr->from_const->type)) {
				/*
				 * 12/23/07: This was missing... Destination
				 * register = GPR and source constant = fp
				 * happens when passing doubles to variadic
				 * functions!
				 * XXX maybe this belongs into icode_make_fcall
				 */
				x_fprintf(out, "\tldx [%%%s], %%%s\n",
					tmpgpr->name, r->name);
			}
			return;
		} else {
getisn:
			if (vr->type == NULL) {
				/* Anonymous saved register */
				switch (vr->size) {
				case 4: p = "lduw"; break;
				case 8: p = "ldx"; break;
				default:
					printf("%d\n", (int)vr->size), abort();
				}
			} else if (vr->type->tlist != NULL) {	
				if (vr->type->tlist->type == TN_FUNCTION) {
					if (vr->stack_addr != NULL) {
						/*
						 * 12/22/07: Check stack_addr too for e.g. anonymified
						 * functions (operands of conditional operator)
						 */
						p = "ldx";
					} else {
						load_sym_addr(r, vr, NULL);
						return;
					}
				} else if (vr->type->tlist->type == TN_ARRAY_OF
					|| vr->type->tlist->type == TN_VARARRAY_OF) {
					/*
					 * Must be automatic array, or
					 * one that comes from a pointer/
					 * parent struct
					 */
					struct vreg *parent = NULL;

					if (vr->parent != NULL) {
						parent = get_parent_struct
							(vr->parent);
					}

					emit_addrof(r, vr, parent);
					return;
				} else {
					if (backend->abi == ABI_SPARC64) {
						p = "ldx";
					} else {
						p = "lduw";
					}
				}
			} else {
				if (IS_CHAR(vr->type->code)) {
					if (vr->type->sign == TOK_KEY_UNSIGNED) {
						p = "ldub";
					} else {
						p = "ldsb";
					}
				} else if (vr->type->code == TY_SCHAR) {
					p = "ldub";
				} else if (vr->type->code == TY_SHORT) {
					p = "ldsh";
				} else if (vr->type->code == TY_USHORT) {
					p = "lduh";
				} else if (vr->type->code == TY_INT
					|| vr->type->code == TY_ENUM) {
					p = "ldsw";
				} else if (vr->type->code == TY_UINT) {
					p = "lduw";
				} else if (vr->type->code == TY_LONG) {
					if (backend->abi == ABI_SPARC64) {
						p = "ldx";
					} else {
						p = "ldsw";
					}
				} else if (vr->type->code == TY_ULONG) {
					if (backend->abi == ABI_SPARC64) {
						p = "ldx";
					} else {
						p = "lduw";
					}
				} else if (vr->type->code == TY_LLONG) {
					if (backend->abi == ABI_SPARC64) {
						p = "ldx";
					} else {
						p = "ldd";
						unimpl();
					}	
				} else if (vr->type->code == TY_ULLONG) {
					if (backend->abi == ABI_SPARC64) {
						p = "ldx";
					} else {
						p = "ldd";
						unimpl();
					}	
				} else if (vr->type->code == TY_FLOAT) {
					p = "lduw";
				} else if (vr->type->code == TY_DOUBLE) {
					p = "ldx";
				} else if (vr->type->code == TY_LDOUBLE) {
					unimpl();
				} else {
					unimpl();
				}
			}	
		}	
	}


	if (!is_static
		&& (vr->stack_addr || vr->var_backed) /* XXX */) {

		struct stack_block	*stack_addr = NULL;
		unsigned long		parent_offset = 0;
		int			is_anon = 0;

		if (vr->stack_addr != NULL) {
			stack_addr = vr->stack_addr;
			is_anon = 1;
		} else if (vr->var_backed->stack_addr != NULL) {
			stack_addr = vr->var_backed->stack_addr;
		} else {
			if (vr2 != NULL) {
				if (vr2->var_backed) {
					if (vr2->var_backed->stack_addr
						== NULL) {
						is_static = 1;
					} else {
						stack_addr = vr2->
							var_backed->stack_addr;
					}
				}
				parent_offset = calc_offsets(vr);
			} else {
				is_static = 1;
			}
		}
		if (is_static) {
			/*
		 	 * We first have to get the symbol address,
			 * then load thru it
			 */
			load_sym_addr(r, NULL, vr2? vr2->type->name: vr->type->name);
		} else if (stack_addr != NULL) {
			/*
			 * Stack address - if the offset is above 4095,
			 * we have to construct the stack address in a
			 * register and load indirectly
			 */
			int	changed_offset = 0;

			if (is_anon && was_int_ldouble) {
				stack_addr->offset -= 8;
				changed_offset = 1;
			}
			if (do_stack(NULL, NULL, stack_addr, parent_offset) == -1) { 
				is_bigstackoff = 1;
				do_stack(out, tmpgpr2, stack_addr, parent_offset);
			}
			if (is_anon
				&& stack_addr->nbytes == 16
				&& r->type != REG_FPR) {
				/*
				 * XXX this is a particularly nasty kludge
				 * to load a long double as two longs (the
				 * frontend currently has no pleasant way
				 * to patch stack offsets.) If an anonymous
				 * stack item has size 16 but is loaded
				 * into a GPR, it must be a long double.
				 */
				if (!was_int_ldouble
					&& !dont_kludge_ldouble) {
					was_int_ldouble = 8;
				} else {
					was_int_ldouble = 0;
				}
			}
			if (changed_offset) {
				/*
				 * 05/21/08: This (compensating for the
				 * offset kludge above) was missing, so stack
				 * offsets were getting trashed!
				 */
				stack_addr->offset += 8;
			}
		}
	}

	x_fprintf(out, "\t%s ", p);

	if (is_static) {
		if (vr2 != NULL) {
			x_fprintf(out, "[%%%s]", tmpgpr->name    /* , calc_offsets(vr) */);
		} else {
			x_fprintf(out, "[%%%s]", r->name);
		}
	} else if (is_bigstackoff) {
		x_fprintf(out, "[%%%s]", tmpgpr2->name);
	} else {
		print_mem_operand(vr, NULL, vr2);
	}
	x_fprintf(out, ", %%%s     ! was load for %s!",
			 r->name, vr->type && vr->type->name? vr->type->name: "?");
	x_fputc('\n', out);
}


static void
emit_load_addrlabel(struct reg *r, struct icode_instr *ii) {
	char	buf[128];
	sprintf(buf, ".%s", (char *)ii->dat);
	load_sym_addr(r, NULL, buf);
}

static void
emit_comp_goto(struct reg *r) {
	x_fprintf(out, "\tjmp %%%s\n", r->name);
}


/*
 * Takes vreg source arg - not preg - so that it can be either a preg
 * or immediate (where that makes sense!)
 */
static void
emit_store(struct vreg *dest, struct vreg *src) {
	char		*p = NULL;
	int		is_static = 0;
	int		is_bigstackoff = 0;
	struct vreg	*vr2 = NULL;

	if (dest->parent) {
		vr2 = get_parent_struct(dest);
		if (vr2->stack_addr == NULL
			&& vr2->var_backed
			&& vr2->var_backed->stack_addr == NULL) {
#if 0
puts("XXXXXXX WARNING: UNNECESSARY ADDROF?!?!!");
#endif
			emit_addrof(tmpgpr, dest /*2*/, /*NULL*/ vr2);
			is_static = 1;
		}
#if 0
	} else if (dest->var_backed
		&& dest->var_backed->stack_addr == NULL
		&& dest->var_backed->dtype->storage != TOK_KEY_REGISTER) {
		x_fprintf(out, "\tla $%s, %s\n",
			tmpgpr->name, dest->var_backed->dtype->name);
#endif
	}

	if (src->pregs[0] && src->pregs[0]->type == REG_FPR) {	
		/* Performs no fp-int conversion! */
		if (dest->type == NULL) {
			unimpl();
		} else if (dest->size == 4) {
			p = "st";
		} else if (dest->size == 8) {
			p = "std";
		} else {
			/* ?!?!? */
			p = "stq";
		}
	} else {
		if (dest->type == NULL) {
			/* Store anonymous register */
			if (backend->abi == ABI_SPARC64) {
				p = "stx";
			} else {
				p = "stw";
			}
		} else if (dest->type->tlist != NULL) {
			/* Must be pointer */
			if (backend->abi == ABI_SPARC64) {
				p = "stx";
			} else {
				p = "stw";
			}
		} else {
			if (IS_CHAR(dest->type->code)) {
				p = "stb";
			} else if (IS_SHORT(dest->type->code)) {
				p = "sth";
			} else if (IS_INT(dest->type->code)
				|| dest->type->code == TY_ENUM) {
				p = "stw";
			} else if (IS_LONG(dest->type->code)) {
				if (backend->abi == ABI_SPARC64) {
					p = "stx";
				} else {
					p = "stw";
				}
			} else if (IS_LLONG(dest->type->code)) {
				if (backend->abi == ABI_SPARC64) {
					p = "stx";
				} else {
					p = "std";
					unimpl();
				}
			} else {
			printf("well ... %d(%p)\n",
				dest->type->code, dest);
			printf("%s\n", src->pregs[0]->name); 
			printf("%d\n", src->pregs[0]->used);
			*(char *)0 = 0;
				unimpl();
			}	
		}	
	}

	/*
	 * If we're loading something static, we first need to get its
	 * symbol address. If this is a static struct, the base struct
	 * address has already been loaded to tmpgpr!
	 */
	if (vr2 == NULL
		&& dest->var_backed
		&& dest->stack_addr == NULL) /* XXX */ {
		emit_addrof(tmpgpr, dest, NULL);
		is_static = 1;
	} else if (dest->from_ptr != NULL) {
		/*
		 * 12/29/07: Added
		 */ 
		;
	} else {
		/* May be stack address */
		struct stack_block	*sb = NULL;
		unsigned long		parent_offset = 0;


		if (dest->stack_addr != NULL) {
			sb = dest->stack_addr;
		} else if (vr2
			&& vr2->var_backed
			&& vr2->var_backed->stack_addr) {
			sb = vr2->var_backed->stack_addr;
			parent_offset = calc_offsets(dest);
		} else if (dest->var_backed && dest->var_backed->stack_addr) {
			sb = dest->var_backed->stack_addr;
		}
		if (sb != NULL) {
			/*
			 * Check if the offset is small enough
			 * 12/29/07: This stuff should be redundant now since
			 * these offsets checks are done on a higher level
			 *
			 */
			if (do_stack(NULL, NULL, sb, parent_offset) == -1) {
				/* No, we have to load indirectly */
				do_stack(out, tmpgpr2, sb, /*0*/ parent_offset);
				is_bigstackoff = 1;
			}
		}
	}

	x_fprintf(out, "\t%s ", p);
	if (src->from_const) {
		print_mem_operand(src, NULL, NULL);
		x_fprintf(out, ", ");
	} else {
		/* Must be register */
		x_fprintf(out, "%%%s, ", src->pregs[0]->name);
	}
	if (is_static) {
		if (vr2 != NULL) {
			x_fprintf(out, "[%%%s]", tmpgpr->name
				/*calc_offsets(dest)*/  );
		} else {
			x_fprintf(out, "[%%%s]", tmpgpr->name);
		}
	} else {
		if (!is_bigstackoff) {
			print_mem_operand(dest, NULL, vr2);
		} else {
			x_fprintf(out, "[%%%s]", tmpgpr2->name);
		}
	}
	x_fputc('\n', out);
}


static void
emit_neg(struct reg **dest, struct icode_instr *src) {
	(void) src;
	if (dest[0]->type == REG_FPR) {
		char	*isn;

		if (src->src_vreg->type->code == TY_FLOAT) {
			isn = "fnegs";
		} else if (src->src_vreg->type->code == TY_DOUBLE) {
			isn = "fnegd";
		} else { /* long double */
			isn = "fnegq";
		}
		x_fprintf(out, "\t%s %%%s, %%%s\n",
			isn, dest[0]->name, dest[0]->name);	
	} else {
		x_fprintf(out, "\tneg %%%s\n", dest[0]->name);
	}	
}	


static void
emit_sub(struct reg **dest, struct icode_instr *src) {
	char	*p = NULL;

	if (dest[0]->type == REG_FPR) {
		if (src->src_vreg->type->code == TY_FLOAT) {
			p = "fsubs";
		} else if (src->src_vreg->type->code == TY_DOUBLE) {
			p = "fsubd";
		} else {
			p = "fsubq";
		}
		x_fprintf(out, "\t%s %%%s, ",
			p, dest[0]->name); 
	} else {
		/* XXX take immediate into account */
		if ((src->dest_vreg->type->tlist || src->src_vreg->type->tlist)
			&& src->dest_vreg->size != src->src_vreg->size
			&& backend->abi == ABI_SPARC64) {
			/*
			 * XXX this belongs into the frontend, but I don't know
			 * how to do it right yet
			 */
			struct reg	*notptr = src->src_vreg->size == 8?
				src->dest_vreg->pregs[0]: src->src_vreg->pregs[0];
			unsigned	ui = 0xffffffff;

			load_32bit_imm(tmpgpr, &ui);
			x_fprintf(out, "\tand %%%s, %%%s, %%%s\n",
				notptr->name, tmpgpr->name, notptr->name);
			p = "sub";   
		} else if (src->src_vreg->size == 8) {
			if (src->src_vreg->type->sign == TOK_KEY_UNSIGNED) {
				p = "sub";
			} else {
				p = "sub";
			}	
		} else {
			if (src->src_vreg->type->sign == TOK_KEY_UNSIGNED) {
				p = "sub";
			} else {
				p = "sub";
			}
		}
		
		x_fprintf(out, "\t%s %%%s, ",
			p, dest[0]->name);
	}
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fprintf(out, ", %%%s\n", dest[0]->name);
}

static void
emit_add(struct reg **dest, struct icode_instr *src) {
	char	*p = NULL;

	if (dest[0]->type == REG_FPR) {
		if (src->src_vreg->type->code == TY_FLOAT) {
			p = "fadds";
		} else if (src->src_vreg->type->code == TY_DOUBLE) {
			p = "faddd";
		} else {
			p = "faddq";
		}
		x_fprintf(out, "\t%s %%%s, ",
			p, dest[0]->name);
	} else {
		/* XXX take immediate into account ... addi, etc */
		if ((src->dest_vreg->type->tlist || src->src_vreg->type->tlist)
			&& src->dest_vreg->size != src->src_vreg->size
			&& backend->abi == ABI_SPARC64) {
			/*
	 		 * XXX this belongs into the frontend, but I don't know how to do it
			 * right yet
	 		 */
#if 0
			x_fprintf(out, "\tdli %%%s, 0xffffffff\n", tmpgpr->name);
			x_fprintf(out, "\tand %%%s, %%%s, %%%s\n",
				notptr->name, notptr->name, tmpgpr->name);
#endif
			p = "add";
		} else if (src->src_vreg->size == 8) {
			if (src->src_vreg->type->sign == TOK_KEY_UNSIGNED) {
				p = "add";
			} else {
				p = "add";
			}	
		} else {
			if (src->src_vreg->type->sign == TOK_KEY_UNSIGNED) {
				p = "add";
			} else {
				p = "add";
			}
		}
		x_fprintf(out, "\t%s %%%s, ",
			p, dest[0]->name);
	}	
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fprintf(out, ", %%%s\n", dest[0]->name);
}

static void
emit_div(struct reg **dest, struct icode_instr *src, int formod) {
	struct type	*ty = src->src_vreg->type;
	char		*p = NULL;

	(void) dest; (void) formod;

	if (IS_FLOATING(ty->code)) {
		if (ty->code == TY_FLOAT) {
			p = "fdivs";
		} else if (ty->code == TY_DOUBLE) {
			p = "fdivd";
		} else {
			p = "fdivq";
		}
		x_fprintf(out, "\t%s %%%s, %%%s, %%%s\n",
			p, dest[0]->name,
			src->src_pregs[0]->name,
			dest[0]->name);
		return;
	} else {
		if (ty->sign == TOK_KEY_UNSIGNED) {
			if (backend->abi == ABI_SPARC64) {
				p = "udivx";
			} else {
				unimpl();
			}
		} else {
			/* signed integer division */
			if (backend->abi == ABI_SPARC64) {
				p = "sdivx";
			} else {
				unimpl();
			}
		}
	}
	x_fprintf(out, "\t%s %%%s, ", p, dest[0]->name);
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fprintf(out, ", %%%s\n", dest[0]->name);
}

static void
emit_mul(struct reg **dest, struct icode_instr *src); 


static void
emit_mod(struct reg **dest, struct icode_instr *src) {
	/*
	 * There's no mod instruction, and it seems we can't get a
	 * remainder out of any div instruction either, so do
	 * res = dest - ((dest / src) * src
	 */
	if (backend->abi == ABI_SPARC64) {
		x_fprintf(out, "\tmov %%%s, %%%s\n",
			dest[0]->name, tmpgpr->name);
	} else {
		unimpl();
	}
	emit_div(dest, src, 1);    /* dest / src */
	emit_mul(dest, src);       /* (dest / src) * src */
	x_fprintf(out, "\tsub %%%s, %%%s, %%%s\n",
		tmpgpr->name, dest[0]->name, dest[0]->name);
}


static void
emit_mul(struct reg **dest, struct icode_instr *src) {
	struct type	*ty = src->src_vreg->type;
	char		*p = NULL;

	(void) dest;

	if (IS_FLOATING(ty->code)) {
		if (ty->code == TY_FLOAT) {
			p = "fmuls";
		} else if (ty->code == TY_DOUBLE) {
			p = "fmuld";
		} else {
			p = "fmulq";
		}
		x_fprintf(out, "\t%s %%%s, %%%s, %%%s\n",
			p, dest[0]->name,
			src->src_pregs[0]->name,
			dest[0]->name);
		return;
	} else if (ty->sign == TOK_KEY_UNSIGNED) {
		if (backend->abi == ABI_SPARC64) {
			p = "mulx";
		} else {
			unimpl();
		}	
	} else {
		/* signed integer multiplication */
		if (backend->abi == ABI_SPARC64) {
			p = "mulx";
		} else {
			unimpl();
		}
	}	
	x_fprintf(out, "\t%s %%%s, ", p, dest[0]->name);
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fprintf(out, ", %%%s\n", dest[0]->name);
}

static void
emit_shl(struct reg **dest, struct icode_instr *src) {
	char	*p;
	(void) dest; (void) src;
	
	if (src->dest_vreg->size == 8) {
		if (backend->abi == ABI_SPARC64) {
			p = "sllx";
		} else {
			p = NULL;
			unimpl();
		}
	} else {
		p = "sll";
	}
	if (src->src_vreg->from_const) {
		emit_load(tmpgpr, src->src_vreg);
	}
	x_fprintf(out, "\t%s %%%s, ", p, dest[0]->name);
	if (src->src_vreg->from_const) {
		x_fprintf(out, "%%%s", tmpgpr->name);
	} else {
		x_fprintf(out, "%%%s", src->src_pregs[0]->name);
	}
	x_fprintf(out, ", %%%s\n", dest[0]->name);
}

static void
emit_shr(struct reg **dest, struct icode_instr *src) {
	char	*p;

	(void) dest; (void) src;
	if (src->dest_vreg->size == 8) {
		if (backend->abi == ABI_SPARC64) {
			if (src->dest_vreg->type->sign == TOK_KEY_UNSIGNED) {
				p = "srlx";
			} else {
				p = "srax";
			}	
		} else {
			p = NULL;
			unimpl();
		}
	} else {
		if (src->dest_vreg->type->sign == TOK_KEY_UNSIGNED) {
			p = "srl";
		} else {
			p = "sra";
		}	
	}
	if (src->src_vreg->from_const) {
		emit_load(tmpgpr, src->src_vreg);
	}
	x_fprintf(out, "\t%s %%%s, ", 
		p, dest[0]->name);
	if (src->src_vreg->from_const) {
		x_fprintf(out, "%%%s", tmpgpr->name);
	} else {	
		x_fprintf(out, "%%%s", src->src_pregs[0]->name);
	}
	x_fprintf(out, ", %%%s\n", dest[0]->name);
}

static void
emit_or(struct reg **dest, struct icode_instr *src) {
	x_fprintf(out, "\tor %%%s, %%%s, %%%s\n",
		dest[0]->name, src->src_pregs[0]->name, dest[0]->name);
}

static void
emit_and(struct reg **dest, struct icode_instr *src) {
	x_fprintf(out, "\tand %%%s, %%%s, %%%s\n",
		dest[0]->name,
		src->src_vreg? src->src_pregs[0]->name: tmpgpr->name,
		dest[0]->name);
}

static void
emit_xor(struct reg **dest, struct icode_instr *src) {
	x_fprintf(out, "\txor %%%s, ", 
		dest[0]->name);
	if (src->src_vreg == NULL) {
		x_fprintf(out, "%%%s", dest[0]->name);
	} else {	
		print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	}
	x_fprintf(out, ", %%%s\n", dest[0]->name);
}

static void
emit_not(struct reg **dest, struct icode_instr *src) {
	(void) src;
	x_fprintf(out, "\tnot %%%s\n", dest[0]->name);
}	

static void
emit_ret(struct icode_instr *ii) {
	(void) ii;

	x_fprintf(out, "\tjmp %%i7+8\n");
	x_fprintf(out, "\trestore\n"); /* delay slot */
}

static struct icode_instr	*last_cmp_instr;


static void
emit_cmp(struct reg **dest, struct icode_instr *src) {
	last_cmp_instr = src;

	(void) src; (void) dest;
	if (src->dest_pregs[0]->type == REG_FPR) {
		x_fprintf(out, "\tfcmp%c %%%s, %%%s\n",
			src->dest_vreg->type->code == TY_FLOAT? 's': 'd',
			src->dest_pregs[0]->name, src->src_pregs[0]->name);
	} else {
		x_fprintf(out, "\tcmp %%%s, %%%s\n",
			src->dest_pregs[0]->name,
			src->src_vreg?
				src->src_pregs[0]->name: "g0");
	}
}

static void
emit_conv_fp(struct icode_instr *ii) {
	struct extendsign	*ext = ii->dat;
	struct reg		*r = ext->dest_preg;
	struct reg		*src_r = ext->src_preg;
	struct type		*to = ext->dest_type;
	struct type		*from = ext->src_type;
	char			*instr = NULL;
	unsigned		needand = 0;


#if 0
	/* XXXX WRONG - must be done in frontend!!!! */
	if (IS_CHAR(to->code)) {
#endif
	if (IS_CHAR(to->code) || IS_SHORT(to->code))
		to = make_basic_type(TY_INT);
#if 0
		needand = 0xff;
	} else if (IS_SHORT(to->code)) {
		to = make_basic_type(TY_INT);
		needand = 0xffff;
	}
#endif

	if (to->code == TY_FLOAT) {
		if (from->code == TY_DOUBLE) {
			instr = "fdtos";
		} else if (IS_INT(from->code)) {
			instr = "fitos";
		} else if (IS_LONG(from->code) || IS_LLONG(from->code)) {
			instr = "fxtos";
		} else if (from->code == TY_LDOUBLE) {
			instr = "fqtos";
		} else {
printf("%d\n", from->code);
			unimpl();
		}
	} else if (to->code == TY_DOUBLE) {
		if (from->code == TY_FLOAT) {
			instr = "fstod";
		} else if (IS_INT(from->code)) {
			instr = "fitod";
		} else if (IS_LONG(from->code) || IS_LLONG(from->code)) {
			instr = "fxtod";
		} else if (from->code == TY_LDOUBLE) {
			instr = "fqtod";
		} else {
			unimpl();
		}
	} else if (IS_INT(to->code)) {
		if (from->code == TY_FLOAT) {
			instr = "fstoi";
		} else if (from->code == TY_DOUBLE) {
			instr = "fdtoi";
		} else {
			/* long double */
			instr = "fqtoi";
		}
	} else if (IS_LONG(to->code) || IS_LLONG(to->code)) {
		if (from->code == TY_FLOAT) {
			instr = "fstox";
		} else if (from->code == TY_DOUBLE) {
			instr = "fdtox";
		} else {
			/* long double */
			instr = "fqtox";
		}
	} else if (to->code == TY_LDOUBLE) {
		if (from->code == TY_FLOAT) {
			instr = "fstoq";
		} else if (from->code == TY_DOUBLE) {
			instr = "fdtoq";
		} else if (IS_INT(from->code)) {
			instr = "fitoq";
		} else if (IS_LONG(from->code) || IS_LLONG(from->code)) {
			instr = "fxtoq";
		} else {
			printf("%d\n", from->code);
			unimpl();
		}
	} else {
		printf("%d\n", to->code);
		unimpl();
	}
	x_fprintf(out, "\t%s %%%s, %%%s\n",
		instr, src_r->name, r->name);		
	if (needand) {
		load_32bit_imm(tmpgpr, &needand);
		x_fprintf(out, "\tand %%%s, %%%s, %%%s\n",
			r->name, tmpgpr->name, r->name);
	}
}

static void
emit_extend_sign(struct icode_instr *ii) {
	struct extendsign	*ext = ii->dat;
	struct reg		*r = ext->dest_preg;
	struct type		*to = ext->dest_type;
	struct type		*from = ext->src_type;
	int			to_size = backend->get_sizeof_type(to, NULL);
	int			shift_bits = 0;

	if (IS_SHORT(to->code)) {
		/* Source must be char */
		shift_bits = 8;
	} else if (IS_INT(to->code)) {
		if (IS_CHAR(from->code)) {
			shift_bits = 24;
		} else {
			/* Must be short */ 
			shift_bits =16; 
		}
	} else if (IS_LONG(to->code)) {
		int	dest_size;

		if (backend->abi == ABI_SPARC64) {
			dest_size = 64;
		} else {
			dest_size = 32;
		}
		if (IS_CHAR(from->code)) {
			shift_bits = dest_size - 8;
		} else if (IS_SHORT(from->code)) {
			shift_bits = dest_size - 16;
		} else {
			/* Must be int */
			shift_bits = dest_size - 32;
		}
	} else if (IS_LLONG(to->code)) {
		if (backend->abi == ABI_SPARC32
			|| backend->abi == ABI_SPARC64) {
			if (IS_CHAR(from->code)) {
				shift_bits = 56;
			} else if (IS_SHORT(from->code)) {
				shift_bits = 48;
			} else if (IS_INT(from->code)) {
				shift_bits = 32;
			} else {
				/* Must be long */
				if (backend->abi == ABI_SPARC32) {
					shift_bits = 32;
				} else {
					;
				}
			}
		} else {
			unimpl();
		}
	} else {
		unimpl();
	}

	if (backend->abi != ABI_SPARC64) {
		/* sllx/srax below cannot be used */
		unimpl();
	}

	x_fprintf(out, "\tmov %d, %%%s\n", shift_bits, tmpgpr->name); 
	x_fprintf(out, "\tsll%s %%%s, %%%s, %%%s\n",
		to_size == 8? "x": "",
		r->name,
		tmpgpr->name, r->name);
	x_fprintf(out, "\tsra%s %%%s, %%%s, %%%s\n",
		to_size == 8? "x": "",
		r->name, tmpgpr->name, r->name); 
}

static void
emit_branch(struct icode_instr *ii) {
	char		*lname;
	struct vreg	*src_vreg = NULL;
	struct reg	**src_pregs = NULL;
	struct reg	*src_preg;
	char		*is_64bit;
	int		is_signed = 0;

	if (last_cmp_instr) {
		/* Not unconditional jump - get comparison */
		src_vreg = last_cmp_instr->src_vreg;
		src_pregs = last_cmp_instr->src_pregs;
		/*
		 * 06/29/08: This was missing the check whether we are comparing
		 * pointers, and thus need unsigned comparison
		 */
		is_signed =
			last_cmp_instr->dest_vreg->type->sign != TOK_KEY_UNSIGNED
		&& last_cmp_instr->dest_vreg->type->tlist == NULL;	
	}

	if (src_pregs == NULL || src_vreg == NULL) {
		/* Compare with zero. XXX not for fp!!! */
		src_preg = &sparc_gprs[0];
	} else {
		src_preg = src_pregs[0];
	}
	if (last_cmp_instr && last_cmp_instr->dest_vreg->size == 8) {
		if (backend->abi == ABI_SPARC64) {
			is_64bit = "%xcc, ";
		} else {
			is_64bit = "";
			unimpl();
		}
	} else {
		is_64bit = "";
	}


#define SLT_INSTR(is_signed) \
	(is_signed? "slt": "sltu")

	lname = ((struct icode_instr *)ii->dat)->dat;
	if (src_preg && src_preg->type == REG_FPR) {
		switch (ii->type) {
		case INSTR_BR_EQUAL:
			x_fprintf(out, "\tfbe .%s\n", lname);
			break;
		case INSTR_BR_NEQUAL:
			x_fprintf(out, "\tfbne .%s\n", lname);
			break;
		case INSTR_BR_SMALLER:
			x_fprintf(out, "\tfbl .%s\n", lname);
			break;
		case INSTR_BR_SMALLEREQ:
			x_fprintf(out, "\tfble .%s\n", lname);
			break;
		case INSTR_BR_GREATER:
			x_fprintf(out, "\tfbg .%s\n", lname);
			break;
		case INSTR_BR_GREATEREQ:
			x_fprintf(out, "\tfbge .%s\n", lname);
			break;
		case INSTR_JUMP:
			/* XXX huh - old bug !?? */
			x_fprintf(out, "\tj .%s\n", lname);
		}
	} else {
		switch (ii->type) {
		case INSTR_JUMP:
			x_fprintf(out, "\tba,pt %%xcc, .%s\n", lname);
			break;
		case INSTR_BR_EQUAL:
			x_fprintf(out, "\tbeq %s.%s\n", is_64bit, lname);
			break;
		case INSTR_BR_SMALLER:
			x_fprintf(out, "\tbl%s %s.%s\n",
				is_signed? "": "u", is_64bit, lname);
			break;
		case INSTR_BR_SMALLEREQ:
			x_fprintf(out, "\tble%s %s.%s\n",
				is_signed? "": "u", is_64bit, lname);
			break;
		case INSTR_BR_GREATER:
			x_fprintf(out, "\tbg%s %s.%s\n",
				is_signed? "": "u", is_64bit, lname);
			break;
		case INSTR_BR_GREATEREQ:
			x_fprintf(out, "\tbge%s %s.%s\n",
				is_signed? "": "u", is_64bit, lname);
			break;
		case INSTR_BR_NEQUAL:
			x_fprintf(out, "\tbne %s.%s\n", is_64bit, lname);
			break;
		}
	}
	last_cmp_instr = NULL; /* XXX hmm ok? */
	x_fprintf(out, "\tnop\n");
}


static void
emit_mov(struct copyreg *cr) {
	struct reg	*dest = cr->dest_preg;
	struct reg	*src = cr->src_preg;

	if (src == NULL) {
		/* Move null to register */
		x_fprintf(out, "\tmov %%g0, %%%s\n",
			dest->name);
	} else {
		if (dest->type != src->type) {
			/* Must be fpr vs gpr */
			size_t	src_size = backend->
				get_sizeof_type(cr->src_type, NULL);

unimpl();
			if (dest->type == REG_FPR) {
				x_fprintf(out, "\t%smtc1 $%s, $%s\n",
					src_size == 8? "d": "",
					src->name, dest->name);
			} else {
				x_fprintf(out, "\t%smfc1 $%s, $%s\n",
					src_size == 8? "d": "",
					dest->name, src->name);
			}
		} else if (dest->type == REG_FPR) {
			int	c;

			if (cr->src_type->code == TY_DOUBLE) {
				c = 'd';
			} else if (cr->src_type->code == TY_FLOAT) {
				c = 's';
			} else if (cr->src_type->code == TY_LDOUBLE) {
				c = 'q';
			} else if (cr->src_type->code < TY_INT	
				|| (IS_LONG(cr->src_type->code)
					&& backend->abi != ABI_SPARC64)) {
				unimpl();
				c = 'w';
			} else {	
				unimpl();
				c = 'l';
			}
			x_fprintf(out, "\tfmov%c %%%s, %%%s\n",
				c, src->name, dest->name);
		} else {
			x_fprintf(out, "\tmov %%%s, %%%s\n",
				src->name, dest->name);
		}
	}	
}


static void
emit_setreg(struct reg *dest, int *value) {
	/* XXX signed vs unsigned :/ */
	if (*value < 0 && *value > -4095) {
		x_fprintf(out, "\tmov %d, %%%s\n", *value, dest->name);
	} else if (*value >= 0 && *value < 4096) {
		x_fprintf(out, "\tmov %u, %%%s\n",
			*(unsigned *)value, dest->name);
	} else {
		load_32bit_imm(dest, (unsigned *)value);
	}
}

static void
emit_xchg(struct reg *r1, struct reg *r2) {
	(void) r1; (void) r2;
	unimpl();
}

static void
emit_initialize_pic(struct function *f) {
	(void) f;
	x_fprintf(out, "\tsethi %%hi(_GLOBAL_OFFSET_TABLE_-4), %%%s\n",
		pic_reg->name);
	x_fprintf(out, "\tcall _GetPC\n");
	x_fprintf(out, "\tadd %%%s, %%lo(_GLOBAL_OFFSET_TABLE_+4), %%%s\n",
		pic_reg->name, pic_reg->name);
}

static int 
do_stack(FILE *out, struct reg *r, struct stack_block *sb, unsigned long off);

static void
emit_addrof(struct reg *dest, struct vreg *src, struct vreg *structtop) {
	struct decl	*d;

	if (src == NULL) {
		d = curfunc->proto->dtype->tlist->tfunc->lastarg;
	} else {
		d = src->var_backed;
	}	

	if (structtop != NULL) {
		d = structtop->var_backed;
	}	

	if (d != NULL) {
		if (d->dtype && IS_THREAD(d->dtype->flags)) {
			/*
			 * 02/02/08: Thread-local storage
			 */
			x_fprintf(out, "\tsethi %%tle_hix22(%s), %%%s\n",
				d->dtype->name, dest->name);	
			x_fprintf(out, "\txor %%%s, %%tle_lox10(%s), %%%s\n",
				dest->name, d->dtype->name, dest->name);
			x_fprintf(out, "\tadd %%%s, %%g7, %%%s\n",
				dest->name, dest->name); 
			if (src->parent != NULL) {
				do_add(dest->name, calc_offsets(src), dest->name);
			}
			return;
		} else if (picflag
			&& d->dtype
			&& (d->dtype->storage == TOK_KEY_STATIC
			|| d->dtype->storage == TOK_KEY_EXTERN)) {
			; /* complete this.............. */
			x_fprintf(out, "\tsethi %%hi(%s), %%%s\n",
				d->dtype->name, tmpgpr->name);
			x_fprintf(out, "\tor %%%s, %%lo(%s), %%%s\n",
				tmpgpr->name, d->dtype->name, tmpgpr->name);
#if 0
			x_fprintf(out, "\tadd %%%s, %%%s, %%%s\n",
				pic_reg->name, tmpgpr->name, dest->name);
#endif
			x_fprintf(out, "\tldx [%%%s + %%%s], %%%s\n",
				pic_reg->name, tmpgpr->name, dest->name);
			if (src->parent) {
				do_add(dest->name,
					calc_offsets(src),
					dest->name);
			}
			return;
		}
	} else if (picflag && src->from_const) {
		/*
		 * Not variable, but may be a constant that needs a PIC
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
			sprintf(buf, "_Float%lu", tf->count); /* XXX hmm need preceding . ? */
			name = buf;
		}
		if (name != NULL) {
			x_fprintf(out, "\tldx [%%%s + %s], %%%s\n",
				pic_reg->name, name, dest->name);
			return;
		}
	}


	if (src && src->from_ptr && src->parent) {
		do_add(src->from_ptr->pregs[0]->name,
			calc_offsets(src),
			dest->name);
	} else if (src && src->parent != NULL) {
		/* Structure or union type */
		if (d != NULL) {
			if (d->stack_addr != NULL) {
				do_stack(out, dest,
					d->stack_addr, calc_offsets(src));
			} else {
				/* Static */
				unsigned long off = calc_offsets(src);
				load_sym_addr(dest, NULL, d->dtype->name);
				if (off) {
					do_add(dest->name, off, dest->name);
				}		
			}	
		} else if (structtop && structtop->from_ptr) {
			do_add(structtop->from_ptr->pregs[0]->name,
				calc_offsets(src),
				dest->name);
		} else {
			printf("hm attempt to take address of %s\n",
				src->type->name);	
			unimpl();
		}	
	} else if (src && src->from_ptr) {	
		x_fprintf(out, "\tmov %%%s, %%%s\n",
			src->from_ptr->pregs[0]->name, dest->name);
	} else if (src && src->from_const && src->from_const->type == TOK_STRING_LITERAL) {
		/* 08/27/08: This was missing */
		emit_load(dest, src);
	} else {		
		if (d && d->stack_addr) {
			/* src = NULL means start of variadic */
			do_stack(out, dest, d->stack_addr, 0);
		} else if (d) {
			/*
			 * Must be static variable - symbol itself is
			 * address
			 */
			load_sym_addr(dest, structtop? structtop: src, NULL); 
		} else if (src && src->stack_addr) {
			/* Anonymous item */
			do_stack(out, dest, src->stack_addr, 0);
		} else {
			unimpl(); /* ??? */
		}
	}	
}


/*
 * Copy initializer to automatic variable of aggregate type
 */
static void
emit_copyinit(struct decl *d) {
	unsigned int		ui;

	do_stack(out, &o_regs[0], d->stack_addr, 0);
	load_sym_addr(&o_regs[1], NULL, d->init_name->name);
	/* XXX dli?? */
#if 0
	x_fprintf(out, "\tmov %lu, %%o2\n", (unsigned long)d->vreg->size);
#endif
	ui = backend->get_sizeof_type(d->dtype, NULL);  /*d->vreg->size;*/
	load_32bit_imm(&o_regs[2], &ui);
	x_fprintf(out, "\tcall memcpy, 0\n", tmpgpr->name);
	x_fprintf(out, "\tnop\n", tmpgpr->name);
}


/*
 * Assign one struct to another (may be any of automatic or static or
 * addressed thru pointer)
 */
static void
emit_copystruct(struct copystruct *cs) {
	struct vreg	*stop;
	unsigned	ui;

	/* XXX :-/ */
	ui = cs->src_vreg->size;
	load_32bit_imm(&o_regs[2], &ui);
	if (cs->src_from_ptr == NULL) {
		if (cs->src_vreg->parent) {
			stop = get_parent_struct(cs->src_vreg);
		} else {
			stop = NULL;
		}	
		emit_addrof(&o_regs[1], cs->src_vreg, stop); 
	} else {
		if (cs->src_vreg->parent) {
			x_fprintf(out, "\t%s %%%s, %lu, %%%s\n",
				backend->abi == ABI_SPARC64? "add": "add",
				cs->src_from_ptr->name,
				calc_offsets(cs->src_vreg),
				cs->src_from_ptr->name);
		}
		x_fprintf(out, "\tmov %%%s, %%%s\n",
			cs->src_from_ptr->name, o_regs[1].name);
	}

	if (cs->dest_vreg == NULL) {
		/* Copy to hidden return pointer */
		emit_load(&o_regs[0], curfunc->hidden_pointer);
	} else if (cs->dest_from_ptr == NULL) {
		if (cs->dest_vreg->parent) {
			stop = get_parent_struct(cs->dest_vreg);
		} else {
			stop = NULL;
		}
		emit_addrof(&o_regs[0], cs->dest_vreg, stop); 
	} else {	
		if (cs->dest_vreg->parent) {
			x_fprintf(out, "\t%s %%%s, %lu, %%%s\n",
				backend->abi == ABI_SPARC64? "add": "add",
				cs->dest_from_ptr->name,
				calc_offsets(cs->dest_vreg),
				cs->dest_from_ptr->name);
		}
		x_fprintf(out, "\tmov %%%s, %%%s\n",
			cs->dest_from_ptr->name, o_regs[0].name);
	}
	x_fprintf(out, "\tcall memcpy, 0\n");
	x_fprintf(out, "\tnop\n");
}

static void
emit_intrinsic_memcpy(struct int_memcpy_data *data) {
	struct reg      *dest = data->dest_addr;
	struct reg      *src = data->src_addr;
	struct reg      *nbytes = data->nbytes;
	struct reg      *temp = data->temp_reg;
	static int      labelcount;

	if (data->type == BUILTIN_MEMSET && src != temp) {
		x_fprintf(out, "\tmov %%%s, %%%s\n",
			src->name, temp->name);
	}

	x_fprintf(out, "\tcmp %%%s, %%%s\n",
		nbytes->name, sparc_gprs[0].name);
	x_fprintf(out, "\tbeq %%xcc, .Memcpy_done%d\n", labelcount);
	x_fprintf(out, "\t.Memcpy_start%d:\n", labelcount);
	if (data->type == BUILTIN_MEMCPY) {
		x_fprintf(out, "\tldub [%%%s], %%%s\n", src->name, temp->name);
	}
	x_fprintf(out, "\tstb %%%s, [%%%s]\n", temp->name, dest->name);
	if (data->type == BUILTIN_MEMCPY) {
		x_fprintf(out, "\tadd %%%s, 1, %%%s\n", src->name, src->name);
	}
	x_fprintf(out, "\tadd %%%s, 1, %%%s\n", dest->name, dest->name);
	x_fprintf(out, "\tsub %%%s, 1, %%%s\n", nbytes->name, nbytes->name);
	x_fprintf(out, "\tcmp %%%s, %%%s\n",
		nbytes->name, sparc_gprs[0].name);
	x_fprintf(out, "\tbne %%xcc, .Memcpy_start%d\n", labelcount);
	x_fprintf(out, "\tnop\n");
	x_fprintf(out, "\t.Memcpy_done%d:\n", labelcount);

	++labelcount;
}	

static void
emit_zerostack(struct stack_block *sb, size_t nbytes) {
	static struct vreg	vr;
	unsigned int	ui = nbytes;

	load_32bit_imm(&o_regs[2], &ui);
	x_fprintf(out, "\tmov 0, %%o1\n");
	vr.stack_addr = sb;
	emit_addrof(&o_regs[0], &vr, NULL);
	x_fprintf(out, "\tcall memset, 0\n");
	x_fprintf(out, "\tnop\n");
}

static void
emit_alloca(struct allocadata *ad) {
	if (ad->size_reg != &o_regs[0]) {
		x_fprintf(out, "\tmov %%%s, %%o0\n", ad->size_reg->name);
	}
	x_fprintf(out, "\tcall malloc, 0\n");
	x_fprintf(out, "\tnop\n");
#if 0
	store_reg_to_stack_block(&o_regs[0], ad->addr);
#endif
}

static void
emit_dealloca(struct stack_block *sb, struct reg *r) {
	char	*regname = r? r->name: "o0";

	do_stack(out, r? r: &o_regs[0], sb, 0);
	x_fprintf(out, "\t%s [%%%s], %%%s\n",
		sb->nbytes == 4? "lduw": "ldx",
		regname, regname);
	x_fprintf(out, "\tcall free, 0\n");
	x_fprintf(out, "\tnop\n");
}

static void
emit_alloc_vla(struct stack_block *sb) {
	
	do_stack(out, tmpgpr2, sb, 0);
	x_fprintf(out, "\t%s [%%%s+%lu], %%%s\n",
		backend->abi != ABI_SPARC64? "lduw": "ldx",
		tmpgpr2->name,
		(unsigned long)backend->get_ptr_size(),
		o_regs[0].name); 
	x_fprintf(out, "\tcall malloc, 0\n");
	x_fprintf(out, "\tnop\n");
	do_stack(out, tmpgpr2, sb, 0);
	x_fprintf(out, "\t%s %%%s, [%%%s]\n",
		backend->abi != ABI_SPARC64? "stw": "stx",
		o_regs[0].name, tmpgpr2->name);
}

static void
emit_dealloc_vla(struct stack_block *sb, struct reg *r) {
	char	*regname = r? r->name: "o0";

	do_stack(out, r? r: /*&o_regs[0]*/tmpgpr2, sb, 0);
	x_fprintf(out, "\t%s [%%%s], %%%s\n",
		sb->nbytes == 4? "lduw": "ldx",
		tmpgpr2->name, regname);
	x_fprintf(out, "\tcall free, 0\n");
	x_fprintf(out, "\tnop\n");
}

static void
emit_put_vla_size(struct vlasizedata *data) {
	do_stack(out, tmpgpr2, data->blockaddr, /*data->offset*/0);
	/*
	 * 06/01/11: data->offset wasn't used, we always nonsensically
	 * accessed the item after the pointer
	 */  
	x_fprintf(out, "\t%s %%%s, [%%%s+%lu]\n",
		"stx" /* XXX 32bit */, data->size->name, tmpgpr2->name,
		data->offset /*(unsigned long)backend->get_ptr_size()*/);
}	

static void
emit_retr_vla_size(struct vlasizedata *data) {
	do_stack(out, tmpgpr2, data->blockaddr, /*data->offset*/0);
	/*
	 * 06/01/11: data->offset wasn't used, we always nonsensically
	 * accessed the item after the pointer
	 */  
	x_fprintf(out, "\t%s [%%%s+%lu], %%%s\n",
		backend->abi != ABI_SPARC64? "lduw": "ldx",
		tmpgpr2->name,
		data->offset, /*(unsigned long)backend->get_ptr_size(),*/
		data->size->name);
}

static void
emit_load_vla(struct reg *r, struct stack_block *sb) {
	do_stack(out, /*&o_regs[0]*/tmpgpr2, sb, 0);
	x_fprintf(out, "\t%s [%%%s], %%%s\n",
		"ldx" /* XXX 32bit */, /*o_regs[0].*/tmpgpr2->name, r->name);	
}

static void
emit_frame_address(struct builtinframeaddressdata *dat) {
	x_fprintf(out, "\tmov %%fp, %%%s\n", dat->result_reg->name);
}	

/*
 * If ``r'' is not null, do_stack() forms the address in that register
 * using ``add'' or ``sub''.
 * Otherwise, the plain address, e.g. [%fp - 128], is written to out.
 *
 * If ``out'' is NULL (r is then ignored), the purpose is to calculate
 * whether the offset is small enough to use for displacement
 * addressing. The function then returns 0 if it fits, and -1 if it
 * doesn't.
 *
 * An offset above 4096 means that we cannot address ``[%fp -/+ off]''
 * Instead we have to form the address in a register and load
 * indirectly through it.
 */
static int
do_stack(FILE *out,
	struct reg *r,
	struct stack_block *stack_addr,
	unsigned long offset) {

	char		*sign;
	unsigned long	final_offset;

	if (stack_addr->is_func_arg || !stack_addr->use_frame_pointer) {
		sign = "+";

		/* We address [fp + off1 + off2] */
		final_offset = stack_addr->offset + offset;
		if (backend->abi == ABI_SPARC64) {
			/* Bias! [%fp+off] becomes [%fp+2047+off] */
			final_offset += 2047;
		}
	} else {
		sign = "-";

		/*
		 * We address [fp - off1 + off2]. After adding up
		 * off1 and off2, the first - may turn into a +
		 */ 
		if (backend->abi == ABI_SPARC64) {
			/*
			 * Bias! [%fp-off] becomes [%fp+2047-off]
			 */
			offset += 2047;
		}
		if ((long)offset >= stack_addr->offset) {
			/* We're above %fp now */
			sign = "+";
			final_offset = offset - stack_addr->offset;
		} else {
			final_offset = stack_addr->offset - offset;
		}
	}

	if (r == NULL && out == NULL) {
		/* Check whether the value is small enough to be immediate */
		if (final_offset >= 4096) {
			return -1;
		} else {
			return 0;
		}
	}

	if (stack_addr->use_frame_pointer) {
		if (r != NULL) {
#if 0
			x_fprintf(out, "\t%s %%fp, %lu, %%%s\n",
				*sign == '+'? "add": "sub",
				final_offset,
				r->name);
#endif
			if (*sign == '+') {
				do_add("fp", final_offset, r->name);
			} else {
				do_sub("fp", final_offset, r->name);
			}
		} else {
			x_fprintf(out, "[%%fp %s %lu]",
				sign, final_offset);
		}
	} else {
		if (r != NULL) {
#if 0
			x_fprintf(out, "\tadd %%sp, %lu, %%%s\n",
				final_offset, r->name);
#endif
			do_add("sp", final_offset, r->name); 
		} else {
			x_fprintf(out, "[%%sp + %lu]", final_offset);
		}
	}
	return 0;
}

static void
print_mem_operand(struct vreg *vr, struct token *constant,
struct vreg *vr_parent) {
	static int	was_llong;

	if (vr && vr->from_const != NULL) {
		constant = vr->from_const;
	}
	if (constant != NULL) {
		struct token	*t = vr->from_const;

		if (IS_INT(t->type) || IS_LONG(t->type)) {
			cross_print_value_by_type(out, t->data, t->type, 'd');
#if ALLOW_CHAR_SHORT_CONSTANTS
		} else if (IS_CHAR(t->type) || IS_SHORT(t->type)) {	
			cross_print_value_by_type(out, t->data, t->type, 'd');
#endif
		} else if (IS_LLONG(t->type)) {
			char	buf[128];

			llong_to_hex(buf, t->data, host_endianness);
			x_fprintf(out, "%s", buf);
		} else if (t->type == TOK_STRING_LITERAL) {
			struct ty_string	*ts = t->data;
			unimpl();  /* Caller must use load_sym_addr() instead */
			x_fprintf(out, "_Str%ld",
				ts->count);
		} else if (t->type == TY_FLOAT
			|| t->type == TY_DOUBLE
			|| t->type == TY_LDOUBLE) {
			struct ty_float	*tf = t->data;
	
			x_fprintf(out, "_Float%lu",
				tf->count);
		} else {
			printf("loadimm: Bad data type %d\n", t->type);
			exit(EXIT_FAILURE);
		}
	} else if (vr->parent != NULL) {
		struct vreg	*vr2 = vr_parent;
		struct decl	*d2;

		if ((d2 = vr2->var_backed) != NULL) {
			if (d2->stack_addr) {
				do_stack(out,
					NULL,
					d2->stack_addr,
					calc_offsets(vr));
			} else {
				/* static */
				x_fprintf(out, "[%%%s + %lu]",
					tmpgpr->name, calc_offsets(vr));
			}
		} else if (vr2->from_ptr) {
			/* Struct comes from pointer */
			x_fprintf(out, "[%%%s + %lu]",
				vr2->from_ptr->pregs[0]->name,	
				(unsigned long)calc_offsets(vr));
		} else {
			printf("BUG: Bad load for %s\n",
				vr->type->name? vr->type->name: "structure");
			abort();
		}	
	} else if (vr->var_backed) {
		struct decl	*d = vr->var_backed;

		if (d->stack_addr != NULL) {
			do_stack(out, NULL, d->stack_addr, 0);
		} else {
			/*
			 * Static or register variable
			 */
			if (d->dtype->storage == TOK_KEY_REGISTER) {
				unimpl();
			} else {
#if 0
				x_fprintf(out, "0($%s)", tmpgpr->name);
#endif
				x_fprintf(out, "%s",
					vr->var_backed->dtype->name);
			}	
		}
	} else if (vr->stack_addr) {
		do_stack(out, NULL, vr->stack_addr, 0);
#if 0
		if (vr->stack_addr->use_frame_pointer) {
			x_fprintf(out, "[%%fp %s %lu]",
				vr->stack_addr->is_func_arg? "+": "-",
				vr->stack_addr->offset);
		} else {
			x_fprintf(out, "[%%sp + %lu]", vr->stack_addr->offset);
		}
#endif
	} else if (vr->from_ptr) {
		x_fprintf(out, "[%%%s]", vr->from_ptr->pregs[0]->name);
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
	}	
}

static void
emit_sparc_load_int_from_ldouble(struct icode_instr *ii) {
	dont_kludge_ldouble = 1;
	emit_load(ii->dat, ii->src_vreg);
	dont_kludge_ldouble = 0;
}

struct emitter_sparc sparc_emit_sparc_as = {
	emit_sparc_load_int_from_ldouble
};

struct emitter sparc_emit_as = {
	0, /* need_explicit_extern_decls */
	init,
	emit_strings,
	emit_fp_constants,
	NULL, /* llong_constants */
	NULL, /* support_buffers */
	emit_pic_support,

	NULL, /* support_decls */
	NULL, /* extern_decls */
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
	NULL, /* emit_dwarf2_line */
	NULL, /* emit_dwarf2_files */
	emit_inlineasm,
	emit_unimpl,
	emit_empty,
	emit_label,
	emit_call,
	emit_callindir,
	emit_func_header,
	emit_func_intro,
	emit_func_outro,
	NULL,
	NULL,
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
	emit_extend_sign,
	emit_conv_fp,
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
	NULL,
	NULL,
	NULL, /* print_mem_operand */
	NULL, /* finish_program */
	NULL, /* stupidtrace */
	NULL /* finish_stupidtrace */
};

