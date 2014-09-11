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
 * Emit GAS code from intermediate x86 code
 */
#include "x86_emit_gas.h"
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
#include "control.h"
#include "symlist.h"
#include "typemap.h"
#include "dwarf.h"
#include "cc1_main.h"
#include "x86_gen.h"
#include "amd64_gen.h"
#include "amd64_emit_gas.h"
#include "expr.h"
#include "inlineasm.h"
#include "error.h"
#include "n_libc.h"

static FILE	*out;
static size_t	data_segment_offset;
static size_t	bss_segment_offset;
static size_t	data_thread_segment_offset;
static size_t	bss_thread_segment_offset;

void    as_print_string_init(FILE *, size_t howmany, struct ty_string *str);

static void
emit_setsection(int value);

static int 
init(FILE *fd, struct scope *s) {
	(void) s;
	out = fd;

	/* XXX this is kludgy & assumes regs are already init'ed */
	x86_fprs[0].name = "st"; /* gas doesn't recognize st(0) :-( */
	x86_fprs[1].name = "st(1)";
	x86_fprs[2].name = "st(2)";
	x86_fprs[3].name = "st(3)";
	x86_fprs[4].name = "st(4)";
	x86_fprs[5].name = "st(5)";
	x86_fprs[6].name = "st(6)";
	x86_fprs[7].name = "st(7)";
	return 0;
}

int	osx_call_renamed; /* XXX Change emit_call() instead */


struct osx_fcall {
	char			*name;
	int			nonlazy;
	int			renamed;
	struct decl		*dec;
	struct osx_fcall	*next;
};

#define FCALL_HTAB_SIZE	128

static struct osx_fcall	*fcall_htab[FCALL_HTAB_SIZE];
static struct osx_fcall	*fcall_htab_tail[FCALL_HTAB_SIZE];

static void
add_osx_fcall(const char *name, struct decl *dec, int nonlazy) {
	unsigned int		key = 0;
	const char		*p;
	struct osx_fcall	*f;

	for (p = name; *p != 0; ++p) {
		key = 33 * key + *p;
	}
	key &= (FCALL_HTAB_SIZE - 1);

	/* Check whether the function has already been called */
	for (f = fcall_htab[key]; f != NULL; f = f->next) {
		if (f->nonlazy != nonlazy) {
			/*
			 * One is a function call and one is a
			 * function pointer reference
			 */
			continue;
		}
		if (strcmp(f->name, name) == 0) {
			/* Yes, don't redeclare */
			return;
		}
	}

	/* Create new symbol entry */
	f = n_xmalloc(sizeof *f);
	f->name = n_xstrdup(name);
	f->nonlazy = nonlazy;
	f->renamed = osx_call_renamed;
	f->dec = dec;
	f->next = NULL;

	if (fcall_htab[key] == NULL) {
		fcall_htab[key] = fcall_htab_tail[key] = f;
	} else {
		fcall_htab_tail[key]->next = f;
		fcall_htab_tail[key] = f;
	}
}


static void
print_mem_operand(struct vreg *vr, struct token *constant);
struct reg *
get_smaller_reg(struct reg *r, size_t size);
void
do_attribute_alias(struct decl *dec);


/*
 * Turns either a byte size into an assembler instruction size postfix.
 * If a type argument is supplied (non-null), it will ensure that ``long long''
 * is mapped to ``dword'', as that type is really dealt with as two
 * dwords rather than a qword.
 */
int
size_to_gaspostfix(size_t size, struct type *type) {
	int	is_fp = 0;

	if (type != NULL) {
		if (IS_FLOATING(type->code) && type->tlist == NULL) {
			is_fp = 1;
		} else {
			is_fp = 0;
			if (type->tlist == NULL) {
				if (IS_LLONG(type->code)) {
					if (backend->arch != ARCH_AMD64) {
						size = 4;
					}	
				}
			}
		}
	}	
	/* XXXXXXXXX long double :( */
	if (size == /*10*/12 || size == 10
		|| size == 16) return 't'; /* long double */
	else if (size == 8) {
		if (type != NULL
			&& IS_FLOATING(type->code)
			&& type->tlist == NULL) {
			return 'l'; /* double */
		} else {	
			return backend->arch == ARCH_AMD64? 'q': 'l';
		}	
	}	
	else if (size == 4 && is_fp) return 's'; /* float */
	else if (size == 4) return 'l'; /* long word (dword) */
	else if (size == 2) return 'w'; /* word */
	else if (size == 1) return 'b'; /* byte */
	else if (type && type->tlist) {
		return backend->arch == ARCH_AMD64? 'q': 'l'; /* XXX BUG!!!? array? */
	} else {
		printf("bad size for size_to_gaspostfix(): %lu\n",
			(unsigned long)size);
		abort();
	}	
	return 0;
}

static void
print_init_list(struct decl *dec, struct initializer *init);
static void
print_reg_assign(struct reg *dest,
	struct reg *srcreg, size_t src_size, struct type *src_type);

