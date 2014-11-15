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
#include "typemap.h"
#include "mips_gen.h"
#include "expr.h"
#include "reg.h"
#include "libnwcc.h"
#include "cc1_main.h"
#include "inlineasm.h"
#include "error.h"
#include "n_libc.h"

static FILE		*out;
static size_t		data_segment_offset;
/*static size_t		bss_segment_offset;*/
extern struct vreg	float_conv_mask;

static int 
init(FILE *fd, struct scope *s) {
	(void) s;
	out = fd;
	return 0;
}
static void
emit_setsect(int value);

static void
print_mem_operand(struct vreg *vr, struct token *constant,
struct vreg *vreg_parent);

static void
emit_addrof(struct reg *dest, struct vreg *src, struct vreg *stop);

static void
emit_load(struct reg *r, struct vreg *vr);

void	as_print_string_init(FILE *out, size_t howmany, struct ty_string *str);

static void
do_softarit_call(struct icode_instr *ii,
	const char *func, int op, int formod);

static int 
do_stack(FILE *out,
	struct reg *r,
	struct stack_block *stack_addr,
	unsigned long offset);


/*
 * 12/25/08: Export print_mem_operand()'s last used offset. E.g. when
 * loading the second part of an 128bit long double, it will be 8.
 * We need this information in emit_load() and emit_store() now as
 * well when using the minimal-toc option
 */
static int	last_extra_offset_for_mem;


static void
do_emit_li(const char *dest, unsigned number, int has_sign) {
	int	need_neg = 0;

	if (has_sign && (int)number < 0) {
		number = -(int)number;
		need_neg = 1;
	}	
	/*
 	 * XXX cross-compilation :-( ...
 	 */
	if (number > 32767) {
		x_fprintf(out, "\tlis %s, %u\n",
			dest, number >> 16);
		x_fprintf(out, "\tori %s, %s, %u\n",
			dest, dest, number & 0xffff);
		/*
		 * 11/13/08: lis performs sign-extension, which isn't
		 * desirable if we're loading a 32bit unsigned value
		 * into a 64bit register. So shift left, then shift
		 * right logical to remove sign bits
		 */
		if (backend->abi == ABI_POWER64
			&& !has_sign
			&& number > 0x80000000) {
			x_fprintf(out, "\tsldi %s, %s, 32\n",
				dest, dest);
			x_fprintf(out, "\tsrdi %s, %s, 32\n",
				dest, dest);
		}
	} else {
		x_fprintf(out, "\tli %s, %u\n", dest, number);
	}
	if (need_neg) {
		x_fprintf(out, "\tneg %s, %s\n", dest, dest);
	}	
}


#if 0
static void
load_symbol(struct reg *r, const char *name) {
	if (backend->abi == ABI_POWER64) {
		x_fprintf(out, "\tlis %s, %s@highesta\n", r->name, name);
		x_fprintf(out, "\tori %s, %s, %s@highera\n", r->name, r->name, name);
		x_fprintf(out, "\tsldi %s,%s,32\n", r->name, r->name);
		x_fprintf(out, "\toris %s, %s, %s@ha\n", r->name, r->name, name);
	} else {
		unimpl();
	}
}
#endif


/*
 * XXX _Toc_ vs _Toc. prefix is botched.. we should just pass a generic prefix
 * string
 */
static char *
fmttocbuf(const char *fmt, unsigned long number, int tocprefix) {
	size_t		len = strlen(fmt) + 20 + (tocprefix? sizeof "_Toc_.": 0);
	static char	buf[512];
	static char	*large;

	if (len > sizeof buf) {
		large = n_xrealloc(large, len);
		if (tocprefix) {
			strcpy(large, tocprefix == 2? "_Toc_.":  "_Toc_");
		} else {
			*large = 0;
		}
		sprintf(large+strlen(large), fmt, number);
		return large;
	} else {
		if (tocprefix) {
			strcpy(buf, tocprefix == 2? "_Toc_.":  "_Toc_");
		} else {
			*buf = 0;
		}
		sprintf(buf+strlen(buf), fmt, number);
		return buf;
	}
}

static void
build_toc_offset(struct reg *r, const char *name) {
	x_fprintf(out, "\tlis %s, (%s - .LCTOC1)@h\n", pic_reg->name, name);
	x_fprintf(out, "\tori %s, %s, (%s - .LCTOC1)@l\n", pic_reg->name, pic_reg->name, name);
	x_fprintf(out, "\tadd %s, %s, %s\n", r->name, r->name, pic_reg->name);
	x_fprintf(out, "\tld %s, 0(%s)\n", r->name, r->name);
}


static void
print_init_expr(struct type *dt, struct expr *ex) {
	struct tyval	*cv;
	int		is_addr_as_int = 0;

	cv = ex->const_value;

	/*
	 * 11/09/08: Don't align here - This is done by new_generic_print_
	 * init_list()_
	 */
#if   0 
	as_align_for_type(out, dt);
	x_fprintf(out, "\t");
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
#if 0
			x_fprintf(out, ".short 0x%x",
				*(unsigned short *)ex->
				const_value->value);
#endif
			x_fprintf(out, ".short ");
			cross_print_value_by_type(out,
				ex->const_value->value, TY_USHORT, 'x');	
			break;
		case TY_INT:
		case TY_ENUM:	
		case TY_UINT:
#if 0
			x_fprintf(out, ".long 0x%x",
				(unsigned)*(int *)ex->
				const_value->value);
#endif
			x_fprintf(out, ".long ");
			cross_print_value_by_type(out,
				ex->const_value->value, TY_UINT, 'x');	
			break;
#if 0
			x_fprintf(out, ".long 0x%x",
				*(unsigned int *)ex->
				const_value->value);
#endif
			break;
		case TY_LONG:
			if (backend->abi == ABI_POWER64) {
#if 0
				x_fprintf(out, ".llong 0x%lx",
					(unsigned long)*(long *)ex->
					const_value->value);
#endif
				x_fprintf(out, ".llong ");
				cross_print_value_by_type(out,
					ex->const_value->value, dt->code, 'x');
				
			} else {	
#if 0
				x_fprintf(out, ".long 0x%x",
					(unsigned)*(long *)ex->
					const_value->value);
#endif
				x_fprintf(out, ".long ");
				cross_print_value_by_type(out,
					ex->const_value->value, dt->code, 'x');
			}
			break;
		case TY_ULONG:
			if (backend->abi == ABI_POWER64) {
#if 0
				x_fprintf(out, ".llong 0x%x",
					(unsigned long)*(unsigned long *)ex->
					const_value->value);
#endif
				x_fprintf(out, ".llong ");
				cross_print_value_by_type(out,
					ex->const_value->value, dt->code, 'x');
			} else {	
#if 0
				x_fprintf(out, ".long 0x%x",
					(unsigned)*(unsigned long *)ex->
					const_value->value);
#endif
				x_fprintf(out, ".long ");
				cross_print_value_by_type(out,
					ex->const_value->value, dt->code, 'x');
			}	
			break;
		case TY_LLONG:
		case TY_ULLONG:	
#if 0
			x_fprintf(out, ".long 0x%x\n",
				*(unsigned int *)ex->
				const_value->value);
			x_fprintf(out, "\t.long 0x%x",
				((unsigned int *)ex->
				 const_value->value)[1]);
#endif
			x_fprintf(out, ".long ");
			cross_print_value_chunk(out,
				ex->const_value->value, dt->code,
				TY_UINT, 0, 0);
			x_fputc('\n', out);
			x_fprintf(out, "\t.long ");
			cross_print_value_chunk(out,
				ex->const_value->value, dt->code,
				TY_UINT, 0, 1);

			break;
		case TY_FLOAT:
#if 0
			x_fprintf(out, ".long 0x%x\n",
				*(unsigned int *)ex->
				const_value->value);
#endif
			x_fprintf(out, ".long ");
			cross_print_value_by_type(out,
				ex->const_value->value, TY_UINT, 'x');	
			break;
		case TY_DOUBLE:
		case TY_LDOUBLE:
#if 0
			x_fprintf(out, ".long 0x%x\n",
				*(unsigned int *)ex->
				const_value->value);
			x_fprintf(out, "\t.long 0x%x",
				((unsigned int *)ex->
				 const_value->value)[1]);
#endif


			/* 11/13/08: 128bit long double for Linux */
			if (sysflag != OS_AIX && dt->code == TY_LDOUBLE) {
				x_fprintf(out, ".long ");
				cross_print_value_chunk(out,
					ex->const_value->value, dt->code,
					TY_UINT, 0, 0);
				x_fputc('\n', out);
				x_fprintf(out, ".long ");
				cross_print_value_chunk(out,
					ex->const_value->value, dt->code,
					TY_UINT, 0, 1);
				x_fputc('\n', out);
				x_fprintf(out, ".long ");
				cross_print_value_chunk(out,
					ex->const_value->value, dt->code,
					TY_UINT, 0, 2);
				x_fputc('\n', out);
				x_fprintf(out, ".long ");
				cross_print_value_chunk(out,
					ex->const_value->value, dt->code,
					TY_UINT, 0, 3);
				x_fputc('\n', out);
			} else {
				x_fprintf(out, ".long ");
				cross_print_value_chunk(out,
					ex->const_value->value, dt->code,
					TY_UINT, 0, 0);
				x_fputc('\n', out);
				x_fprintf(out, "\t.long ");
				cross_print_value_chunk(out,
					ex->const_value->value, dt->code,
					TY_UINT, 0, 1);
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
			if (backend->abi == ABI_POWER64) {
				x_fprintf(out, ".llong ");
			} else {
				x_fprintf(out, ".long ");
			}	
			if (cv->is_nullptr_const) {
				x_fprintf(out, "0x0");
			} else if (cv->str) {
				x_fprintf(out, "_Str%lu_data", cv->str->count);
			} else if (cv->value) {
#if 0
				x_fprintf(out, "%lu",
					*(unsigned long *)cv->value);
#endif
				cross_print_value_by_type(out,
					cv->value, TY_ULONG, 'd');	
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
					/* 07/30/09: really_accessed() was missing */
					really_accessed(cv->address->dec);

					/* Static variable */
					x_fprintf(out, "%s%s%ld",
						cv->address->dec->dtype->name, sign,
						cv->address->diff);
				} else {
					/* Label */
					x_fprintf(out, ".%s%s%ld",
						cv->address->labelname,
						sign,
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
		} else { /* function */
			struct tyval	*cv = ex->const_value;
			x_fprintf(out, ".%s %s",
				backend->abi == ABI_POWER64? "llong": "long",
				cv->address->dec->dtype->name);
		}
	}
	x_fputc('\n', out);
}



static struct loaded_label {
	char			*name;
	struct loaded_label	*next;
} *loaded_labels_head = NULL,
  *loaded_labels_tail = NULL;


static void
emit_extern_decls(void) {
	struct sym_entry	*se;
	struct loaded_label	*ll;

#if 1 /*_AIX */ 
	for (se = extern_vars; se != NULL; se = se->next) {
		if (se->dec->has_symbol
			|| se->dec->dtype->is_def
			|| se->dec->has_def) {
			continue;
		}

		/*
		 * 12/24/08: Use real_referenecs instead of references. See
		 * comment on really_accessed()
		 */
		if (se->dec->real_references == 0
			&& (!se->dec->dtype->is_func
				|| !se->dec->dtype->is_def)) {
			/* Unneeded declaration */
			continue;
		}
		if (se->dec->invalid) {
			continue;
		}
		x_fprintf(out, "_Toc_%s:\n", se->dec->dtype->name);
		if (mintocflag) {
			x_fprintf(out, "\t.%s %s\n",
				backend->abi == ABI_POWER64? "quad": "long",
				se->dec->dtype->name);
		} else {
			if (sysflag == OS_AIX) {
				x_fprintf(out, "\t.tc %s[TC], %s[%s]\n",
					se->dec->dtype->name, se->dec->dtype->name,
						se->dec->dtype->is_func
						&& !se->dec->dtype->is_def?
						"DS": "RW");
			} else {
				x_fprintf(out, "\t.tc %s[TC], %s\n",
					se->dec->dtype->name, se->dec->dtype->name);
			}
		}
	}
#endif

	/*
	 * 12/25/08: Create entries for labels of which the address was taken
	 * (computed gotos)
	 */
	for (ll = loaded_labels_head; ll != NULL; ll = ll->next) {
		x_fprintf(out, "_Toc_.%s:\n", ll->name);
		if (mintocflag) {
			x_fprintf(out, "\t.%s .%s\n",
				backend->abi == ABI_POWER64? "quad": "long",
				ll->name);
		} else {
			if (sysflag == OS_AIX) {
				x_fprintf(out, "\t.tc .%s[TC], .%s[%s]\n",
					ll->name, ll->name, "RW");
			} else {
				x_fprintf(out, "\t.tc .%s[TC], .%s\n",
					ll->name, ll->name);
			}
		}
	}
}

static void
emit_global_extern_decls(struct decl **d, int ndecls) {
	/*
	 * 03/24/08: Like in MIPS I have to ask why was this case not
	 * handled before splitting into extenr/static global?!??!
 	 */
	(void) d; (void) ndecls;
}
	 

static void
emit_global_static_decls(struct decl **dv, int ndecls) {
	int		i;

	if (sysflag == OS_AIX) {
		x_fprintf(out, "\t.csect .text[PR]\n");
	} else {
		x_fprintf(out, "\t.section \".text\"\n");
	}

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
			x_fprintf(out, ".globl %s\n",
				dv[i]->dtype->name);
			if (tn != NULL) {
				tn->ptrarg = 1;
			}	
		}
	}
}


