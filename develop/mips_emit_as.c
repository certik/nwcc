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
#include "mips_emit_as.h"
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
#include "backend.h"
#include "token.h"
#include "functions.h"
#include "symlist.h"
#include "mips_gen.h"
#include "typemap.h"
#include "cc1_main.h"
#include "expr.h"
#include "reg.h"
#include "debug.h"
#include "libnwcc.h"
#include "inlineasm.h"
#include "error.h"
#include "misc.h"
#include "n_libc.h"

static FILE	*out;
static size_t	data_segment_offset;

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
	x_fprintf(out, ".section .%s\n", p);
	if (value == SECTION_INIT || value == SECTION_UNINIT
		|| value == SECTION_INIT_THREAD || value == SECTION_UNINIT_THREAD) {
		x_fprintf(out, "\t.align 16\n");
	}
}

static void
print_mem_operand(struct vreg *vr, struct token *constant,
struct vreg *vreg_parent);


void	as_print_string_init(FILE *out, size_t howmany, struct ty_string *str);

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

	
	/*
	 * 11/09/08: Suppress alignment because this is already done by
	 * new_generic_print_init_list()
	 */
#if 0
	as_align_for_type(out, dt);
	x_fprintf(out, "\t");
#endif
	if (dt->tlist == NULL && !is_addr_as_int) {
		switch (dt->code) {
		case TY_CHAR:
		case TY_SCHAR:	
		case TY_UCHAR:
		case TY_BOOL:	
#if 0
			x_fprintf(out, ".byte 0x%x",
				*(unsigned char *)ex->
				const_value->value);
#endif
			x_fprintf(out, ".byte ");
			cross_print_value_by_type(out,
				ex->const_value->value, TY_UCHAR, 'x');	
			break;
		case TY_SHORT:
		case TY_USHORT:
			x_fprintf(out, ".half ");
			cross_print_value_by_type(out,
				ex->const_value->value, TY_USHORT, 'x');	
#if 0
			x_fprintf(out, ".half 0x%x",
				(unsigned short)*(short *)ex->
				const_value->value);
#endif
			break;
		case TY_INT:
		case TY_UINT:	
		case TY_ENUM:	
#if 0
			x_fprintf(out, ".word 0x%x",
				(unsigned)*(int *)ex->
				const_value->value);
			break;
		case TY_UINT:
			x_fprintf(out, ".word 0x%x",
				*(unsigned int *)ex->
				const_value->value);
#endif
			x_fprintf(out, ".word ");
			cross_print_value_by_type(out,
				ex->const_value->value, TY_UINT, 'x');	
			break;
		case TY_LONG:
			if (backend->abi == ABI_MIPS_N64) {
#if 0
				x_fprintf(out, ".dword 0x%lx",
					(unsigned long)*(long *)ex->
					const_value->value);
#endif
				x_fprintf(out, ".dword ");
				cross_print_value_by_type(out,
					ex->const_value->value, TY_ULONG, 'x');
			} else {	
#if 0
				x_fprintf(out, ".word 0x%x",
					(unsigned)*(long *)ex->
					const_value->value);
#endif
				x_fprintf(out, ".word ");
				cross_print_value_by_type(out,
					ex->const_value->value, TY_ULONG, 'x');
			}
			break;
		case TY_ULONG:
			if (backend->abi == ABI_MIPS_N64) {
#if 0
				x_fprintf(out, ".dword 0x%x",
					(unsigned long)*(unsigned long *)ex->
					const_value->value);
#endif
				x_fprintf(out, ".dword ");
				cross_print_value_by_type(out,
					ex->const_value->value, TY_ULONG, 'x');
			} else {	
#if 0
				x_fprintf(out, ".word 0x%x",
					(unsigned)*(unsigned long *)ex->
					const_value->value);
#endif
				x_fprintf(out, ".word ");
				cross_print_value_by_type(out,
					ex->const_value->value, TY_ULONG, 'x');
			}	
			break;
		case TY_LLONG:
		case TY_ULLONG:	
#if 0
			x_fprintf(out, ".word 0x%x\n",
				*(unsigned int *)ex->
				const_value->value);
			x_fprintf(out, "\t.word 0x%x",
				((unsigned int *)ex->
				 const_value->value)[1]);
#endif
			x_fprintf(out, ".word ");
			cross_print_value_chunk(out, ex->const_value->value,
				dt->code, TY_UINT, 0, 0);	
			x_fprintf(out, "\n\t.word ");
			cross_print_value_chunk(out, ex->const_value->value,
				dt->code, TY_UINT, 0, 1);
			break;
		case TY_FLOAT:
#if 0
			x_fprintf(out, ".word 0x%x\n",
				*(unsigned int *)ex->
				const_value->value);
#endif
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
				dt->code, TY_UINT, 0, 3);
#if 0
			x_fprintf(out, ".word 0x%x\n",
				*(unsigned int *)ex->
				const_value->value);
			x_fprintf(out, "\t.word 0x%x\n",
				((unsigned int *)ex->
				const_value->value)[1]);
#endif
			break;
		default:	
			printf("print_init_expr: "
				"unsupported datatype %d\n",
				dt->code);
			unimpl();
		}
	} else {
		if (is_addr_as_int || dt->tlist->type == TN_POINTER_TO) {
			if (backend->abi == ABI_MIPS_N64) {
				x_fprintf(out, ".dword ");
			} else {
				x_fprintf(out, ".word ");
			}	
			if (cv->is_nullptr_const) {
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
					x_fprintf(out, "\n\t.space %lu\n",
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
	 * XXX 03/24/08: Why didn't the original code handle this case
	 * before refactoring?!?! is it bogus or is this emitter buggy
	 * because it misses this case?
	 */
	(void) d; (void) ndecls;
}

static void
emit_global_static_decls(struct decl **dv, int ndecls) {
	int		i;

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


	if (list) emit_setsection(SECTION_INIT);
	for (d = list; d != NULL; d = d->next) {
		if (d->invalid) {
			continue;
		}
		data_segment_offset += generic_print_init_var(out,
			d, data_segment_offset, print_init_expr, 1);
#if 0 
		size = backend->get_sizeof_decl(d, NULL);
		data_segment_offset += size;
#endif
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

			as_align_for_type(out, d->dtype, 0);
			size = backend->get_sizeof_decl(d, NULL);
			x_fprintf(out, "%s:\n", d->dtype->name);

#if 0
			align = backend->get_align_type(d->dtype);
			alignbits = 0;
			while (align >>= 1) {
				++alignbits;
			}
			x_fprintf(out, "\t.align %lu\n", alignbits);
#endif
			x_fprintf(out, "\t.space %lu\n", size);
#if 0
			bss_segment_offset += size;
#endif
		}
	}

}

static void
emit_static_init_thread_vars(struct decl *list) {
	(void) list;
}

static void
emit_static_uninit_thread_vars(struct decl *list) {
	(void) list;
}


#if 0
static void 
emit_static_decls(void) {
	struct decl	*d;
	struct decl	**dv;
	size_t		size;
	int		i;

	(void) d; (void) size;

	bss_segment_offset = 0;
	if (static_uninit_vars != NULL) {
		x_fprintf(out, ".section .bss\n");
		for (d = static_uninit_vars; d != NULL; d = d->next) {
			if (d->dtype->storage == TOK_KEY_STATIC
				&& d->references == 0) {
				continue;
			}

			as_align_for_type(out, d->dtype);
			size = backend->get_sizeof_decl(d, NULL);
			x_fprintf(out, "%s:\n", d->dtype->name);

#if 0
			align = backend->get_align_type(d->dtype);
			alignbits = 0;
			while (align >>= 1) {
				++alignbits;
			}
			x_fprintf(out, "\t.align %lu\n", alignbits);
#endif
			x_fprintf(out, "\t.space %lu\n", size);
#if 0
			bss_segment_offset += size;
#endif
		}
	}

	if (static_init_vars) x_fprintf(out, ".section .data\n");
	data_segment_offset = 0;
	for (d = static_init_vars; d != NULL; d = d->next) {
		data_segment_offset += generic_print_init_var(out,
			d, data_segment_offset, print_init_expr, 1);
#if 0
		struct type	*dt = d->dtype;
		
		if (d->dtype->storage == TOK_KEY_STATIC
			&& d->references == 0) {
			continue;
		}

		/* Constant initializer expression */
		x_fprintf(out, "\t%s:\n", dt->name);
		generic_print_init_list(out, d, d->init, print_init_expr);
		if (dt->code == TY_STRUCT
			&& (dt->tlist == NULL
				|| dt->tlist->type == TN_ARRAY_OF)) {
			/* May need padding at the end */
			as_align_for_type(out, dt);
		}	
#if 0 
		size = backend->get_sizeof_decl(d, NULL);
		data_segment_offset += size;
#endif
#endif
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

			/*
			 * 11/09/08: Switch to new_generic_print_init_list()!
			 * This is untested, but the old way was wrong on
			 * SPARC and PPC as well
			 */
#if 0
			generic_print_init_list(out, in->dec,
				in->init, print_init_expr);
#endif
			generic_print_init_list(out, in->dec,
				in->init, print_init_expr);
		}	
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
				x_fprintf(out, "\t.align 2\n");
				break;
			case TY_DOUBLE:
			case TY_LDOUBLE:
				x_fprintf(out, "\t.align 3\n");
			}

			x_fprintf(out, "_Float%lu:\n", tf->count);
			switch (tf->num->type) {
			case TY_FLOAT:
#if 0
				x_fprintf(out, "\t.word %u\n",
					*(unsigned *)tf->num->value);
#endif
				x_fprintf(out, "\t.word ");
				cross_print_value_by_type(out,
					tf->num->value, TY_UINT, 'x');
				break;
			case TY_DOUBLE:
#if 0
				x_fprintf(out, "\t.word %u\n",
					*(unsigned *)tf->num->value);
				x_fprintf(out, "\t.word %u\n",
					((unsigned *)tf->num->value)[1]);
#endif
				x_fprintf(out, "\t.word ");
				cross_print_value_chunk(out,
					tf->num->value,
					tf->num->type, TY_UINT, 0, 0);	
				x_fprintf(out, "\n\t.word ");
				cross_print_value_chunk(out,
					tf->num->value,
					tf->num->type, TY_UINT, 0, 1);
				break;
			case TY_LDOUBLE:	
				x_fprintf(out, "\t.word ");
				cross_print_value_chunk(out,
					tf->num->value,
					tf->num->type, TY_UINT, 0, 0);	
				x_fprintf(out, "\n\t.word ");
				cross_print_value_chunk(out,
					tf->num->value,
					tf->num->type, TY_UINT, 0, 1);
				x_fprintf(out, "\n\t.word ");
				cross_print_value_chunk(out,
					tf->num->value,
					tf->num->type, TY_UINT, 0, 2);
				x_fprintf(out, "\n\t.word ");
				cross_print_value_chunk(out,
					tf->num->value,
					tf->num->type, TY_UINT, 0, 3);
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
	x_fprintf(out, "# ");
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
	if (backend->abi == ABI_MIPS_N64) {
		x_fprintf(out, "\tdla $%s, %s\n", tmpgpr->name, name);
	} else {
		x_fprintf(out, "\tla $%s, %s\n", tmpgpr->name, name);
	}
	x_fprintf(out, "\tjal $31, $%s\n", tmpgpr->name);
}

static void
emit_callindir(struct reg *r) {
	/*
	 * XXX I don't know why the move below is necessary, but if
	 * r is e.g. $2, the program crashes without it :/
	 */
	x_fprintf(out, "\tmove $%s, $%s\n", tmpgpr->name, r->name);
	x_fprintf(out, "\tjal $31, $%s\n", tmpgpr->name);
}	

static void
emit_func_header(struct function *f) {
	(void) f;
}

static void
emit_func_intro(struct function *f) {
	(void) f;
}

static void
emit_func_outro(struct function *f) {
	(void) f;
}

static void
do_add(const char *src, unsigned long off, const char *dest);

static void
emit_allocstack(struct function *f, size_t nbytes) {
	(void) f;
	if (!f->gotframe) {
		/* Creating stack frame */
		x_fprintf(out, "\t.frame $sp, %lu, $31\n",
			(unsigned long)nbytes);
		f->gotframe = 1;
	}
	if (backend->abi == ABI_MIPS_N64) {
		x_fprintf(out, "\tdsubu $sp, $sp, %lu\n",
			(unsigned long)nbytes);
	} else {
		x_fprintf(out, "\tsubu $sp, $sp, %lu\n",
			(unsigned long)nbytes);
	}	
}


static void
emit_freestack(struct function *f, size_t *nbytes) {
#if 0
	char	*instr;

	if (backend->abi == ABI_MIPS_N64) {
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
			do_add("sp", f->total_allocated, "sp");
		}	
	} else {
		if (*nbytes != 0) {
#if 0
			x_fprintf(out, "\t%s $sp, $sp, %lu\n",
				instr, (unsigned long)*nbytes);	
#endif
			do_add("sp", *nbytes, "sp");
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
		x_fprintf(out, "$%s", r->name);
	}
}



static void
emit_inc(struct icode_instr *ii) {
	x_fprintf(out, "\t%s%s $%s, $%s, 1\n",
		ii->src_vreg->size == 8? "daddi": "addi",
		ii->src_vreg->type->sign == TOK_KEY_UNSIGNED? "u": "",
		ii->src_pregs[0]->name, ii->src_pregs[0]->name);
}

static void
emit_dec(struct icode_instr *ii) {
#if 0
	x_fprintf(out, "\t%s%s $%s, $%s, -1\n",
		ii->src_vreg->size == 8? "daddi": "addi",
		ii->src_vreg->type->sign == TOK_KEY_UNSIGNED? "u": "",
		ii->src_pregs[0]->name, ii->src_pregs[0]->name);
#endif
	x_fprintf(out, "\tli $%s, 1\n", tmpgpr->name);
	x_fprintf(out, "\t%s%s $%s, $%s, $%s\n",
		ii->src_vreg->size == 8? "dsub": "sub",
		ii->src_vreg->type->sign == TOK_KEY_UNSIGNED? "u": "",
		ii->src_pregs[0]->name, ii->src_pregs[0]->name,
		tmpgpr->name);
}

static void
emit_addrof(struct reg *dest, struct vreg *src, struct vreg *stop);

static void
emit_load(struct reg *r, struct vreg *vr) {
	char		*p = NULL;
	struct vreg	*vr2 = NULL;
	static int	was_ldouble;

	/*
	 * If we need to do displacement addressing with a static
	 * variable, its address must be moved to a GPR first...
	 */
	if (vr->parent) {
		vr2 = get_parent_struct(vr);
		if (vr2->stack_addr == NULL
			&& vr2->var_backed
			&& vr2->var_backed->dtype->storage
				!= TOK_KEY_REGISTER) {
			emit_addrof(tmpgpr, vr2, NULL);
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
			p = "l.s";
		} else if (vr->size == 8) {
			p = "l.d";
		} else if (vr->size == 16) {
			/* long double */
			/*
			 * 07/16/09: Implemented with double emulation
			 */
			p = "l.d";
			/*unimpl();*/
		} else {
			/* ?!?!? */
			unimpl();
		}
	} else {
		if (vr->type
			&& vr->type->tlist != NULL
			&& (vr->type->tlist->type == TN_ARRAY_OF
			|| vr->type->tlist->type == TN_VARARRAY_OF)) {
			p = backend->abi == ABI_MIPS_N64? "dla": "la";
		} else if (vr->from_const) {
			if (IS_LLONG(vr->type->code)
				|| (IS_LONG(vr->type->code)
					&& backend->abi == ABI_MIPS_N64)) {
				p = "dli";
			} else if (vr->type->code == TY_LDOUBLE) {
				p = "ld";
			} else {
				p = "li";
			}
		} else {
			if (vr->type == NULL) {
				switch (vr->size) {
				case 4: p = "lw"; break;
				case 8: p = "ld"; break;
				default:
					printf("%d\n", (int)vr->size), abort();
				}
			} else if (vr->type->tlist != NULL) {	
				if (vr->type->tlist->type == TN_FUNCTION) {
					p = backend->abi == ABI_MIPS_N64? "dla": "la";
				} else {
					if (backend->abi == ABI_MIPS_N64) {
						p = "ld";
					} else {
						p = "lw";
					}
				}	
			} else {
				if (IS_CHAR(vr->type->code)) {
					if (vr->type->sign == TOK_KEY_UNSIGNED) {
						p = "lbu";
					} else {
						p = "lb";
					}
				} else if (vr->type->code == TY_SCHAR) {
					p = "lb";
				} else if (vr->type->code == TY_SHORT) {
					p = "lh";
				} else if (vr->type->code == TY_USHORT) {
					p = "lhu";
				} else if (vr->type->code == TY_INT
					|| vr->type->code == TY_ENUM) {
					p = "lw";
				} else if (vr->type->code == TY_UINT) {
					p = "lwu";
				} else if (vr->type->code == TY_LONG) {
					if (backend->abi == ABI_MIPS_N64) {
						p = "ld";
					} else {
						p = "lw";
					}
				} else if (vr->type->code == TY_ULONG) {
					if (backend->abi == ABI_MIPS_N64) {
						p = "ld";
					} else {
						p = "lwu";
					}
				} else if (vr->type->code == TY_LLONG) {
					p = "ld";
				} else if (vr->type->code == TY_ULLONG) {
					p = "ld";
				} else if (vr->type->code == TY_FLOAT) {
					p = "lw";
				} else if (vr->type->code == TY_DOUBLE) {
					p = "ld";
				} else if (vr->type->code == TY_LDOUBLE) {
					p = "ld";
				} else {
					unimpl();
				}
			}	
		}	
	}
	x_fprintf(out, "\t%s $%s, ", p, r->name);

	print_mem_operand(vr, NULL, vr2);
	x_fputc('\n', out);


	if (was_ldouble) {
		was_ldouble = 0;
	} else if (vr->type != NULL
		&& vr->type->code == TY_LDOUBLE
		&& vr->type->tlist == NULL
		&& vr->size == 16) {
		was_ldouble = 1;
	}
}


static void
emit_load_addrlabel(struct reg *r, struct icode_instr *ii) {
	(void) r; (void) ii;
	if (picflag) {
        	x_fprintf(out, "\t%s $%s, %%got_page(%s)($%s)\n",
			backend->abi == ABI_MIPS_N64? "ld": "lw",
			r->name,
			ii->dat,
			pic_reg->name);
		x_fprintf(out, "\t%s $%s, $%s, %%got_ofst(.%s)\n",
			backend->abi == ABI_MIPS_N64? "daddiu": "addiu",
			r->name, r->name, ii->dat);
	} else {
		unimpl();
	}
}

static void
emit_comp_goto(struct reg *r) {
	x_fprintf(out, "\tj $%s\n", r->name);
}



/*
 * Takes vreg source arg - not preg - so that it can be either a preg
 * or immediate (where that makes sense!)
 */
static void
emit_store(struct vreg *dest, struct vreg *src) {
	char			*p = NULL;
	int			is_bigstackoff = 0;
	static int		was_ldouble;
	struct stack_block	*sb = NULL;
	struct vreg		*vr2 = NULL;

	if (dest->parent) {
		vr2 = get_parent_struct(dest);
		if (vr2->stack_addr == NULL
			&& vr2->var_backed
			&& vr2->var_backed->dtype->storage
			!= TOK_KEY_REGISTER) {
			emit_addrof(tmpgpr, vr2, NULL);
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
			p = "s.d";
		} else if (dest->size == 4) {
			p = "s.s";
		} else if (dest->size == 8) {
			p = "s.d";
		} else if (dest->size == 16) {
			/* 06/15/09: Implemented long double emulation */
			p = "s.d";
		} else {
			unimpl();
		}
	} else {
		if (dest->type == NULL) {
			p = "sd";
		} else if (dest->type->tlist != NULL) {
			/* Must be pointer */
			if (backend->abi == ABI_MIPS_N64) {
				p = "sd";
			} else {
				p = "sw";
			}
		} else {
			if (IS_CHAR(dest->type->code)) {
				p = "sb";
			} else if (IS_SHORT(dest->type->code)) {
				p = "sh";
			} else if (IS_INT(dest->type->code)
				|| dest->type->code == TY_ENUM) {
				p = "sw";
			} else if (IS_LONG(dest->type->code)) {
				if (backend->abi == ABI_MIPS_N64) {
					p = "sd";
				} else {
					p = "sw";
				}
			} else if (IS_LLONG(dest->type->code)) {
				p = "sd";
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
	 * 07/14/09: Check whether this is a stack access (which may require
	 * an indirect access)
	 */
	if (dest->stack_addr != NULL) {
		sb = dest->stack_addr;
	} else if (vr2
		&& vr2->var_backed
		&& vr2->var_backed->stack_addr) {
		sb = vr2->var_backed->stack_addr;
	} else if (dest->var_backed && dest->var_backed->stack_addr) {
		sb = dest->var_backed->stack_addr;
	}
	if (sb != NULL) {
		/*
		 * Check if the offset is small enough
		 * 07/14/09: Copied from SPARC emitter, where there was
		 * a comment saying this stuff isn't needed anymore
		 * because it's done at a higher level. Nothing could
		 * be further from the truth
		 * XXX Figure out how to do it entirely in the general
		 * backend routines without these emitter hacks
		 */
#if 0
		if (do_stack(NULL, NULL, sb, parent_offset) == -1) {
			/* No, we have to load indirectly */
			do_stack(out, tmpgpr2, sb, /*0*/ parent_offset);
			is_bigstackoff = 1;
		}
#endif
	}


	x_fprintf(out, "\t%s ", p);
	if (src->from_const) {
		print_mem_operand(src, NULL, NULL);
		x_fprintf(out, ", ");
	} else {
		/* Must be register */
		x_fprintf(out, "$%s, ", src->pregs[was_ldouble]->name);
	}
	if (is_bigstackoff) {
		x_fprintf(out, "0(%s)", tmpgpr2->name);
	} else {
		print_mem_operand(dest, NULL, vr2);
	}
	x_fputc('\n', out);

	if (dest->type != NULL
		&& dest->type->code == TY_LDOUBLE
		&& dest->type->tlist == NULL) {
		assert(dest->is_multi_reg_obj == 2);
		if (was_ldouble) {
			was_ldouble = 0;
		} else {
			was_ldouble = 1;
		}
	}
}


static void
emit_neg(struct reg **dest, struct icode_instr *src) {
	(void) src;
	if (dest[0]->type == REG_FPR) {
		char	*isn;

		if (src->src_vreg->type->code == TY_FLOAT) {
			isn = "neg.s";
		} else {
			isn = "neg.d";
		}
		x_fprintf(out, "\t%s $%s, $%s\n",
			isn, dest[0]->name, dest[0]->name);	
	} else {
		x_fprintf(out, "\tneg $%s\n", dest[0]->name);
	}	
}	


static void
emit_sub(struct reg **dest, struct icode_instr *src) {
	char	*p = NULL;

	if (dest[0]->type == REG_FPR) {
		if (src->src_vreg->type->code == TY_FLOAT) {
			p = "sub.s";
		} else if (src->src_vreg->type->code == TY_DOUBLE) {
			p = "sub.d";
		} else {
			unimpl();
		}
		x_fprintf(out, "\t%s $%s, $%s, ",
			p, dest[0]->name, dest[0]->name); 
	} else {
		/* XXX take immediate into account */
		if ((src->dest_vreg->type->tlist || src->src_vreg->type->tlist)
			&& src->dest_vreg->size != src->src_vreg->size
			&& backend->abi == ABI_MIPS_N64) {
			/*
			 * XXX this belongs into the frontend, but I don't know
			 * how to do it right yet
			 */
			struct reg	*notptr = src->src_vreg->size == 8?
				src->dest_vreg->pregs[0]: src->src_vreg->pregs[0];
			x_fprintf(out, "\tdli $%s, 0xffffffff\n", tmpgpr->name);
			x_fprintf(out, "\tand $%s, $%s, $%s\n",
				notptr->name, notptr->name, tmpgpr->name);
			p = "dsub";   
		} else if (src->src_vreg->size == 8) {
			if (src->src_vreg->type->sign == TOK_KEY_UNSIGNED) {
				p = "dsubu";
			} else {
				p = "dsub";
			}	
		} else {
			if (src->src_vreg->type->sign == TOK_KEY_UNSIGNED) {
				p = "subu";
			} else {
				p = "sub";
			}
		}
		
		x_fprintf(out, "\t%s $%s, $%s, ",
			p, dest[0]->name, dest[0]->name);
	}
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fputc('\n', out);
}

static void
emit_add(struct reg **dest, struct icode_instr *src) {
	char	*p = NULL;

	if (dest[0]->type == REG_FPR) {
		if (src->src_vreg->type->code == TY_FLOAT) {
			p = "add.s";
		} else if (src->src_vreg->type->code == TY_DOUBLE) {
			p = "add.d";
		} else {
			unimpl();
		}
		x_fprintf(out, "\t%s $%s, $%s, ",
			p, dest[0]->name, dest[0]->name);
	} else {
		/* XXX take immediate into account ... addi, etc */
		if ((src->dest_vreg->type->tlist || src->src_vreg->type->tlist)
			&& src->dest_vreg->size != src->src_vreg->size
			&& backend->abi == ABI_MIPS_N64) {
			/*
	 		 * XXX this belongs into the frontend, but I don't know how to do it
			 * right yet
	 		 */
			struct reg	*notptr = src->src_vreg->size == 8?
				 src->dest_vreg->pregs[0]: src->src_vreg->pregs[0];
			x_fprintf(out, "\tdli $%s, 0xffffffff\n", tmpgpr->name);
			x_fprintf(out, "\tand $%s, $%s, $%s\n",
				notptr->name, notptr->name, tmpgpr->name);
			p = "dadd";
		} else if (src->src_vreg->size == 8) {
			if (src->src_vreg->type->sign == TOK_KEY_UNSIGNED) {
				p = "daddu";
			} else {
				p = "dadd";
			}	
		} else {
			if (src->src_vreg->type->sign == TOK_KEY_UNSIGNED) {
				p = "addu";
			} else {
				p = "add";
			}
		}
		x_fprintf(out, "\t%s $%s, $%s, ",
			p, dest[0]->name, dest[0]->name);
	}	
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fputc('\n', out);
}

static void
emit_div(struct reg **dest, struct icode_instr *src, int formod) {
	struct type	*ty = src->src_vreg->type;
	char		*p = NULL;

	(void) dest;

	if (IS_FLOATING(ty->code)) {
		if (ty->code == TY_FLOAT) {
			p = "div.s";
		} else if (ty->code == TY_DOUBLE) {
			p = "div.d";
		} else {
			unimpl();
		}
		x_fprintf(out, "\t%s $%s, $%s, $%s\n",
			p, dest[0]->name, dest[0]->name,
			src->src_pregs[0]->name);
		return;
	} else {
		if (ty->sign == TOK_KEY_UNSIGNED) {
			if (src->src_vreg->size == 8) {
				p = "ddivu";
			} else {
				p = "divu";
			}	
		} else {
			/* signed integer division */
			if (src->src_vreg->size == 8) {
				p = "ddiv";
			} else {
				p = "div";
			}	
		}
	}
	x_fprintf(out, "\t%s $%s, ", p, dest[0]->name);
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fputc('\n', out);
	if (formod) {
		/* Remainder is in HI */
		x_fprintf(out, "\tmfhi $%s\n", dest[0]->name);
	} else { 
		/* Quotient is in LO */
		x_fprintf(out, "\tmflo $%s\n", dest[0]->name);
	}

	/*
	 * The following two instructions after mfhi/mflo may not
	 * modify hi/lo!
	 */
	x_fprintf(out, "\tnop\n");
	x_fprintf(out, "\tnop\n");
}


static void
emit_mod(struct reg **dest, struct icode_instr *src) {
	emit_div(dest, src, 1);
}


static void
emit_mul(struct reg **dest, struct icode_instr *src) {
	struct type	*ty = src->src_vreg->type;
	char		*p = NULL;

	(void) dest;

	if (IS_FLOATING(ty->code)) {
		if (ty->code == TY_FLOAT) {
			p = "mul.s";
		} else if (ty->code == TY_DOUBLE) {
			p = "mul.d";
		} else {
			unimpl();
		}
		x_fprintf(out, "\t%s $%s, $%s, $%s\n",
			p, dest[0]->name, dest[0]->name,
			src->src_pregs[0]->name);
		return;
	} else if (ty->sign == TOK_KEY_UNSIGNED) {
		if (src->src_vreg->size == 8) {
			p = "dmultu";
		} else {
			p = "multu";
		}	
	} else {
		/* signed integer multiplication */
		if (src->src_vreg->size == 8) {
			p = "dmult";
		} else {
			p = "mult";
		}
	}	
	x_fprintf(out, "\t%s $%s, ", p, dest[0]->name);
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fputc('\n', out);
	x_fprintf(out, "\tmflo $%s\n", dest[0]->name);
	x_fprintf(out, "\tnop\n");
	x_fprintf(out, "\tnop\n");
}

static void
emit_shl(struct reg **dest, struct icode_instr *src) {
	char	*p;
	(void) dest; (void) src;
	
	if (src->dest_vreg->size == 8
		|| (src->src_vreg->from_const
		&& (src->src_vreg->from_const->flags & TOK_FLAG_LONG_SHIFT))) {
		p = "dsll";
	} else {
		p = "sll";
	}
	if (src->src_vreg->from_const) {
		emit_load(tmpgpr, src->src_vreg);
	}
	x_fprintf(out, "\t%s $%s, $%s, ", p, dest[0]->name, dest[0]->name);
	if (src->src_vreg->from_const) {
		x_fprintf(out, "$%s\n", tmpgpr->name);
	} else {
		x_fprintf(out, "$%s\n", src->src_pregs[0]->name);
	}
}

static void
emit_shr(struct reg **dest, struct icode_instr *src) {
	char	*p;

	(void) dest; (void) src;
	if (src->dest_vreg->size == 8
		|| (src->src_vreg->from_const
		&& (src->src_vreg->from_const->flags & TOK_FLAG_LONG_SHIFT))) {
		if (src->dest_vreg->type->sign == TOK_KEY_UNSIGNED) {
			p = "dsrl";
		} else {
			p = "dsra";
		}	
	} else {
		if (src->dest_vreg->type->sign == TOK_KEY_UNSIGNED) {
			p = "sra";
		} else {
			p = "srl";
		}	
	}
	if (src->src_vreg->from_const) {
		emit_load(tmpgpr, src->src_vreg);
	}
	x_fprintf(out, "\t%s $%s, $%s, ", 
		p, dest[0]->name, dest[0]->name);
	if (src->src_vreg->from_const) {
		x_fprintf(out, "$%s\n", tmpgpr->name);
	} else {	
		x_fprintf(out, "$%s\n", src->src_pregs[0]->name);
	}
}

static void
emit_or(struct reg **dest, struct icode_instr *src) {
	x_fprintf(out, "\tor $%s, $%s, $%s\n",
		dest[0]->name, dest[0]->name, src->src_pregs[0]->name);
}

static void
emit_and(struct reg **dest, struct icode_instr *src) {
	x_fprintf(out, "\tand $%s, $%s, $%s\n",
		dest[0]->name, dest[0]->name,
		src->src_vreg? src->src_pregs[0]->name: tmpgpr->name);
}

static void
emit_xor(struct reg **dest, struct icode_instr *src) {
	x_fprintf(out, "\txor $%s, $%s, ", 
		dest[0]->name, dest[0]->name);
	if (src->src_vreg == NULL) {
		x_fprintf(out, "$%s\n", dest[0]->name);
	} else {	
		print_mem_or_reg(src->src_pregs[0], src->src_vreg);
		x_fputc('\n', out);
	}
}

static void
emit_not(struct reg **dest, struct icode_instr *src) {
	char	*p;

	/*
	 * NEG means flip all bits, then add one. NOT means flip
	 * all bits. As there does not seem to be a NOT instruction,
	 * let's use NEG, then SUB 1
	 */
	(void) src;
	x_fprintf(out, "\tneg $%s\n", dest[0]->name);
	x_fprintf(out, "\tli $%s, 1\n", tmpgpr->name);
	if (src->src_vreg->size == 8) {
		p = "dsubu";
	} else {
		p = "subu";
	}	
	x_fprintf(out, "\t%s $%s, $%s, $%s\n",
		p, dest[0]->name, dest[0]->name, tmpgpr->name);	
}	

static void
emit_ret(struct icode_instr *ii) {
	(void) ii;

	x_fprintf(out, "\tj $31\n");
}

static struct icode_instr	*last_cmp_instr;


static void
emit_cmp(struct reg **dest, struct icode_instr *src) {
	last_cmp_instr = src;

	(void) src; (void) dest;
}

static void
emit_extend_sign(struct icode_instr *ii) {
	struct extendsign	*ext = ii->dat;
	struct reg		*r = ext->dest_preg;
	struct type		*to = ext->dest_type;
	struct type		*from = ext->src_type;
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

		if (backend->abi == ABI_MIPS_N64) {
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
		if (backend->abi == ABI_MIPS_N32
			|| backend->abi == ABI_MIPS_N64) {
			if (IS_CHAR(from->code)) {
				shift_bits = 56;
			} else if (IS_SHORT(from->code)) {
				shift_bits = 48;
			} else if (IS_INT(from->code)) {
				shift_bits = 32;
			} else {
				/* Must be long */
				if (backend->abi == ABI_MIPS_N32) {
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
	x_fprintf(out, "\tli $%s, %d\n", tmpgpr->name, shift_bits); 
	x_fprintf(out, "\tsll $%s, $%s, $%s\n", r->name,
		r->name, tmpgpr->name);
	x_fprintf(out, "\tsra $%s, $%s, $%s\n",
		r->name, r->name, tmpgpr->name); 
}

static int
do_stack(FILE *out,
	struct reg *r,
	struct stack_block *stack_addr,
	unsigned long offset);

static void
emit_from_ldouble(struct icode_instr *ii) {
	do_stack(out, &mips_gprs[4], ii->dest_vreg->var_backed->stack_addr, 0);
	do_stack(out, &mips_gprs[5], ii->src_vreg->var_backed->stack_addr, 0);
	x_fprintf(out, "\t%s $25, __nwcc_conv_from_ldouble\n",
		backend->abi == ABI_MIPS_N64? "dla": "la");
	x_fprintf(out, "\tjal $31, $25\n");
}

static void
emit_to_ldouble(struct icode_instr *ii) {
	do_stack(out, &mips_gprs[4], ii->dest_vreg->var_backed->stack_addr, 0);
	do_stack(out, &mips_gprs[5], ii->src_vreg->var_backed->stack_addr, 0);
	x_fprintf(out, "\t%s $25, __nwcc_conv_to_ldouble\n",
		backend->abi == ABI_MIPS_N64? "dla": "la");
	x_fprintf(out, "\tjal $31, $25\n");
}

static void
emit_branch(struct icode_instr *ii) {
	char		*lname;
	struct vreg	*src_vreg = NULL;
	struct reg	**src_pregs = NULL;
	struct reg	**dest_pregs = NULL;
	struct reg	*src_preg;
	char		*opcode;
	int		is_signed = 0;

	if (last_cmp_instr) {
		/* Not unconditional jump - get comparison */
		src_vreg = last_cmp_instr->src_vreg;
		src_pregs = last_cmp_instr->src_pregs;
		dest_pregs = last_cmp_instr->dest_pregs;

		/*
		 * 06/29/08: This was missing the check whether we are 
		 * comparing pointers and thus need unsigned comparison
		 */
		is_signed =
			last_cmp_instr->dest_vreg->type->sign != TOK_KEY_UNSIGNED
		&& last_cmp_instr->dest_vreg->type->tlist == NULL;	
	}

	if (src_pregs == NULL || src_vreg == NULL) {
		/* Compare with zero. XXX not for fp!!! */
		src_preg = &mips_gprs[0];
	} else {
		src_preg = src_pregs[0];
	}

#define SLT_INSTR(is_signed) \
	(is_signed? "slt": "sltu")

	lname = ((struct icode_instr *)ii->dat)->dat;
	if (src_preg && src_preg->type == REG_FPR) {
		char	*fmt = NULL;

		if (src_vreg->type->code == TY_FLOAT) {
			fmt = "s";
		} else if (src_vreg->type->code == TY_DOUBLE) {
			fmt = "d";
		} else {
			/* long double or bug */
			unimpl();
		}

		switch (ii->type) {
		case INSTR_BR_EQUAL:
			x_fprintf(out, "\tc.eq.%s $%s, $%s\n",
				fmt, dest_pregs[0]->name, src_preg->name);
			x_fprintf(out, "\tbc1t .%s\n", lname); 
			break;
		case INSTR_BR_NEQUAL:
			x_fprintf(out, "\tc.eq.%s $%s, $%s\n",
				fmt, dest_pregs[0]->name, src_preg->name);
			x_fprintf(out, "\tbc1f .%s\n", lname); 
			break;
		case INSTR_BR_SMALLER:
			x_fprintf(out, "\tc.lt.%s $%s, $%s\n",
				fmt, dest_pregs[0]->name, src_preg->name);
			x_fprintf(out, "\tbc1t .%s\n", lname); 
			break;
		case INSTR_BR_SMALLEREQ:
			x_fprintf(out, "\tc.le.%s $%s, $%s\n",
				fmt, dest_pregs[0]->name, src_preg->name);
			x_fprintf(out, "\tbc1t .%s\n", lname); 
			break;
		case INSTR_BR_GREATER:
			x_fprintf(out, "\tc.le.%s $%s, $%s\n",
				fmt, dest_pregs[0]->name, src_preg->name);
			x_fprintf(out, "\tbc1f .%s\n", lname); 
			break;
		case INSTR_BR_GREATEREQ:
			x_fprintf(out, "\tc.lt.%s $%s, $%s\n",
				fmt, dest_pregs[0]->name, src_preg->name);
			x_fprintf(out, "\tbc1f .%s\n", lname); 
			break;
		case INSTR_JUMP:
			/* XXX huh - old bug !?? */
			x_fprintf(out, "\tj .%s\n", lname);
		}
	} else {
		switch (ii->type) {
		case INSTR_JUMP:
			x_fprintf(out, "\tj .%s\n", lname);
			break;
		case INSTR_BR_EQUAL:
			x_fprintf(out, "\tbeq $%s, $%s, .%s\n",
				dest_pregs[0]->name, src_preg->name, lname);
			break;
		case INSTR_BR_SMALLER:
			opcode = SLT_INSTR(is_signed);
			x_fprintf(out, "\t%s $%s, $%s, $%s\n",
				opcode, tmpgpr->name,
				dest_pregs[0]->name,
				src_preg->name); 
			x_fprintf(out, "\tbne $%s, $0, .%s\n",
				tmpgpr->name, lname);
			break;
		case INSTR_BR_SMALLEREQ:
			opcode = SLT_INSTR(is_signed);
			x_fprintf(out, "\t%s $%s, $%s, $%s\n",
				opcode, tmpgpr->name,
				src_preg->name,
				dest_pregs[0]->name);
			x_fprintf(out, "\tbeq $%s, $0, .%s\n",
				tmpgpr->name, lname);
			break;
		case INSTR_BR_GREATER:
			opcode = SLT_INSTR(is_signed);
			x_fprintf(out, "\t%s $%s, $%s, $%s\n",
				opcode, tmpgpr->name,
				src_preg->name,
				dest_pregs[0]->name);
			x_fprintf(out, "\tbne $%s, $0, .%s\n",
				tmpgpr->name, lname);
			break;
		case INSTR_BR_GREATEREQ:
			opcode = SLT_INSTR(is_signed);
			x_fprintf(out, "\t%s $%s, $%s, $%s\n",
				opcode, tmpgpr->name,
				dest_pregs[0]->name,
				src_preg->name);
			x_fprintf(out, "\tbeq $%s, $0, .%s\n",
				tmpgpr->name, lname);
			break;
		case INSTR_BR_NEQUAL:
			x_fprintf(out, "\tbne $%s, $%s, .%s\n",
				dest_pregs[0]->name, src_preg->name, lname);
			break;
		}
	}
	last_cmp_instr = NULL; /* XXX hmm ok? */
}


static void
emit_mov(struct copyreg *cr) {
	struct reg	*dest = cr->dest_preg;
	struct reg	*src = cr->src_preg;

	if (src == NULL) {
		/* Move null to register */
		x_fprintf(out, "\tmove $%s, $0\n",
			dest->name);
	} else {
		if (dest->type != src->type) {
			/* Must be fpr vs gpr */
			size_t	src_size = backend->
				get_sizeof_type(cr->src_type, NULL);

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

			if (cr->src_type->code == TY_DOUBLE
				|| cr->src_type->code == TY_LDOUBLE) {
				c = 'd';
			} else if (cr->src_type->code == TY_FLOAT) {
				c = 's';
			} else if (cr->src_type->code < TY_INT	
				|| (IS_LONG(cr->src_type->code)
					&& backend->abi != ABI_MIPS_N64)) {
				c = 'w';
			} else {	
				c = 'l';
			}
			x_fprintf(out, "\tmov.%c $%s, $%s\n",
				c, dest->name, src->name);
		} else {
			x_fprintf(out, "\tmove $%s, $%s\n",
				dest->name, src->name);
		}
	}	
}


static void
emit_setreg(struct reg *dest, int *value) {
	/* XXX signed vs unsigned :/ */
	x_fprintf(out, "\tli $%s, %u\n", dest->name, *(unsigned *)value);
}

static void
emit_xchg(struct reg *r1, struct reg *r2) {
	(void) r1; (void) r2;
	unimpl();
}

static void
emit_addrof(struct reg *dest, struct vreg *src, struct vreg *structtop) {
	struct decl	*d;
	long		offset = 0;

	if (src == NULL) {
		d = curfunc->proto->dtype->tlist->tfunc->lastarg;
	} else {
		d = src->var_backed;
	}	

	if (structtop != NULL) {
		d = structtop->var_backed;
	}	

	/* 07/17/09: Add PIC support */
	if (d != NULL) {
		if (picflag
			&& d->dtype
			&& (d->dtype->storage == TOK_KEY_STATIC
			|| d->dtype->storage == TOK_KEY_EXTERN)) {
			x_fprintf(out, "\t%s $%s, %%got_disp(%s)($%s)\n",
				backend->abi == ABI_MIPS_N64? "ld": "lw",
				dest->name,
				d->dtype->name,
				pic_reg->name);
			if (src->parent != NULL) {
				do_add(dest->name,
					calc_offsets(src),
					dest->name);
			}
			return;
		}
	}

	if (d && d->stack_addr != NULL) {
		offset = d->stack_addr->offset;
	}	

	if (src && src->parent != NULL) {
		/* Structure or union type */
		if (d != NULL) {
			if (d->stack_addr != NULL) {
				x_fprintf(out, "\t%s $%s, %lu($%s)\n",
					backend->abi == ABI_MIPS_N64? "dla": "la",
					dest->name,
					offset+calc_offsets(src),
					d->stack_addr->use_frame_pointer?
						"fp": "sp");
			} else {
				/* Static */
				x_fprintf(out, "\t%s $%s, %s\n",
					backend->abi == ABI_MIPS_N64? "dla": "la",
					tmpgpr->name, d->dtype->name);
				x_fprintf(out, "\t%s $%s, %lu($%s)\n",
					backend->abi == ABI_MIPS_N64? "dla": "la",
					dest->name,
					calc_offsets(src),
					tmpgpr->name);
			}	
		} else if (structtop->from_ptr) {
			x_fprintf(out, "\t%s $%s, %lu($%s)\n",
				backend->abi == ABI_MIPS_N64? "dla": "la",
				dest->name,
				calc_offsets(src),
				structtop->from_ptr->pregs[0]->name);
		} else {
			printf("hm attempt to take address of %s\n",
				src->type->name);	
			unimpl();
		}	
	} else if (src && src->from_ptr) {	
		x_fprintf(out, "\tmove $%s, $%s\n",
			dest->name, src->from_ptr->pregs[0]->name);
	} else if (src && src->from_const && src->from_const->type == TOK_STRING_LITERAL) {
		/* 08/21/08: This was missing */
		emit_load(dest, src);
	} else if (src && src->from_const && picflag) {
		struct ty_float	*tf;

		/* 07/17/09: Floating point constant in memory, with PIC enabled */
		if (!IS_FLOATING(src->from_const->type)) {
			(void) fprintf(stderr, "BUG: Attempt to take adress "
				"of integer constant?!\n");
			unimpl();
		}
		tf = src->from_const->data;
		x_fprintf(out, "\t%s $%s, %%got_page(_Float%lu)($%s)\n",
			backend->abi == ABI_MIPS_N64? "ld": "lw",
			dest->name,
			tf->count,
			pic_reg->name);
#if 0
		x_fprintf(out, "\t%s $%s, %%got_ofst(_Float%lu)($%s)\n",
			backend->abi == ABI_MIPS_N64? "dla": "la",
			dest->name,
			tf->count,
			pic_reg->name);
#endif
		x_fprintf(out, "\t%s $%s, $%s, %%got_ofst(_Float%lu)\n",
			backend->abi == ABI_MIPS_N64? "daddiu": "addiu",
			dest->name,
			dest->name,
			tf->count);
	} else {		
		if (d && d->stack_addr) {
			/* src = NULL means start of variadic */
			x_fprintf(out, "\t%s $%s, %ld($%s)\n",
				backend->abi == ABI_MIPS_N64? "dla": "la",
				dest->name, offset,
				d->stack_addr->use_frame_pointer? "fp": "sp");
		} else if (d) {
			/*
			 * Must be static variable - symbol itself is
			 * address
			 */
			x_fprintf(out, "\t%s $%s, %s\n",
				backend->abi == ABI_MIPS_N64? "dla": "la",
				dest->name, d->dtype->name);
		} else if (src && src->stack_addr) {
			/* Anonymous item */
			x_fprintf(out, "\t%s $%s, %lu($%s)\n",
				backend->abi == ABI_MIPS_N64? "dla": "la",
				dest->name, (unsigned long)src->stack_addr->offset,
				src->stack_addr->use_frame_pointer? "fp": "sp");
		} else {
			debug_print_vreg_backing(src, 0);
			unimpl(); /* ??? */
		}
	}

	if (src != NULL && src->addr_offset != 0) {
		do_add(dest->name, src->addr_offset, dest->name);
	}
}

static void
emit_initialize_pic(struct function *f) {
	x_fprintf(out, "\tlui $%s, %%hi(%%neg(%%gp_rel(%s)))\n",
		pic_reg->name, f->proto->dtype->name);
	x_fprintf(out, "\tdaddu $%s, $%s, $%s\n",
		pic_reg->name, pic_reg->name, tmpgpr->name);
	x_fprintf(out, "\tdaddiu $%s, $%s, %%lo(%%neg(%%gp_rel(%s)))\n",
		pic_reg->name, pic_reg->name, f->proto->dtype->name);
}


/*
 * Copy initializer to automatic variable of aggregate type
 */
static void
emit_copyinit(struct decl *d) {
	x_fprintf(out, "\t%s $4, %lu($fp)\n",
		backend->abi == ABI_MIPS_N64? "dla": "la",
		d->stack_addr->offset);
	x_fprintf(out, "\t%s $5, %s\n",
		backend->abi == ABI_MIPS_N64? "dla": "la",
		d->init_name->name);
	/* XXX dli?? */
	x_fprintf(out, "\tli $6, %lu\n", backend->get_sizeof_type(d->dtype, NULL));
	x_fprintf(out, "\t%s $%s, memcpy\n",
		backend->abi == ABI_MIPS_N64? "dla": "la",
		tmpgpr->name);
	x_fprintf(out, "\tjal $31, $%s\n", tmpgpr->name);
}


static void
emit_putstructregs(struct putstructregs *ps) {
	struct reg      *destreg = ps->destreg;
	struct reg      *ptrreg = ps->ptrreg;
	struct vreg     *vr = ps->src_vreg;
	int             size = vr->size;
	int             offset = 0;

	while (destreg != &mips_gprs[4+8]) {
		if (size <= 0) {
			/* We're already done! */
			return;
		}
		/*
		 * Use 3 as temp reg because it is not
		 * loaded with the target address yet
		 */
		x_fprintf(out, "\t%s $%s, %d($%s)\n",
			backend->abi == ABI_MIPS_N32? "lw": "ld",
			destreg->name,
			offset,
			ptrreg->name);
		size -= mips_gprs[0].size;
		offset += mips_gprs[0].size;
		++destreg;
	}
}

/*
 * Assign one struct to another (may be any of automatic or static or
 * addressed thru pointer)
 */
static void
emit_copystruct(struct copystruct *cs) {
	struct vreg	*stop;

	/* XXX dli?! */
	x_fprintf(out, "\tli $6, %lu\n", (unsigned long)cs->src_vreg->size);
	if (cs->src_from_ptr == NULL) {
		if (cs->src_vreg->parent) {
			stop = get_parent_struct(cs->src_vreg);
		} else {
			stop = NULL;
		}	
		emit_addrof(tmpgpr, cs->src_vreg, stop); 
		x_fprintf(out, "\tmove $5, $%s\n", tmpgpr->name);
	} else {
		if (cs->src_vreg->parent) {
			x_fprintf(out, "\t%siu $%s, $%s, %lu\n",
				backend->abi == ABI_MIPS_N64? "dadd": "add",
				cs->src_from_ptr->name,
				cs->src_from_ptr->name,
				calc_offsets(cs->src_vreg));
		}
		if (cs->src_vreg->addr_offset != 0) {
			do_add(cs->src_from_ptr->name, cs->src_vreg->addr_offset, "5");
		} else {
			x_fprintf(out, "\tmove $5, $%s\n", cs->src_from_ptr->name);
		}
	}

	if (cs->dest_vreg == NULL) {
		/* Copy to hidden return pointer */
		emit_load(tmpgpr, curfunc->hidden_pointer);
		x_fprintf(out, "\tmove $4, $%s\n", tmpgpr->name);
	} else if (cs->dest_from_ptr == NULL) {
		if (cs->dest_vreg->parent) {
			stop = get_parent_struct(cs->dest_vreg);
		} else {
			stop = NULL;
		}	
		emit_addrof(tmpgpr, cs->dest_vreg, stop); 
		x_fprintf(out, "\tmove $4, $%s\n", tmpgpr->name);
	} else {	
		if (cs->dest_vreg->parent) {
			x_fprintf(out, "\t%siu $%s, $%s, %lu\n",
				backend->abi == ABI_MIPS_N64? "dadd": "add",
				cs->dest_from_ptr->name,
				cs->dest_from_ptr->name,
				calc_offsets(cs->dest_vreg));
		}	
		if (cs->dest_vreg->addr_offset != 0) {
			do_add(cs->dest_from_ptr->name, cs->dest_vreg->addr_offset, "4");
		} else {
			x_fprintf(out, "\tmove $4, $%s\n", cs->dest_from_ptr->name);
		}
	}
	x_fprintf(out, "\t%s $%s, memcpy\n",
		backend->abi == ABI_MIPS_N64? "dla": "la",
		tmpgpr->name);
	x_fprintf(out, "\tjal $31, $%s\n", tmpgpr->name);
}

static void
emit_intrinsic_memcpy(struct int_memcpy_data *data) {
	struct reg	*dest = data->dest_addr;
	struct reg	*src = data->src_addr;
	struct reg	*nbytes = data->nbytes;
	struct reg	*temp = data->temp_reg;
	static int	labelcount;
	
	if (data->type == BUILTIN_MEMSET && src != temp) {
		x_fprintf(out, "\tmove $%s, $%s\n", temp->name, src->name);
	}
	x_fprintf(out, "\tbeq $%s, $0, .Memcpy_done%d\n",
		nbytes->name, labelcount);
	x_fprintf(out, ".Memcpy_start%d:\n", labelcount);
	if (data->type == BUILTIN_MEMCPY) {
		x_fprintf(out, "\tlbu $%s, 0($%s)\n", temp->name, src->name);
	}
	x_fprintf(out, "\tsb $%s, 0($%s)\n", temp->name, dest->name);
	x_fprintf(out, "\t%s $%s, $%s, 1\n",
		backend->abi == ABI_MIPS_N64? "daddiu": "addiu", dest->name, dest->name);
	if (data->type == BUILTIN_MEMCPY) {
		x_fprintf(out, "\t%s $%s, $%s, 1\n",
			backend->abi == ABI_MIPS_N64? "daddiu": "addiu", src->name, src->name);
	}

	/* XXX this ok?? if so why isn't emit_dec() doing this */
	x_fprintf(out, "\taddi $%s, $%s, -1\n", nbytes->name, nbytes->name);
	x_fprintf(out, "\tbne $%s, $0, .Memcpy_start%d\n",
		nbytes->name, labelcount);
	x_fprintf(out, ".Memcpy_done%d:\n", labelcount);
	++labelcount;
}

static void
emit_zerostack(struct stack_block *sb, size_t nbytes) {
	static struct vreg	vr;

	x_fprintf(out, "\tli $6, %lu\n",
		(unsigned long)nbytes);
	x_fprintf(out, "\tli $5, 0\n");
	vr.stack_addr = sb;
	emit_addrof(&mips_gprs[4], &vr, NULL);
	x_fprintf(out, "\t%s $%s, memset\n",
		backend->abi == ABI_MIPS_N64? "dla": "la", tmpgpr->name);
	x_fprintf(out, "\tjal $31, $%s\n", tmpgpr->name);
}

static void
emit_alloca(struct allocadata *ad) {
	x_fprintf(out, "\tmove $4, $%s\n", ad->size_reg->name);
	x_fprintf(out, "\t%s $%s, malloc\n", 
		backend->abi == ABI_MIPS_N64? "dla": "la",
		tmpgpr->name);
	x_fprintf(out, "\tjal $31, $%s\n", tmpgpr->name);
	if (&mips_gprs[2] != ad->result_reg) {
		x_fprintf(out, "\tmove $%s, $2\n",
			ad->result_reg->name);
	}
}

static void
emit_dealloca(struct stack_block *sb, struct reg *r) {
	char	*regname = r? r->name: "4";

	x_fprintf(out, "\t%s $%s, %lu($fp)\n",
		sb->nbytes == 4? "lw": "ld",
		regname,
		(unsigned long)sb->offset);	
	x_fprintf(out, "\t%s $%s, free\n",
		backend->abi == ABI_MIPS_N64? "dla": "la",
		tmpgpr->name);
	x_fprintf(out, "\tjal $31, $%s\n", tmpgpr->name);
}

static void
emit_alloc_vla(struct stack_block *sb) {
	x_fprintf(out, "\t%s $4, %lu($fp)\n",
		backend->abi == ABI_MIPS_N64? "ld": "lw",
		(unsigned long)(sb->offset + backend->get_ptr_size()));
	x_fprintf(out, "\t%s $%s, malloc\n",
		backend->abi == ABI_MIPS_N64? "dla": "la",
		tmpgpr->name);
	x_fprintf(out, "\tjal $31, $%s\n", tmpgpr->name);
	x_fprintf(out, "\t%s $2, %lu($fp)\n",
		backend->abi == ABI_MIPS_N64? "sd": "sw",
		(unsigned long)sb->offset);
}

static void
emit_dealloc_vla(struct stack_block *sb, struct reg *r) {
	char	*regname = r? r->name: "4";

	x_fprintf(out, "\t%s $%s, %lu($fp)\n",
		backend->abi == ABI_MIPS_N64? "ld": "lw",
		regname,
		(unsigned long)sb->offset);	
	x_fprintf(out, "\t%s $%s, free\n",
		backend->abi == ABI_MIPS_N64? "dla": "la",
		tmpgpr->name);
	x_fprintf(out, "\tjal $31, $%s\n", tmpgpr->name);
}

static void
emit_put_vla_size(struct vlasizedata *data) {
	/*
	 * 07/17/09: This used to access blockaddr->offset - data->offset,
	 * which could yield a negative result (converted to a huge positve
	 * value) and doesn't seem to make sense.
	 * The frame pointer points to the bottom of the frame, so if the
	 * base address of the bookkeeping data is at blockaddr, then the
	 * offset into it must be added
	 * XXX This is not proven to be correct yet and may be wrong on
	 * other architectures as well
	 */
	x_fprintf(out, "\t%s $%s, %lu($fp)\n",
		backend->abi != ABI_MIPS_N64? "sw": "sd",
		data->size->name,
		(unsigned long)data->blockaddr->offset + data->offset);
}

static void
emit_retr_vla_size(struct vlasizedata *data) {
	/*
	 * 07/17/09: This used to access blockaddr->offset - data->offset
	 */
	x_fprintf(out, "\t%s $%s, %lu($fp)\n",
		backend->abi != ABI_MIPS_N64? "lw": "ld",
		data->size->name,
		(unsigned long)data->blockaddr->offset + data->offset);
}

static void
emit_load_vla(struct reg *dest, struct stack_block *sb) {
	x_fprintf(out, "\t%s $%s, %lu($fp)\n",
		backend->abi != ABI_MIPS_N64? "lw": "ld",
		dest->name,
		(unsigned long)sb->offset);
}

static void
emit_frame_address(struct builtinframeaddressdata *dat) {
	x_fprintf(out, "\tmove $%s, $fp\n", dat->result_reg->name);
}	


static void
do_add(const char *src, unsigned long off, const char *dest) {
	if (off >= 0x8000) {
		x_fprintf(out, "\tdli $%s, %lu\n", tmpgpr2->name, off);
		x_fprintf(out, "\t%s $%s, $%s, $%s\n",
			backend->abi == ABI_MIPS_N64? "dadd": "add",
			dest, src, tmpgpr2->name);
	} else {
		x_fprintf(out, "\t%s $%s, $%s, %lu\n",
			backend->abi == ABI_MIPS_N64? "daddi": "addi",
			dest, src, off);
	}
}

#if 0
static void
do_stack(FILE *out, struct decl *d, unsigned long offset) {
#endif
static int
do_stack(FILE *out,
	struct reg *r,
	struct stack_block *stack_addr,
	unsigned long offset) {

	unsigned long final_offset = stack_addr->offset + offset;

        if (r == NULL && out == NULL) {
		/*
		 * 11/07/08: Check whether the value is small enough
		 * to be immediate
		 */
		if ((signed long)final_offset < -0x8000
			|| (signed long)final_offset > 0x7fff) {
			return -1;
		} else {
			return 0;
		}
	}


	if (r != NULL) {
		/* 11/07/08: Form address in register */
		if (stack_addr->use_frame_pointer) {
			do_add("fp", final_offset, r->name);
		} else {
			do_add("sp", final_offset, r->name);
		}
	} else {
		if (stack_addr->use_frame_pointer) {
			x_fprintf(out, "%lu($fp)", final_offset);
		} else {
			x_fprintf(out, "%lu($sp)", final_offset);
		}
	}
	return 0;
}




static void
print_mem_operand(struct vreg *vr, struct token *constant,
struct vreg *vr_parent) {
	static int	was_ldouble;
	int		extra_offset_for_mem = 0;

	/*
	 * 07/15/09: Add 128bit long double support. Note that Linux/MIPSel
	 * loads the upper 8 bytes first when passing stuff to functions.
	 * It appears that this isn't so on big endian MIPS, but the
	 * distinction has not been tested yet
	 */
#if 0
	if (get_target_endianness() == ENDIAN_LITTLE) {
		if (!was_ldouble) {
			if (vr->type != NULL
				&& vr->type->code == TY_LDOUBLE
				&& vr->type->tlist == NULL) {
				extra_offset_for_mem = 8;
			}
		}
	} else
#endif
	{
		if (was_ldouble) {
			extra_offset_for_mem = 8;
		}
	}

	if (vr && vr->from_const != NULL) {
		constant = vr->from_const;
	}
	if (constant != NULL) {
		struct token	*t = vr->from_const;

		/* if (t->type == TY_INT) {
			x_fprintf(out, "%d", 
				*(int *)t->data);
		} else if (t->type == TY_UINT) {
			x_fprintf(out, "%u", 
				*(unsigned *)t->data);
		} else if (t->type == TY_LONG) {
			x_fprintf(out, "%ld", 
				*(long *)t->data);
		} else if (t->type == TY_ULONG) {
			x_fprintf(out, "%lu",
				*(unsigned long *)t->data);
		}*/
		if (IS_INT(t->type) || IS_LONG(t->type)) {
			cross_print_value_by_type(out, t->data, t->type, 'd');
#if ALLOW_CHAR_SHORT_CONSTANTS
		} else if (IS_CHAR(t->type) || IS_SHORT(t->type)) {	
			cross_print_value_by_type(out, t->data, t->type, 'd');
#endif
		} else if (IS_LLONG(t->type)) {
			/*
			 * 07/15/09: llong_to_hex() yields endianness-dependent
			 * results, e.g. 0x1234 will yield 0x123400000000
			 * on a little endian system! So we have to use
			 * cross_print_value_by_type(), and also use this for
			 * big endian systems because llong_to_hex() is very
			 * dubious and may cause other problems
			 */
			if (1 /*get_target_endianness() == ENDIAN_LITTLE*/) {
				cross_print_value_by_type(out, t->data, t->type, 'd');
			} else {
				char	buf[128];
				llong_to_hex(buf, t->data, host_endianness);
				x_fprintf(out, "%s", buf);
			}
		} else if (t->type == TOK_STRING_LITERAL) {
			struct ty_string	*ts = t->data;
			x_fprintf(out, "_Str%ld",
				ts->count);
		} else if (t->type == TY_FLOAT
			|| t->type == TY_DOUBLE
			|| t->type == TY_LDOUBLE) {
			struct ty_float	*tf = t->data;

			if (t->type == TY_LDOUBLE) {
				if (was_ldouble) {
					was_ldouble = 0;
				} else {
					was_ldouble = 1;
				}
			}
	
			if (extra_offset_for_mem != 0) {
				assert(t->type == TY_LDOUBLE);
				if (picflag) {
					unimpl();
				} else {
					x_fprintf(out, "_Float%lu+%d",
						tf->count,
						extra_offset_for_mem);
				}
			} else {
				if (picflag) {
					unimpl();
				} else {
					x_fprintf(out, "_Float%lu",
						tf->count);
				}
			}
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
					vr2->var_backed->stack_addr,
					calc_offsets(vr));
			} else {
				/* static */
				x_fprintf(out, "%lu",
					calc_offsets(vr));
				x_fprintf(out, "($%s)",
					tmpgpr->name);
			}
		} else if (vr2->from_ptr) {
			/* Struct comes from pointer */
			x_fprintf(out, "%lu",
				(unsigned long)calc_offsets(vr));
			x_fprintf(out, "($%s)",
				vr2->from_ptr->pregs[0]->name);	
		}else {
			printf("BUG: Bad load for %s\n",
				vr->type->name? vr->type->name: "structure");
			abort();
		}	
	} else if (vr->var_backed) {
		struct decl	*d = vr->var_backed;

		if (d->stack_addr != NULL) {
			do_stack(out, NULL, d->stack_addr, extra_offset_for_mem);
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
		unsigned long offset = vr->stack_addr->offset + extra_offset_for_mem;
		if (vr->stack_addr->use_frame_pointer) {
			x_fprintf(out, "%lu($fp)", offset);
		} else {
			x_fprintf(out, "%lu($sp)", offset);
		}
	} else if (vr->from_ptr) {
		x_fprintf(out, "%d($%s)", extra_offset_for_mem, vr->from_ptr->pregs[0]->name);
	} else {
		abort();
	}
	if (constant == NULL) {
		/*if (was_llong) {
			x_fprintf(out, " + 4 ");
			was_llong = 0;
		} else*/
		if (was_ldouble) {
			was_ldouble = 0;
		} else if (vr->is_multi_reg_obj) {
			if (vr->type->code == TY_LDOUBLE) {
				was_ldouble = 1;
			} /*else {
				was_llong = 1;
			}*/
		}
	}	
}

static void
emit_mips_mfc1(struct icode_instr *ii) {
	struct reg	*dest = ii->dat;

	x_fprintf(out, "\t%smfc1 $%s, $%s\n",
		ii->src_vreg->size == 8? "d": "",
		dest->name, ii->src_pregs[0]->name);
}

static void
emit_mips_mtc1(struct icode_instr *ii) {
	struct reg	*dest = ii->dat;

	x_fprintf(out, "\t%smtc1 $%s, $%s\n",
		ii->src_vreg->size == 8? "d": "",
		ii->src_pregs[0]->name, dest->name);
}

static void
emit_mips_cvt(struct icode_instr *ii) {
	struct type	*dest_type = ii->dest_vreg->type;
	struct type	*src_type = ii->src_vreg->type;
	char		*p = NULL;
	struct reg	*fpr = NULL;

	if (src_type->code == TY_FLOAT) {
		if (dest_type->code == TY_DOUBLE) {
			p = "cvt.d.s";
		} else if (dest_type->code == TY_LDOUBLE) {
			unimpl();
		} else {
			printf("dest_type is %d\n", dest_type->code);
			printf("src_type is %d\n", src_type->code);
			unimpl();
		}
		fpr = ii->src_pregs[0]; 
	} else if (src_type->code == TY_DOUBLE) {
		if (dest_type->code == TY_FLOAT) {
			p = "cvt.s.d";
		} else if (dest_type->code == TY_LDOUBLE) {
			unimpl();
		} else {
			unimpl();
		}
		fpr = ii->src_pregs[0]; 
	} else if (src_type->code == TY_LDOUBLE) {
		unimpl();
	} else {
		/* Integral to floating point */
		if (src_type->code < TY_INT /* 08/01/09: This was missing */
			|| IS_INT(src_type->code)
			|| (IS_LONG(src_type->code)
				&& backend->abi != ABI_MIPS_N64)) {
			if (dest_type->code == TY_FLOAT) {
				p = "cvt.s.w";
			} else if (dest_type->code == TY_DOUBLE) {
				p = "cvt.d.w";
			} else {
				unimpl();
			}
		} else {
			/* Must be 64bit integer */
			if (dest_type->code == TY_FLOAT) {
				p = "cvt.s.l";
			} else if (dest_type->code == TY_DOUBLE) {
				p = "cvt.d.l";
			} else {
				unimpl();
			}
		}
		fpr = ii->dest_pregs[0]; 
	}
	x_fprintf(out, "\t%s $%s, $%s\n",
		p, fpr->name, fpr->name);
}

static void
emit_mips_trunc(struct icode_instr *ii) {
	struct type	*dest_type = ii->dest_vreg->type;
	struct type	*src_type = ii->src_vreg->type;
	char		*p = NULL;
	struct reg	*fpr = ii->src_pregs[0];

	if (src_type->code == TY_FLOAT) {
		if (IS_INT(dest_type->code)
			|| (IS_LONG(dest_type->code)
				&& backend->abi != ABI_MIPS_N64)) {
			p = "trunc.w.s";
		} else if (IS_LONG(dest_type->code)
			|| IS_LLONG(dest_type->code)) {
			p = "trunc.l.s";
		} else {
			p = "trunc.w.s"; /* XXX !! */
		}	
	} else if (src_type->code == TY_DOUBLE) {
		if (IS_INT(dest_type->code)
			|| (IS_LONG(dest_type->code)
				&& backend->abi != ABI_MIPS_N64)) {
			p = "trunc.w.d";
		} else if (IS_LONG(dest_type->code)
			|| IS_LLONG(dest_type->code)) {
			p = "trunc.l.d";
		} else {
			p = "trunc.w.d"; /* XXX !!! */
		}	
	} else {
		unimpl();
	}
	x_fprintf(out, "\t%s $%s, $%s\n",
		p, fpr->name, fpr->name);
}

static void
emit_mips_make_32bit_mask(struct icode_instr *ii) {
	x_fprintf(out, "\tdli $%s, 0xffffffff\n", ((struct reg *)ii->dat)->name);
}

struct emitter_mips mips_emit_mips_as = {
	emit_mips_mfc1,
	emit_mips_mtc1,
	emit_mips_cvt,
	emit_mips_trunc,
	emit_mips_make_32bit_mask
};

struct emitter mips_emit_as = {
	0, /* need_explicit_extern_decls */
	init,
	emit_strings,
	emit_fp_constants,
	NULL, /* llong_constants */
	NULL, /* support_buffers */
	NULL, /* pic_support */
	
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
	NULL, /* conv_fp */
	emit_from_ldouble,
	emit_to_ldouble, 
	emit_branch,
	emit_mov,
	emit_setreg,
	emit_xchg,
	emit_addrof,
	emit_initialize_pic,
	emit_copyinit,
	emit_putstructregs,
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