static void
print_init_expr(struct type *dt, struct expr *ex) {
	struct tyval	*cv;
	int		is_addr_as_int = 0;

	cv = ex->const_value;
#if 0
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
		case TY_UCHAR:
		case TY_SCHAR:
		case TY_BOOL:
			x_fprintf(out, ".byte ");
			cross_print_value_by_type(out, ex->const_value->value,
				TY_UCHAR, 'x');	
			break;
		case TY_SHORT:
			x_fprintf(out, ".word ");
			cross_print_value_by_type(out, ex->const_value->value,
				dt->code, 'x');	
			break;
		case TY_USHORT:
			x_fprintf(out, ".word ");
			cross_print_value_by_type(out, ex->const_value->value,
				dt->code, 'x');	
			break;
		case TY_INT:
		case TY_ENUM:	
			x_fprintf(out, ".long ");
			cross_print_value_by_type(out, ex->const_value->value,
				dt->code, 'x');	
			break;
		case TY_UINT:
			x_fprintf(out, ".long ");
			cross_print_value_by_type(out, ex->const_value->value,
				dt->code, 'x');	
			break;
		case TY_LONG:
		case TY_LLONG:	
			if (backend->arch != ARCH_AMD64
				&& dt->code == TY_LLONG) {
				x_fprintf(out, ".long ");
				cross_print_value_chunk(out,
					ex->const_value->value,
					TY_LLONG, TY_UINT, 0, 0);
				x_fputc('\n', out);
				x_fprintf(out, "\t.long ");
				cross_print_value_chunk(out,
					ex->const_value->value,
					TY_LLONG, TY_UINT, 0, 1);
			} else if (backend->arch == ARCH_AMD64) {
				x_fprintf(out, ".quad ");
				cross_print_value_by_type(out,
					ex->const_value->value,
					TY_LONG, 'd');
			} else {	
				x_fprintf(out, ".long ");
				cross_print_value_by_type(out,
					ex->const_value->value,
					TY_LONG, 'd');
			}
			break;
		case TY_ULONG:	
		case TY_ULLONG:
			if (backend->arch != ARCH_AMD64
				&& dt->code == TY_ULLONG) {	
				x_fprintf(out, ".long ");
				cross_print_value_chunk(out,
					ex->const_value->value,
					TY_ULLONG, TY_UINT, 0, 0);
				x_fputc('\n', out);
				x_fprintf(out, "\t.long ");
				cross_print_value_chunk(out,
					ex->const_value->value,
					TY_ULLONG, TY_UINT, 0, 1);
			} else if (backend->arch == ARCH_AMD64) {
				x_fprintf(out, ".quad ");
				cross_print_value_by_type(out,
					ex->const_value->value,
					TY_ULONG, 'd');
			} else {	
				x_fprintf(out, ".long ");
				cross_print_value_by_type(out,
					ex->const_value->value,
					TY_ULONG, 'd');
			}	
			break;
		case TY_FLOAT:
			x_fprintf(out, ".long ");
			cross_print_value_by_type(out,
				ex->const_value->value,
				TY_UINT, 'x');
			break;
		case TY_DOUBLE:
			x_fprintf(out, ".long ");
			cross_print_value_chunk(out,
				ex->const_value->value,
				TY_DOUBLE, TY_UINT, 0, 0);
			x_fputc('\n', out);
			x_fprintf(out, "\t.long ");
			cross_print_value_chunk(out,
				ex->const_value->value,
				TY_DOUBLE, TY_UINT, 0, 1);
			break;
		case TY_LDOUBLE:
			x_fprintf(out, ".long ");
			cross_print_value_chunk(out,
				ex->const_value->value,
				TY_LDOUBLE, TY_UINT, 0, 0);
			x_fputc('\n', out);
			x_fprintf(out, "\t.long ");
			cross_print_value_chunk(out,
				ex->const_value->value,
				TY_LDOUBLE, TY_UINT, 0, 1);
			x_fputc('\n', out);
			x_fprintf(out, ".word ");
			cross_print_value_chunk(out,
				ex->const_value->value,
				TY_LDOUBLE, TY_UINT, TY_USHORT, 2);
			if (sysflag == OS_OSX || backend->arch == ARCH_AMD64) {
				/* 02/23/09: Pad to 16 */
				x_fprintf(out, "\n\t.space 6\n");
			} else {
				/* 02/23/09: Pad to 12 */
				x_fprintf(out, "\n\t.zero 2\n");
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
				x_fprintf(out, ".quad ");
			} else {	
				x_fprintf(out, ".long ");
			}	
			if (cv->is_nullptr_const) {
				x_fprintf(out, "0");
			} else if (cv->str) {
				x_fprintf(out, "._Str%lu", cv->str->count);
			} else if (cv->value) {
				cross_print_value_by_type(out,
					cv->value, TY_ULONG, 'x');	
			} else if (cv->address) {
				/* XXX */
				char	*sign;

				if (cv->address->diff < 0) {
				/*	sign = "-"*/
					/*
					 * 08/02/09: The address difference is
					 * already negative, and the code below
					 * unconditionally used +, so we got
					 * constructs like
					 *
					 *    -+-10
					 *
					 * Without the extra sign, we now get
					 *
					 *    +-10
					 *
					 * which looks goofy but works right
					 * (``add minus 10'')
					 */
					sign = "";
				} else {
					sign = "";
				}
				if (cv->address->dec != NULL) {
					/* Static variable */
					x_fprintf(out, "%s%s%s+%ld",
						sysflag == OS_OSX? "_": "",
						cv->address->dec->dtype->name,
						sign,
						cv->address->diff);
				} else {
					/* Label */
					x_fprintf(out, ".%s%s+%ld",
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
			as_print_string_init(out, arrsize, cv->str);

			if (arrsize >= cv->str->size) {
				if (arrsize > cv->str->size) {
					if (sysflag == OS_OSX) {
						x_fprintf(out, "\n\t.space %lu\n",
							arrsize - cv->str->size);
					} else {
						x_fprintf(out, "\n\t.zero %lu\n",
							arrsize - cv->str->size);
					}
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
			if (init->varinit && init->left_type->tbit != NULL) {
#if 0
				if (se->next != NULL) {
					continue;
				}
#endif
				continue;
			} else {
				/* XXX cross-comp */
				if (sysflag == OS_OSX) {
					x_fprintf(out, "\t.space %lu\n",
						(unsigned long)*(size_t *)init->data);
				} else {
					x_fprintf(out, "\t.zero %lu\n",
						(unsigned long)*(size_t *)init->data);
				}
#if 0
			x_fprintf(out, "\ttimes %lu db 0\n",
				(unsigned long)*(size_t *)init->data);	
#endif
				startse = se;
				/*
				 * 02/26/10: Skipping the struct members after
				 * this NULL initializer was wrong if there are
				 * further initialized members!
				 * We have to distinguish between initializers
				 * that were created to implicitly initialize
				 * the remainder of a struct;
				 *
				 *    struct foo x = { 0 };
				 *
				 * ... and initializers that were created to
				 * be overwritten with variable initializiers
				 * at runtime;
				 *
				 *    struct foo x = { func(), 123, 456 };
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
				 * is properly aligned.
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
						if (sysflag == OS_OSX) {
							x_fprintf(out, "\t.space %lu\n",
								(unsigned long)tmp);
						} else {
							x_fprintf(out, "\t.zero %lu\n",
								(unsigned long)tmp);
						}
					}
				} else {
					d = dec->dtype->tstruc->scope->slist->dec;
				}
			}


			if (d != NULL) {
				unsigned long	offset;


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
					if (sysflag == OS_OSX) {
						x_fprintf(out, "\t.space %lu\n",
							(unsigned long)nbytes);
					} else {
						x_fprintf(out, "\t.zero %lu\n",
							(unsigned long)nbytes);
					}
				}
			}
			se = se->next;
		}	
	}	
}

static void
emit_static_init_vars(struct decl *list) {
	struct decl	*d;
#if 0
	struct decl	**dv;
#endif
	size_t		size;
#if 0
	int		i;
#endif

	if (list) {
		emit_setsection(SECTION_INIT);
		if (backend->arch == ARCH_AMD64) {
			/* XXX */
			x_fprintf(out, "\t.align 16\n");
		}
	}
	for (d = list; d != NULL; d = d->next) {

		/*
		 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
		 * will always be 0 since we output immediately. Maybe
		 * do this in gen_finish_output() instead?
		 */
		if (d->dtype->storage == TOK_KEY_STATIC
			&& d->references == 0) {
			continue;
		}
		if (d->invalid) {
			continue;
		}	

		/* Constant initializer expression */
		size = backend->get_sizeof_decl(d, NULL);

		if (sysflag != OS_OSX) {
			x_fprintf(out, "\t.type %s, @object\n", d->dtype->name);
			x_fprintf(out, "\t.size %s, %lu\n",
				d->dtype->name, size);
			x_fprintf(out, "%s:\n", d->dtype->name);
		} else {
			x_fprintf(out, "_%s:\n", d->dtype->name);
		}
		print_init_list(d, d->init);

		if (d->next != NULL) {
			unsigned long	align;

			align = calc_align_bytes(data_segment_offset,
				d->dtype, d->next->dtype, 0);
			if (align) {
				if (sysflag == OS_OSX) {
					x_fprintf(out, "\t.space %lu\n", align);
				} else {
					x_fprintf(out, "\t.zero %lu\n", align);
				}
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
		if (!use_common_variables && sysflag != OS_OSX) {
			emit_setsection(SECTION_UNINIT);
			if (backend->arch == ARCH_AMD64) {
				/* XXX */
				x_fprintf(out, "\t.align 16\n");
			}
		}
		if (sysflag == OS_OSX) {
		}
		for (d = list; d != NULL; d = d->next) {
			if (d->dtype->storage == TOK_KEY_STATIC
				&& d->references == 0) {
				continue;
			}

			if (d->invalid) {
				continue;
			}

			size = backend->get_sizeof_decl(d, NULL);
#if 1 /* 02/03/08: No more common variables! */
      /* 05/17/09: Enabled (for compatibility of course), but optionally */
      /* 07/07/09: This was sadly done for static variables too, which should
       * of course not be shared */
			if (use_common_variables
				&& d->dtype->storage != TOK_KEY_STATIC
				&& sysflag != OS_OSX) {
				x_fprintf(out, ".comm %s,%lu,4\n", /* XXX */
					d->dtype->name, size);	
				continue;
			}
#endif
			if (sysflag != OS_OSX) {
				x_fprintf(out, "\t.type %s, @object\n", d->dtype->name);
				x_fprintf(out, "\t.size %s, %lu\n",
					d->dtype->name, size);
			}

			if (sysflag == OS_OSX) {
				/*
				 * 07/07/09: XXXXXXXXXXXXXXXXXXXX This should
				 * most likely not be common for static
				 * variables
				 */
				x_fprintf(out, "\t.zerofill __DATA, __common, %s%s, %lu, 2\n",
					IS_ASM_RENAMED(d->dtype->flags)? "": "_",
					d->dtype->name, size);
			} else {
				x_fprintf(out, "%s:\n", d->dtype->name);
				x_fprintf(out, "\t.space %lu\n", size);
			}

			if (d->next != NULL) {
				unsigned long	align;

				align = calc_align_bytes(bss_segment_offset,
					d->dtype, d->next->dtype, 0);
				if (align) {
					/* XXX wrong? */
					x_fprintf(out, "\t.space %lu\n", align);
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
		if (backend->arch == ARCH_AMD64) {
			/* XXX */
			x_fprintf(out, "\t.align 16\n");
		}
		for (d = list; d != NULL; d = d->next) {
			/*
			 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
			 * will always be 0 since we output immediately. Maybe
			 * do this in gen_finish_output() instead?
			 */
			if (d->dtype->storage == TOK_KEY_STATIC
				&& d->references == 0) {
				continue;
			}
			if (d->invalid) {
				continue;
			}	

			/* Constant initializer expression */
			size = backend->get_sizeof_decl(d, NULL);
			if (sysflag != OS_OSX) {
				x_fprintf(out, "\t.type %s, @object\n", d->dtype->name);
				x_fprintf(out, "\t.size %s, %lu\n",
					d->dtype->name, size);
			}
			x_fprintf(out, "%s:\n", d->dtype->name);
			print_init_list(d, d->init);

			if (d->next != NULL) {
				unsigned long	align;

				align = calc_align_bytes(data_thread_segment_offset,
					d->dtype, d->next->dtype, 0);
				if (align) {
					if (sysflag == OS_OSX) {
						x_fprintf(out, "\t.space %lu\n", align);
					} else {
						x_fprintf(out, "\t.zero %lu\n", align);
					}
					data_thread_segment_offset += align;
				}	
			}
			data_thread_segment_offset += size;
		}
	}
}

static void
emit_static_uninit_thread_vars(struct decl *list) {
	struct decl	*d;
	size_t		size;

	if (list != NULL) {
		emit_setsection(SECTION_UNINIT_THREAD);
		if (backend->arch == ARCH_AMD64) {
			/* XXX */
			x_fprintf(out, "\t.align 16\n");
		}
		for (d = list; d != NULL; d = d->next) {
			if (d->dtype->storage == TOK_KEY_STATIC
				&& d->references == 0) {
				continue;
			}
			if (d->invalid) {
				continue;
			}	

			size = backend->get_sizeof_decl(d, NULL);
			if (sysflag != OS_OSX) {
				x_fprintf(out, "\t.type %s, @object\n", d->dtype->name);
				x_fprintf(out, "\t.size %s, %lu\n", d->dtype->name, size);
			}
			x_fprintf(out, "%s:\n", d->dtype->name);
			x_fprintf(out, "\t.space %lu\n", size);
			if (d->next != NULL) {
				unsigned long	align;

				align = calc_align_bytes(bss_thread_segment_offset,
					d->dtype, d->next->dtype, 0);
				if (align) {
					/* XXX wrong? */
					x_fprintf(out, "\t.space %lu\n", align);
					bss_thread_segment_offset += align;
				}	
			}	
			bss_thread_segment_offset += size;
		}
	}
}



static void
emit_finish_program(void) {
	if (sysflag == OS_OSX) {
		int	i;

		/*
		 * First generate function call references
		 */
		x_fprintf(out, ".section __IMPORT,__jump_table,symbol_stubs,"
			"self_modifying_code+pure_instructions,5\n");
		for (i = 0; i < FCALL_HTAB_SIZE; ++i) {
			struct osx_fcall	*c;

			for (c = fcall_htab[i]; c != NULL; c = c->next) {
				if (c->nonlazy) {
					 /* Not a call */
					continue;
				}
				x_fprintf(out, "L_%s$stub:\n",
					c->name);
				x_fprintf(out, "\t.indirect_symbol %s%s\n",
					c->renamed? "": "_",
					c->name);
				x_fprintf(out, "\thlt ; hlt ; hlt ; hlt ; hlt\n");
			}
		}

		/*
		 * Then generate nonlazy function pointer references
		 */
		x_fprintf(out, ".section __IMPORT,__pointers,non_lazy_symbol_pointers\n");
		for (i = 0; i < FCALL_HTAB_SIZE; ++i) {
			struct osx_fcall	*c;

			for (c = fcall_htab[i]; c != NULL; c = c->next) {
				if (!c->nonlazy) {
					 /* Lazy reference */
					continue;
				}
				if (c->dec && c->dec->dtype->is_def) {
					/*
					 * Symbol has a definition, so we must declare
					 * the indirect pointer in the data section
					 */
					continue;
				}
				if (c->dec && c->dec->dtype->storage == TOK_KEY_STATIC) {
					/* 
					 * May be local variable - don't declare as
					 * indirect
					 */
					continue;
				}
				x_fprintf(out, "L_%s$non_lazy_ptr:\n",
					c->name);
				x_fprintf(out, "\t.indirect_symbol %s%s\n",
					c->renamed? "": "_",
					c->name);
				x_fprintf(out, "\t.long 0\n");
			}
		}

		/*
		 * Generate references to variables and function which are
		 * locally defined. Those OF COURSE don't work with
		 * indirect_symbol.
		 */
		x_fprintf(out, ".data\n");
		x_fprintf(out, ".align 2\n");
		for (i = 0; i < FCALL_HTAB_SIZE; ++i) {
			struct osx_fcall	*c;

			for (c = fcall_htab[i]; c != NULL; c = c->next) {
				if ((c->dec == NULL || !c->dec->dtype->is_def)
					&& (c->dec == NULL || c->dec->dtype->storage != TOK_KEY_STATIC)) {
					/* 
					 * We only want to output symbols with a
					 * local definition
					 */
					continue;
				}
				x_fprintf(out, "L_%s$non_lazy_ptr:\n",
					c->name);
				x_fprintf(out, "\t.long %s%s\n",
					c->renamed? "": "_",
					c->name);
			}
		}
	}
}


static void
emit_stupidtrace(struct stupidtrace_entry *ent) {
	static unsigned long	count;

	x_fprintf(out, "\tpush $_Envstr0\n");
	x_fprintf(out, "\tcall getenv\n");
	x_fprintf(out, "\tadd $4, %%esp\n");
	x_fprintf(out, "\tcmp $0, %%eax\n");
	x_fprintf(out, "\tje _Strace_label%lu\n", ++count);
	x_fprintf(out, "\tpush $%s\n", ent->bufname);
	x_fprintf(out, "\tcall puts\n");
	x_fprintf(out, "\tadd $4, %%esp\n");
	x_fprintf(out, "_Strace_label%lu:\n", count);
}


static void
emit_finish_stupidtrace(struct stupidtrace_entry *ent) {
	static int	init;

	emit_setsection(SECTION_TEXT);
	if (!init) {
		x_fprintf(out, "_Envstr0:\n\t.asciz \"NWCC_STUPIDTRACE\"\n");
		init = 1;
	}
	x_fprintf(out, "%s:\n\t.asciz \"--- (trace) --- %s\"\n", ent->bufname, ent->func->proto->dtype->name);
}



void
do_attribute_alias(struct decl *dec) {
	struct attrib *a;

	if ((a = lookup_attr(dec->dtype->attributes, ATTRF_ALIAS)) != NULL) {
		x_fprintf(out, ".set %s, %s\n", dec->dtype->name, a->parg);
	}
}

static void
emit_extern_decls(void) {

	/*
	 * 05/17/09: For attribute alias: Any non-static alias declaration
	 * introduces a new GLOBAL declaration and an alias setting for
	 * another symbol
	 *
	 *    void foo() __attribute__("bar");
	 */
	if (used_attribute_alias) {
		struct sym_entry	*se;

		for (se = extern_vars; se != NULL; se = se->next) {
			struct attrib	*a;

			if (se->dec->invalid) {
				continue;
			}
			if (se->dec->has_symbol
				|| se->dec->dtype->is_def
				|| se->dec->has_def) {
				continue;
			}
			a = lookup_attr(se->dec->dtype->attributes, ATTRF_ALIAS);
			if (a != NULL) {
				x_fprintf(out, "\t.global %s\n",
					se->dec->dtype->name);
				x_fprintf(out, "\t.set %s, %s\n",
					se->dec->dtype->name, a->parg);
			}
		}
	}
}

static void
#if 0
emit_extern_decls(void) {
#endif
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
			if (d[i]->invalid) {
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
					if (sysflag == OS_OSX) {
						x_fprintf(out, ".globl %s%s\n",
							IS_ASM_RENAMED(d[i]->dtype->flags)? "": "_", 
							d[i]->dtype->name,
							d[i]);
					} else {
						x_fprintf(out, ".globl %s\n",
							d[i]->dtype->name,
							d[i]);
						do_attribute_alias(d[i]);
					}
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
			if (sysflag == OS_OSX) {
				x_fprintf(out, ".globl %s%s\n",
					IS_ASM_RENAMED(dv[i]->dtype->flags)? "": "_", 
					dv[i]->dtype->name);
			} else {
				x_fprintf(out, ".globl %s\n",
					dv[i]->dtype->name);
				do_attribute_alias(dv[i]);
			}
			dv[i]->has_symbol = 1;
		}
	}
}

static void
emit_struct_inits(struct init_with_name *list) {
	struct init_with_name	*in;

	if (list != NULL) {
		emit_setsection(SECTION_INIT);
		for (in = list; in != NULL; in = in->next) {
			x_fprintf(out, ".%s:\n", in->name);
			print_init_list(in->dec, in->init);
		}	
	}
}

static void
emit_fp_constants(struct ty_float *list) {
	if (list != NULL) {
		struct ty_float	*tf;

		emit_setsection(SECTION_INIT);
		for (tf = list; tf != NULL; tf = tf->next) {
			/* XXX cross-compilation */
			/* XXX VERY mips-like */
			int	size = 0;

			switch (tf->num->type) {
			case TY_FLOAT:	
				x_fprintf(out, "\t.align 4\n");
				size = 4;
				break;
			case TY_DOUBLE:
			case TY_LDOUBLE:
				x_fprintf(out, "\t.align 8\n");
				if (tf->num->type == TY_DOUBLE) {
					size = 8;
				} else {
					size = 10;
				}
			}

			if (sysflag != OS_OSX) {
				x_fprintf(out, "\t.type _Float%lu, @object\n", tf->count);
				x_fprintf(out, "\t.size _Float%lu, %d\n", tf->count, size);
			}
			x_fprintf(out, "_Float%lu:\n", tf->count);

			switch (tf->num->type) {
			case TY_FLOAT:
				x_fprintf(out, "\t.long ");
				cross_print_value_by_type(out,
					tf->num->value, TY_UINT, 'x');	
				break;
			case TY_DOUBLE:
				x_fprintf(out, "\t.long ");
				cross_print_value_chunk(out,
					tf->num->value,
					TY_DOUBLE, TY_UINT, 0, 0);
				x_fprintf(out, "\n\t.long ");
				cross_print_value_chunk(out,
					tf->num->value,
					TY_DOUBLE, TY_UINT, 0, 1);
				break;
			case TY_LDOUBLE:
				x_fprintf(out, "\t.long ");
				cross_print_value_chunk(out,
					tf->num->value,
					TY_DOUBLE, TY_UINT, 0, 0);
				x_fprintf(out, "\n\t.long ");
				cross_print_value_chunk(out,
					tf->num->value,
					TY_DOUBLE, TY_UINT, 0, 1);
				x_fprintf(out, "\n\t.word ");
				cross_print_value_chunk(out,
					tf->num->value,
					TY_DOUBLE, TY_UINT, TY_USHORT, 2);
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
	static int	have_fbuf = 0;
	static int	have_x87cw = 0;

	if (/*float_const != NULL
		||*/ (x87cw_old.var_backed != NULL
#if ! REMOVE_FLOATBUF
		|| floatbuf.var_backed != NULL
#endif
		)
			&& (!have_fbuf || !have_x87cw)) {
		emit_setsection(SECTION_INIT);

#if ! REMOVE_FLOATBUF
		if (floatbuf.var_backed != NULL && !have_fbuf) {
			x_fprintf(out, "\t.align 8\n");
			x_fprintf(out, "%s:\n\t.long 0x0\n\t.long 0x0\n",
				floatbuf.type->name);
			have_fbuf = 1;
		}
#endif
	
		if (x87cw_old.var_backed != NULL && !have_x87cw) {
			x_fprintf(out, "\t%s:\n\t.word 0x0\n", x87cw_old.type->name);
			x_fprintf(out, "\t%s:\n\t.word 0x0\n", x87cw_new.type->name);
			have_x87cw = 1;
		}
	}
	if (amd64_need_negmask) {
		x86_emit_gas.setsection(SECTION_INIT);
		x_fprintf(out, "\t.align 8\n"); /* XXX 16 too large - is 8 right?! */
		if (amd64_need_negmask & 1) {
			x_fprintf(out, "_SSE_Negmask:\n");
			x_fprintf(out, "\t.long 0x80000000\n");
			x_fprintf(out, "\t.align 8\n"); /* XXX 16 too large - is 8 right?! */
		}
		if (amd64_need_negmask & 2) {
			x_fprintf(out, "_SSE_Negmask_double:\n");
			x_fprintf(out, "\t.long 0x00000000\n");
			x_fprintf(out, "\t.long 0x80000000\n");
		}
	}

	if (amd64_need_ulong_float_mask) {
		x86_emit_gas.setsection(SECTION_INIT);
		x_fprintf(out, "_Ulong_float_mask:\n");
		x_fprintf(out, "\t.long 1602224128\n");
	}
}

static void
emit_strings(struct ty_string *list) {
	if (list != NULL) {
		struct ty_string	*str;

		emit_setsection(SECTION_RODATA);
		for (str = list; str != NULL; str = str->next) {
			x_fprintf(out, "._Str%lu:\n", str->count);
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
	x_fprintf(out, "#APP\n");
	/*
	 * There may be an empty body for statements where only the side
	 * effect is desired;
	 * __asm__("" ::: "memory");
	 */
	if (stmt->code != NULL) {
		inline_instr_to_gas(out, stmt->code);
	}	
	x_fprintf(out, "#NO_APP\n");
}	

static void
emit_unimpl(void) {
	static int	n;

	x_fprintf(out, "\tpush dword _Unimpl_msg\n");
	x_fprintf(out, "\tpush dword %d\n", n++);
	x_fprintf(out, "\tcall printf\n");
	x_fprintf(out, "\taddl $8, %%esp\n");
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
		if (sysflag == OS_OSX) {
			/* XXX This @#^%*&#$&$ stuff does not work for asm-renamed symbols */
			x_fprintf(out, "_%s:\n", name);
		} else {
			x_fprintf(out, "%s:\n", name);
		}
	} else {
		x_fprintf(out, ".%s:\n", name);
	}
}

static void
emit_call(const char *name) {
	if (sysflag == OS_OSX) {
		if (backend->arch == ARCH_X86) {
			x_fprintf(out, "\tcall L_%s$stub\n", name);
			add_osx_fcall(name, NULL, 0);
		} else {
			x_fprintf(out, "\tcall %s%s\n",
				osx_call_renamed? "": "_", name);
		}
		return;
	}
	if (picflag) {
		if (backend->arch == ARCH_AMD64) {
			x_fprintf(out, "\tcall %s@PLT\n", name);
		} else {
			x_fprintf(out, "\tcall %s@PLT\n", name);
		}
	} else {
		x_fprintf(out, "\tcall %s\n", name);
	}
}

static void
emit_callindir(struct reg *r) {
	x_fprintf(out, "\tcall *%%%s\n", r->name);
}	

static void
emit_func_header(struct function *f) {
	if (sysflag != OS_OSX) {
		x_fprintf(out, "\t.type %s, @function\n", f->proto->dtype->name);
	}
}

static void
emit_func_intro(struct function *f) {
	(void) f;
	x_fprintf(out, "\tpushl %%ebp\n"); /* XXX */
	x_fprintf(out, "\tmov %%esp, %%ebp\n");
}

static void
emit_func_outro(struct function *f) {
	char	*name = f->proto->dtype->name;

/*	x_fprintf(out, "\tret\n");*/
	/* 02/03/08: Add size for PIC code */
	if (sysflag != OS_OSX) {
		x_fprintf(out, "\t.size %s, .-%s\n", name, name); 
	}
}


static void
emit_push(struct function *f, struct icode_instr *ii) {
	struct vreg	*vr = ii->src_vreg;
	struct reg	*r = ii->src_pregs?
				(void *)ii->src_pregs[0]: (void *)NULL;

	(void) f;

	if (ii->src_pregs) {
		if (ii->src_vreg->is_multi_reg_obj) {
			x_fprintf(out, "\tpushl %%%s\n",
				ii->src_pregs[1]->name);
			f->total_allocated += 4;
		}	
		x_fprintf(out, "\tpushl %%%s\n", /*ascii_type*/ r->name);
	} else if (vr->var_backed) {
		struct decl	*d = vr->var_backed;

		if (vr->parent != NULL) {
			/* Structure or union member */
			struct decl	*d2/* = d*/;
			struct vreg	*vr2;

			vr2 = get_parent_struct(vr);
			d2 = vr2->var_backed;
			if (vr2->from_ptr) {
				/* XXX hmm this is nonsense?!!?? */
				x_fprintf(out, "\tpushl [%%%s",
					vr2->from_ptr->pregs[0]->name);
				if (vr->parent->type->code == TY_STRUCT) {
					x_fprintf(out, " + %s.%s",
					vr2->from_ptr->type->tstruc->tag,
					d2->dtype->name);
				}
				x_fprintf(out, "]\n");
			} else if (d2 && d2->stack_addr != NULL) {
				x_fprintf(out, "\tpushl -%ld(%%ebp",
					d2->stack_addr->offset);
				if (vr->parent->type->code == TY_STRUCT) {
				/* XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX */
					x_fprintf(out, " + %lu",
						calc_offsets(vr));	
				}	
				x_fprintf(out, ")\n"); 
			} else if (d2 != NULL) {	
				/* Must be static */
				x_fprintf(out, "\tpushl [%s",
					d2->dtype->name);
				if (vr->parent->type->code == TY_STRUCT) {
					x_fprintf(out, " + %lu",
						calc_offsets(vr));
				}	
				x_fprintf(out, "]\n"); 
			} else {
				unimpl();
			}	
		} else {
			if (d->stack_addr != NULL) {
				/* Stack */
				x_fprintf(out, "\tpushl -%ld(%%ebp)\n",
					/*ascii_type*/ d->stack_addr->offset);
			} else {
				/* Static or register variable */
				if (d->dtype->storage == TOK_KEY_REGISTER) {
					unimpl();
				} else {
					x_fprintf(out, "\tpushl [%s]\n",
						/*ascii_type*/ d->dtype->name);
				}
			}
		}
	} else if (vr->from_const) {
		struct token	*t = vr->from_const;

		if (t->type == TOK_STRING_LITERAL) {
			struct ty_string	*s = t->data;
			x_fprintf(out, "\tpushl $._Str%lu\n", s->count);
		} else if (IS_INT(t->type) || IS_LONG(t->type)) {
			/* 
			 * There are only forms of ``int'' and ``long''
			 * constants
			 */
			x_fprintf(out, "\tpushl $");
			cross_print_value_by_type(out, t->data, t->type, 0);
			x_fputc('\n', out);
		} else if (IS_LLONG(t->type)) {	
			unimpl();
		} else {
			puts("BUG in NASM emit_push()");
			exit(EXIT_FAILURE);
		}
	} else if (vr->from_ptr) {	
		x_fprintf(out, "\tpushl [%%%s]\n", /*ascii_type,*/
				vr->from_ptr->pregs[0]->name);
	} else {
		unimpl();
	}

	f->total_allocated += 4;
}

static void
emit_allocstack(struct function *f, size_t nbytes) {
	(void) f;
	x_fprintf(out, "\tsubl $%lu, %%esp\n", (unsigned long)nbytes);
}


static void
emit_freestack(struct function *f, size_t *nbytes) {
	if (nbytes == NULL) {
		/* Procedure outro */
		if (f->total_allocated != 0) {
			x_fprintf(out, "\taddl $%lu, %%esp\n",
				(unsigned long)f->total_allocated);
		}	
		x_fprintf(out, "\tpop %%ebp\n");
	} else {
		if (*nbytes != 0) {
			x_fprintf(out, "\taddl $%lu, %%esp\n",
				(unsigned long)*nbytes);
			f->total_allocated -= *nbytes;
		}
	}
}

static void
emit_adj_allocated(struct function *f, int *nbytes) {
	f->total_allocated += *nbytes; 
}	

static int	cursect = 0;

static void
emit_setsection(int value) {
	char	*p = NULL;

	if (cursect == value) {
		return;
	}
	if (sysflag == OS_OSX) {
		p = generic_mach_o_section_name(value);
	} else {
		p = generic_elf_section_name(value);
	}

	if (p != NULL) {
		if (sysflag == OS_OSX) {
			x_fprintf(out, ".%s\n", p);
		} else {
			x_fprintf(out, ".section .%s\n", p);
		}
	}	
	cursect = value;
}

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

		if (d->stack_addr) {
			x_fprintf(out, "-%ld(%%ebp)",
				d->stack_addr->offset);
		} else {
			x_fprintf(out, "%s", d->dtype->name);
		}
	} else if (r != NULL) {	
		x_fprintf(out, "%%%s", r->name);
	} else if (vr->from_const) {
		if (backend->arch == ARCH_AMD64) {
			amd64_print_mem_operand_gas(vr, NULL);
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
			x_fprintf(out, "\t%s%c ",
				p, size_to_gaspostfix(vr->size, vr->type));
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
			x_fprintf(out, "\t%s ", p);
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
		/*_fprintf(out, "\t%s %%%s, ", p, r->name);*/
		x_fprintf(out, "\t%s ", p);
	}

	if (backend->arch == ARCH_AMD64) {
		amd64_print_mem_operand_gas(vr, NULL);
	} else {
		print_mem_operand(vr, NULL);
	}
	if (r->type != REG_FPR || !STUPID_X87(r)) {
		x_fprintf(out, ", %%%s", r->name);
	}	

	x_fputc('\n', out);
}

static void
emit_load_addrlabel(struct reg *r, struct icode_instr *label) {
	x_fprintf(out, "\tlea .%s, %%%s\n", label->dat, r->name);
}

static void
emit_comp_goto(struct reg *r) {
	x_fprintf(out, "\tjmp *%%%s\n", r->name);
}


/*
 * Takes vreg source arg - not preg - so that it can be either a preg
 * or immediate (where that makes sense!)
 */
static void
emit_store(struct vreg *dest, struct vreg *src) {
	char		*p = NULL;
	int		floating = 0;
	int		post = 0;
	static int	was_llong;

	if (dest->size) {
		post = size_to_gaspostfix(dest->size,
			dest->type);	
	}
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
			post = 0;
			floating = 0; /* because SSE is non-x87-like */
		}
	} else {
		if (dest->stack_addr != NULL || dest->from_const != NULL) {
			p = "mov";
		} else {
			p = "mov";
		}	
	}

	if (post) {
		x_fprintf(out, "\t%s%c ", p, post);
	} else {
		x_fprintf(out, "\t%s ", p);
	}	
	if (floating) {
		/* Already done - floating stores only ever come from st0 */
		print_mem_operand(dest, NULL);
		x_fputc('\n', out);
		return;
	}
	if (src->from_const) {
		print_mem_operand(src, NULL);
	} else {
		/* Must be register */
		if (was_llong) {
			x_fprintf(out, "%%%s, ", src->pregs[1]->name);
			was_llong = 0;
		} else {
			x_fprintf(out, "%%%s, ", src->pregs[0]->name);
			if (src->is_multi_reg_obj) {
				was_llong = 1;
			}
		}
	}
	print_mem_operand(dest, NULL);
	x_fputc('\n', out);
}


static void
emit_neg(struct reg **dest, struct icode_instr *src) {
	(void) src;
	if (dest[0]->type == REG_FPR) {
		/* fchs only works with TOS! */
		x_fprintf(out, "\tfchs\n");
	} else {
		x_fprintf(out, "\tneg %%%s\n", dest[0]->name);
		if (src->src_vreg->is_multi_reg_obj) {
			x_fprintf(out, "\tadc $0, %%%s\n", dest[1]->name);
			x_fprintf(out, "\tneg %%%s\n", dest[1]->name);
		}	
	}	
}	


static void
emit_sub(struct reg **dest, struct icode_instr *src) {
	if (dest[0]->type == REG_FPR) {
		if (STUPID_X87(dest[0])) {
			/*
			 * 05/27/08: fsubrp instead of fsubp because the order
			 * was reversed... We used to use fsubp in the nasm
			 * emitter but that one generated fsubrp instead
			 * automatically! XXX why?
			 */
			x_fprintf(out, "\tfsubrp ");
		} else {
			x_fprintf(out, "\tsubs%c ",
				src->src_vreg->type->code == TY_FLOAT? 's': 'd');
		}
	} else {
		x_fprintf(out, "\tsub%c ",
			size_to_gaspostfix(dest[0]->size,
				src->dest_vreg->type));	
	}
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);

	x_fprintf(out, ", %%%s", dest[0]->name);
	x_fputc('\n', out);
	if (src->src_vreg->is_multi_reg_obj
		&& src->dest_vreg->is_multi_reg_obj) { /* for ptr arit */
		/* long long */
		x_fprintf(out, "\n\tsbb %%%s, %%%s\n",
			src->src_pregs[1]->name, dest[1]->name);	
	}	
}

static void
emit_add(struct reg **dest, struct icode_instr *src) {
	if (dest[0]->type == REG_FPR) {
		if (STUPID_X87(dest[0])) {
			x_fprintf(out, "\tfaddp ");
		} else {
			x_fprintf(out, "\tadds%c ",
				src->src_vreg->type->code == TY_FLOAT? 's': 'd');
		}
	} else {	
		x_fprintf(out, "\tadd ");
	}	
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fprintf(out, ", %%%s", dest[0]->name);
	if (src->src_vreg->is_multi_reg_obj
		&& src->dest_vreg->is_multi_reg_obj) { /* for ptr arit */
		/* long long */
		x_fprintf(out, "\n\tadc %%%s, %%%s\n",
			src->src_pregs[1]->name,
			dest[1]->name);
	}	
	x_fputc('\n', out);
}

static void
make_divmul_call(struct icode_instr *src,
	const char *func,
	int want_remainder) {

	int	extra_pushed = 4;
	int	osx_alignment = 0;

	if (want_remainder != -1) {
		extra_pushed += 4;
	}

	if (sysflag == OS_OSX) {
		if (extra_pushed == 4) {
			/* 
			 * 28 bytes passed, 8 for ebp/ret = 36 (need 12
			 * for 16-byte alignment)
			 */
			osx_alignment = 12;
		} else {
			/* 
			 * 32 bytes passed, 8 for ebp/ret = 40 (need 8 
			 * for 16-byte alignment)
			 */
			osx_alignment = 8;
		} 
		x_fprintf(out, "\tsubl $%d, %%esp\n",
			osx_alignment);
	}
	x_fprintf(out, "\tsubl $4, %%esp\n");
	x_fprintf(out, "\tmovl %%%s, (%%esp)\n", src->dest_pregs[1]->name);
	x_fprintf(out, "\tsubl $4, %%esp\n");
	x_fprintf(out, "\tmovl %%%s, (%%esp)\n", src->dest_pregs[0]->name);
	x_fprintf(out, "\tsubl $4, %%esp\n");
	x_fprintf(out, "\tmovl %%%s, (%%esp)\n", src->src_pregs[1]->name);
	x_fprintf(out, "\tsubl $4, %%esp\n");
	x_fprintf(out, "\tmovl %%%s, (%%esp)\n", src->src_pregs[0]->name);
	x_fprintf(out, "\tlea 8(%%esp), %%eax\n");
	x_fprintf(out, "\tlea (%%esp), %%ecx\n");

	/* Push data size */
	x_fprintf(out, "\tpushl $64\n");

	if (want_remainder != -1) {
		x_fprintf(out, "\tpushl $%d\n", want_remainder);
	}
	
	/* Push src address */
	x_fprintf(out, "\tpushl %%ecx\n");
	/* Push dest address */
	x_fprintf(out, "\tpushl %%eax\n");
/*	x_fprintf(out, "\tcall %s\n", func);*/
	emit_call(func);

	/*
	 * Result is saved in destination stack buffer - let's move it
	 * to eax:edx
	 */
	x_fprintf(out, "\tmovl %d(%%esp), %%eax\n", 16+extra_pushed);
	x_fprintf(out, "\tmovl %d(%%esp), %%edx\n", 20+extra_pushed);
	if (sysflag == OS_OSX) {
		x_fprintf(out, "\taddl $%d, %%esp\n",
			osx_alignment);
	}
	x_fprintf(out, "\taddl $%d, %%esp\n", 24+extra_pushed);
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
			x_fprintf(out, "\tcltd\n");
		} else {
			x_fprintf(out, "\txor %%edx, %%edx\n");
		}	
	}

	if (IS_FLOATING(ty->code)) {
		/*
		 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXx
		 * XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXx
		 * that goddamn fdivp does not work as i expect
		 * it to with that god awful terrible gas ^%$&@!~!~
		 * so fxch operands
		 */
		if (STUPID_X87(src->src_pregs[0])) {
			x_fprintf(out, "\tfxch %%st(1)\n");
			x_fprintf(out, "\tfdivp ");
		} else {
			x_fprintf(out, "\tdivs%c %%%s, %%%s\n",
				ty->code == TY_FLOAT? 's': 'd',
				src->src_pregs[0]->name,
				dest[0]->name);
			return;
		}
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
		x_fprintf(out, "\tmov %%edx, %%%s\n", dest[0]->name);
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
		if (STUPID_X87(dest[0])) {
			x_fprintf(out, "\tfmulp ");
		} else {
			x_fprintf(out, "\tmuls%c ",
				ty->code == TY_FLOAT? 's': 'd');
		}
	} else if (ty->sign == TOK_KEY_UNSIGNED) {
		x_fprintf(out, "\tmul ");
	} else {
		/* signed integer multiplication */
		/* XXX should use mul for pointer arithmetic :( */
		x_fprintf(out, "\timul ");
	}	
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	if (IS_FLOATING(ty->code)) {
		x_fprintf(out, ", %%%s\n", src->dest_pregs[0]->name);
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
			x_fprintf(out, "\tpushl %%ecx\n");
#if 0
			x_fprintf(out, "\tmov $%d, %%ecx\n",
				*(int *)src->src_vreg->from_const->
				data);
#endif
			x_fprintf(out, "\tmov $");
			cross_print_value_by_type(out,
				src->src_vreg->from_const->data,
				TY_INT, 'd');
			x_fprintf(out, ", %%ecx\n");
			
		}
		x_fprintf(out, "\tshld %%cl, %%%s, %%%s\n",
			dest[0]->name, dest[1]->name);	
		x_fprintf(out, "\t%s %%cl, %%%s\n",
			is_signed? "sal": "shl", dest[0]->name);
/*		if (!is_signed) {*/
			x_fprintf(out, "\tand $32, %%ecx\n");
			x_fprintf(out, "\tje .shftdone%lu\n", shift_idx);
			x_fprintf(out, "\tmov %%%s, %%%s\n",
				dest[0]->name, dest[1]->name);
			x_fprintf(out, "\txor %%%s, %%%s\n",
				dest[0]->name, dest[0]->name);
			x_fprintf(out, ".shftdone%lu:\n", shift_idx++);
/*		}*/
		if (src->src_vreg->from_const) {
			x_fprintf(out, "\tpop %%ecx\n");
		}	
	} else {
		if (src->src_vreg->from_const) {
#if 0
			x_fprintf(out, "\tshl $%d, %%%s\n",
				*(int *)src->src_vreg->from_const->data,
				dest[0]->name);
#endif
			x_fprintf(out, "\t%s $",
				is_signed? "sal": "shl");
			cross_print_value_by_type(out,
				src->src_vreg->from_const->data,
				TY_INT, 'd');
			x_fprintf(out, ", %%%s\n", dest[0]->name);
		} else {	
			x_fprintf(out, "\t%s %%cl, %%%s\n",
				is_signed? "sal": "shl", dest[0]->name);
		}
	}	
}


/* XXX sar for signed values !!!!!!!! */
static void
emit_shr(struct reg **dest, struct icode_instr *src) {
	int	is_signed = src->dest_vreg->type->sign != TOK_KEY_UNSIGNED;

	if (src->dest_vreg->is_multi_reg_obj) {
		x_fprintf(out, "\tshrdl %%cl, %%%s, %%%s\n",
			dest[1]->name, dest[0]->name);	
		x_fprintf(out, "\t%s %%cl, %%%s\n",
			is_signed? "sar": "shr", dest[1]->name);
/*		if (!is_signed) {*/
			x_fprintf(out, "\tand $32, %%ecx\n");
			x_fprintf(out, "\tje .shftdone%lu\n", shift_idx);
			x_fprintf(out, "\tmov %%%s, %%%s\n",
				dest[1]->name, dest[0]->name);
			/*
			 * 06/21/08: This was always sign-extending! Zero-
			 * extend for unsigned values
			 */
			if (is_signed) {
				x_fprintf(out, "\t%s $31, %%%s\n",
					is_signed? "sar": "shr", dest[1]->name);
			} else {
				x_fprintf(out, "\tmovl $0, %%%s\n",
					dest[1]->name);
			}
			x_fprintf(out, ".shftdone%lu:\n", shift_idx++);
/*		}*/
	} else {	
		if (src->src_vreg->from_const) {
#if 0
			x_fprintf(out, "\tshr $%d, %%%s\n",
				*(int *)src->src_vreg->from_const->data,
				dest[0]->name);
#endif
			x_fprintf(out, "\t%s $", is_signed? "sar": "shr");
			cross_print_value_by_type(out,
				src->src_vreg->from_const->data,
				TY_INT, 'd');
			x_fprintf(out, ", %%%s\n", dest[0]->name);
		} else {	
			x_fprintf(out, "\t%s %%cl, %%%s\n",
				is_signed? "sar": "shr",dest[0]->name);
		}	
	}	
}

static void
emit_or(struct reg **dest, struct icode_instr *src) {
	x_fprintf(out, "\tor ");
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fprintf(out, ", %%%s\n", dest[0]->name);
	if (src->src_vreg->is_multi_reg_obj) {
		x_fprintf(out, "\tor %%%s, %%%s\n",
			src->src_pregs[1]->name, dest[1]->name);
	}
}

static void
emit_and(struct reg **dest, struct icode_instr *src) {
	x_fprintf(out, "\tand ", dest[0]->name);
	print_mem_or_reg(src->src_pregs[0], src->src_vreg);
	x_fprintf(out, ", %%%s\n", dest[0]->name);
	if (src->src_vreg->is_multi_reg_obj) {
		x_fprintf(out, "\tand %%%s, %%%s\n",
			src->src_pregs[1]->name, dest[1]->name);
	}	
}

static void
emit_xor(struct reg **dest, struct icode_instr *src) {
	x_fprintf(out, "\txor ");
	if (src->src_vreg == NULL) {
		x_fprintf(out, "%%%s, %%%s\n",
			dest[0]->name, dest[0]->name);
		if (src->src_vreg->is_multi_reg_obj) {
			x_fprintf(out, "\txor %%%s, %%%s\n",
				dest[1]->name, dest[1]->name);
		}	
	} else {	
		print_mem_or_reg(src->src_pregs[0], src->src_vreg);
		x_fprintf(out, ", %%%s\n", dest[0]->name);
		if (src->src_vreg->is_multi_reg_obj) {
			x_fprintf(out, "\txor %%%s, %%%s\n",
				src->src_pregs[1]->name, dest[1]->name);
		}	
	}
}

static void
emit_not(struct reg **dest, struct icode_instr *src) {
	(void) src;
	x_fprintf(out, "\tnot %%%s\n", dest[0]->name);
	if (src->src_vreg->is_multi_reg_obj) {
		x_fprintf(out, "\tnot %%%s\n", dest[1]->name);
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
		x_fprintf(out, "\tret $4\n");
	} else {		
		x_fprintf(out, "\tret\n"); /* XXX */
	}	
}

extern struct icode_instr	*last_x87_cmp;
extern struct icode_instr	*last_sse_cmp;

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
				if (!(src->hints
				& HINT_INSTR_NEXT_NOT_SECOND_LLONG_WORD)) {
					/* 
					 * 06/02/08: Conditional for
					 * repeated cmps on same dword!
					 */
					was_llong = 1;
				}
				reg_idx = 1;
			} else {	
				reg_idx = 0;
			}
		}
		fprintf(out, "\tcmp ");
	}	
	if (src->src_pregs == NULL || src->src_vreg == NULL) {
		x_fprintf(out, "$0");
	} else {
		print_mem_or_reg(src->src_pregs[/*0*/reg_idx], src->src_vreg);
	}
	if (dest[0]->type == REG_FPR) {
		x_fprintf(out, ", %%%s", dest[0]->name);
	} else {
		x_fprintf(out, ", %%%s", dest[reg_idx]->name);
	}
	x_fputc('\n', out);
	if (need_ffree) {
		x_fprintf(out, "\tffree %%st\n");
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
			fprintf(out, "\tfucomip %%st(1), %%st(0)\n");
		} else {
			/*
			 * Since this is fp but not x87, it must be SSE.
			 */
			is_sse = 1;
			if (ii->dest_vreg->type->code == TY_FLOAT) {
				x_fprintf(out, "\tucomiss %%%s, %%%s\n",
					last_sse_cmp->dest_pregs[0]->name,
					last_sse_cmp->src_pregs[0]->name);
			} else {
				/* double */
				x_fprintf(out, "\tucomisd %%%s, %%%s\n",
					last_sse_cmp->dest_pregs[0]->name,
					last_sse_cmp->src_pregs[0]->name);
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
			x_fprintf(out, "\tpush %%rax\n"); /* save ah/al */
			x_fprintf(out, "\tpush %%rbx\n"); /* save bl */

			/*
			 * Now painfully construct flags in ah like
			 * lahf plus masking does on x86. It is VERY
			 * important to do the set stuff before or'ing
			 * because or also sets flags! (sadly I got
			 * this wrong first, which cost me a good hour
			 * :-() 
			 */
			x_fprintf(out, "\tsetc %%ah\n");
			x_fprintf(out, "\tsetp %%al\n");
			x_fprintf(out, "\tsetz %%bl\n");
			x_fprintf(out, "\tshl $2, %%al\n");
			x_fprintf(out, "\tshl $6, %%bl\n");
			x_fprintf(out, "\tor %%al, %%ah\n");
			x_fprintf(out, "\tor %%bl, %%ah\n");
		} else {
			x_fprintf(out, "\tpush %%eax\n"); /* save ah */
			x_fprintf(out, "\tlahf\n");

			/* Mask off unused flags */
			x_fprintf(out, "\tand $%d, %%ah\n", FP_EQU_MASK);
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
		x_fprintf(out, "\tcmp $%d, %%ah\n", cmp_with);

		if (backend->arch == ARCH_AMD64) {
			x_fprintf(out, "\tpop %%rbx\n");  /* restore bl */
			x_fprintf(out, "\tpop %%rax\n");  /* restore ah/al */
		} else {
			x_fprintf(out, "\tpop %%eax\n");  /* restore ah */
		}

		if (!is_sse) {
			/* Now free second used fp reg */
			x_fprintf(out, "\tffree %%st(0)\n");
		}

		/* Finally branch! */
		x_fprintf(out, "\t%s .%s\n",
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
	x_fprintf(out, "\t%s .%s\n", opcode, lname);
}

static void
print_reg_assign(
	struct reg *dest,
	struct reg *srcreg,
	size_t src_size,
	struct type *src_type) {

	if (dest->size == src_size || src_size == 8) {
		/* == 8 for long long on x86 */
		x_fprintf(out, "\tmov %s, ", dest->name);
	} else {	
		/* dest size > src size */
		if (src_type->sign == TOK_KEY_UNSIGNED) {	
			if (backend->arch == ARCH_AMD64
				&& dest->size == 8
				&& src_size == 4) {
				if (!isdigit(dest->name[1])) {
					/*
					 * e.g. mov eax, edx zero-extends
					 * upper 32 bits of rax
					 */
					x_fprintf(out, "mov ");
					dest = dest->composed_of[0];
				} else {
					/* XXX there must be a better way :( */
					x_fprintf(out, "\tpush %%rax\n");
					x_fprintf(out, "\tmov %%%s, %%eax\n",
						srcreg->name);
					x_fprintf(out, "\tmov %%rax, %%%s\n",
						dest->name);
					x_fprintf(out, "\tpop %%rax\n");
					return;
				}
			} else {	
				x_fprintf(out, "\tmovzx ");
			}	
		} else {
			if (backend->arch == ARCH_AMD64
				&& dest->size == 8
				&& src_size == 4) {
				/* dword -> qword = special case */
				x_fprintf(out, "\tmovslq ");
			} else {	
				x_fprintf(out, "\tmovsx ");
			}	
		}	
	}	

	x_fprintf(out, "%%%s, %%%s\n", srcreg->name, dest->name);
}


static void
emit_mov(struct copyreg *cr) {
	struct reg	*dest = cr->dest_preg;
	struct reg	*src = cr->src_preg;
	struct type	*src_type = cr->src_type;

	if (src == NULL) {
		/* Move null to register (XXX fp?)  */
		x_fprintf(out, "\tmov $0, %%%s\n",
			dest->name);
	} else if (dest->type == REG_FPR) {
		/* XXX ... */
		if (STUPID_X87(dest)) {
			emit_x86->fxch(dest, src);	
		} else {
			x_fprintf(out, "\tmovs%c %%%s, %%%s\n",
				src_type->code == TY_FLOAT? 's': 'd',
				src->name, dest->name);
		}	
	} else if (dest->size == src->size) {
		x_fprintf(out, "\tmov %%%s, %%%s\n", src->name, dest->name);
	} else if (dest->size > src->size) {
		print_reg_assign(dest, src, src->size, src_type);
		/*x_fprintf(out, "%s\n", src->name);*/
	} else {
		/* source larger than dest */
		src = get_smaller_reg(src, dest->size);
		x_fprintf(out, "\tmov %%%s, %%%s\n", src->name, dest->name);
	}	
}


static void
emit_setreg(struct reg *dest, int *value) {
	x_fprintf(out, "\tmov $%d, %%%s\n", *(int *)value, dest->name);
}

static void
emit_xchg(struct reg *r1, struct reg *r2) {
	x_fprintf(out, "\txchg %%%s, %%%s\n", r2->name, r1->name);
}

static void
emit_initialize_pic(struct function *f) {
	char			buf[128];
	static unsigned long	count;

	(void) f;
	if (sysflag == OS_OSX) {
		sprintf(buf, "\"L%011lu$pb\"", count);
		f->pic_label = n_xstrdup(buf);
		x_fprintf(out, "\tcall _Piclab%lu\n", count);
		x_fprintf(out, "%s:\n", buf);
		x_fprintf(out, "_Piclab%lu:\n", count);
		x_fprintf(out, "\tpopl %%ebx\n");
		++count;
	} else {
		sprintf(buf, "._Piclab%lu", count++);
		x_fprintf(out, "\tcall %s\n", buf);
		x_fprintf(out, "%s:\n", buf);
		x_fprintf(out, "\tpopl %%ebx\n");
		x_fprintf(out, "\taddl $_GLOBAL_OFFSET_TABLE_+[.-%s], %%ebx\n", buf);
	}
}	

static void
emit_addrof(struct reg *dest, struct vreg *src, struct vreg *structtop) {
	struct decl	*d;
	long		offset = 0;
	char		*sign = NULL;
	char		*base_pointer_frame;
	char		*base_pointer_stack;

	if (backend->arch == ARCH_AMD64) {
		base_pointer_frame = "rbp";
		base_pointer_stack = "rsp";
	} else {
		base_pointer_frame = "ebp";
		base_pointer_stack = "esp";
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
				sign = "";
			} else {
				sign = "-";
			}	
			offset = d->stack_addr->offset;
		} else if (IS_THREAD(d->dtype->flags)) {
			/* 02/02/08: __thread variable */
			if (backend->arch == ARCH_AMD64) {
				x_fprintf(out, "\tmovq %%fs:0, %%%s\n", dest->name);
				x_fprintf(out, "\tleaq %s@TPOFF(%%%s), %%%s\n",
					d->dtype->name, dest->name, dest->name);
			} else {
				/* x86 */
				x_fprintf(out, "\tmovl %%gs:0, %%%s\n", dest->name);
				x_fprintf(out, "\tleal %s@NTPOFF(%%%s), %%%s\n",
					d->dtype->name, dest->name, dest->name);
			}
			if (src->parent != NULL) {
				x_fprintf(out, "\tadd $%ld, %%%s\n",
					calc_offsets(src), dest->name);
			}
			return;
		} else if (picflag) {
			if (d->dtype->is_func && d->dtype->tlist->type == TN_FUNCTION) {
				if (backend->arch == ARCH_AMD64) {
					if (sysflag == OS_OSX) {
#if 0
						x_fprintf(out, "\tmovq %s%s(%%rip), %%%s\n",
							IS_ASM_RENAMED(d->dtype->flags)? "": "_",
							d->dtype->name, dest->name);
#endif
						x_fprintf(out, "\tmovq %s%s@GOTPCREL(%%rip), %%%s\n",
							IS_ASM_RENAMED(d->dtype->flags)? "": "_",
							d->dtype->name, dest->name);
					} else {
						x_fprintf(out, "\tmovq %s@PLT, %%%s\n",
							d->dtype->name, dest->name);
					}
				} else {
					/*
					 * XXX hmmm lea here but mov for data.
					 * Seems correct though
					 */
					if (sysflag == OS_OSX) {
						/*
						 * XXX We always use the stub entry even for
						 * local functions, which don't need it. This
						 * should probably be distinguished
						 * 02/15/09: WRONG we are forced to distinguish
						 */
						/*if (d->dtype->storage == TOK_KEY_EXTERN) {*/
						if (1) { //!d->dtype->is_def) {
							if (IS_ASM_RENAMED(d->dtype->flags)) {
								osx_call_renamed = 1;
							}
							add_osx_fcall(d->dtype->name, d, 1);
							x_fprintf(out, "\tlea L_%s$non_lazy_ptr-%s(%%ebx), %%%s\n",
								d->dtype->name,
								curfunc->pic_label,
								dest->name);
							x_fprintf(out, "\tmov (%%%s), %%%s\n", 
								dest->name, dest->name);
							osx_call_renamed = 0;
						} else {
							x_fprintf(out, "\tlea _%s-%s(%%ebx), %%%s\n",
								d->dtype->name,
								curfunc->pic_label,
								dest->name);
						}
					} else {
						x_fprintf(out, "\tlea %s@GOT(%%ebx), %%%s\n",
							d->dtype->name, dest->name);
					}
				}
				return;
			} else if (d->dtype->storage == TOK_KEY_EXTERN
				|| d->dtype->storage == TOK_KEY_STATIC) {
				if (backend->arch == ARCH_AMD64) {
					if (sysflag == OS_OSX) {
						x_fprintf(out, "\tmovq %s%s@GOTPCREL(%%rip), %%%s\n",
							IS_ASM_RENAMED(d->dtype->flags)? "": "_",
							d->dtype->name, dest->name);
					} else {
						x_fprintf(out, "\tmovq %s@GOTPCREL(%%rip), %%%s\n",
							d->dtype->name, dest->name);
					}
				} else {
					/* x86 */
					if (sysflag == OS_OSX) {
/*						x_fprintf(out, "\tlea _%s-%s(%%ebx), %%%s\n",*/
						/*
						 * 02/08/09: Always use a non_lazy_ptr reference
						 * XXX As with function pointers, we should probably
						 * distinguish between local symbols and extern ones
						 * 02/14/09: We HAVE to, in fact! It is apparently
						 * completely impossible to create a correct indirect
						 * symbol for local variables :-(
						 */
						if (1) { //d->dtype->storage == TOK_KEY_EXTERN) {
							if (IS_ASM_RENAMED(d->dtype->flags)) {
								osx_call_renamed = 1;
							}
							add_osx_fcall(d->dtype->name, d, 1); /* XXX misnomer */
							x_fprintf(out, "\tlea L_%s$non_lazy_ptr-%s(%%ebx), %%%s\n",
								d->dtype->name,
								curfunc->pic_label,
								dest->name);
							/*
							 * 02/14/09: Looks like we need another level of
							 * indirection
							 */
							x_fprintf(out, "\tmov (%%%s), %%%s\n",
								dest->name, dest->name); 
							osx_call_renamed = 0;
						} else {
							x_fprintf(out, "\tlea _%s-%s(%%ebx), %%%s\n",
								d->dtype->name,
								curfunc->pic_label,
								dest->name);
						}
					} else {
						x_fprintf(out, "\tmov %s@GOT(%%ebx), %%%s\n",
							d->dtype->name, dest->name);	
					}
				}
				if (src->parent != NULL) {
					x_fprintf(out, "\tadd $%ld, %%%s\n",
						calc_offsets(src), dest->name);
				}
				return;
			}
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
			sprintf(buf, "._Str%lu", ts->count);
			name = buf;
		} else if (IS_FLOATING(src->from_const->type)) {
			struct ty_float		*tf = src->from_const->data;
			sprintf(buf, "_Float%lu", tf->count); /* XXX hmm need preceding . ? */
			name = buf;
		}
		if (name != NULL) {
			if (backend->arch == ARCH_AMD64) {
				x_fprintf(out, "\tmovq %s@GOTPCREL(%%rip), %%%s\n",
					name, dest->name);
			} else {
				if (sysflag == OS_OSX) {
					x_fprintf(out, "\tlea %s-%s(%%ebx), %%%s\n",
						name, curfunc->pic_label, dest->name);
				} else {
					x_fprintf(out, "\tmov %s@GOT(%%ebx), %%%s\n",
						name, dest->name);	
				}
			}
			return;
		}
	}

	if (src && src->parent != NULL) {
		/* Structure or union type */
		long	soff = (long)calc_offsets(src); /* XXX */

		if (d != NULL) {
			if (sign != NULL) {
				/* Must be stack address */
				if (strcmp(sign, "-") == 0) { /* XXX :( */
					if (soff >= offset) {
						sign = "";
						offset = soff - offset;
					} else {
						offset -= soff;
					}
				} else {
					offset += soff;
				}
			}

			if (d->stack_addr != NULL) {
				/* XXX assumes frame pointer always to be used */
				x_fprintf(out, "\tlea %s%ld(%%%s), %%%s",
					sign, offset, base_pointer_frame, dest->name);
			} else {
				/* Static */
				/* 08/01/07: Wow this didn't work?!?!? */
				x_fprintf(out, "\tlea %s+%ld, %%%s",
					d->dtype->name, soff, dest->name);
			}	
		} else if (structtop->from_ptr) {
			x_fprintf(out, "\tlea %ld(%%%s), %%%s",
				soff,	
				structtop->from_ptr->pregs[0]->name,
				dest->name);
		} else  {	
			printf("hm attempt to take address of %s\n",
				src->type->name);	
			unimpl();
		}
		x_fputc('\n', out);
	} else if (src && src->from_ptr) {	
		x_fprintf(out, "\tmov %%%s, %%%s\n",
			src->from_ptr->pregs[0]->name, dest->name);
	} else if (src && src->from_const && src->from_const->type == TOK_STRING_LITERAL) {
		/* 08/21/08: This was missing */
		emit_load(dest, src);
	} else if (src && src->stack_addr) {
		/*
		 * 07/26/12: For some reason anonymous stack items hadn't been
		 * supported yet
		 */
		x_fprintf(out, "\tlea ");
		if (backend->arch == ARCH_AMD64) {
			amd64_print_mem_operand_gas(src, NULL);
		} else {
			print_mem_operand(src, NULL);
		}
		x_fprintf(out, ", %%%s\n", dest->name); 
	} else {
		if (d && d->stack_addr) {
			if (src == NULL) {
				/* Move past object */
				offset += d->stack_addr->nbytes;
			}

			/* XXX assumes frame pointer always to be used */
			x_fprintf(out, "\tlea %s%ld(%%%s), %%%s\n",
				sign, offset, base_pointer_frame, dest->name);	
		} else if (d) {
			/*
			 * Must be static variable - symbol itself is
			 * address
			 */
			x_fprintf(out, "\tmov $%s, %%%s\n",
				d->dtype->name, dest->name);
		} else {
			printf("BUG: Cannot take address of item!\n");
			printf("%p\n", src->stack_addr);
			abort();
		}
	}
}

/* XXX hmm this only works with TOS?! */
static void
emit_fxch(struct reg *r, struct reg *r2) {
	if (r == &x86_fprs[0]) {
		x_fprintf(out, "\tfxch %%%s\n", r2->name);
	} else {
		x_fprintf(out, "\tfxch %%%s\n", r->name);
	}	
}

static void
emit_ffree(struct reg *r) {
	x_fprintf(out, "\tffree %%%s\n", r->name);
}	


static void
emit_fnstcw(struct vreg *vr) {
	if (backend->arch == ARCH_X86) {
		x_fprintf(out, "\tfnstcw (%s)\n", vr->type->name);
	} else {
		x_fprintf(out, "\tfnstcw %s(%%rip)\n", vr->type->name);
	}
}

static void
emit_fldcw(struct vreg *vr) {
	if (backend->arch == ARCH_X86) {
		x_fprintf(out, "\tfldcw (%s)\n", vr->type->name);
	} else {
		x_fprintf(out, "\tfldcw %s(%%rip)\n", vr->type->name);
	}
}

static char *
get_symbol_value_representation(const char *symname, int is_func) {
	static char	buf[1024];
	if (0) {  /*picflag) {*/
	} else {
		sprintf(buf, "$.%s", symname);
	}
	return buf;
}


/*
 * Copy initializer to automatic variable of aggregate type
 */
static void
emit_copyinit(struct decl *d) {
	if (sysflag == OS_OSX) {
		/*
		 * Ensure 16-byte alignment (we pass 12, and need to
		 * account for the callee using 8 bytes for ret addr
		 * and ebp, so 12+12+8 = 32)
		 */
		x_fprintf(out, "\tsubl $12, %%esp\n");
	}
	x_fprintf(out, "\tpushl $%lu\n",
		(unsigned long)backend->get_sizeof_type(d->dtype, NULL)  /*d->vreg->size*/);

	/*
	 * 07/26/12: This was missing support for position independence?!!?
	 * Support it in an ad hoc manner here
	 */
	/*x_fprintf(out, "\tpushl $.%s\n", d->init_name->name);*/ /* XXX */
	x_fprintf(out, "\tpushl %s\n",
		get_symbol_value_representation(d->init_name->name, 0));
	x_fprintf(out, "\tlea -%lu(%%ebp), %%eax\n", d->stack_addr->offset);
	x_fprintf(out, "\tpushl %%eax\n");
	if (sysflag == OS_OSX) {
		add_osx_fcall("memcpy", NULL, 0);
		x_fprintf(out, "\tcall L_memcpy$stub\n");
		x_fprintf(out, "\taddl $24, %%esp\n");
	} else {
		x_fprintf(out, "\tcall memcpy\n");
		x_fprintf(out, "\taddl $12, %%esp\n");
	}
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
	static unsigned long lcount;

	/* Get temporary register not used by our pointer(s), if any */
	if (sysflag != OS_OSX) {
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
	} else {
		x_fprintf(out, "\tpushl %%eax\n");
		x_fprintf(out, "\tpushl %%ecx\n");
		x_fprintf(out, "\tpushl %%esi\n");
		x_fprintf(out, "\tpushl %%edi\n");
		tmpreg = &x86_gprs[0];
	}

	x_fprintf(out, "\tpushl $%lu\n", (unsigned long)cs->src_vreg->size);
	if (cs->src_from_ptr == NULL) {
		if (cs->src_vreg->parent) {
			stop = get_parent_struct(cs->src_vreg);
		} else {
			stop = NULL;
		}	
		emit_addrof(tmpreg, cs->src_vreg, stop); 
		x_fprintf(out, "\tpushl %%%s\n", tmpreg->name);
	} else {
		if (cs->src_vreg->parent) {
			x_fprintf(out, "\tadd $%lu, %%%s\n",
				calc_offsets(cs->src_vreg),	
				/*cs->src_vreg->memberdecl->offset,*/
				cs->src_from_ptr->name);
		}	
		x_fprintf(out, "\tpushl %%%s\n", cs->src_from_ptr->name);
	}

	if (sysflag == OS_OSX) {
		x_fprintf(out, "\tmovl 20(%%esp), %%eax\n");
	}
	if (cs->dest_from_ptr == NULL) {
		if (cs->dest_vreg->parent) {
			stop = get_parent_struct(cs->dest_vreg);
		} else {
			stop = NULL;
		}	
		emit_addrof(tmpreg, cs->dest_vreg, stop); 
		x_fprintf(out, "\tpushl %%%s\n", tmpreg->name);
	} else {	
		if (cs->dest_vreg->parent) {
			x_fprintf(out, "\tadd $%lu, %%%s\n",
				calc_offsets(cs->dest_vreg),	
				/*cs->dest_vreg->memberdecl->offset,*/
				cs->dest_from_ptr->name);
		}
		x_fprintf(out, "\tpushl %%%s\n", cs->dest_from_ptr->name);
	}	
	if (sysflag == OS_OSX) {
#if 0
		add_osx_fcall("memcpy", NULL, 0);
		x_fprintf(out, "\tcall L_memcpy$stub\n");
		x_fprintf(out, "\taddl $24, %%esp\n");
#endif
		x_fprintf(out, "\tpopl %%edi\n");
		x_fprintf(out, "\tpopl %%esi\n");
		x_fprintf(out, "\tpopl %%ecx\n");
		x_fprintf(out, "\t.copystruct%lu:\n", lcount);
		x_fprintf(out, "\tmov (%%esi), %%al\n");
		x_fprintf(out, "\tmov %%al, (%%edi)\n");
		x_fprintf(out, "\tinc %%edi\n");
		x_fprintf(out, "\tinc %%esi\n");
#if 0
		/* This loop yields a SEGFAULT on OSX. Give me a BREAK!!!! */
		x_fprintf(out, "\tloop .copystruct%lu\n", lcount++);
#endif
		x_fprintf(out, "\tdec %%ecx\n");
		x_fprintf(out, "\tcmp $0, %%ecx\n");
		x_fprintf(out, "\tjne .copystruct%lu\n", lcount++);

		x_fprintf(out, "\tpopl %%edi\n");
		x_fprintf(out, "\tpopl %%esi\n");
		x_fprintf(out, "\tpopl %%ecx\n");
		x_fprintf(out, "\tpopl %%eax\n");
	} else {
		x_fprintf(out, "\tcall memcpy\n");
		x_fprintf(out, "\taddl $12, %%esp\n");
	}
}

static void
emit_intrinsic_memcpy(struct int_memcpy_data *data) {
	struct reg	*dest = data->dest_addr;
	struct reg	*src = data->src_addr;
	struct reg	*nbytes = data->nbytes;
	struct reg	*temp = data->temp_reg;
	static int	labelcount;
	
	if (data->type == BUILTIN_MEMSET && src != temp) {
		x_fprintf(out, "\tmov %%%s, %%%s\n", src->name, temp->name);
	}
	x_fprintf(out, "\tcmp $0, %%%s\n", nbytes->name);
	x_fprintf(out, "\tje .Memcpy_done%d\n", labelcount);
	x_fprintf(out, ".Memcpy_start%d:\n", labelcount);
	if (data->type == BUILTIN_MEMCPY) {
		x_fprintf(out, "\tmov (%%%s), %%%s\n", src->name, temp->name);
	}
	x_fprintf(out, "\tmov %%%s, (%%%s)\n", temp->name, dest->name);
	x_fprintf(out, "\tinc %%%s\n", dest->name);
	if (data->type == BUILTIN_MEMCPY) {
		x_fprintf(out, "\tinc %%%s\n", src->name);
	}
	x_fprintf(out, "\tdec %%%s\n", nbytes->name);
	x_fprintf(out, "\tcmp $0, %%%s\n", nbytes->name);
	x_fprintf(out, "\tjne .Memcpy_start%d\n", labelcount);
	x_fprintf(out, ".Memcpy_done%d:\n", labelcount);
	++labelcount;
}

static void
emit_zerostack(struct stack_block *sb, size_t nbytes) {
	if (sysflag == OS_OSX) {
		/*
		 * 16-byte alignment (we pass 12, callee uses
		 * 8 for ret addr and ebp, so we need 12 to
		 * get to a multiple of 16)
		 */
		x_fprintf(out, "\tsub $12, %%esp\n");
	}
	x_fprintf(out, "\tpushl $%lu\n", (unsigned long)nbytes);
	x_fprintf(out, "\tpushl $0\n");
	x_fprintf(out, "\tleal -%lu(%%ebp), %%ecx\n",
		(unsigned long)sb->offset);
	x_fprintf(out, "\tpush %%ecx\n");
	if (sysflag == OS_OSX) {
		add_osx_fcall("memset", NULL, 0);
		x_fprintf(out, "\tcall L_memset$stub\n");
		x_fprintf(out, "\taddl $24, %%esp\n");
	} else {
		x_fprintf(out, "\tcall memset\n");
		x_fprintf(out, "\taddl $12, %%esp\n");
	}
}	

static void
emit_alloca(struct allocadata *ad) {
	if (sysflag == OS_OSX) {
		/*
		 * 16-byte alignment (we pass 4, callee uses
		 * 8 for ret addr and ebp, so we need 4 to
		 * get to a multiple of 16)
		 */
		x_fprintf(out, "\tsub $4, %%esp\n");
	}
	x_fprintf(out, "\tpush %%%s\n", ad->size_reg->name);
	if (sysflag == OS_OSX) {
		add_osx_fcall("malloc", NULL, 0);
		x_fprintf(out, "\tcall L_malloc$stub\n");
		x_fprintf(out, "\taddl $8, %%esp\n");
	} else {
		x_fprintf(out, "\tcall malloc\n");
		x_fprintf(out, "\taddl $4, %%esp\n");
	}
	if (ad->result_reg != &x86_gprs[0]) {
		x_fprintf(out, "\tmov %%eax, %%%s\n",
			ad->result_reg->name);
	}
}

static void
emit_dealloca(struct stack_block *sb, struct reg *r) {
	char	*regname = r? r->name: "ecx";

	if (sysflag == OS_OSX) {
		/*
		 * 16-byte alignment (we pass 4, callee uses
		 * 8 for ret addr and ebp, so we need 4 to
		 * get to a multiple of 16)
		 */
		x_fprintf(out, "\tsub $4, %%esp\n");
	}

	x_fprintf(out, "\tmov -%lu(%%ebp), %%%s\n",
		(unsigned long)sb->offset,
		regname);
	x_fprintf(out, "\tpush %%%s\n", regname);
	if (sysflag == OS_OSX) {
		add_osx_fcall("free", NULL, 0);
		x_fprintf(out, "\tcall L_free$stub\n");
		x_fprintf(out, "\taddl $8, %%esp\n");
	} else {
		x_fprintf(out, "\tcall free\n");
		x_fprintf(out, "\taddl $4, %%esp\n");
	}
}	

static void
emit_alloc_vla(struct stack_block *sb) {
	if (sysflag == OS_OSX) {
		/*
		 * 16-byte alignment (we pass 4, callee uses
		 * 8 for ret addr and ebp, so we need 4 to
		 * get to a multiple of 16)
		 */
		x_fprintf(out, "\tsub $4, %%esp\n");
	}
	x_fprintf(out, "\tmov -%lu(%%ebp), %%ecx\n",
		(unsigned long)sb->offset - backend->get_ptr_size());	
	x_fprintf(out, "\tpush %%ecx\n");
	if (sysflag == OS_OSX) {
		add_osx_fcall("malloc", NULL, 0);
		x_fprintf(out, "\tcall L_malloc$stub\n");
		x_fprintf(out, "\taddl $8, %%esp\n");
	} else {
		x_fprintf(out, "\tcall malloc\n");
		x_fprintf(out, "\taddl $4, %%esp\n");
	}
	x_fprintf(out, "\tmov %%eax, -%lu(%%ebp)\n",
		(unsigned long)sb->offset);	
}

static void
emit_dealloc_vla(struct stack_block *sb, struct reg *r) {
	char	*regname = r? r->name: "ecx";

	if (sysflag == OS_OSX) {
		/*
		 * 16-byte alignment (we pass 4, callee uses
		 * 8 for ret addr and ebp, so we need 4 to
		 * get to a multiple of 16)
		 */
		x_fprintf(out, "\tsub $4, %%esp\n");
	}
	x_fprintf(out, "\tmov -%lu(%%ebp), %%%s\n",
		(unsigned long)sb->offset,
		regname);
	x_fprintf(out, "\tpush %%%s\n", regname);
	if (sysflag == OS_OSX) {
		add_osx_fcall("free", NULL, 0);
		x_fprintf(out, "\tcall L_free$stub\n");
		x_fprintf(out, "\taddl $8, %%esp\n");
	} else {
		x_fprintf(out, "\tcall free\n");
		x_fprintf(out, "\taddl $4, %%esp\n");
	}
}	

static void
emit_put_vla_size(struct vlasizedata *data) {
	x_fprintf(out, "\tmov %%%s, -%lu(%%ebp)\n",
		data->size->name,
		(unsigned long)data->blockaddr->offset - data->offset);
}

static void
emit_retr_vla_size(struct vlasizedata *data) {
	x_fprintf(out, "\tmov -%lu(%%ebp), %%%s\n",
		(unsigned long)data->blockaddr->offset - data->offset,
		data->size->name);
}

static void
emit_load_vla(struct reg *r, struct stack_block *sb) {
	x_fprintf(out, "\tmov -%lu(%%ebp), %%%s\n",
		(unsigned long)sb->offset, r->name);
}

static void
emit_frame_address(struct builtinframeaddressdata *dat) {
	x_fprintf(out, "\tmov %%ebp, %%%s\n", dat->result_reg->name);
}

static void
emit_cdq(void) {
	x_fprintf(out, "\tcltd\n");
}

static void
emit_fist(struct fistdata *dat) {
	int	size = backend->get_sizeof_type(dat->target_type, NULL);

	x_fprintf(out, "\tfistp%c ",
		/*dat->vr->size*/ size == 4? 'l': 'q');
	if (backend->arch == ARCH_AMD64) {
		amd64_print_mem_operand_gas(dat->vr, NULL);
	} else {
		print_mem_operand(dat->vr, NULL);
		print_mem_operand(NULL, NULL); /* 06/12/08: Reset long long flag */
	}
	x_fputc('\n', out);
}

static void
emit_fild(struct filddata *dat) {
	/* s = 16bit, l = 32bit, q = 64bit */
	x_fprintf(out, "\tfild%c ",
		dat->vr->size == 4? 'l': 'q');
	if (backend->arch == ARCH_AMD64) {
		amd64_print_mem_operand_gas(dat->vr, NULL);
	} else {	
		print_mem_operand(dat->vr, NULL);
		print_mem_operand(NULL, NULL); /* 06/15/08: Reset long long flag */
	}
	x_fputc('\n', out);
}

static void
emit_x86_ulong_to_float(struct icode_instr *ii) {
	struct amd64_ulong_to_float	*data = ii->dat;
	static unsigned long		count;

	x_fprintf(out, "\ttestl %%%s, %%%s\n", data->src_gpr->name, data->src_gpr->name);
	x_fprintf(out, "\tjs ._Ulong_float%lu\n", count);
	x_fprintf(out, "\tjmp ._Ulong_float%lu\n", count+1);
	x_fprintf(out, "._Ulong_float%lu:\n", count);
	x_fprintf(out, "\tfadds (_Ulong_float_mask)\n");
	x_fprintf(out, "._Ulong_float%lu:\n", count+1);
	count += 2;
}

static void
emit_save_ret_addr(struct function *f, struct stack_block *sb) {
	(void) f;

	x_fprintf(out, "\tmovl 4(%%ebp), %%eax\n");
	x_fprintf(out, "\tmovl %%eax, -%lu(%%ebp)\n", sb->offset);
}

static void
emit_check_ret_addr(struct function *f, struct stack_block *saved) {
	static unsigned long	labval = 0;

	(void) f;
	
	x_fprintf(out, "\tmovl -%lu(%%ebp), %%ecx\n", saved->offset);
	x_fprintf(out, "\tcmpl 4(%%ebp), %%ecx\n");
	x_fprintf(out, "\tje .doret%lu\n", labval);
/*	x_fprintf(out, "\tcall __nwcc_stack_corrupt\n");*/
	emit_call("__nwcc_stack_corrupt");
	x_fprintf(out, ".doret%lu:\n", labval++);
}

#define EXTRA_LLONG(was_llong) \
	(was_llong? 4: 0)

static void
do_stack(FILE *out, struct decl *d, int was_llong) {
	char	*sign;

	if (d->stack_addr->is_func_arg) {
		sign = "";
		if (was_llong) {
			was_llong = 4;
		}	
	} else {
		sign = "-";
		if (was_llong) {
			was_llong = -4; /* XXX hmm... */
		}	
	}
	x_fprintf(out, "%s%lu(%%ebp",
		sign, d->stack_addr->offset+/*EXTRA_LLONG(was_llong)*/was_llong);
}


static void
print_mem_operand(struct vreg *vr, struct token *constant) {
	static int	was_llong;
	int		needbracket = 1; /* XXX bogus */

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

	/*
	 * 04/11/08: Since some emitter functions are shared between
	 * x86 and AMD64, print_mem_operand() of x86 may not be used
	 * because it is 32bit. Caller should use AMD64 version
	 * instead! It's better to assert instead of fixing the
	 * issue quietly here, because then function is better
	 * checked for 64bit cleanliness
	 */
	assert(backend->arch == ARCH_X86);

	if (vr && vr->from_const != NULL) {
		constant = vr->from_const;
	}
	if (constant != NULL) {
		struct token	*t = vr->from_const;

		if (!IS_FLOATING(t->type)) {
			x_fputc('$', out);
		}	
		if (IS_INT(t->type) || IS_LONG(t->type)) {
			cross_print_value_by_type(out, t->data, t->type, 'd');
#if ALLOW_CHAR_SHORT_CONSTANTS
		} else if (IS_CHAR(t->type) || IS_SHORT(t->type)) {	
			cross_print_value_by_type(out, t->data, t->type, 'd');
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
				cross_print_value_by_type(out, 
					p, TY_UINT, 'd');	
			} else {
				/* ULLONG */
				cross_print_value_by_type(out, 
					p, TY_UINT, 'd');	
			}
		} else if (t->type == TOK_STRING_LITERAL) {
			struct ty_string	*ts = t->data;
			x_fprintf(out, "._Str%ld",
				ts->count);
		} else if (t->type == TY_FLOAT
			|| t->type == TY_DOUBLE
			|| t->type == TY_LDOUBLE) {
			struct ty_float	*tf = t->data;
	
			x_fprintf(out, "(_Float%lu)",
				tf->count);
		} else {
			printf("loadimm: Bad data type %d\n", t->type);
			abort();
			return;
		}
	} else if (vr->parent != NULL) {
		struct vreg	*vr2;
		struct decl	*d2;

		vr2 = get_parent_struct(vr);
		if ((d2 = vr2->var_backed) != NULL) {
			if (d2->stack_addr) {
				/* XXX kludgy? */
				unsigned long	off = calc_offsets(vr);

				if (vr2->var_backed->stack_addr->is_func_arg) {
					vr2->var_backed->stack_addr->offset +=
						/*vr->memberdecl->offset*/off;
				} else {
					vr2->var_backed->stack_addr->offset -=
						/*vr->memberdecl->offset*/off;
				}	
				do_stack(out, vr2->var_backed, was_llong);
				if (vr2->var_backed->stack_addr->is_func_arg) {
					vr2->var_backed->stack_addr->offset -=
						/*vr->memberdecl->offset*/off;
				} else {
					vr2->var_backed->stack_addr->offset +=
						/*vr->memberdecl->offset*/off;
				}	
			} else {
				/* static */
				x_fprintf(out, "%s+%lu",
					d2->dtype->name,
					calc_offsets(vr)+EXTRA_LLONG(was_llong));
				needbracket = 0;
			}
		} else if (vr2->from_ptr) {
			/* Struct comes from pointer */
			x_fprintf(out, "%lu(%%%s",
				calc_offsets(vr)+EXTRA_LLONG(was_llong),	
				vr2->from_ptr->pregs[0]->name);	
		} else {
			printf("BUG: Bad load for %s\n",
				vr->type->name? vr->type->name: "structure");
			abort();
		}	
	} else if (vr->var_backed) {
		struct decl	*d = vr->var_backed;

		if (d->stack_addr != NULL) {
			do_stack(out, d, was_llong);
		} else {
			if (d->dtype->storage == TOK_KEY_REGISTER) {
				unimpl();
			} else {
				if (d->dtype->tlist != NULL
					&& d->dtype->tlist->type
					== TN_FUNCTION) {
					x_fprintf(out, "$%s", d->dtype->name);
				} else {
					x_fprintf(out, "%s", d->dtype->name);
					if (was_llong) {
						x_fprintf(out, "+4");
					}	
				}	
				needbracket = 0;
			}	
		}
	} else if (vr->stack_addr) {
		 /*
		  * 07/26/12: Honor use_frame_pointer
		  */
		if (vr->stack_addr->use_frame_pointer) {
			x_fprintf(out, "-%lu(%%ebp",
				vr->stack_addr->offset - EXTRA_LLONG(was_llong));
		} else {
			x_fprintf(out, "%lu(%%esp",
				vr->stack_addr->offset + EXTRA_LLONG(was_llong));
		}
	} else if (vr->from_ptr) {
		if (was_llong) {
			x_fputc('4', out);
		}	
		x_fprintf(out, "(%%%s", vr->from_ptr->pregs[0]->name);
	} else {
		abort();
	}
	if (constant == NULL) {
		if (was_llong) {
			/*x_fprintf(out, " + 4 ");*/
			was_llong = 0;
		} else if (vr->is_multi_reg_obj) {
			was_llong = 1;
		}	
		if (needbracket) { 
			x_fputc(')', out);
		}	
	}	
}	


void
print_item_gas_x86(FILE *out, void *item, int item_type, int postfix) {
	print_asmitem_x86(out, item, item_type, postfix, TO_GAS);
}

struct emitter x86_emit_gas = {
	0, /* need_explicit_extern_decls */
	init,
	emit_strings,
	emit_fp_constants,
	NULL, /* llong_constants */
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
	NULL, /* struct_defs */
	emit_comment,

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
	NULL, /*emit_define*/
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
	emit_finish_program,
	emit_stupidtrace,
	emit_finish_stupidtrace
};


struct emitter_x86 x86_emit_x86_gas = {
	emit_fxch,
	emit_ffree,
	emit_fnstcw,
	emit_fldcw,
	emit_cdq,
	emit_fist,
	emit_fild,
	emit_x86_ulong_to_float
};		