static void
emit_static_init_vars(struct decl *list) {
	struct decl	*d;

	if (list == NULL
		&& ((str_const == NULL && float_const == NULL && llong_const == NULL) || !mintocflag)) {
		return;
	}

	if (sysflag == OS_AIX) {
		x_fprintf(out, "\t.csect .data[RW], 3\n");
	} else {
		x_fprintf(out, "\t.section \".data\"\n");
	}

	x_fprintf(out, "\t.align 2\n");
	for (d = list; d != NULL; d = d->next) {
		if (d->invalid) {
			continue;
		}
		if (d->dtype->storage == TOK_KEY_STATIC
			&& d->references == 0) {
			/* 10/17/08: Addded for __func__ */
			continue;
		}
		data_segment_offset += generic_print_init_var(out,
			d, data_segment_offset, print_init_expr, 1);
	}

	emit_setsect(SECTION_TOC);

	for (d = list; d != NULL; d = d->next) {
		if (d->is_alias) continue;
		if (d->invalid) {
			/* 11/07/08: Missing */
			continue;
		}

		/*
		 * 12/24/08: Use real_referenecs instead of references. See
		 * comment on really_accessed()
		 */
		if (d->dtype->storage == TOK_KEY_STATIC
			&& d->real_references == 0) {
			/*
			 * 10/17/08: Added to avoid linker error
			 * when creating alias for __func__, but
			 * __func__ is not referenced and defined
			 */
			continue;
		}
		x_fprintf(out, "_Toc_%s:\n", d->dtype->name);
		if (mintocflag) {
			x_fprintf(out, "\t.%s %s\n",
				backend->abi == ABI_POWER64? "quad": "long",
				d->dtype->name);
		} else {
			x_fprintf(out, "\t.tc %s[TC], %s\n",
				d->dtype->name, d->dtype->name);
		}
	}

	if (mintocflag) {
		struct ty_string	*str;
		struct ty_float		*tf;
		struct ty_llong		*tll;
		struct init_with_name	*in;
		struct function		*func;

		/*
		 * 12/25/08: Handle all sorts of constants without a TOC.
		 * Strings and initializers are already defined (in text
		 * and data sections), whereas long integers and floating
		 * poiint values (which without -mminimal-toc were put
		 * into the TOC entries themselves) are defined right here
		 * in .toc1. That's what gcc does as well so it's probably
		 * OK
		 *
		 * We also define function entries here
		 */

		for (func = funclist; func != NULL; func = func->next) {
			char    *name = func->proto->dtype->name;
			int     suppress = 0;

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
				x_fprintf(out, "\t.%s %s\n",
					backend->abi == ABI_POWER64? "quad": "long",
					name);
			}
		}


		for (str = str_const; str != NULL; str = str->next) {
			x_fprintf(out, "_Str%lu:\n", str->count);
			x_fprintf(out, "\t.%s _Str%lu_data\n",
				backend->abi == ABI_POWER64? "quad": "long",
				str->count);
		}
		for (tf = float_const; tf != NULL; tf = tf->next) {
			x_fprintf(out, "_Float%lu_data:\n", tf->count);
			switch (tf->num->type) {
			case TY_FLOAT:
				if (backend->abi == ABI_POWER64) {
					x_fprintf(out, "\t.quad ");
				} else {
					x_fprintf(out, "\t.long ");
				}
				cross_print_value_by_type(out,
					tf->num->value, TY_UINT, 'x');
				if (backend->abi == ABI_POWER64) {
					/* Complete 64bit item */
					x_fprintf(out, "00000000");
				}
				break;
			case TY_DOUBLE:
				x_fprintf(out, "\t.long ");
				cross_print_value_chunk(out,
					tf->num->value, TY_DOUBLE,
					TY_UINT, 0, 0);
				x_fputc('\n', out);
				x_fprintf(out, "\t.long ");
				cross_print_value_chunk(out,
					tf->num->value, TY_DOUBLE,
					TY_UINT, 0, 1);
				x_fputc('\n', out);
				break;
			case TY_LDOUBLE:
				x_fprintf(out, "\t.long ");
				cross_print_value_chunk(out,
					tf->num->value, TY_LDOUBLE,
					TY_UINT, 0, 0);
				x_fputc('\n', out);
				x_fprintf(out, "\t.long ");
				cross_print_value_chunk(out,
					tf->num->value, TY_LDOUBLE,
					TY_UINT, 0, 1);
				x_fputc('\n', out);
				x_fprintf(out, "\t.long ");
				cross_print_value_chunk(out,
					tf->num->value, TY_LDOUBLE,
					TY_UINT, 0, 2);
				x_fputc('\n', out);
				x_fprintf(out, "\t.long ");
				cross_print_value_chunk(out,
					tf->num->value, TY_LDOUBLE,
					TY_UINT, 0, 3);
				x_fputc('\n', out);
			}
			x_fputc('\n', out);
			x_fprintf(out, "_Float%lu:\n", tf->count);
			x_fprintf(out, "\t.%s _Float%lu_data\n",
				backend->abi == ABI_POWER64? "quad": "long",
				tf->count);
		}
		for (tll = llong_const; tll != NULL; tll = tll->next) {
			x_fprintf(out, "_Llong%lu_data:\n",
				tll->count);
			x_fprintf(out, "\t.long ");
			cross_print_value_chunk(out,
				tll->num->value, TY_ULLONG,
				TY_UINT, 0, 0);
			x_fputc('\n', out);
			x_fprintf(out, "\t.long ");
			cross_print_value_chunk(out,
				tll->num->value, TY_ULLONG,
				TY_UINT, 0, 1);
			x_fputc('\n', out);


			x_fprintf(out, "_Llong%lu:\n",
				tll->count);
			x_fprintf(out, "\t.%s _Llong%lu_data\n",
				backend->abi == ABI_POWER64? "quad": "long", tll->count);
		}

		for (in = init_list_head; in != NULL; in = in->next) {
			x_fprintf(out, "%s:\n", in->name);
			x_fprintf(out, "\t.%s %s_data\n",
				backend->abi == ABI_POWER64? "quad": "long",
				in->name);
		}
	}
}

