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
 * Emit YASM code from intermediate amd64 code
 */
#include "amd64_emit_yasm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "scope.h"
#include "type.h"
#include "decl.h"
#include "icode.h"
#include "subexpr.h"
#include "token.h"
#include "typemap.h"
#include "functions.h"
#include "symlist.h"
#include "dwarf.h"
#include "cc1_main.h"
#include "x86_gen.h"
#include "expr.h"
#include "x86_emit_nasm.h"
#include "inlineasm.h"
#include "error.h"
#include "n_libc.h"

static FILE	*out;

static int 
init(FILE *fd, struct scope *s) {
	(void) s;
	out = fd;
	return 0;
}

static void
print_mem_operand(struct vreg *vr, struct token *constant);

/*
 * 04/11/08: Export print_mem_operand() for x86 emitter functions
 * which are shared between x86 and AMD64 and need this
 */
void
amd64_print_mem_operand_yasm(struct vreg *vr, struct token *constant) {
	print_mem_operand(vr, constant);
}

/*
 * Turns either a byte size into an assembler type string. If a type
 * argument is supplied (non-null), it will ensure that ``long long''
 * is mapped to ``dword'', as that type is really dealt with as two
 * dwords rather than a qword.
 */
static char *
size_to_asmtype(size_t size, struct type *type) {
	(void) type;

	if (size == /*10*/12
		|| size == 10
		|| size == 16) return "tword"; /* long double */
	else if (size == 8) return "qword"; /* double/long long */
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

extern void
print_nasm_offsets(struct vreg *vr);

static void
emit_support_decls(void) {
	x86_emit_nasm.support_decls();
}

static void
emit_extern_decls(void) {
	x86_emit_nasm.extern_decls();
}

static void
emit_global_extern_decls(struct decl **d, int ndecls) {
	x86_emit_nasm.global_extern_decls(d, ndecls);
}

static void
emit_global_static_decls(struct decl **d, int ndecls) {
	x86_emit_nasm.global_static_decls(d, ndecls);
}

static void
emit_static_init_vars(struct decl *list) {
	x86_emit_nasm.static_init_vars(list);
}

static void
emit_static_uninit_vars(struct decl *list) {
	x86_emit_nasm.static_uninit_vars(list);
}

static void
emit_static_init_thread_vars(struct decl *list) {
	x86_emit_nasm.static_init_thread_vars(list);
}

static void
emit_static_uninit_thread_vars(struct decl *list) {
	x86_emit_nasm.static_uninit_thread_vars(list);
}



#if 0
static void 
emit_static_decls(void) {
	x86_emit_nasm.static_decls();
}
#endif

static void
emit_struct_inits(struct init_with_name *list) {
	x86_emit_nasm.struct_inits(list);
}

extern void
print_nasm_string_init(size_t howmany, struct ty_string *str);

static void
emit_strings(struct ty_string *list) {
	x86_emit_nasm.strings(list);
}

static void
emit_fp_constants(struct ty_float *list) {
	x86_emit_nasm.fp_constants(list);
}

static void
emit_support_buffers(void) {
	if (amd64_need_negmask) {
		x86_emit_nasm.setsection(SECTION_INIT);
		if (amd64_need_negmask & 1) {
			/* XXX alignment!??!? */
			x_fprintf(out, "_SSE_Negmask:\n");
			x_fprintf(out, "\tdd 0x80000000\n");
			/* XXX alignment!??!? */
		}
		if (amd64_need_negmask & 2) {
			x_fprintf(out, "_SSE_Negmask_double:\n");
			x_fprintf(out, "\tdd 0x00000000\n");
			x_fprintf(out, "\tdd 0x80000000\n");
		}
	}
	if (amd64_need_ulong_float_mask) {
		x86_emit_nasm.setsection(SECTION_INIT);
		x_fprintf(out, "_Ulong_float_mask:\n");
		x_fprintf(out, "\tdd 1602224128\n");
	}

	x86_emit_nasm.support_buffers();
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
	unimpl();
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
		x_fprintf(out, "\tcall $%s wrt ..plt\n", name);
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
	x_fprintf(out, "\tpush qword rbp\n"); /* XXX */
	x_fprintf(out, "\tmov rbp, rsp\n");
}

static void
emit_func_outro(struct function *f) {
	if (picflag) {
		x_fprintf(out, "._End_%s:\n", f->proto->dtype->name);
		x_fprintf(out, "\tsize %s %s._End_%s-%s\n",
			f->proto->dtype->name,
			f->proto->dtype->name,
			f->proto->dtype->name,
			f->proto->dtype->name);
	}
}


static void
emit_define(const char *name, const char *fmt, ...) {
	x86_emit_nasm.define(name, fmt);
}

static void
emit_push(struct function *f, struct icode_instr *ii) {
	struct vreg	*vr = ii->src_vreg;
	struct reg	*r = ii->src_pregs?
				(void *)ii->src_pregs[0]: (void *)NULL;
	char		*ascii_type;


	(void) f;

	/*
	 * XXX 07/26/07 Hmm ascii_type not used anymore??
	 */
	if (ii->src_vreg->type
		&& ii->src_vreg->type->tlist
		&& ii->src_vreg->type->tlist->type == TN_ARRAY_OF) {
		ascii_type = size_to_asmtype(8, NULL);
	} else {	
		if (ii->src_vreg->type && IS_VLA(ii->src_vreg->type->flags)) {
			ascii_type = size_to_asmtype(8, NULL);
		} else {	
			ascii_type = size_to_asmtype(ii->src_vreg->size,
				ii->src_vreg->type);
		}
	}

	if (ii->src_pregs) {
		x_fprintf(out, "\tpush %s\n", /*ascii_type*/ r->name);
	} else if (vr->var_backed) {
		struct decl	*d = vr->var_backed;

		if (vr->parent != NULL) {
			/* Structure or union member */
			struct decl	*d2/* = d*/;
			struct vreg	*vr2;

			vr2 = get_parent_struct(vr);
			d2 = vr2->var_backed;
			if (vr2->from_ptr) {
				x_fprintf(out, "\tpush %s [%s",
					ascii_type,
					vr2->from_ptr->pregs[0]->name);
				if (vr->parent->type->code == TY_STRUCT) {
					x_fprintf(out, " + %s.%s",
					vr2->from_ptr->type->tstruc->tag,
					d2->dtype->name);
				}
				x_fprintf(out, "]\n");
			} else if (d2 && d2->stack_addr != NULL) {
				x_fprintf(out, "\tpush %s [rbp - %ld",
					ascii_type,	
					d2->stack_addr->offset);
				if (vr->parent->type->code == TY_STRUCT) {
					x_fputc(' ', out);
					print_nasm_offsets(/*d*/vr2);
				}	
				x_fprintf(out, "]\n"); 
			} else if (d2 != NULL) {	
				/* Must be static */
				x_fprintf(out, "\tpush %s [$%s",
					ascii_type,	
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
				x_fprintf(out, "\tpush %s [rbp - %ld]\n",
					ascii_type,	
					d->stack_addr->offset);
			} else {
				/* Static or register variable */
				if (d->dtype->storage == TOK_KEY_REGISTER) {
					unimpl();
				} else {
					x_fprintf(out, "\tpush %s [$%s]\n",
						ascii_type, d->dtype->name);
				}
			}
		}
	} else if (vr->from_const) {
		struct token	*t = vr->from_const;

		if (t->type == TOK_STRING_LITERAL) {
			struct ty_string	*s = t->data;
			x_fprintf(out, "\tpush qword _Str%lu\n", s->count);
		} else if (IS_INT(t->type)) {
			/* 
			 * There are only forms of ``int'' and ``long''
			 * constants
			 */
			x_fprintf(out, "\tpush dword ");
			cross_print_value_by_type(out,
				t->data, t->type, 0);	
			x_fputc('\n', out);
		} else if (IS_LONG(t->type) || IS_LLONG(t->type)) {	
			x_fprintf(out, "\tpush qword ");
			cross_print_value_by_type(out,
				t->data, t->type, 0);	
			x_fputc('\n', out);
		} else {
			puts("BUG in NASM emit_push()");
			exit(EXIT_FAILURE);
		}
	} else if (vr->from_ptr) {	
		x_fprintf(out, "\tpush %s [%s]\n", /*ascii_type,*/
				ascii_type,
				vr->from_ptr->pregs[0]->name);
	} else {
		unimpl();
	}

	f->total_allocated += 4;
}

static void
emit_allocstack(struct function *f, size_t nbytes) {
	(void) f;
	x_fprintf(out, "\tsub rsp, %lu\n", (unsigned long)nbytes);
}


static void
emit_freestack(struct function *f, size_t *nbytes) {
	if (nbytes == NULL) {
		/* Procedure outro */
		if (f->total_allocated != 0) {
			x_fprintf(out, "\tadd rsp, %lu\n",
				(unsigned long)f->total_allocated);
		}	
		x_fprintf(out, "\tpop rbp\n");
	} else {
		if (*nbytes != 0) {
			x_fprintf(out, "\tadd rsp, %lu\n",
				(unsigned long)*nbytes);
			f->total_allocated -= *nbytes;
		}
	}
}

static void
emit_adj_allocated(struct function *f, int *nbytes) {
	f->total_allocated += *nbytes; 
}	


#if 0
static void
emit_struct_defs(void) {
	if (x86_emit_nasm.struct_defs) {
		x86_emit_nasm.struct_defs();
	}
}
#endif

static void
emit_setsection(int value) {
	x86_emit_nasm.setsection(value);
}

static void
emit_alloc(size_t nbytes) {
	unimpl();
	(void) nbytes;
}


static void
print_mem_or_reg(struct reg *r, struct vreg *vr) {
	if (vr->on_var) {
		struct decl	*d = vr->var_backed;
		char		*p = size_to_asmtype(
			backend->get_sizeof_decl(d, NULL),
			d->dtype);

		if (d->stack_addr) {
			if (d->stack_addr->use_frame_pointer) {
				x_fprintf(out, "%s [rbp - %ld]",
					p, d->stack_addr->offset);
			} else {
				x_fprintf(out, "%s [rsp + %ld]",
					p, d->stack_addr->offset);
			}
		} else {
			x_fprintf(out, "%s %s", p, d->dtype->name);
		}
	} else {
		x_fprintf(out, "%s", r->name);
	}
}



static void
emit_inc(struct icode_instr *ii) {
	x86_emit_nasm.inc(ii);
}

static void
emit_dec(struct icode_instr *ii) {
	x86_emit_nasm.dec(ii);
}

static void
emit_load(struct reg *r, struct vreg *vr) {
	char		*p;
	int		needsize = 1;

	if (r->type == REG_FPR) {
		if (STUPID_X87(r)) {
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
			/* SSE */
			if (!IS_FLOATING(vr->type->code)) {
				p = "cvtsi2ss";
			} else {
				if (vr->type->code == TY_FLOAT) {
					p = "movss";
				} else {
					/* Must be double */
					p = "movsd";
				}
			}
			needsize = 0;
			x_fprintf(out, "\t%s %s, ", p, r->name);
		}
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
					/* XXX 32->64bit? ... */
					p = "movzx";
				} else {
					if (r->size == 8 && vr->size == 4) {
						if (vr->from_const) {
							needsize = 0;
							p = "mov";
						} else {
							p = "movsxd";
						}
					} else {
						p = "movsx";
					}
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

#if 0 
		if (vr->stack_addr != NULL) {
			size = r->size;
			ty = NULL;
		}
#endif
		if (size == 0) {
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
	x_fprintf(out, "\tlea %s, .%s\n", r->name, ii->dat);
}

static void
emit_comp_goto(struct reg *r) {
	x_fprintf(out, "\tjmp [%s]\n", r->name);
}


/*
 * Takes vreg source arg - not preg - so that it can be either a preg
 * or immediate (where that makes sense!)
 */
static void
emit_store(struct vreg *dest, struct vreg *src) {
	char		*p = NULL;
	int		floating = 0;

	if (src->pregs[0] && src->pregs[0]->type == REG_FPR) {	
		if (STUPID_X87(src->pregs[0])) {
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
			if (!IS_FLOATING(dest->type->code)) {
				unimpl();
			} else {
				if (dest->type->code == TY_FLOAT) {
					p = "movss";
				} else {
					p = "movsd"; 
				}
			}
			floating = 0; /* because SSE is non-x87-like */
		}
	} else {
		if (dest->stack_addr != NULL || dest->from_const != NULL) {
			p = "mov";
		} else {
			if (src->pregs[0] && dest->size < src->pregs[0]->size) {
				if (src->type == NULL
					|| src->type->sign
						== TOK_KEY_UNSIGNED) {
				/* XXX wow this should never be executed? */
					char	*asize = size_to_asmtype(
						dest->size, dest->type);	
					x_fprintf(out, "\tmov %s ", asize);
					print_mem_operand(dest, NULL);
					x_fprintf(out, ", 0\n");
					if (dest->is_multi_reg_obj) {
						x_fprintf(out, "\tmov %s ",
							asize);	
						print_mem_operand(dest, NULL);
						x_fprintf(out, ", 0\n");
					}	
					p = "mov";
				} else {
					if (dest->from_ptr
						&& src->pregs[0]->size == 4) {
						/*
						 * ``movsxd [r9], eax''
						 * doesn't work  :S
						 */
						src->pregs[0] =
							find_top_reg(
								src->pregs[0]);
						x_fprintf(out,
							 "\tmovsxd %s, %s\n",
							src->pregs[0]->name,
					src->pregs[0]->composed_of[0]->name);
						p = "mov";
					} else {
						if (src->pregs[0]->size == 4) {
							p = "movsxd";
						} else {
							p = "movsx";
						}
					}
				}	
			} else {
				p = "mov";
			}	
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
		x_fprintf(out, ", %s\n", src->pregs[0]->name);
	}
}


static void
emit_neg(struct reg **dest, struct icode_instr *src) {
	x86_emit_nasm.neg(dest, src);
}	


/* XXX 64bit */
static void
emit_sub(struct reg **dest, struct icode_instr *src) {
	if (dest[0]->type == REG_FPR) {
		if (STUPID_X87(dest[0])) {
			/* 05/27/08: fsubrp instead of fsubp... See x86 */
			x_fprintf(out, "\tfsubrp %s, ", dest[0]->name);
		} else {
			x_fprintf(out, "\tsubs%c %s, ",
				src->src_vreg->type->code == TY_FLOAT? 's': 'd',
				 dest[0]->name);
		}
	} else {
		if (src->dest_vreg->type->tlist) {
			/* ptr arit ... sub rax, edx doesn't work */
			if (src->src_vreg->size == 4) {
				/* XXX frontend stuff?! */
				struct reg	*r;

				r = find_top_reg(src->src_pregs[0]);
				if (src->src_vreg->type->sign
					!= TOK_KEY_UNSIGNED) {
					x_fprintf(out, "\tmovsxd %s, %s\n",
						r->name,
						 src->src_pregs[0]->name);
				}
				x_fprintf(out, "\tsub %s, %s\n",
					dest[0]->name,
					r->name);	
				return;
			}
		}
		x_fprintf(out, "\tsub %s, ", dest[0]->name);
	}
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fputc('\n', out);
}

/* XXX 64bit */
static void
emit_add(struct reg **dest, struct icode_instr *src) {
	if (dest[0]->type == REG_FPR) {
		if (STUPID_X87(dest[0])) {
			x_fprintf(out, "\tfaddp %s, ", dest[0]->name);
		} else {
			x_fprintf(out, "\tadds%c %s, ",
				src->src_vreg->type->code == TY_FLOAT? 's': 'd',
				dest[0]->name);
		}
	} else {	
		if (src->dest_vreg->type->tlist) {
			/* ptr arit ... add rax, edx doesn't work */
			if (src->src_vreg->size == 4) {
				/* XXX frontend stuff?! */
				struct reg	*r;

				r = find_top_reg(src->src_pregs[0]);
				if (src->src_vreg->type->sign !=
					TOK_KEY_UNSIGNED) {
					x_fprintf(out, "\tmovsxd %s, %s\n",
						r->name,
						 src->src_pregs[0]->name);
				}
				x_fprintf(out, "\tadd %s, %s\n",
					dest[0]->name,
					r->name);	
				return;
			}
		}
		x_fprintf(out, "\tadd %s, ", dest[0]->name);
	}	
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fputc('\n', out);
}

/* XXX 64bit */
static void
emit_div(struct reg **dest, struct icode_instr *src, int formod) {
	struct type	*ty = src->src_vreg->type;
	int		is_x87 = 0;

	(void) dest; (void) formod;
	if (!IS_FLOATING(ty->code)) {
		int	is_64bit = IS_LONG(ty->code) || IS_LLONG(ty->code);

		if (ty->sign != TOK_KEY_UNSIGNED) {
			if (is_64bit) {
				x_fprintf(out, "\txor rdx, rdx\n");
			} else {
				/* sign-extend eax to edx:eax */
				x_fprintf(out, "\tcdq\n");
			}
			x_fprintf(out, "\tidiv ");
		} else {
			if (is_64bit) {
				x_fprintf(out, "\txor rdx, rdx\n");
			} else {
				x_fprintf(out, "\txor edx, edx\n");
			}
			x_fprintf(out, "\tdiv ");
		}	
	} else {
		if (STUPID_X87(src->src_pregs[0])) {
			x_fprintf(out, "\tfdivp ");
			is_x87 = 1;
		} else {
			x_fprintf(out, "\tdivs%c %s, ",
				ty->code == TY_FLOAT? 's': 'd',
				dest[0]->name);
		}
	}
	if (is_x87) {
		print_mem_or_reg(src->dest_pregs[0], src->dest_vreg);
	} else {	
		print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	}
	x_fputc('\n', out);
}


/* XXX 64bit ... */
static void
emit_mod(struct reg **dest, struct icode_instr *src) {
	emit_div(dest, src, 1);
	if (!IS_LLONG(src->dest_vreg->type->code)
		&& !IS_LONG(src->dest_vreg->type->code)) {
		x_fprintf(out, "\tmov %s, edx\n", dest[0]->name);
	} else {
		x_fprintf(out, "\tmov %s, rdx\n", dest[0]->name);
	}
}


/* XXX ouch..how does it work with 64bit? */
static void
emit_mul(struct reg **dest, struct icode_instr *src) {
	struct type	*ty = src->src_vreg->type;
	int		is_x87 = 0;

	(void) dest;

	if (IS_FLOATING(ty->code)) {
		if (STUPID_X87(dest[0])) {
			x_fprintf(out, "\tfmulp ");
			is_x87 = 1;
		} else {
			x_fprintf(out, "\tmuls%c %s, ",
				ty->code == TY_FLOAT? 's': 'd',
				dest[0]->name);
		}
	} else if (ty->sign == TOK_KEY_UNSIGNED) {
		x_fprintf(out, "\tmul ");
	} else {
		/* signed integer multiplication */
		/* XXX should use mul for pointer arithmetic :( */
		x_fprintf(out, "\timul %s, ",
			src->src_pregs[0]->size == 4? "eax": "rax");
	}
	if (is_x87) {
		print_mem_or_reg(src->dest_pregs[0], src->dest_vreg);
	} else {	
		print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	}
	x_fputc('\n', out);
}

/* XXX sal for signed values!!!!! */
static void
emit_shl(struct reg **dest, struct icode_instr *src) {
	x86_emit_nasm.shl(dest, src);
}


/* XXX sar for signed values !!!!!!!! */
static void
emit_shr(struct reg **dest, struct icode_instr *src) {
	x86_emit_nasm.shr(dest, src);
}

static void
emit_or(struct reg **dest, struct icode_instr *src) {
	x86_emit_nasm.or(dest, src);
}

static void
emit_and(struct reg **dest, struct icode_instr *src) {
	x86_emit_nasm.and(dest, src);
}

static void
emit_xor(struct reg **dest, struct icode_instr *src) {
	x86_emit_nasm.xor(dest, src);
}

static void
emit_not(struct reg **dest, struct icode_instr *src) {
	x86_emit_nasm.not(dest, src);
}	

static void
emit_ret(struct icode_instr *ii) {
	(void) ii;

	x_fprintf(out, "\tret\n"); /* XXX */
}

extern struct icode_instr	*last_x87_cmp;
extern struct icode_instr	*last_sse_cmp;

static void
emit_cmp(struct reg **dest, struct icode_instr *src) {
	if (dest[0]->size == 8
		&& src->src_pregs
		&& src->src_pregs[0]->size == 4) {
		src->src_pregs[0] = find_top_reg(src->src_pregs[0]);
	} else if (src->src_pregs && src->src_pregs[0]->size == 8
		&& dest[0]->size == 4) {
		dest[0] = find_top_reg(dest[0]);
	}

	if (dest[0]->type == REG_FPR) {
		if (STUPID_X87(dest[0])) {
			last_x87_cmp = src;
		} else {
			last_sse_cmp = src;
		}
		return;
	} else {
		fprintf(out, "\tcmp %s, ", dest[0]->name);
	}	
	if (src->src_pregs == NULL || src->src_vreg == NULL) {
		fputc('0', out);
	} else {
		print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	}
	x_fputc('\n', out);
}

static void
emit_branch(struct icode_instr *ii) {
	x86_emit_nasm.branch(ii);
}

static void
emit_mov(struct copyreg *cr) {
	x86_emit_nasm.mov(cr);
}


static void
emit_setreg(struct reg *dest, int *value) {
	x86_emit_nasm.setreg(dest, value);
}

static void
emit_xchg(struct reg *r1, struct reg *r2) {
	x86_emit_nasm.xchg(r1, r2);
}


static void
emit_addrof(struct reg *dest, struct vreg *src, struct vreg *structtop) {
	x86_emit_nasm.addrof(dest, src, structtop);
}

/*
 * Copy initializer to automatic variable of aggregate type
 */
static void
emit_copyinit(struct decl *d) {
#if 0
		(unsigned long)d->vreg->size);
#endif
	x_fprintf(out, "\tmov rdx, %lu\n",
		(unsigned long)backend->get_sizeof_type(d->dtype, 0));
	x_fprintf(out, "\tmov rsi, %s\n", d->init_name->name);
	x_fprintf(out, "\tlea rdi, [rbp - %lu]\n", d->stack_addr->offset);
	x_fprintf(out, "\tcall memcpy\n");
}


/*
 * Assign one struct to another (may be any of automatic or static or
 * addressed thru pointer)
 *
 * This stuff now copies intrinsically - i.e. doesn't use memcpy() -
 * because that way we can guarantee that no registers are trashed.
 * This simplifies things like passing structs to functions on the
 * stack, but is currently very simplistic and thus not very fast
 * for big structs
 *
 * (Note that the frontend cannot assume that all registers remain
 * intact because other backends do still call memcpy())
 */
static void
emit_copystruct(struct copystruct *cs) {
	struct vreg		*stop;
	static unsigned long	lcount;

	x_fprintf(out, "\tpush rdx\n");
	x_fprintf(out, "\tpush rdi\n");
	x_fprintf(out, "\tpush rsi\n");
	x_fprintf(out, "\tpush rax\n");
	x_fprintf(out, "\tpush rcx\n");

	/* arg 3 = rdx */
#if 0
	x_fprintf(out, "\tmov rdx, %lu\n", (unsigned long)cs->src_vreg->size);
#endif
	x_fprintf(out, "\tmov rcx, %lu\n", (unsigned long)cs->src_vreg->size);
	if (cs->src_from_ptr == NULL) {
		if (cs->src_vreg->parent) {
			stop = get_parent_struct(cs->src_vreg);
		} else {
			stop = NULL;
		}	
		/* arg 2 = rsi */
		emit_addrof(&amd64_x86_gprs[4], cs->src_vreg, stop); 
	} else {
		if (cs->src_vreg->parent) {
			x_fprintf(out, "\tadd %s, %lu\n",
				cs->src_from_ptr->name,
				calc_offsets(cs->src_vreg)
				/*cs->src_vreg->memberdecl->offset*/);	
		}
		/* arg 2 = rsi */
		x_fprintf(out, "\tmov rsi, %s\n", cs->src_from_ptr->name);
	}

	if (cs->dest_vreg == NULL) {
		/* copy to hidden pointer */
		emit_load(&amd64_x86_gprs[5], curfunc->hidden_pointer);
	} else if (cs->dest_from_ptr == NULL) {
		if (cs->dest_vreg->parent) {
			stop = get_parent_struct(cs->dest_vreg);
		} else {
			stop = NULL;
		}	
		/* arg 3 = rdi */
		emit_addrof(&amd64_x86_gprs[5], cs->dest_vreg, stop); 
	} else {	
		if (cs->dest_vreg->parent) {
			x_fprintf(out, "\tadd %s, %lu\n",
				cs->dest_from_ptr->name,
				calc_offsets(cs->dest_vreg)
				/*cs->dest_vreg->memberdecl->offset*/);	
		}	
		/* arg 3 = rdi */
		x_fprintf(out, "\tmov rdi, %s\n", cs->dest_from_ptr->name);
	}	
#if 0
	x_fprintf(out, "\tcall memcpy\n");
#endif
	x_fprintf(out, "\t.copystruct%lu:\n", lcount);
	x_fprintf(out, "\tmov al, [rsi]\n");
	x_fprintf(out, "\tmov [rdi], al\n");
	x_fprintf(out, "\tinc rdi\n");
	x_fprintf(out, "\tinc rsi\n");
	x_fprintf(out, "\tloop .copystruct%lu\n", lcount++);

	/* 04/06/08: rcx was missing! */
	x_fprintf(out, "\tpop rcx\n");
	x_fprintf(out, "\tpop rax\n");
	x_fprintf(out, "\tpop rsi\n");
	x_fprintf(out, "\tpop rdi\n");
	x_fprintf(out, "\tpop rdx\n");
}

static void
emit_intrinsic_memcpy(struct int_memcpy_data *data) {
	x86_emit_nasm.intrinsic_memcpy(data);
}	

static void
emit_zerostack(struct stack_block *sb, size_t nbytes) {
	x_fprintf(out, "\tmov rdx, %lu\n",
		(unsigned long)nbytes);
	x_fprintf(out, "\tmov rsi, 0\n");
	x_fprintf(out, "\tlea rdi, [rbp - %lu]\n",
		(unsigned long)sb->offset);
	x_fprintf(out, "\tcall memset\n");
}	

static void
emit_alloca(struct allocadata *ad) {
	x_fprintf(out, "\tmov %s, %s\n",
		ad->size_reg->size == 4? "edi": "rdi", ad->size_reg->name);
	x_fprintf(out, "\tcall malloc\n");
	if (ad->result_reg != &amd64_x86_gprs[0]) {
		x_fprintf(out, "\tmov %s, rax\n",
			ad->result_reg->name);
	}
}

static void
emit_dealloca(struct stack_block *sb, struct reg *r) {
	char	*regname = r? r->name: "rdi";
	x_fprintf(out, "\tmov %s, [rbp - %lu]\n",
		regname,	
		(unsigned long)sb->offset);
	x_fprintf(out, "\tcall free\n");
}

static void
emit_alloc_vla(struct stack_block *sb) {
	x_fprintf(out, "\tmov rdi, [rbp - %lu]\n",
		(unsigned long)sb->offset - backend->get_ptr_size());
	x_fprintf(out, "\tcall malloc\n");
	x_fprintf(out, "\tmov [rbp - %lu], rax\n",
		(unsigned long)sb->offset);	
}

static void
emit_dealloc_vla(struct stack_block *sb, struct reg *r) {
	char	*regname = r? r->name: "rdi";
	x_fprintf(out, "\tmov %s, [rbp - %lu]\n",
		regname,	
		(unsigned long)sb->offset);
	x_fprintf(out, "\tcall free\n");
}

static void
emit_put_vla_size(struct vlasizedata *data) {
	x_fprintf(out, "\tmov [rbp - %lu], %s\n",
		(unsigned long)data->blockaddr->offset - data->offset,
		data->size->name);
}

static void
emit_retr_vla_size(struct vlasizedata *data) {
	x_fprintf(out, "\tmov %s, [rbp - %lu]\n",
		data->size->name,
		(unsigned long)data->blockaddr->offset - data->offset);
}

static void
emit_load_vla(struct reg *r, struct stack_block *sb) {
	x_fprintf(out, "\tmov %s, [rbp - %lu]\n",
		r->name,
		(unsigned long)sb->offset);
}

static void
emit_frame_address(struct builtinframeaddressdata *dat) {
	x_fprintf(out, "\tmov %s, rbp\n", dat->result_reg->name);
}	
	

static void
emit_save_ret_addr(struct function *f, struct stack_block *sb) {
	(void) f;

	x_fprintf(out, "\tmov rax, [rbp + 8]\n");
	x_fprintf(out, "\tmov [rbp - %lu], rax\n", sb->offset);
}

static void
emit_check_ret_addr(struct function *f, struct stack_block *saved) {
	static unsigned long	labval = 0;

	(void) f;
	
	x_fprintf(out, "\tmov rcx, [rbp - %lu]\n", saved->offset);
	x_fprintf(out, "\tcmp rcx, qword [rbp + 8]\n");
	x_fprintf(out, "\tje .doret%lu\n", labval);
	x_fprintf(out, "\textern __nwcc_stack_corrupt\n");
	x_fprintf(out, "\tcall __nwcc_stack_corrupt\n");
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
	x_fprintf(out, "[rbp %s %lu", sign, d->stack_addr->offset);
}

static void
print_mem_operand(struct vreg *vr, struct token *constant) {
	int		needbracket = 1;

	if (vr && vr->from_const != NULL) {
		constant = vr->from_const;
	}
	if (constant != NULL) {
		struct token	*t = vr->from_const;

		if (IS_INT(t->type) || IS_LONG(t->type) || IS_LLONG(t->type)) {
			cross_print_value_by_type(out, t->data, t->type, 0);
#if ALLOW_CHAR_SHORT_CONSTANTS
		} else if (IS_CHAR(t->type) || IS_SHORT(t->type)) {	
			cross_print_value_by_type(out, t->data, t->type, 0);
#endif
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
			exit(EXIT_FAILURE);
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
		/*
		 * 07/26/12: This was missing support for using the
		 * stack rather than frame pointer
		 */
		if (vr->stack_addr->use_frame_pointer) {
			x_fprintf(out, "[rbp - %lu", vr->stack_addr->offset);
		} else {
			x_fprintf(out, "[rsp + %lu", vr->stack_addr->offset);
		}
	} else if (vr->from_ptr) {
		x_fprintf(out, "[%s", vr->from_ptr->pregs[0]->name);
	} else {
		abort();
	}
	if (constant == NULL) {
		if (needbracket) { 
			x_fputc(']', out);
		}	
	}	
}	


/* Mem to FPR */
static void
emit_amd64_cvtsi2sd(struct icode_instr *ip) {
	x_fprintf(out, "\tcvtsi2sd %s, ", ip->dest_pregs[0]->name);
	print_mem_or_reg(ip->src_pregs[0], ip->src_vreg);
	x_fputc('\n', out);
}

/* Mem to FPR */
static void
emit_amd64_cvtsi2ss(struct icode_instr *ip) {
	x_fprintf(out, "\tcvtsi2ss %s, ", ip->dest_pregs[0]->name);
	print_mem_or_reg(ip->src_pregs[0], ip->src_vreg);
	x_fputc('\n', out);
}

/* Mem to FPR */
static void
emit_amd64_cvtsi2sdq(struct icode_instr *ip) {
	x_fprintf(out, "\tcvtsi2sd %s, ", ip->dest_pregs[0]->name);
	print_mem_or_reg(ip->src_pregs[0], ip->src_vreg);
	x_fputc('\n', out);
}

/* Mem to FPR */
static void
emit_amd64_cvtsi2ssq(struct icode_instr *ip) {
	x_fprintf(out, "\tcvtsi2ss %s, ", ip->dest_pregs[0]->name);
	print_mem_or_reg(ip->src_pregs[0], ip->src_vreg);
	x_fputc('\n', out);
}

/* FPR to GPR */
static void
emit_amd64_cvttsd2si(struct icode_instr *ip) {
	x_fprintf(out, "\tcvttsd2si %s, %s\n",
		 ip->dest_pregs[0]->name, ip->src_pregs[0]->name);
}

/* FPR to GPR */
/* 08/01/08: 64bit target version */
static void
emit_amd64_cvttsd2siq(struct icode_instr *ip) {
	x_fprintf(out, "\tcvttsd2siq %s, %s\n",
		 ip->dest_pregs[0]->name, ip->src_pregs[0]->name);
}

/* FPR to GPR */
static void
emit_amd64_cvttss2si(struct icode_instr *ip) {
	x_fprintf(out, "\tcvttss2si %s, %s\n",
		 ip->dest_pregs[0]->name, ip->src_pregs[0]->name);
}

/* FPR to GPR */
/* 08/01/08: 64bit target version */
static void
emit_amd64_cvttss2siq(struct icode_instr *ip) {
	x_fprintf(out, "\tcvttss2siq %s, %s\n",
		 ip->dest_pregs[0]->name, ip->src_pregs[0]->name);
}

static void
emit_amd64_cvtsd2ss(struct icode_instr *ii) {
	struct reg	*r = ii->dat;

	x_fprintf(out, "\tcvtsd2ss %s, %s\n", r->name, r->name); 
}

static void
emit_amd64_cvtss2sd(struct icode_instr *ii) {
	struct reg	*r = ii->dat;

	x_fprintf(out, "\tcvtss2sd %s, %s\n", r->name, r->name); 
}

static void
emit_amd64_load_negmask(struct icode_instr *ii) {
/*	struct reg	*r = ii->dat;*/
	struct amd64_negmask_data	*dat = ii->dat;
	struct reg			*target_fpr = dat->target_fpr;
	struct reg			*support_gpr = dat->support_gpr;
	int				for_double;

	if (ii->src_vreg != NULL) {
		for_double = 1;
	} else {
		for_double = 0;
	}

	if (0) { /*picflag) {*/
	/* XXX unsupported?!?!? */
	} else {
		x_fprintf(out, "\tmovs%c %s, [_SSE_Negmask%s]\n",
				for_double? 'd': 's',
				target_fpr->name,
				for_double? "_double": "");
	}
}

static void
emit_amd64_xorps(struct icode_instr *ii) {
	struct reg	*r = ii->dat;

	x_fprintf(out, "\txorps %s, %s\n",
		ii->dest_vreg->pregs[0]->name, r->name);	
}

static void
emit_amd64_xorpd(struct icode_instr *ii) {
	struct reg	*r = ii->dat;

	x_fprintf(out, "\txorpd %s, %s\n",
		ii->dest_vreg->pregs[0]->name, r->name);	
}

static void
emit_amd64_ulong_to_float(struct icode_instr *ii) {
	struct amd64_ulong_to_float	*data = ii->dat;
	static unsigned long		count;

	if (data->code == TY_LDOUBLE) {
		x_fprintf(out, "\ttest %s, %s\n", data->src_gpr->name, data->src_gpr->name);
		x_fprintf(out, "\tjs ._Ulong_float%lu\n", count);
		x_fprintf(out, "\tjmp ._Ulong_float%lu\n", count+1);
		x_fprintf(out, "._Ulong_float%lu:\n", count);
		x_fprintf(out, "\tfadd dword [_Ulong_float_mask]\n");
		x_fprintf(out, "._Ulong_float%lu:\n", count+1);
		count += 2;
	} else {
		x_fprintf(out, "\ttest %s, %s\n", data->src_gpr->name, data->src_gpr->name);
		x_fprintf(out, "\tjs ._Ulong_float%lu\n", count);
		x_fprintf(out, "\tcvtsi2%sq %s, %s\n", 
			data->code == TY_DOUBLE? "sd": "ss", data->dest_sse_reg->name, data->src_gpr->name);
		x_fprintf(out, "\tjmp ._Ulong_float%lu\n", count+1);
		x_fprintf(out, "._Ulong_float%lu:\n", count);
		x_fprintf(out, "\tmov %s, %s\n", data->temp_gpr->name, data->src_gpr->name);
		x_fprintf(out, "\tand %s, 1\n", data->src_gpr->composed_of[0]->name);
		x_fprintf(out, "\tshr %s\n", data->temp_gpr->name); 
		x_fprintf(out, "\tor %s, %s\n", data->temp_gpr->name, data->src_gpr->name);
		x_fprintf(out, "\tcvtsi2%sq %s, %s\n",
			data->code == TY_DOUBLE? "sd": "ss", data->dest_sse_reg->name, data->temp_gpr->name);
		x_fprintf(out, "\tadd%s %s, %s\n",
			data->code == TY_DOUBLE? "sd": "ss", data->dest_sse_reg->name, data->dest_sse_reg->name);
		x_fprintf(out, "._Ulong_float%lu:\n", count+1);
		count += 2;
	}
}

struct emitter amd64_emit_yasm = {
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

	NULL, /*emit_struct_defs,*/
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
	NULL, /* initialize_pic */
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

struct emitter_amd64	emit_amd64_yasm = {
	emit_amd64_cvtsi2sd,
	emit_amd64_cvtsi2ss,
	emit_amd64_cvtsi2sdq,
	emit_amd64_cvtsi2ssq,
	emit_amd64_cvttsd2si,
	emit_amd64_cvttsd2siq,
	emit_amd64_cvttss2si,
	emit_amd64_cvttss2siq,
	emit_amd64_cvtsd2ss,
	emit_amd64_cvtss2sd,
	emit_amd64_load_negmask,
	emit_amd64_xorps,
	emit_amd64_xorpd,
	emit_amd64_ulong_to_float
};