static void
emit_static_uninit_vars(struct decl *list) {
	struct decl	*d;
	size_t		size;

	if (list == NULL) {
		return;
	}
	for (d = list; d != NULL; d = d->next) {
		/*
		 * 12/24/08: Use real_referenecs instead of references. See
		 * comment on really_accessed()
		 */
		if (d->dtype->storage == TOK_KEY_STATIC
			&& d->real_references == 0) {
			continue;
		}
		if (d->invalid) {
			continue;
		}

		as_align_for_type(out, d->dtype, 0);
		size = backend->get_sizeof_decl(d, NULL);
		x_fprintf(out, "\t.%s ",
			d->dtype->storage == TOK_KEY_STATIC?
			"lcomm": "comm");
		if (sysflag == OS_AIX) {
			x_fprintf(out, "%s, %d, _%s.bss_\n",
				d->dtype->name, (int)size, tunit_name);
		} else {	
			x_fprintf(out, "%s, %d, %d\n",
				d->dtype->name,
				(int)size,
				(int)backend->get_align_type(d->dtype));
		}
	}


	emit_setsect(SECTION_TOC);
	for (d = list; d != NULL; d = d->next) {
		if (d->invalid || d->real_references == 0) {
			/* 11/07/08: Missing */
			/* 
			 * 12/24/08: Added references check to avoid linker
			 * error if a variable isn't defined
			 */
			continue;
		}
		x_fprintf(out, "_Toc_%s:\n", d->dtype->name);
		if (mintocflag) {
			x_fprintf(out, "\t.%s %s\n",
				backend->abi == ABI_POWER64? "quad": "long",
				d->dtype->name);
		} else {
			x_fprintf(out, "\t.tc %s[TC], %s\n",
				d->dtype->name, d->dtype->name);
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
	struct function	*func;
	size_t		size;
	int		i;

	(void) d; (void) size;

	bss_segment_offset = 0;
	x_fprintf(out, "\t.lcomm _Divbuf0_data, 8, _%s.bss_\n",
		tunit_name);
	x_fprintf(out, "\t.lcomm _Divbuf1_data, 8, _%s.bss_\n",
		tunit_name);
	if (static_uninit_vars != NULL) {
		for (d = static_uninit_vars; d != NULL; d = d->next) {
			/*
			 * 12/24/08: Use real_referenecs instead of references. See
			 * comment on really_accessed()
		 	 */
			if (d->dtype->storage == TOK_KEY_STATIC
				&& d->real_references == 0) {
				continue;
			}

			as_align_for_type(out, d->dtype);
			size = backend->get_sizeof_decl(d, NULL);
			x_fprintf(out, "\t.%s ",
				d->dtype->storage == TOK_KEY_STATIC?
				"lcomm": "comm");
			x_fprintf(out, "%s, %d, _%s.bss_\n",
				d->dtype->name, size, tunit_name);
		}
	}

	data_segment_offset = 0;
	if (static_init_vars != NULL) {
#if _AIX
		x_fprintf(out, "\t.csect .data[RW], 3\n");
#else
		x_fprintf(out, "\t.section \".data\"\n");
#endif
		x_fprintf(out, "\t.align 2\n");
	}
	for (d = static_init_vars; d != NULL; d = d->next) {
		data_segment_offset += generic_print_init_var(out,
			d, data_segment_offset, print_init_expr, 1);
	}

	emit_setsect(SECTION_TOC);

	if (mintocflag) {
		x_fprintf(out, "_Divbuf0:\n");
		x_fprintf(out, "\t.%s _Divbuf0_data\n",
			backend->abi == ABI_POWER64? "quad": "long");
		x_fprintf(out, "_Divbuf1:\n");
		x_fprintf(out, "\t.%s _Divbuf1_data\n",
			backend->abi == ABI_POWER64? "quad": "long");
	} else {
		x_fprintf(out, "_Divbuf0:\n");
		x_fprintf(out, "\t.tc _Divbuf0_data[TC], _Divbuf0_data\n");
		x_fprintf(out, "_Divbuf1:\n");
		x_fprintf(out, "\t.tc _Divbuf1_data[TC], _Divbuf1_data\n");
	}
	for (d = static_uninit_vars; d != NULL; d = d->next) {
		x_fprintf(out, "_Toc_%s:\n", d->dtype->name);
		if (mintocflag) {
			x_fprintf(out, "\t.%s %s\n",
				backend->abi == ABI_POWER64? "quad": "long",
				d->dtype->name);
		} else {
			x_fprintf(out, "\t.tc %s[TC], %s\n",
				d->dtype->name, d->dtype->name);
		}
	}

	if (!mintocflag) {
		for (func = funclist; func != NULL; func = func->next) {
			char	*fname = func->proto->dtype->name;

			x_fprintf(out, "_Toc_%s:\n", fname);
			x_fprintf(out, "\t.tc %s[TC], %s\n",
				fname, fname);
		}
	}
	for (d = static_init_vars; d != NULL; d = d->next) {
		if (d->is_alias) continue;
		x_fprintf(out, "_Toc_%s:\n", d->dtype->name);
		if (mintocflag) {
			x_fprintf(out, "\t.%s %s\n",
				backend->abi == ABI_POWER64? "quad": "long",
				d->dtype->name);
		} else {
			x_fprintf(out, "\t.tc %s[TC], %s\n",
				d->dtype->name, d->dtype->name);
		}
	}
}
#endif


static void
emit_struct_inits(struct init_with_name *list) {
	struct init_with_name	*in;

	if (list != NULL) {
		if (sysflag == OS_AIX) {
			x_fprintf(out, "\t.csect .data[RW]\n");
		} else {
			x_fprintf(out, "\t.section \".data\"\n");
		}

		for (in = list; in != NULL; in = in->next) {
			as_align_for_type(out, make_basic_type(TY_LONG), 0); /* XXX */
			x_fprintf(out, "%s_data:\n", in->name);
#if 0
			generic_print_init_list(out, in->dec,
				in->init, print_init_expr);
#endif
			/*
			 * 11/09/08: Use the new initializer printing function
			 * for correct alignment
			 */
			new_generic_print_init_list(out, in->dec, in->init, print_init_expr);
		}

		if (!mintocflag) {
			emit_setsect(SECTION_TOC);
			for (in = list; in != NULL; in = in->next) {
				x_fprintf(out, "%s:\n", in->name);
				if (mintocflag) {
					x_fprintf(out, "\t.%s %s\n",
						backend->abi == ABI_POWER64? "quad": "long",
						in->name);
				} else {
					x_fprintf(out, "\t.tc %s_data[TC], %s_data\n",
						 in->name, in->name);
				}
			}
		}
	}
}

static void
emit_llong_constants(struct ty_llong *list) {
	struct ty_llong		*tll;

	if (list != NULL && !mintocflag) {
		x_fprintf(out, "\t.align 3\n");
		emit_setsect(SECTION_TOC);
		for (tll = list; tll != NULL; tll = tll->next) {
#if 0
			/* this breaks for some reason :/ */
			if (!tll->loaded) {
				/* Not actually loaded */
				continue;
			}	
#endif
			x_fprintf(out, "_Llong%lu:\n",
				tll->count);
			x_fprintf(out, "\t.tc _Llong%lu_", tll->count);
			cross_print_value_by_type(out, tll->num->value,
				TY_ULLONG, 'x');
			x_fprintf(out, "[TC], ");
			cross_print_value_by_type(out, tll->num->value,
				TY_ULLONG, 'x');
			x_fputc('\n', out);
		}
	}
}





static void
emit_fp_constants(struct ty_float *list) {
	struct ty_float		*tf;
	if (/*float_const*/ list != NULL && !mintocflag) {
		x_fprintf(out, "\t.align 3\n");
		emit_setsect(SECTION_TOC);
		for (tf = list; tf != NULL; tf = tf->next) {
			/* XXX cross-compilation ... */
			x_fprintf(out, "_Float%lu:\n", tf->count);
			x_fprintf(out, "\t.tc _Float%lu_",
				tf->count);
			cross_print_value_by_type(out, tf->num->value,
				TY_UINT, 'x');
			x_fprintf(out, "[TC], ");

			if (backend->abi != ABI_POWER64) {
				cross_print_value_by_type(out, tf->num->value,
					TY_UINT, 'x');
				if (tf->num->type == TY_DOUBLE
					|| tf->num->type == TY_LDOUBLE) {
					x_fprintf(out, ", ");
					cross_print_value_chunk(out,
						tf->num->value,
						TY_DOUBLE, TY_UINT, 0, 1);
				} 
			} else {
				/*
				 * That
				 *    .tc blabla[TC], 0xstuff, 0xstuff
				 * stuff doesn't work in 64bit mode,
				 * because those two numbers are 64bit
				 * now instead of 32bit.
				 * So we print doubles/long doubles as
				 * a single 64bit number
				 */
				if (tf->num->type == TY_FLOAT) {
					cross_print_value_by_type(out,
						tf->num->value, TY_UINT, 'x');

					/*
					 * The number we just printed is a
					 * 32bit value, which without
					 * intervention would become the
					 * low word of a double word. But
					 * we want it to become the high
					 * one, so print 32 zero bits as low
					 * part!
					 */
					x_fprintf(out, "00000000");
				} else {
					if (sysflag != OS_AIX && tf->num->type == TY_LDOUBLE) {
						cross_print_value_chunk(out,
							tf->num->value,
							TY_LDOUBLE, TY_ULLONG, 0, 0);
						x_fprintf(out, ", ");
						cross_print_value_chunk(out,
							tf->num->value,
							TY_LDOUBLE, TY_ULLONG, 0, 1);
					} else {
						cross_print_value_by_type(out,
							tf->num->value, TY_ULLONG, 'x');
					}
				}
			}

			x_fputc('\n', out);
		}


	}
}



static int	need_conv_to_ldouble;
static int	need_conv_from_ldouble;


static void
emit_support_buffers(void) {
	static int	have_mask;
	struct decl	*d;

	if (have_mask) {
		return;
	}
	have_mask = 1;
	if (float_conv_mask.var_backed != NULL) {
		emit_setsect(SECTION_TOC);

		if (mintocflag) {
			x_fprintf(out, "%s_data:\n",
				float_conv_mask.var_backed->dtype->name);
			x_fprintf(out, "\t.long 0x43300000\n");
			x_fprintf(out, "\t.long 0x80000000\n");
			x_fprintf(out, "%s:\n",
				float_conv_mask.var_backed->dtype->name);
			x_fprintf(out, "\t.%s %s_data\n",
				backend->abi == ABI_POWER64? "quad": "long",
				float_conv_mask.var_backed->dtype->name);
		} else {
			x_fprintf(out, "\t.align 3\n");
			x_fprintf(out, "%s:\n",
				 float_conv_mask.var_backed->dtype->name);
			x_fprintf(out, "\t.tc %s_hmm[TC], ",
				float_conv_mask.var_backed->dtype->name);
			x_fprintf(out, "0x43300000, 0x80000000\n");
		}
	}
	if (sysflag == OS_AIX) {
		x_fprintf(out, "\t.lcomm _Divbuf0_data, 8, _%s.bss_\n",
			tunit_name);
		x_fprintf(out, "\t.lcomm _Divbuf1_data, 8, _%s.bss_\n",
			tunit_name);
	} else {
		x_fprintf(out, "\t.lcomm _Divbuf0_data, 8, 8\n");
		x_fprintf(out, "\t.lcomm _Divbuf1_data, 8, 8\n");
	}

	emit_setsect(SECTION_TOC);

	if (mintocflag) {
		x_fprintf(out, "_Divbuf0:\n");
		x_fprintf(out, "\t.%s _Divbuf0_data\n",
			backend->abi == ABI_POWER64? "quad": "long");
		x_fprintf(out, "_Divbuf1:\n");
		x_fprintf(out, "\t.%s _Divbuf1_data\n",
			backend->abi == ABI_POWER64? "quad": "long");
	} else {
		x_fprintf(out, "_Divbuf0:\n");
		x_fprintf(out, "\t.tc _Divbuf0_data[TC], _Divbuf0_data\n");
		x_fprintf(out, "_Divbuf1:\n");
		x_fprintf(out, "\t.tc _Divbuf1_data[TC], _Divbuf1_data\n");
	}

	/*
	 * Declare libnwcc references. We have to avoid clashes when the
	 * program explicitly or implicitly declared the functions itself;
	 * In that case there is already an extern declaration and re-
	 * declaring would cause linker errors
	 */
	if (need_conv_to_ldouble
		&& ((d = lookup_symbol(&global_scope, "__nwcc_conv_to_ldouble", 0)) == NULL
		/*	|| !d->has_symbol*/)) {
		x_fprintf(out, "_Toc___nwcc_conv_to_ldouble:\n");
		x_fprintf(out, "\t.tc __nwcc_conv_to_ldouble[TC], __nwcc_conv_to_ldouble\n");
	}
	if (need_conv_from_ldouble
		&& ((d = lookup_symbol(&global_scope, "__nwcc_conv_from_ldouble", 0)) == NULL
		/*	|| !d->has_symbol*/)) {
		x_fprintf(out, "_Toc___nwcc_conv_from_ldouble:\n");
		x_fprintf(out, "\t.tc __nwcc_conv_from_ldouble[TC], __nwcc_conv_from_ldouble\n");
	}
}	

static void
emit_strings(struct ty_string *list) {
	struct ty_string	*str;

	if (list != NULL) {
		if (sysflag == OS_AIX) {
			x_fprintf(out, "\t.csect text[PR]\n");
		} else {
			/*x_fprintf(out, "\t.section \".text\"\n");*/
			/*
			 * 10/28/08: We wrongly used section .text here; Must be
			 * .rodata or code instructions will be misaligned
			 */
			x_fprintf(out, "\t.section \".rodata\"\n");
		}

		for (str = list; str != NULL; str = str->next) {
			x_fprintf(out, "_Str%lu_data:\n", str->count);
			as_print_string_init(out, str->size, str);
		}

		if (!mintocflag) {
			/*
			 * 12/25/08: If we're using minimal TOC, then symbol
			 * declarations can only be performed when the indirect
			 * TOC is declared or we'll get linker errors
			 * (It's done in emit_static_init_vars() instead)
			 */
			emit_setsect(SECTION_TOC);
			for (str = list; str != NULL; str = str->next) {
				x_fprintf(out, "_Str%lu:\n", str->count);
				x_fprintf(out, "\t.tc _Str%lu_data[TC], _Str%lu_data\n",
					str->count, str->count);
			}
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
	if (sysflag == OS_AIX) {
		x_fprintf(out, "\tbl .%s\n", name);
	} else {
		x_fprintf(out, "\tbl %s\n", name);
	}

	x_fprintf(out, "\tnop\n");
}

static void
emit_callindir(struct reg *r) {
	if (backend->abi == ABI_POWER64) {
		x_fprintf(out, "\tstd 2, 40(1)\n");
		x_fprintf(out, "\tld 0, 0(%s)\n", r->name);
		x_fprintf(out, "\tmtctr 0\n");
		x_fprintf(out, "\tld 2, 8(%s)\n", r->name); 
		x_fprintf(out, "\tld 11, 16(%s)\n", r->name);
		x_fprintf(out, "\tbctrl\n");
		x_fprintf(out, "\tnop\n");
		x_fprintf(out, "\tld 2, 40(1)\n");
	} else {	
		x_fprintf(out, "\tstw 2, 20(1)\n");
		x_fprintf(out, "\tlwz 0, 0(%s)\n", r->name);
		x_fprintf(out, "\tmtctr 0\n");
		x_fprintf(out, "\tlwz 2, 4(%s)\n", r->name); 
		x_fprintf(out, "\tlwz 11, 8(%s)\n", r->name);
		x_fprintf(out, "\tbctrl\n");
		x_fprintf(out, "\tnop\n");
		x_fprintf(out, "\tlwz 2, 20(1)\n");
	}	
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
emit_allocstack(struct function *f, size_t nbytes) {
	(void) f;
	if (!f->gotframe) {
		/* Creating stack frame */
		if (nbytes > 32767) {
			/*
			 * More than 16 bits - need stwux
			 */
			unsigned int	ui = (unsigned)-nbytes;

			/* XXX 64bit. XXX cross-comp */
#if 0
			x_fprintf(out, "\tlis 0, 0x%x\n",
				((unsigned short *)&ui)[0]);
			x_fprintf(out, "\tori 0, 0, 0x%x\n",
				((unsigned short *)&ui)[1]);
#endif
			x_fprintf(out, "\tlis 0, ");
			cross_print_value_chunk(out, &ui, TY_UINT, TY_USHORT,
				0, 0);	
			x_fprintf(out, "\n\tori 0, 0, ");
			cross_print_value_chunk(out, &ui, TY_UINT, TY_USHORT,
				0, 1);
			x_fputc('\n', out);
			x_fprintf(out, "\t%s 1, 1, 0\n",
				backend->abi == ABI_POWER64? "stdux": "stwux");
		} else {
			x_fprintf(out, "\t%s 1, -%lu(1)\n",
				backend->abi == ABI_POWER64? "stdu": "stwu",
				(unsigned long)nbytes);	
		}
		x_fprintf(out, "\tmr 31, 1\n");
		f->gotframe = 1;
	} else {
		x_fprintf(out, "\tsi 1, 1, %lu\n",
			(unsigned long)nbytes);
	}
}


static void
do_add(const char *src, unsigned long off, const char *dest) {
	if (off >= 0x8000) {
		do_emit_li(tmpgpr2->name, off, 0);
		x_fprintf(out, "\tadd %s, %s, %s\n",
			dest, src, tmpgpr2->name);
	} else {
		x_fprintf(out, "\taddic %s, %s, %lu\n",
			dest, src, off);
	}
}

/*
 * Never use addi/subi! Those can't take a gpr0 source argument 
 */
static void
do_addi(struct reg *dest, struct reg *src, int val) {
	x_fprintf(out, "\taddic %s, %s, %d\n", dest->name, src->name, val);
}

static void
emit_freestack(struct function *f, size_t *nbytes) {
	char	*instr = "addic";

	if (nbytes == NULL) {
		/* Procedure outro */
		if (f->total_allocated != 0) {
			/* XXX never needed? */
#if 0
			x_fprintf(out, "\t%s 1, 1, %lu\n",
				instr, (unsigned long)f->total_allocated);
#endif
			x_fprintf(out, "\t%s 1, 0(1)\n",
				backend->abi == ABI_POWER64? "ld": "lwz");
		}	
	} else {
		if (*nbytes != 0) {
			x_fprintf(out, "\t%s 1, 1, %lu\n",
				instr, (unsigned long)*nbytes);	
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

static int	cursect = 0;

static void
emit_setsect(int value) {
	cursect = value;

	/*
	 * 12/25/08: OK with minimal TOC there's too much code now,
	 * so use emit_setsect(). For now only with TOC
	 */
	if (value == SECTION_TOC) {
		if (sysflag == OS_AIX) {
			x_fprintf(out, ".toc\n");
		} else {
			if (mintocflag) {
				x_fprintf(out, ".section \".toc1\",\"aw\"\n");
			} else {
				x_fprintf(out, ".section \".toc\",\"aw\"\n");
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
		unimpl();
	} else {
		x_fprintf(out, "%s", r->name);
	}
}



static void
emit_inc(struct icode_instr *ii) {
	if (ii->src_vreg->is_multi_reg_obj) {
		x_fprintf(out, "\taddic %s, %s, 1\n",
			ii->src_pregs[1]->name,
			ii->src_pregs[1]->name);
		x_fprintf(out, "\taddze %s, %s\n",
			ii->src_pregs[0]->name,
			ii->src_pregs[0]->name);
	} else {
		do_addi(ii->src_pregs[0], ii->src_pregs[0], 1);
	}
}

static void
emit_dec(struct icode_instr *ii) {
	if (ii->src_vreg->is_multi_reg_obj) {
		x_fprintf(out, "\tsubic %s, %s, 1\n",
			ii->src_pregs[1]->name,
			ii->src_pregs[1]->name);
		x_fprintf(out, "\taddme %s, %s\n",
			ii->src_pregs[0]->name,
			ii->src_pregs[0]->name);
	} else {
		x_fprintf(out, "\t%s %s, %s, 1\n",
			ii->src_vreg->size == 8? "subic": "subic",
			ii->src_pregs[0]->name, ii->src_pregs[0]->name);
	}
}


static void
emit_load(struct reg *r, struct vreg *vr) {
	char		*p = NULL;
	int		need_ext = 0;
	int		static_var = 0;
	int		for_struct = 0;
	int		is_bigstackoff = 0;
	static int	was_ldouble;
	static int	was_llong;
#define EXT_RLWINM	1
#define EXT_EXTSH	2
	struct vreg	*vr2 = NULL;
	int		static_float;
	int		is_string = 0;
	int		is_function = 0;


	if (vr == &float_conv_mask) {
		/* XXX what a terrible kludge */
		x_fprintf(out, "\tlfd %s, %s(2)\n",
			 r->name,
			 float_conv_mask.var_backed->dtype->name);
		return;
	}


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
			/*
			 * 12/21/08: This is not REALLY necessarily
			 * a static variable, so we have to mark it
			 * as a struct address load. The distinction
			 * is needed because we later have to avoid
			 * loading static variable TOC pointers to
			 * r0 because it cannot be used correctly in
			 * a displacement context. A non-static
			 * struct address does not suffer from that
			 * problem
			 */
			emit_addrof(tmpgpr, vr, vr2);
			static_var = 1;
			for_struct = 1;
		}
	}

	if (vr->var_backed
		&& vr->var_backed->stack_addr == NULL
		&& vr->parent == NULL) {

		if (vr->type->tlist == NULL
			|| (vr->type->tlist->type != TN_ARRAY_OF
			&& vr->type->tlist->type != TN_VARARRAY_OF
			&& vr->type->tlist->type != TN_FUNCTION)) {
			static_var = 1;
		} else if (vr->type->tlist != NULL
			&& vr->type->tlist->type == TN_FUNCTION) {
			is_function = 1;
		}
	}

	if (!static_var) {
		if (vr->stack_addr || vr->var_backed) {

			struct stack_block	*stack_addr = NULL;
			unsigned long		parent_offset = 0;

			if (vr->stack_addr != NULL) {
				stack_addr = vr->stack_addr;
			} else if (vr->var_backed->stack_addr != NULL) {
				stack_addr = vr->var_backed->stack_addr;
			} else {
				if (vr2 != NULL) {
					if (vr2->var_backed) {
						if (vr2->var_backed->stack_addr
							== NULL) {
							;
						} else {
							stack_addr = vr2->
								var_backed->stack_addr;
						}
					}
					parent_offset = calc_offsets(vr);
				}
			}
			if (stack_addr != NULL) {
				/*
				 * Stack address - if the offset is above 4095,
				 * we have to construct the stack address in a
				 * register and load indirectly
				 */
				int	offdiff = 0;

				if (was_ldouble) {
					offdiff = 8;
				} else if (was_llong && backend->abi == ABI_POWER32) {
					offdiff = 4;
				}
				stack_addr->offset += offdiff;

				if (do_stack(NULL, NULL, stack_addr, parent_offset) == -1) { 
					is_bigstackoff = 1;
					do_stack(out, tmpgpr2, stack_addr, parent_offset);
				}
				if (offdiff) {
					stack_addr->offset -= offdiff;
				}
			}
		}
	}

	if (r->type == REG_FPR) {
		/* No conversion between int/fp is handled here! */
		if (vr->size == 4) {
			/* p = "l.s"; */
			p = "lfs";
		} else if (vr->size == 8) {
			/* p = "l.d"; */
			p = "lfd";
		} else if (vr->size == 16) {
			/* long double */
			p = "lfd";
		} else {
			/* ?!?!? */
			unimpl();
		}
	} else {
		if (vr->type
			&& vr->type->tlist != NULL
			&& (vr->type->tlist->type == TN_ARRAY_OF
			|| vr->type->tlist->type == TN_VARARRAY_OF)) {
			if (vr->from_const != NULL) { 
				if (vr->from_const->type == TOK_STRING_LITERAL) {
					is_string = 1;
				}
				p = backend->abi == ABI_POWER64? "ld": "lwz";
			} else {
				emit_addrof(r, vr, vr2);
				return; 
			}
		} else if (vr->from_const) {
			if (IS_FLOATING(vr->type->code)) {
				puts("BUG? - load of fp value to gpr not handled");
			}
			if (IS_LLONG(vr->type->code)
				&& backend->abi != ABI_POWER64) {
				do_emit_li(r->name,
					((unsigned *)
					vr->from_const->data)[was_llong],
					0);
				if (was_llong) {
					was_llong = 0;
				} else if (vr->is_multi_reg_obj) {
					was_llong = 1;
				}
				return;
			} else if (IS_LLONG(vr->type->code)
				|| (IS_LONG(vr->type->code)
					&& backend->abi == ABI_POWER64)) {
				x_fprintf(out, "\tld %s, ", r->name);
				print_mem_operand(vr, NULL, vr2);
				x_fputc('\n', out);
				if (mintocflag) {
					/*
					 * 12/25/08: This only loaded the
					 * base TOC pointer.. We have to
					 * indirect more
					 */
					struct ty_llong	*tll = vr->from_const->data2;

					if (mintocflag == 2) {
						build_toc_offset(r, fmttocbuf("_Llong%lu", tll->count, 0));
					} else {
						x_fprintf(out, "\tld %s, _Llong%lu - .LCTOC1(%s)\n",
							r->name,
							tll->count,
							r->name);
					}
					x_fprintf(out, "\tld %s, 0(%s)\n",
						r->name, r->name);
				}
				return;
			} else {
				/*p = "li";*/
				/* XXXXXXXXXXXXXXX cross-compilation :-( */
				/*
				 * 11/26/08: This didn't take the possibility of
				 * short and char constants into account, so it
				 * loaded garbage values for them!
				 * Examples of small-typed constants are bitfield
				 * masks and things like ``(char)1''
				 */
				unsigned int	value;

				if (IS_CHAR(vr->type->code)) {
					value = *(unsigned char *)vr->from_const->data;
				} else if (IS_SHORT(vr->type->code)) {
					value = *(unsigned short *)vr->from_const->data;
				} else {
					value = *(unsigned int *)vr->from_const->data;
				}
				do_emit_li(r->name,
					value,
					vr->type->sign != TOK_KEY_UNSIGNED);
				return;
			}
		} else {
			if (vr->type == NULL) {
				if (vr->size == 8) {
					if (backend->abi == ABI_POWER64) {
						p = "ld";
					} else {
						p = "lwz";
					}
				} else {
					p = "lwz";
				}
			} else if (vr->type->tlist != NULL) {	
				if (backend->abi == ABI_POWER64) {
					p = "ld";
				} else {
					p = "lwz";
				}
			} else {
				if (IS_CHAR(vr->type->code)) {
					p = "lbz";
					if (vr->type->code == TY_SCHAR) {
						need_ext = EXT_RLWINM;
					}
				} else if (vr->type->code == TY_SHORT) {
					/*p = "lha";*/
					p = "lhz";
					need_ext = EXT_EXTSH;
				} else if (vr->type->code == TY_USHORT) {
					p = "lhz";
				} else if (vr->type->code == TY_INT
					|| vr->type->code == TY_ENUM) {
					/* p = "lwa"; */
					p = "lwz";
				} else if (vr->type->code == TY_UINT) {
					p = "lwz";
				} else if (vr->type->code == TY_LONG) {
					if (backend->abi == ABI_POWER64) {
						p = "ld";
					} else {
						p = "lwz";
					}
				} else if (vr->type->code == TY_ULONG) {
					if (backend->abi == ABI_POWER64) {
						p = "ld";
					} else {
						p = "lwz";
					}
				} else if (vr->type->code == TY_LLONG) {
					if (backend->abi == ABI_POWER64) {
						p = "ld";
					} else {
						p = "lwz";
					}
				} else if (vr->type->code == TY_ULLONG) {
					if (backend->abi == ABI_POWER64) {
						p = "ld";
					} else {
						p = "lwz";
					}
				} else if (vr->type->code == TY_FLOAT) {
					p = "lw";
				} else if (vr->type->code == TY_DOUBLE) {
					p = "ld";
				} else if (vr->type->code == TY_LDOUBLE) {
					unimpl();
				} else {
					printf("cannot load %d\n",
						vr->type->code);
					unimpl();
				}
			}	
		}	
	}

	/*
	 * For static structs and nonstatic scalars, this loads the
	 * scalar itself. For static scalars, this loads the TOC
	 * pointer
	 * XXX whew this is VERY confusing
	 *
	 * 12/21/08: Use tmpgpr2 instead of r0, because r0 in displacement
	 * context always means 0. Why was this never noticed before?
	 *
	 * 12/21/08: Use tmpgpr2 for static floats as well! If we're
	 * loading a static float, the load instruction will actually
	 * load the pointer to a GPR. However, we used to use ``r'' as
	 * a target register, which is an FPR, and which caused terrible
	 * things such as trashing GPR r1 to load the pointer (when FPR
	 * f1 was meant to be the target register)
	 *
	 * 12/25/08: Extended for floating point with mintocflag set
	 */
	if (r->type == REG_FPR && vr->from_const && mintocflag) {
		/*
		 * 12/25/08: We first have to load the indirect TOC
		 * pointer to a GPR, i.e. not the destination FPR!
		 */
		x_fprintf(out, "\t%s %s, ",
			backend->abi == ABI_POWER64? "ld": "lwz",
			tmpgpr->name);
	} else {
		static_float = r->type == REG_FPR && static_var && !for_struct;
		if (backend->abi == ABI_POWER64) {
			char	*extreg = (static_var && !for_struct)? tmpgpr2->name: power_gprs[0].name;
	
			x_fprintf(out, "\t%s %s, ",
				static_var && vr->parent == NULL? "ld": p,
				(need_ext || static_float)? /*power_gprs[0].name*/ /* tmpgpr2*/ extreg: r->name);
		} else {
			char	*extreg = (static_var && !for_struct)? tmpgpr2->name: power_gprs[0].name;

			x_fprintf(out, "\t%s %s, ",
				static_var && vr->parent == NULL? "lwz": p,
				(need_ext || static_float)? /*power_gprs[0].name*/ /*tmpgpr2*/ extreg: r->name);
		}
	}

	if (is_bigstackoff) {
		x_fprintf(out, "0(%s)", tmpgpr2->name);
	} else {
		print_mem_operand(vr, NULL, vr2);
	}
	x_fputc('\n', out);
	if (static_var && vr->parent == NULL) {
		/*
		 * Fetching scalar through TOC pointer in r
		 * 12/21/08: Use tmpgpr2 instead of r0
		 */
		if (mintocflag) {
			/*
			 * 12/25/08: Need another indirect load
			 * to get the pointer, which is not put
			 * directly into the TOC
			 */
			if (mintocflag == 2) {
				struct reg	*tmpr;

				tmpr = (need_ext || static_float)? tmpgpr2: r;
				build_toc_offset(tmpr, fmttocbuf(vr->var_backed->dtype->name, 0, 1));
			} else {
				x_fprintf(out, "\t%s %s, _Toc_%s - .LCTOC1(%s)\n",
					backend->abi == ABI_POWER64? "ld": "lwz",
					(need_ext || static_float)? tmpgpr2->name: r->name,
					vr->var_backed->dtype->name,
					(need_ext || static_float)? tmpgpr2->name: r->name);
			}
		}

		x_fprintf(out, "\t%s %s, %s(%s)\n",
			p, /*r->name,*/
			need_ext? power_gprs[0].name: r->name,
			was_llong? "4": "0",
			/*r->name*/ (need_ext || static_float)? tmpgpr2->name: r->name); 
		if (vr->is_multi_reg_obj) {
			if (was_llong) {
				was_llong= 0;
			} else {
				was_llong = 1;
			}
		}
	} else if (is_string && mintocflag) {
		/*
		 * 12/25/08: Need another indirect load
		 * to get the pointer, which is not put
		 * directly into the TOC
		 */
		struct ty_string	*ts = vr->from_const->data;

		if (mintocflag == 2) {
			build_toc_offset(r, fmttocbuf("_Str%lu", ts->count, 0));
		} else {
			x_fprintf(out, "\t%s %s, _Str%lu - .LCTOC1(%s)\n",
				backend->abi == ABI_POWER64? "ld": "lwz",
				r->name,
				ts->count,
				r->name);
		}
	} else if (r->type == REG_FPR && mintocflag && vr->from_const) {
		/*
		 * Need another indirect load to get the pointer,
		 * which is not put directly into the TOC
		 */
		struct ty_float		*tf = vr->from_const->data;

		if (mintocflag == 2) {
			build_toc_offset(tmpgpr, fmttocbuf("_Float%lu", tf->count, 0));
		} else {
			x_fprintf(out, "\t%s %s, _Float%lu - .LCTOC1(%s)\n",
				backend->abi == ABI_POWER64? "ld": "lwz",
				tmpgpr->name,
				tf->count,
				tmpgpr->name);
		}

		/*
		 * OK, now that the address is available, we can finally
		 * load the actual floating point value
		 */
		x_fprintf(out, "\t%s %s, %d(%s)\n",
			p, r->name,
			last_extra_offset_for_mem,
			tmpgpr->name); 
	} else if (is_function && mintocflag) {
		if (mintocflag == 2) {
			build_toc_offset(r, fmttocbuf(vr->var_backed->dtype->name, 0, 1));
		} else {
			x_fprintf(out, "\t%s %s, _Toc_%s - .LCTOC1(%s)\n",
				backend->abi == ABI_POWER64? "ld": "lwz",
				r->name,
				vr->var_backed->dtype->name,
				r->name);
		}
	}


	if (need_ext) {
		if (need_ext == EXT_EXTSH) {
			x_fprintf(out, "\textsh %s, %s\n",
				power_gprs[0].name, power_gprs[0].name);
		} else {
			/* rlwinm */
			#if 0
			x_fprintf(out, "\trlwinm %s, %s, %s, 0xff\n",
				power_gprs[0].name, power_gprs[0].name,
				power_gprs[0].name);
			#endif

			/*
			 * 12/20/08: Use extsb instead of rlwinm. Why
			 * was this not used before?! Not available in
			 * 32bit or POWER?!??!
			 */
			x_fprintf(out, "\textsb %s, %s\n",
				power_gprs[0].name, power_gprs[0].name);
		}
		x_fprintf(out, "\tmr %s, %s\n", r->name, power_gprs[0].name);
	}

	if (was_ldouble) {
		was_ldouble = 0;
	} else if (vr->type != NULL && vr->type->code == TY_LDOUBLE && vr->size == 16) {
		was_ldouble = 1;
	}
}

static void
emit_load_addrlabel(struct reg *t, struct icode_instr *ii) {
	struct loaded_label	*ll;

	if (mintocflag) {
		x_fprintf(out, "\t%s %s, .LCTOC0@toc(2)\n",
			backend->abi == ABI_POWER64? "ld": "lwz",
			t->name); 
		if (mintocflag == 2) {
			build_toc_offset(t, fmttocbuf(ii->dat, 0, 2));
		} else {
			x_fprintf(out, "\t%s %s, _Toc_.%s - .LCTOC1(%s)\n",
				backend->abi == ABI_POWER64? "ld": "lwz",
				t->name,
				ii->dat,
				t->name);
		}
	} else {
		x_fprintf(out, "\t%s %s, _Toc_.%s@toc(2)\n",
			backend->abi == ABI_POWER64? "ld": "lwz",
			t->name,
			ii->dat);
	}
	
	/*
	 * 12/25/08: Save label so we can later create a TOC table entry
	 * if needed. But check whether the label is already known
	 */
	for (ll = loaded_labels_head; ll != NULL; ll = ll->next) {
		if (strcmp(ll->name, ii->dat) == 0) {
			/* All done - already known label */
			return;
		}
	}

	/* OK, add label */
	ll = n_xmalloc(sizeof *ll);
	ll->name = ii->dat;
	ll->next = NULL;

	if (loaded_labels_head == NULL) {
		loaded_labels_head = loaded_labels_tail = ll;
	} else {
		loaded_labels_tail->next = ll;
		loaded_labels_tail = ll;
	}
}

static void
emit_comp_goto(struct reg *r) {
	x_fprintf(out, "\tmtctr %s\n", r->name);
	x_fprintf(out, "\tbctr\n");
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
	static int	was_llong;
	struct vreg	*vr2 = NULL;

	if (dest->var_backed
		&& dest->parent == NULL
		&& dest->var_backed->stack_addr == NULL) {
		is_static = 1;
		emit_addrof(tmpgpr, dest, NULL);
	}

	if (dest->parent) {
		vr2 = get_parent_struct(dest);
		if (vr2->stack_addr == NULL
			&& vr2->var_backed
			&& vr2->var_backed->dtype->storage
			!= TOK_KEY_AUTO
			&& vr2->var_backed->dtype->storage != 0) {
			emit_addrof(tmpgpr, dest, vr2);
			is_static = 1;
		}
	}

	if (!is_static) {
		if (dest->from_ptr != NULL) {
			/*
			 * 12/29/07: Added
			 */
			;
		} else {
			/* May be stack address */
			struct stack_block      *sb = NULL;
			unsigned long           parent_offset = 0;


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
	}

	if (src->pregs[0] && src->pregs[0]->type == REG_FPR) {	
		/* Performs no fp-int conversion! */
		if (src->type->code == TY_FLOAT) {
			p = "stfs";

			x_fprintf(out, "\tfrsp %s, %s\n",
				src->pregs[0]->name, src->pregs[0]->name);
		} else if (src->type->code == TY_DOUBLE) {
			p = "stfd";
		} else {
			p = "stfd";
		}

	} else {
		if (dest->type == NULL) {
			if (backend->abi == ABI_POWER64) {
				p = "std";
			} else {
				p = "stw";
			}	
		} else if (dest->type->tlist != NULL) {
			/* Must be pointer */
			if (backend->abi == ABI_POWER64) {
				p = "std";
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
				if (backend->abi == ABI_POWER64) {
					p = "std";
				} else {
					p = "stw";
				}
			} else if (IS_LLONG(dest->type->code)) {
				if (backend->abi == ABI_POWER64) {
					p = "std";
				} else {
					p = "stw";
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

	x_fprintf(out, "\t%s ", p);
	if (src->from_const) {
		print_mem_operand(src, NULL, NULL);
		x_fprintf(out, ", ");
	} else {
		/* Must be register */
		if (was_llong) {
			x_fprintf(out, "%s, ", src->pregs[1]->name);
			was_llong = 0;
		} else {
			x_fprintf(out, "%s, ", src->pregs[0]->name);
			if (src->is_multi_reg_obj) {
				was_llong = 1;
			}
		}
	}
	if (!is_static) {
#if 0
		struct stack_block	*sb;

		/*
		 * 11/02/07: Check if the stack offset needs more
		 * than 16 bits. In that case the address must be
		 * formed in a register because it is too big for
		 * immediate encoding
		 */
		if (dest->stack_addr
#endif
		if (!is_bigstackoff) {
			print_mem_operand(dest, NULL, vr2);
		} else {
			x_fprintf(out, "0(%s)", tmpgpr2->name);
		}
	} else {
		x_fprintf(out, "0(%s)", tmpgpr->name);
	}
	x_fputc('\n', out);
}


static void
emit_neg(struct reg **dest, struct icode_instr *src) {
	(void) src;
	if (dest[0]->type == REG_FPR) {
		x_fprintf(out, "\tfneg %s, %s\n",
			dest[0]->name, dest[0]->name);
	} else {
		x_fprintf(out, "\tneg %s, %s\n",
			dest[0]->name, dest[0]->name);
		if (dest[0]->size == 8 && src->src_vreg->size < 8) {
			/*
			 * 12/20/08: The result is a double word,
			 * but the item has a smaller type. This
			 * can mess up pointer arithmetic;
			 *
			 *   int i = -1;
			 *   buf[-i];
			 *
			 *... here the negation, which should yield
			 * 1, will still be negative
			 * XXX why!?!?
			 */
			if (src->src_vreg->size == 4) {
				x_fprintf(out, "\textsw %s, %s\n",
					dest[0]->name, dest[0]->name);
			} else if (src->src_vreg->size == 2) {
				x_fprintf(out, "\textsh %s, %s\n",
					dest[0]->name, dest[0]->name);
			} else {
				x_fprintf(out, "\textsb %s, %s\n",
					dest[0]->name, dest[0]->name);
			}
		}
	}	
}	


static void
emit_sub(struct reg **dest, struct icode_instr *src) {
	char	*p = NULL;

	if (dest[0]->type == REG_FPR) {
		if (src->src_vreg->type->code == TY_FLOAT) {
			p = "fsubs"; /*"fsub";*/
		} else if (src->src_vreg->type->code == TY_DOUBLE) {
			p = "fsub";
		} else {
			unimpl();
		}
		x_fprintf(out, "\t%s %s, %s, ",
			p, dest[0]->name, dest[0]->name); 
	} else {
		/* XXX take immediate into account */
		if (src->src_vreg->size == 8) {
			if (backend->abi != ABI_POWER64) {
				do_softarit_call(src, "__nwcc_sub",
					TOK_OP_MINUS, 0);
				return;
			} else {
				/*unimpl();*/
				p = "subf";
			}
		} else {
			/* XXX */
			p = "subfc";
		}
		
		x_fprintf(out, "\t%s %s, %s, %s\n",
			p, dest[0]->name,
			src->src_pregs[0]->name, dest[0]->name);
		return;
	}
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fputc('\n', out);
}

static void
emit_add(struct reg **dest, struct icode_instr *src) {
	char	*p = NULL;

	if (dest[0]->type == REG_FPR) {
		if (src->src_vreg->type->code == TY_FLOAT) {
			p = "fadds"; /*"fadd";*/
		} else if (src->src_vreg->type->code == TY_DOUBLE) {
			p = "fadd";
		} else {
			unimpl();
		}
		x_fprintf(out, "\t%s %s, %s, ",
			p, dest[0]->name, dest[0]->name);
	} else {
		/* XXX take immediate into account ... addi, etc */
		if (src->src_vreg->size == 8) {
			if (backend->abi != ABI_POWER64) {
				do_softarit_call(src, "__nwcc_add",
					TOK_OP_PLUS, 0);
				return;
			} else {
				/*unimpl();*/
				p = "add";
			}
		} else {
			p = "add";
		}
		x_fprintf(out, "\t%s %s, %s, ",
			p, dest[0]->name, dest[0]->name);
	}	
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fputc('\n', out);
}

/* Note that this is 32bit-only */
static void
do_softarit_call(struct icode_instr *src,
	const char *func, int op, int formod) {

	struct reg	**dest = src->dest_pregs;
	struct type	*ty = src->dest_vreg->type;

	/* Store data to div buffer 1 */
	if (mintocflag) {
		x_fprintf(out, "\tlwz %s, .LCTOC0@toc(2)\n",
			tmpgpr->name);
		if (mintocflag == 2) {
			build_toc_offset(tmpgpr, fmttocbuf("_Divbuf0", 0, 0));
		} else {
			x_fprintf(out, "\tlwz %s, _Divbuf0 - .LCTOC1(%s)\n",
				tmpgpr->name,
				tmpgpr->name);
		}
	} else {
		x_fprintf(out, "\tlwz %s, _Divbuf0(2)\n", tmpgpr->name);
	}
	x_fprintf(out, "\tstw %s, 0(%s)\n",
		dest[0]->name, tmpgpr->name);
	if (src->dest_vreg->is_multi_reg_obj) {
		x_fprintf(out, "\tstw %s, 4(%s)\n",
			dest[1]->name, tmpgpr->name);
	}

	if (op == TOK_OP_BSHL || op == TOK_OP_BSHR) {
		/* data, shiftbits, datasize, wrong_endianness */

		/* Load data buffer addresses */
		if (src->src_pregs[0] != &power_gprs[4]) {
			x_fprintf(out, "\tmr 4, %s\n",
				src->src_pregs[0]->name);
		}
		x_fprintf(out, "\tlwz 3, _Divbuf0(2)\n");
		x_fprintf(out, "\tli 5, %d\n",
			backend->get_sizeof_type(ty, NULL) * 8);
		x_fprintf(out, "\tli 6, 1\n");
		goto do_call;
	}

	/* Store data to div buffer 2 */
	if (mintocflag) {
		x_fprintf(out, "\tlwz %s, .LCTOC0@toc(2)\n",
			tmpgpr->name);
		if (mintocflag == 2) {
			build_toc_offset(tmpgpr, fmttocbuf("_Divbuf1", 0, 0));
		} else {
			x_fprintf(out, "\tlwz %s, _Divbuf1 - .LCTOC1(%s)\n",
				tmpgpr->name,
				tmpgpr->name);
		}
	} else {
		x_fprintf(out, "\tlwz %s, _Divbuf1(2)\n", tmpgpr->name);
	}
	x_fprintf(out, "\tstw %s, 0(%s)\n",
		src->src_pregs[0]->name, tmpgpr->name);
	if (src->src_vreg->is_multi_reg_obj) {
		x_fprintf(out, "\tstw %s, 4(%s)\n",
			src->src_pregs[1]->name, tmpgpr->name);
	}

	/* Load buffer addresses */
	x_fprintf(out, "\tlwz 3, _Divbuf0(2)\n");
	x_fprintf(out, "\tmr 4, %s\n", tmpgpr->name); 

	if (op == TOK_OP_DIVIDE) {
		/* Set want_remainder */
		x_fprintf(out, "\tli 5, %d\n", formod? 1: 0);
	}

	/* Set number of value bits */
	do_emit_li(op == TOK_OP_DIVIDE? "6": "5",
		backend->get_sizeof_type(ty, NULL) * 8, 0);
	if (op == TOK_OP_PLUS || op == TOK_OP_MINUS) {
		/* Has wrong endianness */
		do_emit_li("6", 1, 0);
		if (op == TOK_OP_MINUS) {
			/* maxbit = datasize.. maxbit unneeded?!? */
			do_emit_li("7", backend->get_sizeof_type(ty, NULL) * 8,
				0);
		}
	}

do_call:
	x_fprintf(out, "\tbl .%s\n", func);
	x_fprintf(out, "\tnop\n");

	/* Copy result from _Divbuf0 to result register */
	if (mintocflag) {
		x_fprintf(out, "\tlwz %s, .LCTOC0@toc(2)\n",
			tmpgpr->name);
		if (mintocflag == 2) {
			build_toc_offset(tmpgpr, fmttocbuf("_Divbuf0", 0, 0));
		} else {
			x_fprintf(out, "\tlwz %s, _Divbuf0 - .LCTOC1(%s)\n",
				tmpgpr->name,
				tmpgpr->name);
		}
	} else {
		x_fprintf(out, "\tlwz %s, _Divbuf0(2)\n", tmpgpr->name);
	}
	x_fprintf(out, "\tlwz %s, 0(%s)\n", dest[0]->name, tmpgpr->name);
	if (src->dest_vreg->is_multi_reg_obj) {
		x_fprintf(out, "\tlwz %s, 4(%s)\n", dest[1]->name,
			tmpgpr->name);
	}
}

static void
emit_div(struct reg **dest, struct icode_instr *src, int formod) {
	struct type	*ty = src->src_vreg->type;
	char		*p = NULL;

	(void) dest;

	if (IS_FLOATING(ty->code)) {
		if (ty->code == TY_FLOAT) {
			p = "fdivs"; /* "fdiv";*/
		} else if (ty->code == TY_DOUBLE) {
			p = "fdiv";
		} else {
			unimpl();
		}
		x_fprintf(out, "\t%s %s, %s, %s\n",
			p, dest[0]->name, dest[0]->name,
			src->src_pregs[0]->name);
		return;
	} else {
		int	is_64bit = 0;

		if ((IS_LLONG(ty->code) && backend->abi != ABI_POWER64)
			/*|| formod*/) {
			/* nwcc_(u)lldiv(dest, src, for_mod, data_size) */
			char	*func;

			if (ty->sign) {
				func = "__nwcc_lldiv";
			} else {
				func = "__nwcc_ulldiv";
			}
			do_softarit_call(src, func, TOK_OP_DIVIDE, formod);
		} else {
			char	*instr = NULL;

			if (ty->sign == TOK_KEY_UNSIGNED) {
				if (backend->abi != ABI_POWER64) {
					instr = "divwu";
				} else {
					if (src->src_vreg->size == 4) {
						instr = "divwu";
					} else {
						instr = "divdu";
						is_64bit = 1;
					}
				}
			} else {
				if (backend->abi != ABI_POWER64) {
					instr = "divw";
				} else {
					if (src->src_vreg->size == 4) {
						instr = "divw";
					} else {
						instr = "divd";
						is_64bit = 1;
					}
				}
			}
						
			if (formod) {
				/* Save destination */
				x_fprintf(out, "\tmr %s, %s\n",
					tmpgpr->name, dest[0]->name);
			}
			x_fprintf(out, "\t%s %s, %s, %s\n",
				instr,
				dest[0]->name, dest[0]->name, 
				src->src_pregs[0]->name);
			if (formod) {
				/* x - ((x / y) * y) is the remainder */
				x_fprintf(out, "\tmull%c %s, %s, %s\n",
					is_64bit? 'd': 'w',
					dest[0]->name,
					dest[0]->name,
					src->src_pregs[0]->name);
				x_fprintf(out, "\tsubfc %s, %s, %s\n",
					dest[0]->name, dest[0]->name, tmpgpr->name);
			}
		}
	}
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
			p = "fmuls"; /* "fmul";*/
		} else if (ty->code == TY_DOUBLE) {
			p = "fmul";
		} else {
			unimpl();
		}
		x_fprintf(out, "\t%s %s, %s, %s\n",
			p, dest[0]->name, dest[0]->name,
			src->src_pregs[0]->name);
		return;
	} else {
		char	*func;

		if (IS_LLONG(ty->code) && backend->abi != ABI_POWER64) {
			if (src->src_vreg->type->sign == TOK_KEY_UNSIGNED) {
				func = "__nwcc_ullmul";
			} else {
				func = "__nwcc_llmul";
			}
			do_softarit_call(src, func, TOK_OP_MULTI, 0);
		} else {
			char *instr = NULL;

			if ((IS_LONG(ty->code) && backend->abi != ABI_POWER64)
				|| IS_INT(ty->code)) {
				instr = "mullw";
			} else {
				instr = "mulld";
			}
			x_fprintf(out, "\t%s %s, %s, %s\n",
				instr, dest[0]->name, dest[0]->name,
				src->src_pregs[0]->name);
		}
	}
}

/* XXX slawi */
static void
emit_shl(struct reg **dest, struct icode_instr *src) {
	char	*p;
	(void) dest; (void) src;
	
	p = src->dest_vreg->size == 8? "sld": "slw";
	if (IS_LLONG(src->dest_vreg->type->code)
		&& backend->abi != ABI_POWER64) {
		do_softarit_call(src, "__nwcc_shift_left", TOK_OP_BSHL, 0);
		return;
	}
	if (src->src_vreg->from_const) {
		emit_load(tmpgpr, src->src_vreg);
	}
	x_fprintf(out, "\t%s %s, %s, ", p, dest[0]->name, dest[0]->name);
	if (src->src_vreg->from_const) {
		x_fprintf(out, "%s\n", tmpgpr->name);
	} else {
		x_fprintf(out, "%s\n", src->src_pregs[0]->name);
	}
}

/* XXX srawi ? */
static void
emit_shr(struct reg **dest, struct icode_instr *src) {
	char	*p;

	(void) dest; (void) src;

	/* 11/12/08: Distinguish arithmetic/logical shift */
	if (src->dest_vreg->type->sign == TOK_KEY_UNSIGNED) {
		p = src->dest_vreg->size == 8? "srd": "srw";
	} else {
		p = src->dest_vreg->size == 8? "srad": "sraw";
	}
	if (IS_LLONG(src->dest_vreg->type->code)
		&& backend->abi != ABI_POWER64) {
		do_softarit_call(src, "__nwcc_shift_right", TOK_OP_BSHR, 0);
		return;
	}
	if (src->src_vreg->from_const) {
		emit_load(tmpgpr, src->src_vreg);
	}
	x_fprintf(out, "\t%s %s, %s, ", 
		p, dest[0]->name, dest[0]->name);
	if (src->src_vreg->from_const) {
		x_fprintf(out, "%s\n", tmpgpr->name);
	} else {	
		x_fprintf(out, "%s\n", src->src_pregs[0]->name);
	}
}

static void
emit_or(struct reg **dest, struct icode_instr *src) {
	char	*p;

	if (src->dest_vreg->size == 8) {
		if (IS_LLONG(src->src_vreg->type->code)
			&& backend->abi != ABI_POWER64) {
			x_fprintf(out, "\tor %s, %s, %s\n",
				dest[0]->name, dest[0]->name,
				src->src_pregs[0]->name);
			x_fprintf(out, "\tor %s, %s, %s\n",
				dest[1]->name, dest[1]->name,
				src->src_pregs[1]->name);
			return;
		} else {
			p = "or";
		}
	} else {
		p = "or";
	}	
	x_fprintf(out, "\t%s %s, %s, %s\n",
		p, dest[0]->name, dest[0]->name, src->src_pregs[0]->name);
}

static void
emit_preg_or(struct reg **dest, struct icode_instr *src) {
	x_fprintf(out, "\tor %s, %s, %s\n", dest[0]->name, dest[0]->name,
		src->src_pregs[0]->name);
}


static void
emit_and(struct reg **dest, struct icode_instr *src) {
	char	*p;

	if (src->dest_vreg->size == 8) {
		if (IS_LLONG(src->dest_vreg->type->code)
			&& backend->abi != ABI_POWER64) {
			x_fprintf(out, "\tand %s, %s, %s\n",
				dest[0]->name, dest[0]->name,
				src->src_vreg?
					 src->src_pregs[0]->name:
					"0");
			x_fprintf(out, "\tand %s, %s, %s\n",
				dest[1]->name, dest[1]->name,
				src->src_vreg?
					src->src_pregs[1]->name:
					"0");
			return;
		} else {
			p = "and";
		}
	} else {
		p = "and";
	}	
	x_fprintf(out, "\t%s %s, %s, %s\n",
		p, dest[0]->name, dest[0]->name,
		src->src_vreg? src->src_pregs[0]->name: "0");
}

static void
emit_xor(struct reg **dest, struct icode_instr *src) {
	if (IS_LLONG(src->src_vreg->type->code)
		&& backend->abi != ABI_POWER64) {
		x_fprintf(out, "\txor %s, %s, %s\n",
			dest[0]->name, dest[0]->name,
			src->src_pregs[0]->name);
		x_fprintf(out, "\txor %s, %s, %s\n",
			dest[1]->name, dest[1]->name,
			src->src_pregs[1]->name);
		return;
	}
	x_fprintf(out, "\txor %s, %s, ", 
		dest[0]->name, dest[0]->name);
	if (src->src_vreg == NULL) {
		x_fprintf(out, "%s\n", dest[0]->name);
	} else {	
		print_mem_or_reg(src->src_pregs[0], src->src_vreg);
		x_fputc('\n', out);
	}
}

/*
 * XXX long long support!!!! 
 */
static void
emit_not(struct reg **dest, struct icode_instr *src) {
	char	*p;

	/*
	 * NEG means flip all bits, then add one. NOT means flip
	 * all bits. As there does not seem to be a NOT instruction,
	 * let's use NEG, then SUB 1
	 */
	(void) src;
	x_fprintf(out, "\tneg %s, %s\n", dest[0]->name, dest[0]->name);
	p = "subic";
	x_fprintf(out, "\t%s %s, %s, 1\n",
		p, dest[0]->name, dest[0]->name);	
}	

static void
emit_ret(struct icode_instr *ii) {
	(void) ii;

	if (backend->abi == ABI_POWER64) {
		x_fprintf(out, "\tld 0, 16(1)\n"); /*XXX correct? */
	} else {
		x_fprintf(out, "\tlwz 0, 8(1)\n");
	}	
	x_fprintf(out, "\tmtlr 0\n");
/*	x_fprintf(out, "\tlwz 31, -4(1)\n");*/
	x_fprintf(out, "\tblr\n");
}


static void
emit_extend_sign(struct icode_instr *ii) {
        struct extendsign       *ext = ii->dat;
        struct reg              *r = ext->dest_preg;
        struct type             *to = ext->dest_type;
        struct type             *from = ext->src_type;
        int                     shift_bits = 0;
	int			dest_size;


	if (backend->abi == ABI_POWER64) {
		dest_size = 64;
	} else {
		dest_size = 32;
	}


        if (IS_SHORT(to->code)) {
                /* Source must be char */
                shift_bits = 8;
        } else if (IS_INT(to->code)) {
                if (IS_CHAR(from->code)) {
                        /*shift_bits = 24*/
			shift_bits = dest_size - 8;
                } else {
                        /* Must be short */
                        /*shift_bits =16*/
			shift_bits = dest_size - 16;
                }
        } else if (IS_LONG(to->code)) {
                if (IS_CHAR(from->code)) {
                        shift_bits = dest_size - 8;
                } else if (IS_SHORT(from->code)) {
                        shift_bits = dest_size - 16;
                } else {
                        /* Must be int */
                        shift_bits = dest_size - 32;
                }
        } else if (IS_LLONG(to->code)) {
                if (backend->abi == ABI_POWER32
                        || backend->abi == ABI_POWER64) {
                        if (IS_CHAR(from->code)) {
                                shift_bits = 56;
                        } else if (IS_SHORT(from->code)) {
                                shift_bits = 48;
                        } else if (IS_INT(from->code)) {
                                shift_bits = 32;
                        } else {
                                /* Must be long */
                                if (backend->abi == ABI_POWER32) {
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

#if 0
        if (backend->abi != ABI_POWER64) {
                /* sllx/srax below cannot be used */
                unimpl();
        }
#endif

	do_emit_li(tmpgpr->name, shift_bits, 0);
	x_fprintf(out, "\t%s %s, %s, %s\n",
		backend->abi == ABI_POWER32? "slw": "sld",
		r->name, r->name, tmpgpr->name);
	x_fprintf(out, "\t%s %s, %s, %s\n", 
		backend->abi == ABI_POWER32? "sraw": "srad",
		r->name, r->name, tmpgpr->name);
#if 0
        x_fprintf(out, "\tmov %d, %%%s\n", shift_bits, tmpgpr->name);
        x_fprintf(out, "\tsll%s %%%s, %%%s, %%%s\n",
                to_size == 8? "x": "",
                r->name,
                tmpgpr->name, r->name);
        x_fprintf(out, "\tsra%s %%%s, %%%s, %%%s\n",
                to_size == 8? "x": "",
                r->name, tmpgpr->name, r->name);
#endif
}


static void
emit_conv_from_ldouble(struct icode_instr *ii) {
	do_stack(out, &power_gprs[3], ii->dest_vreg->var_backed->stack_addr, 0);
	do_stack(out, &power_gprs[4], ii->src_vreg->var_backed->stack_addr, 0);
	x_fprintf(out, "\tbl __nwcc_conv_from_ldouble\n");
	x_fprintf(out, "\tnop\n");
	need_conv_from_ldouble = 1;
}


static void
emit_conv_to_ldouble(struct icode_instr *ii) {
	do_stack(out, &power_gprs[3], ii->dest_vreg->var_backed->stack_addr, 0);
	do_stack(out, &power_gprs[4], ii->src_vreg->var_backed->stack_addr, 0);
	x_fprintf(out, "\tbl __nwcc_conv_to_ldouble\n");
	x_fprintf(out, "\tnop\n");
	need_conv_to_ldouble = 1;
}




static struct icode_instr	*last_cmp_instr;


static void
emit_cmp(struct reg **dest, struct icode_instr *src) {
	last_cmp_instr = src;

	(void) src; (void) dest;
}

static void
emit_branch(struct icode_instr *ii) {
	char			*lname;
	struct vreg		*src_vreg = NULL;
	struct reg		**src_pregs = NULL;
	struct reg		**dest_pregs = NULL;
	struct reg		*src_preg;
	struct type		*cmpty = NULL;
	int			is_signed = 0;
	char			kname[256];
	static int		was_llong;
	static unsigned long	kludge;
	int			needkludge = 0;
	unsigned long		label_append_seqno;
	unsigned long		branch_append_seqno;
	unsigned long		append_seqno_diff;

	if (last_cmp_instr) {
		/* Not unconditional jump - get comparison */
		src_vreg = last_cmp_instr->src_vreg;
		src_pregs = last_cmp_instr->src_pregs;
		dest_pregs = last_cmp_instr->dest_pregs;
		cmpty = last_cmp_instr->dest_vreg->type;

#if 0 
		is_signed =
			last_cmp_instr->dest_vreg->type->sign
				!= TOK_KEY_UNSIGNED;
#endif

		if (last_cmp_instr->hints & HINT_INSTR_UNSIGNED) {
			/* 
		 	 * 01/29/09: Override vreg types and specifically perform
		 	 * an unsigned comparison
			 */
			is_signed = 0;
		} else {
			/*
			 * 06/29/08: This was missing the check whether we
			 * are comparing pointers, and thus need unsigned
			 * comparison
			 */
			is_signed = cmpty->sign != TOK_KEY_UNSIGNED
				&& cmpty->tlist == NULL;
		}
	}

	if (src_pregs == NULL || src_vreg == NULL) {
		/* Compare with zero. XXX not for fp!!! */
		x_fprintf(out, "\tli %s, 0\n", power_gprs[0].name);
		src_preg = &power_gprs[0];
	} else {
		src_preg = src_pregs[0];
	}

	lname = ((struct icode_instr *)ii->dat)->dat;

	if (ii->type != INSTR_JUMP) {
		char	*opcode;

		if (src_preg->type == REG_FPR) {
			opcode = "fcmpu";
		} else {
			/*
			 * 06/29/08: This was missing pointer 64bithandling */
			if (backend->abi == ABI_POWER64
				&& cmpty != NULL
				&& (IS_LONG(cmpty->code)
				|| IS_LLONG(cmpty->code)
				|| cmpty->tlist != NULL)) {
				if (is_signed) {
					opcode = "cmpd";
				} else {
					opcode = "cmpld";
				}
			} else {
				if (is_signed) {
					opcode = "cmpw";
				} else {
					opcode = "cmplw";
				}
			}
		}	
#if 0
		x_fprintf(out, "\t%s 7, %s, %s\n",
			src_preg->type == REG_FPR? "fcmpu": "cmpw",
			dest_pregs[0]->name, src_preg->name);
#endif

		x_fprintf(out, "\t%s 7, %s, %s\n",
			opcode, dest_pregs[was_llong]->name,
			src_preg == &power_gprs[0]? src_preg->name:
			src_pregs[was_llong]->name);	
		if (was_llong) {
			was_llong = 0;
		} else if (last_cmp_instr->dest_vreg->is_multi_reg_obj) {
			if (!(last_cmp_instr->hints &
				HINT_INSTR_NEXT_NOT_SECOND_LLONG_WORD)) {
				/*
				 * 06/02/08: Conditional for repeated
				 * cmps on same dword
				 */
				was_llong = 1;
			}
		}
	}

	/*
	 * XXX Most branch instructions can only deal with 16bit
	 * (or something) offsets. So in order to avoid these
	 * problems, we use ``b'' instead, which can address
	 * 24 or so. We should count the instructions between
	 * branch and target label, at least heuristically, to
	 * use normal branches for short distances in the
	 * future
	 *
	 * 01/18/08: We now do implement this heuristic. append_icode_list()
	 * provides an ``append sequence number'' which tells us an rough
	 * estimate of the number of instructions between branch instruction
	 * and target label. This is imperfect for reasons outlined in the
	 * declaration in icode.h
	 */
	label_append_seqno = ((struct icode_instr *)ii->dat)->append_seqno;
	branch_append_seqno = ii->append_seqno;
	if (label_append_seqno > branch_append_seqno) {
		append_seqno_diff = label_append_seqno - branch_append_seqno;
	} else {
		append_seqno_diff = branch_append_seqno - label_append_seqno;
	}

	/*
	 * 01/18/09: Assume each icode instruction takes up 10 real
	 * instructions. This is to compensate synthetic icode instructions
	 * which may expand to multiple real instructions (e.g. COPYSTRUCT,
	 * COPYINIT, etc). This does NOT handle inline asm statements which
	 * may grow ``infinitely'' large. So this heuristic will not be
	 * able to handle HUGE inline asm statements but it is better than
	 * nothing
	 */
	if (append_seqno_diff * 10 >= 65535) {
		needkludge = 1;
	} else {
		needkludge = 0;
	}

	if (needkludge) {
		sprintf(kname, ".kludge%lu", kludge++);
	} else {
		*kname = 0;
	}
	switch (ii->type) {
	case INSTR_JUMP:
		x_fprintf(out, "\tb .%s\n", lname);
		break;
	case INSTR_BR_EQUAL:
		if (!needkludge) {
			x_fprintf(out, "\tbeq 7, .%s\n",
				lname);
		} else {
			x_fprintf(out, "\tbne 7, %s\n", kname);
		}
		break;
	case INSTR_BR_SMALLER:
		if (!needkludge) {
			x_fprintf(out, "\tblt 7, .%s\n",
				lname);
		} else {
			x_fprintf(out, "\tbge 7, %s\n", kname);
		}
		break;
	case INSTR_BR_SMALLEREQ:
		if (!needkludge) {
			x_fprintf(out, "\tble 7, .%s\n",
				lname);
		} else {
			x_fprintf(out, "\tbgt 7, %s\n", kname);
		}
		break;
	case INSTR_BR_GREATER:
		if (!needkludge) {
			x_fprintf(out, "\tbgt 7, .%s\n",
				lname);
		} else {
			x_fprintf(out, "\tble 7, %s\n", kname);
		}
		break;
	case INSTR_BR_GREATEREQ:
		if (!needkludge) {
			x_fprintf(out, "\tbge 7, .%s\n",
				lname);
		} else {
			x_fprintf(out, "\tblt 7, %s\n", kname);
		}
		break;
	case INSTR_BR_NEQUAL:
		if (!needkludge) {
			x_fprintf(out, "\tbne 7, .%s\n",
				lname);
		} else {
			x_fprintf(out, "\tbeq 7, %s\n", kname);
		}
		break;
	}
	if (needkludge) {
		if (ii->type != INSTR_JUMP) {
			x_fprintf(out, "\tnop\n");
			x_fprintf(out, "\tb .%s\n", lname);
			x_fprintf(out, "%s:\n", kname);
		}
	}
	last_cmp_instr = NULL; /* XXX hmm ok? */
	x_fprintf(out, "\tnop\n"); /* fill branch delay slot */
}


static void
emit_mov(struct copyreg *cr) {
	struct reg	*dest = cr->dest_preg;
	struct reg	*src = cr->src_preg;

	if (src == NULL) {
		/* Move null to register */
		x_fprintf(out, "\tmr %s, $0\n", /* XXX !? */
			dest->name);
	} else {
		if (dest->type != src->type) {
			/* Must be fpr vs gpr */
			unimpl();
#if 0
			/* XXX FP use fctiw!! */
#endif
		} else if (dest->type == REG_FPR) {
			x_fprintf(out, "\tfmr %s, %s\n",
				dest->name, src->name);
		} else {
			x_fprintf(out, "\tmr %s, %s\n",
				dest->name, src->name);
		}
	}	
}


static void
emit_setreg(struct reg *dest, int *value) {
	/* XXX cross-comp */
	do_emit_li(dest->name, *(unsigned *)value, 0); /* XXX 1 */
}

static void
emit_xchg(struct reg *r1, struct reg *r2) {
	(void) r1; (void) r2;
	unimpl();
}


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

	if (src && src->parent != NULL) {
		/* Structure or union type */
		if (d != NULL) {
			if (d->stack_addr != NULL) {
				do_stack(out, dest, d->stack_addr, calc_offsets(src));
			} else {
				/* Static */
				really_accessed(d);

				if (mintocflag) {
					assert(strcmp(dest->name, "0") != 0); /* Don't allow r0 */
					x_fprintf(out, "\t%s %s, .LCTOC0@toc(2)\n",
						backend->abi == ABI_POWER64? "ld": "lwz",	
						dest->name);
					if (mintocflag == 2) {
						build_toc_offset(dest, fmttocbuf(d->dtype->name, 0, 1));
					} else {
						x_fprintf(out, "\t%s %s, _Toc_%s - .LCTOC1(%s)\n",
							backend->abi == ABI_POWER64? "ld": "lwz",	
							dest->name,
							d->dtype->name, dest->name);
					}
				} else {
					x_fprintf(out, "\t%s %s, _Toc_%s(2)\n",
						backend->abi == ABI_POWER64? "ld": "lwz",	
						dest->name, d->dtype->name);
				}
#if 0
				do_addi(dest, dest, calc_offsets(src));
#endif
				do_add(dest->name, calc_offsets(src), dest->name);
			}	
		} else if (structtop->from_ptr) {
#if 0
			x_fprintf(out, "\taddi %s, %s, %lu\n",
				dest->name,
				structtop->from_ptr->pregs[0]->name,
				calc_offsets(src));
#endif
#if 0
			do_addi(dest, structtop->from_ptr->pregs[0],
				calc_offsets(src));
#endif
			do_add(structtop->from_ptr->pregs[0]->name, calc_offsets(src), dest->name);
		} else {
			printf("hm attempt to take address of %s\n",
				src->type->name);	
			unimpl();
		}	
	} else if (src && src->from_ptr) {	
		if (dest != src->from_ptr->pregs[0]) {
			x_fprintf(out, "\tmr %s, %s\n",
				dest->name, src->from_ptr->pregs[0]->name);
		}
	} else if (src && src->from_const && src->from_const->type == TOK_STRING_LITERAL) {
		/* 08/21/08: This was missing! */
		emit_load(dest, src);
	} else {		
		if (d && d->stack_addr) {
			/* src = NULL means start of variadic */
#if 0
			x_fprintf(out, "\taddi %s, %s, %ld\n",
				dest->name,
				d->stack_addr->use_frame_pointer? "31": "1",
				offset);
#endif
			do_stack(out, dest, d->stack_addr, 0);
		} else if (d) {
			/*
			 * Must be static variable - symbol itself is
			 * address
			 */
			char	*prefix;

			really_accessed(d);

			if (mintocflag) {
				assert(strcmp(dest->name, "0") != 0); /* Don't allow r0 */
				x_fprintf(out, "\t%s %s, .LCTOC0@toc(2)\n",
					backend->abi == ABI_POWER64? "ld": "lwz",	
					dest->name);
				if (mintocflag == 2) {
					build_toc_offset(dest, fmttocbuf(d->dtype->name, 0, 1));
				} else {
					x_fprintf(out, "\t%s %s, _Toc_%s - .LCTOC1(%s)\n",
						backend->abi == ABI_POWER64? "ld": "lwz",	
						dest->name,
						d->dtype->name, dest->name);
				}
			} else {
#if 1 /*_AIX*/
				prefix = "_Toc_";
#endif
				x_fprintf(out, "\t%s %s, %s%s(2)\n",
					backend->abi == ABI_POWER64? "ld": "lwz",	
					dest->name,
	#if 0
				d->dtype->tlist && d->dtype->tlist->
				type == TN_FUNCTION? "_Toc_": "_Toc_",
	#endif
					prefix,
					d->dtype->name);
			}
		} else if (src && src->stack_addr) {
			/* Anonymous item */
#if 0
			x_fprintf(out, "\taddi %s, %s, %lu\n",
				dest->name,
				src->stack_addr->use_frame_pointer?
				"31": "1",
				(unsigned long)src->stack_addr->offset);
#endif
			do_stack(out, dest, src->stack_addr, 0);
		} else {
			unimpl(); /* ??? */
		}
	}	

	if (src != NULL && src->addr_offset != 0) {
#if 0
		x_fprintf(out, "\taddi %s, %s, %lu\n",
			dest->name, dest->name, (unsigned long)src->addr_offset);
#endif
		do_add(dest->name, src->addr_offset, dest->name);
	}
}


/*
 * Copy initializer to automatic variable of aggregate type
 */
static void
emit_copyinit(struct decl *d) {
	x_fprintf(out, "\taddi 3, 31, %lu\n", d->stack_addr->offset);

	if (mintocflag) {
		char	*isn = backend->abi == ABI_POWER64? "ld": "lwz";

		/* 12/25/08: Handle mintoc */
		x_fprintf(out, "\t%s 4, .LCTOC0@toc(2)\n", isn);
		if (mintocflag == 2) {
			build_toc_offset(&power_gprs[4], fmttocbuf(d->init_name->name, 0, 0));
		} else {
			x_fprintf(out, "\t%s 4, %s - .LCTOC1(4)\n",
				isn, d->init_name->name);
		}
	} else {
		x_fprintf(out, "\t%s 4, %s(2)\n",
			backend->abi == ABI_POWER64? "ld": "lwz",
			d->init_name->name);
	}
	/* XXX dli?? */
	do_emit_li("5", backend->get_sizeof_type(d->dtype, NULL), 0);
	x_fprintf(out, "\tbl .memcpy\n");
	x_fprintf(out, "\tnop\n");
}

static void
emit_putstructregs(struct putstructregs *ps) {
	struct reg	*destreg = ps->destreg;
	struct reg	*ptrreg = ps->ptrreg;
	struct vreg	*vr = ps->src_vreg;

	/*
	 * 11/06/08: Load first pieces into registers
	 */
	if (1 /*backend->abi == ABI_POWER64*/) {
		int		size = vr->size;
		int		offset = 0;


		while (destreg != &power_gprs[3+8]) {
			if (size <= 0) {
				/* We're already done! */
				return;
			}
			/* 
			 * Use 3 as temp reg because it is not
			 * loaded with the target address yet
			 */
			x_fprintf(out, "\t%s %s, %d(%s)\n",
				backend->abi == ABI_POWER64? "ld": "lwz",
				destreg->name, offset, ptrreg->name);
			size -= power_gprs[0].size;
			offset += power_gprs[0].size;
			++destreg;
		}
	} else {
		unimpl();
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
	do_emit_li("5", (unsigned)cs->src_vreg->size, 0);
	if (cs->src_from_ptr == NULL) {
		if (cs->src_vreg->parent) {
			stop = get_parent_struct(cs->src_vreg);
		} else {
			stop = NULL;
		}	
		emit_addrof(tmpgpr, cs->src_vreg, stop); 
		x_fprintf(out, "\tmr 4, %s\n", tmpgpr->name);
	} else {
		if (cs->src_vreg->parent) {
			x_fprintf(out, "\t%sic %s, %s, %lu\n",
				backend->abi == ABI_POWER64? /*"dadd"*/"add": "add",
				cs->src_from_ptr->name,
				cs->src_from_ptr->name,
				calc_offsets(cs->src_vreg));
		}

		if (cs->src_vreg->addr_offset != 0) {
			do_add(cs->src_from_ptr->name, cs->src_vreg->addr_offset, "4");
		} else if (cs->src_from_ptr != &power_gprs[4]) {
			x_fprintf(out, "\tmr 4, %s\n", cs->src_from_ptr->name);
		}
	}


	if (cs->dest_vreg == NULL) {
		/* Copy to hidden return pointer */
		emit_load(tmpgpr, curfunc->hidden_pointer);
		x_fprintf(out, "\tmr 3, %s\n", tmpgpr->name);
	} else if (cs->dest_from_ptr == NULL) {
		if (cs->dest_vreg->parent) {
			stop = get_parent_struct(cs->dest_vreg);
		} else {
			stop = NULL;
		}	
		emit_addrof(tmpgpr, cs->dest_vreg, stop); 
		x_fprintf(out, "\tmr 3, %s\n", tmpgpr->name);
	} else {	
		if (cs->dest_vreg->parent) {
			x_fprintf(out, "\t%sic %s, %s, %lu\n",
				backend->abi == ABI_POWER64? /*"dadd"*/"add": "add",
				cs->dest_from_ptr->name,
				cs->dest_from_ptr->name,
				calc_offsets(cs->dest_vreg));
		}
		if (cs->dest_vreg->addr_offset != 0) {
			do_add(cs->dest_from_ptr->name, cs->dest_vreg->addr_offset, "3");
		} else if (cs->dest_from_ptr != &power_gprs[3]) {
			x_fprintf(out, "\tmr 3, %s\n", cs->dest_from_ptr->name);
		}
	}

	x_fprintf(out, "\tbl .memcpy\n");
	x_fprintf(out, "\tnop\n");
}


static void
emit_intrinsic_memcpy(struct int_memcpy_data *data) {
	struct reg	*dest = data->dest_addr;
	struct reg	*src = data->src_addr;
	struct reg	*nbytes = data->nbytes;
	struct reg	*temp = data->temp_reg;
	static int	labelcount;

	if (data->type == BUILTIN_MEMSET && src != temp) {
		x_fprintf(out, "\tmr %s, %s\n", temp->name, src->name);
	}
	x_fprintf(out, "\tli %s, 0\n", power_gprs[0].name);
	x_fprintf(out, "\tcmpw 7, %s, %s\n",
		nbytes->name, power_gprs[0].name);
	x_fprintf(out, "\tbeq 7, .Memcpy_done%d\n", labelcount);
	x_fprintf(out, "\t.Memcpy_start%d:\n", labelcount);
	if (data->type == BUILTIN_MEMCPY) {
		x_fprintf(out, "\tlbz %s, 0(%s)\n", temp->name, src->name);
	}
	x_fprintf(out, "\tstb %s, 0(%s)\n", temp->name, dest->name);
	if (data->type == BUILTIN_MEMCPY) {
		x_fprintf(out, "\taddic %s, %s, 1\n", src->name, src->name);
	}	
	x_fprintf(out, "\taddic %s, %s, 1\n", dest->name, dest->name);
	x_fprintf(out, "\tsubic %s, %s, 1\n", nbytes->name, nbytes->name);
	x_fprintf(out, "\tcmpw 7, %s, %s\n",
		nbytes->name, power_gprs[0].name);
	x_fprintf(out, "\tbne 7, .Memcpy_start%d\n", labelcount);
	x_fprintf(out, "\t.Memcpy_done%d:\n", labelcount);
	++labelcount;
}

static void
emit_zerostack(struct stack_block *sb, size_t nbytes) {
	static struct vreg	vr;

	x_fprintf(out, "\tli 5, %lu\n", /* XXX dli!??! */
		(unsigned long)nbytes);	
	x_fprintf(out, "\tli 4, 0\n");
	vr.stack_addr = sb;
	emit_addrof(&power_gprs[3], &vr, NULL);
	x_fprintf(out, "\tbl .memset\n");
	x_fprintf(out, "\tnop\n");
}

static void
emit_alloca(struct allocadata *ad) {
	x_fprintf(out, "\tmr 3, %s\n", ad->size_reg->name);
	x_fprintf(out, "\tbl .malloc\n");
	x_fprintf(out, "\tnop\n");
	if (&power_gprs[3] != ad->result_reg) {
		x_fprintf(out, "\tmr %s, 3\n",
			ad->result_reg->name);
	}	
}	

static void
emit_dealloca(struct stack_block *sb, struct reg *r) {
	char	*regname = r? r->name: "31";
	x_fprintf(out, "\t%s 3, %lu(%s)\n",
		backend->abi == ABI_POWER64? "ld": "lwz", sb->offset, regname);
	x_fprintf(out, "\tbl .free\n");
	x_fprintf(out, "\tnop\n");
}	

static void
emit_alloc_vla(struct stack_block *sb) {
	x_fprintf(out, "\t%s 3, %lu(31)\n",
		backend->abi == ABI_POWER64? "ld": "lwz",
		(unsigned long)sb->offset + backend->get_ptr_size());
	x_fprintf(out, "\tbl .malloc\n");
	x_fprintf(out, "\tnop\n");
	x_fprintf(out, "\t%s 3, %lu(31)\n",
		backend->abi == ABI_POWER64? "std": "stw",
		sb->offset);
}	

static void
emit_dealloc_vla(struct stack_block *sb, struct reg *r) {
	char	*regname = r? r->name: "31";
	x_fprintf(out, "\t%s 3, %lu(%s)\n",
		backend->abi == ABI_POWER64? "ld": "lwz", sb->offset, regname);
	x_fprintf(out, "\tbl .free\n");
	x_fprintf(out, "\tnop\n");
}

static void
emit_put_vla_size(struct vlasizedata *data) {
	/*
	 * 06/01/11: data->offset wasn't used, we always nonsensically
	 * accessed the item after the pointer and full array size
	 * - regardless of the dimension
	 */
	x_fprintf(out, "\t%s %s, %lu(31)\n",
		backend->abi == ABI_POWER64? "std": "stw",
		data->size->name,
		(unsigned long)data->blockaddr->offset
			+ data->offset);
#if 0
		/*- data->offset*/ + backend->get_ptr_size());
#endif
}

static void
emit_retr_vla_size(struct vlasizedata *data) {
	/*
	 * 06/01/11: data->offset wasn't used, we always nonsensically
	 * accessed the item after the pointer and full array size
	 * - regardless of the dimension
	 */
	x_fprintf(out, "\t%s %s, %lu(31)\n",
		backend->abi == ABI_POWER64? "ld": "lwz",
		data->size->name,
		(unsigned long)data->blockaddr->offset 
			+ data->offset);
#if 0
			+ /* - data->offset*/ backend->get_ptr_size());
#endif
}

static void
emit_load_vla(struct reg *r, struct stack_block *sb) {
	x_fprintf(out, "\t%s %s, %lu(31)\n",
		backend->abi == ABI_POWER64? "ld": "lwz",
		r->name,
		(unsigned long)sb->offset);
}

static void
emit_frame_address(struct builtinframeaddressdata *dat) {
	x_fprintf(out, "\tmr %s, 31\n", dat->result_reg->name);
}	
			

static int 
do_stack(FILE *out,
	struct reg *r,
	struct stack_block *stack_addr,
	unsigned long offset) {

	unsigned long	final_offset = stack_addr->offset + offset;

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
			do_add("31", final_offset, r->name);
		} else {
			do_add("1", final_offset, r->name);
		}
	} else {
		if (stack_addr->use_frame_pointer) {
			x_fprintf(out, "%lu(31)", final_offset);
		} else {
			x_fprintf(out, "%lu(1)", final_offset);
		}
	}
	return 0;
}

static void
print_mem_operand(struct vreg *vr, struct token *constant,
struct vreg *vr_parent) { 
	static int	was_llong;
	static int	was_ldouble;
	int		extra_offset_for_mem = 0;


	if (was_llong) {
		extra_offset_for_mem = 4;
	} else if (was_ldouble) {
		extra_offset_for_mem = 8;
	}

	/* 12/25/08: Save this info for emit_load()/store() */
	last_extra_offset_for_mem = extra_offset_for_mem;

	if (vr && vr->from_const != NULL) {
		constant = vr->from_const;
	}

	if (constant != NULL) {
		struct token	*t = vr->from_const;

		/*if (t->type == TY_INT) {
			x_fprintf(out, "%d", 
				*(int *)t->data);
		} else if (t->type == TY_UINT) {
			x_fprintf(out, "%u", 
				*(unsigned *)t->data);
		}*/
		
		if (IS_INT(t->type)) {
			cross_print_value_by_type(out, t->data, t->type, 0);
		} else if (backend->abi == ABI_POWER64
			&& (IS_LLONG(t->type)	
			|| IS_LONG(t->type))) {	

			if (mintocflag) {
				x_fprintf(out, ".LCTOC0@toc(2)");
			} else {
				struct ty_llong	*tll = t->data2;

				if (tll == NULL) {
#if 0
					printf("bad seq = %d\n", t->seqno);
#endif
					/* Small value - can be immediate */
					unimpl();
				} else {	
					x_fprintf(out, "_Llong%lu(2)",
						tll->count);	
				}	
			}
			return;
#if ALLOW_CHAR_SHORT_CONSTANTS
		} else if (IS_CHAR(t->type) || IS_SHORT(t->type)) {	
			cross_print_value_by_type(out, t->data, t->type, 0);
#endif
		} else if (IS_LONG(t->type)) {
			cross_print_value_by_type(out, t->data, t->type, 0);
		} else if (IS_LLONG(t->type)) {
			cross_print_value_by_type(out, t->data, TY_UINT, 0);
		} else if (t->type == TOK_STRING_LITERAL) {
			if (mintocflag) {
				x_fprintf(out, ".LCTOC0@toc(2)");
			} else {
				struct ty_string	*ts = t->data;
				x_fprintf(out, "_Str%ld(2)",
					ts->count);
			}
		} else if (t->type == TY_FLOAT
			|| t->type == TY_DOUBLE
			|| t->type == TY_LDOUBLE) {
			struct ty_float	*tf = t->data;
	
			if (sysflag != OS_AIX && t->type == TY_LDOUBLE) {
				if (was_ldouble) {
					/* Loading second part of long double */
					was_ldouble = 0;
				} else {
					was_ldouble = 1;
				}
			}

			if (mintocflag) {
				x_fprintf(out, ".LCTOC0@toc(2)");
			} else {
				if (sysflag != OS_AIX && t->type == TY_LDOUBLE) {
					x_fprintf(out, "_Float%lu+%d(2)", tf->count, extra_offset_for_mem);
				} else {
					x_fprintf(out, "_Float%lu(2)",
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
					calc_offsets(vr)
					+ extra_offset_for_mem);
			} else {
				/* static */
				x_fprintf(out, "%lu",
#if 0
					calc_offsets(vr)
#endif
					/*
					 * XXX static structs require a
					 * TOC entry in PowerOpen so the
					 * preferred way of loading a
					 * member now is
					 * lwz 13, static_struct(2)
					 * addi 13, 13, offset
					 * lwz dest, 0(13)
					 * rather than
				 	 * ... lwz dest, offset(13)
					 */
					0
					+ extra_offset_for_mem);
				x_fprintf(out, "(%s)",
					tmpgpr->name);
			}
		} else if (vr2->from_ptr) {
			/* Struct comes from pointer */
			x_fprintf(out, "%lu",
				(unsigned long)calc_offsets(vr)
				+ extra_offset_for_mem);
			x_fprintf(out, "(%s)",
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
				char	*prefix;


				really_accessed(vr->var_backed);
				if (mintocflag) {
					x_fprintf(out, ".LCTOC0@toc(2)");
				} else {
#if 1 /*_AIX*/
					prefix = "_Toc_";
#endif
					x_fprintf(out, "%s%s(2)",
						prefix,
						vr->var_backed->dtype->name);
				}
			}	
		}
	} else if (vr->stack_addr) {
		unsigned long	offset = vr->stack_addr->offset +
			extra_offset_for_mem;

		if (vr->stack_addr->use_frame_pointer) {
			x_fprintf(out, "%lu(31)", offset);
		} else {
			x_fprintf(out, "%lu(1)", offset);
		}
	} else if (vr->from_ptr) {
		x_fprintf(out, "%d(%s)",
			extra_offset_for_mem,
			vr->from_ptr->pregs[0]->name);
	} else {
		abort();
	}
	if (constant == NULL) {
		if (was_llong) {
			was_llong = 0;
		} else if (was_ldouble) {
			was_ldouble = 0;
		} else if (vr->is_multi_reg_obj) {
			if (vr->type->code == TY_LDOUBLE) {
				was_ldouble = 1;
			} else {
				was_llong = 1;
			}
		}	
	}	
}

static void
emit_power_srawi(struct icode_instr *ii) {
	x_fprintf(out, "\tsrawi %s, %s, %d\n",
		ii->dest_pregs[0]->name,
		ii->src_pregs[0]->name,
		*(int *)ii->dat);
}

static void
emit_power_rldicl(struct icode_instr *ii) {
	x_fprintf(out, "\trldicl %s, %s, %s, %d\n",
		ii->dest_pregs[0]->name,
		ii->dest_pregs[0]->name,
		ii->src_pregs[0]->name,
		*(int *)ii->dat);
}

static void
emit_power_fcfid(struct icode_instr *ii) {
	x_fprintf(out, "\tfcfid %s, %s\n",
		ii->dest_pregs[0]->name,
		ii->src_pregs[0]->name);
}

static void
emit_power_frsp(struct icode_instr *ii) {
	struct reg	*r = ii->dat;
	x_fprintf(out, "\tfrsp %s, %s\n",
		r->name, r->name);
}

static void
emit_power_rlwinm(struct icode_instr *ii) {
	x_fprintf(out, "\trlwinm %s, %s, %s, 0x%x\n",
		ii->dest_pregs[0]->name,
		ii->dest_pregs[0]->name,
		ii->src_pregs[0]->name,
		*(int *)ii->dat);
}

static void
emit_power_slwi(struct icode_instr *ii) {
	x_fprintf(out, "\tslwi %s, %s, %d\n",
		ii->dest_pregs[0]->name,
		ii->src_pregs[0]->name,
		*(int *)ii->dat);
}

static void
emit_power_extsb(struct icode_instr *ii) {
	struct reg	*r = ii->dat;

	x_fprintf(out, "\textsb %s, %s\n", r->name, r->name);
}

static void
emit_power_extsh(struct icode_instr *ii) {
	struct reg	*r = ii->dat;

	x_fprintf(out, "\textsh %s, %s\n", r->name, r->name);
}

static void
emit_power_extsw(struct icode_instr *ii) {
	struct reg	*r = ii->dat;

	x_fprintf(out, "\textsw %s, %s\n", r->name, r->name);
}

static void
emit_power_xoris(struct icode_instr *ii) {
	struct reg	*dest = ii->dest_pregs[0];

	x_fprintf(out, "\txoris %s, %s, 0x%x\n",
		dest->name, dest->name, *(int *)ii->dat);
}

static void
emit_power_lis(struct icode_instr *ii) {
	x_fprintf(out, "\tlis %s, 0x%x\n",
		ii->dest_pregs[0]->name, *(int *)ii->dat);
}

static void
emit_power_loadup4(struct icode_instr *ii) {
	x_fprintf(out, "\tlwz %s, %lu(31)\n",
		ii->dest_pregs[0]->name,
		(unsigned long)ii->src_vreg->stack_addr->offset+4);
}

static void
emit_power_fctiwz(struct icode_instr *ii) {
	struct reg	*r = ii->dat;

	if ((ii->hints & HINT_INSTR_GENERIC_MODIFIER) == 0) {
		/* Is float */
		x_fprintf(out, "\tfrsp %s, %s\n", r->name, r->name);
	}
	x_fprintf(out, "\t%s %s, %s\n",
		(ii->hints & HINT_INSTR_GENERIC_MODIFIER)? "fctidz": "fctiwz",
		r->name, r->name);
}

struct emitter power_emit_as = {
	1, /* need_explicit_extern_decls */
	init,
	emit_strings,
	emit_fp_constants,
	emit_llong_constants,
	emit_support_buffers,
	NULL, /* pic_support */
	
	NULL, /* support_decls */
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
	emit_setsect,
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
	emit_preg_or,
	emit_and,
	emit_xor,
	emit_not,
	emit_ret,
	emit_cmp,
	emit_extend_sign,
	NULL, /* conv_fp */
	emit_conv_from_ldouble,
	emit_conv_to_ldouble,
	emit_branch,
	emit_mov,
	emit_setreg,
	emit_xchg,
	emit_addrof,
	NULL, /* initialize_pic */
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
	NULL, /* stupid_trace */
	NULL /* finish_stupid_trace */
};

struct emitter_power power_emit_power_as = {
	emit_power_srawi,
	emit_power_rldicl,
	emit_power_fcfid,
	emit_power_frsp,
	emit_power_rlwinm,
	emit_power_slwi,
	emit_power_extsb,
	emit_power_extsh,
	emit_power_extsw,
	emit_power_xoris,
	emit_power_lis,
	emit_power_loadup4,
	emit_power_fctiwz
};	

