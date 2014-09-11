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
#ifndef BACKEND_H
#define BACKEND_H

struct scope;
struct expr;
struct type;
struct decl;
struct reg;
struct vreg;
struct token;
struct function;
struct icode_instr;
struct icode_list;
struct scope;
struct stack_block;
struct initializer;
struct sym_entry;
struct fcall_data;
struct init_with_name;
struct inline_asm_io;
struct ty_string;
struct gas_token;
struct copystruct;
struct allocadata;

struct vlasizedata;

#include <stdio.h>
#include <stdarg.h>
#include "builtins.h"
#include "attribute.h"

extern int	optimizing;
extern char	*tunit_name;
extern size_t	tunit_size;


struct vreg	*get_parent_struct(struct vreg *);
size_t		calc_align_bytes(
			size_t offset,
			struct type *cur,
			struct type *next,
			int struct_member);
int 		calc_slot_rightadjust_bytes(int size, int total_size);
int is_immediate_vla_type(struct type *ty);
int
xlate_icode(struct function *f,
	struct icode_list *ilp,
	struct icode_instr **lastret);

int
botch_x86_alignment(struct type *ty);

void
store_preg_to_var_off(struct decl *d, size_t off, size_t size, struct reg *r,
	struct reg *temp);

struct vreg *
vreg_static_alloc(struct type *ty, struct initializer *init);

struct vreg *
vreg_stack_alloc(struct type *ty, struct icode_list *il, int on_frame,
		struct initializer *init);

struct initializer *
make_null_block(struct sym_entry *se,
		struct type *ty, struct type *
		struct_ty, int remaining);

struct init_with_name *
make_init_name(struct initializer *init);

size_t
get_sizeof_const(struct token *constant);

size_t
get_sizeof_elem_type(struct type *ty);

size_t
get_sizeof_decl(struct decl *, struct token *);

size_t
get_align_type(struct type *ty);
size_t
get_struct_align_type(struct type *ty);

unsigned long
calc_offsets(struct vreg *);

int
generic_same_representation(struct type *dest, struct type *src);

void
generic_conv_to_or_from_ldouble(struct vreg *ret, struct type *to, struct type *from,
        struct icode_list *il);

int
generic_pass_struct_union(
	struct vreg *vr, /*int *gprs_used, int *fprs_used,*/
	int *slots_used,
	size_t *stack_bytes_used,
	size_t *real_stack_bytes_used,
	struct icode_list *il);

void
generic_store_struct_arg_slots(struct function *f, struct sym_entry *se);


/* XXX Move function below to icodeinstr.c?! (originally from SPARC backend) */
void
generic_icode_initialize_pic(struct function *f, struct icode_list *il);

int
arch_without_offset_limit(void);

void
save_struct_ptr(struct decl *);

void
reload_struct_ptr(struct decl *);

struct reg * /*icode_instr **/
make_addrof_structret(struct vreg *struct_lvalue, struct icode_list *il);



struct stupidtrace_entry {
	struct function			*func;
	char				*bufname;
	struct stupidtrace_entry	*next;
};

struct stupidtrace_entry *
put_stupidtrace_list(struct function *f);

extern struct stupidtrace_entry	*stupidtrace_list_head;
extern struct stupidtrace_entry	*stupidtrace_list_tail;




/* as/gas-specific emitter functions. Work at least with x86 gas and SGI as */
void	as_align_for_type(FILE *o, struct type *, int struct_member);

void	as_print_string_init(FILE *o, size_t howmany, struct ty_string *str);


typedef int	(*have_immediate_op_func_t)(struct type *ty, int op);
typedef int	(*init_func_t)(FILE *fd, struct scope *s);
typedef int	(*is_multi_reg_obj_func_t)(struct type *t);

typedef int	(*gen_function_func_t)(struct function *f);
typedef	int	(*gen_code_func_t)(void);


typedef int	(*gen_prepare_output_func_t)(void);
typedef int	(*gen_finish_output_func_t)(void);

typedef int	(*gen_var_func_t)(struct decl *d, int is_glob, int inited);
typedef int	(*get_ptr_size_func_t)(void);
typedef struct type *(*get_size_t_func_t)(void);
typedef struct type *(*get_uintptr_t_func_t)(void);
typedef struct type *(*get_wchar_t_func_t)(void);
typedef size_t	(*get_sizeof_basic_func_t)(int code);
typedef size_t	(*get_sizeof_type_func_t)(struct type *ty,
			struct token *tok); 
typedef size_t	(*get_sizeof_elem_type_func_t)(struct type *ty);
typedef size_t	(*get_sizeof_decl_func_t)(struct decl *d,
			struct token *tok); 
typedef size_t	(*get_sizeof_const_func_t)(struct token *t);
typedef struct vreg	*(*get_sizeof_vla_type_func_t)(struct type *,
		struct icode_list *);
typedef size_t	(*get_align_type_func_t)(struct type *ty);
typedef struct reg	(*reg_alloc_func_t)(void);
typedef int	(*reg_free_func_t)(void);

#define INV_FOR_FCALL 1
typedef void	(*invalidate_gprs_func_t)(struct icode_list *il,
		int saveregs, int for_fcall);
typedef void	(*invalidate_except_func_t)(struct icode_list *il,
		int save, int for_fcall,...);
typedef struct reg	*(*alloc_gpr_func_t)(
	struct function *f,
	int size, 
	struct icode_list *,
	struct reg *dontwipe, int line);
typedef struct reg	*(*alloc_16_or_32bit_noesiedi_func_t)(
	struct function *f,
	size_t size, 
	struct icode_list *,
	struct reg *dontwipe);

typedef struct reg	*(*alloc_fpr_func_t)(
	struct function *f,
	int size, 
	struct icode_list *,
	struct reg *dontwipe);
typedef void	(*free_preg_func_t)(struct reg *, struct icode_list *);
typedef void	(*flush_func_t)(void);
typedef void	(*wbreg_func_t)(struct vreg *vr, struct reg *pr);
typedef void	(*icode_prepare_op_func_t)(
		struct vreg **dest,
		struct vreg **src,
		int op,
		struct icode_list *il);

typedef void	(*icode_prepare_load_addrlabel_func_t)(
		struct icode_instr *label);

typedef struct vreg	*(*icode_make_cast_func_t)
	(struct vreg *src, struct type *to, struct icode_list *il);

typedef void	(*icode_make_structreloc_func_t)(struct copystruct *cs,
			struct icode_list *il);
typedef void	(*icode_complete_func_t)(struct function *f,
			struct icode_list *il);
typedef void	(*icode_initialize_pic_func_t)(struct function *f,
		struct icode_list *il);

typedef struct initializer *
	(*make_null_block_func_t)(
		struct sym_entry *se,
		struct type *t,
		struct type *struct_ty,
		int remaining);
typedef struct init_with_name	*
	(*make_init_name_func_t)(struct initializer *);
typedef void	(*debug_print_gprs_func_t)(void);	
typedef struct reg	*(*name_to_reg_func_t)(const char *name);
typedef struct reg	*
(*asmvreg_to_reg_func_t)(struct vreg **, int,
 struct inline_asm_io*, struct icode_list*, int faultin);
typedef char	*(*get_inlineasm_label_func_t)(const char *);
typedef void	(*do_ret_func_t)(struct function *, struct icode_instr *);

typedef struct reg	*(*get_abi_reg_func_t)(int index, struct type *ty);
typedef struct reg	*(*get_abi_ret_reg_func_t)(struct type *p);
typedef int		(*same_representation_func_t)(struct type *dest,
				struct type *src);



struct emitter;
struct reg;
struct copystruct;
struct copyreg;

#include "archdefs.h"
#include "features.h"

struct backend {
	/* Various backend-specific settings and flags */
	int				arch;
	int				abi;
	int				multi_gpr_object;
	int				struct_align;
	int				need_pic_init;
	int				emulate_long_double;
	int				relax_alloc_gpr_order;
	long				max_displacement;
	long				min_displacement;
	have_immediate_op_func_t	have_immediate_op;
	
	/* Initialization */
	init_func_t			init;

	/* Code translation support functions */
	is_multi_reg_obj_func_t		is_multi_reg_obj;
	get_ptr_size_func_t		get_ptr_size;
	get_size_t_func_t		get_size_t;
	get_uintptr_t_func_t		get_uintptr_t;
	get_wchar_t_func_t		get_wchar_t;
	get_sizeof_basic_func_t		get_sizeof_basic;
	get_sizeof_type_func_t		get_sizeof_type;
	get_sizeof_elem_type_func_t	get_sizeof_elem_type;
	get_sizeof_decl_func_t		get_sizeof_decl;
	get_sizeof_const_func_t		get_sizeof_const;
	get_sizeof_vla_type_func_t	get_sizeof_vla_type;
	get_align_type_func_t		get_align_type;


	/* Final code output functions (write asm file) */
	gen_function_func_t		generate_function;

#if XLATE_IMMEDIATELY
	gen_prepare_output_func_t	gen_prepare_output;
	gen_finish_output_func_t	gen_finish_output;
#else
	gen_code_func_t			generate_program;
#endif


	/* Emitter */
	struct emitter			*emit;

	/* XXX unused???? */
	struct reg			*esp;

	/* Code translation support functions (continued - XXX) */
	invalidate_gprs_func_t		invalidate_gprs;
	invalidate_except_func_t	invalidate_except;
	alloc_gpr_func_t		alloc_gpr;
	alloc_16_or_32bit_noesiedi_func_t	alloc_16_or_32bit_noesiedi;
	alloc_fpr_func_t		alloc_fpr;
	free_preg_func_t		free_preg;
	struct vreg			*(*icode_make_fcall)(
		struct fcall_data *,
		struct vreg **vrs,
		int nvrs,
		struct icode_list * 
	);
	int				(*icode_make_return)(
		struct vreg *vr, struct icode_list *il
	);
	flush_func_t			flushregs;
	icode_prepare_op_func_t		icode_prepare_op;
	icode_prepare_load_addrlabel_func_t	icode_prepare_load_addrlabel;
	icode_make_cast_func_t		icode_make_cast;
	icode_make_structreloc_func_t	icode_make_structreloc;
	icode_initialize_pic_func_t	icode_initialize_pic;
	icode_complete_func_t		icode_complete_func;
	make_null_block_func_t		make_null_block;
	make_init_name_func_t		make_init_name;
	debug_print_gprs_func_t		debug_print_gprs;
	name_to_reg_func_t		name_to_reg;
	asmvreg_to_reg_func_t		asmvreg_to_reg;
	get_inlineasm_label_func_t	get_inlineasm_label;
	do_ret_func_t			do_ret;
	get_abi_reg_func_t		get_abi_reg;
	get_abi_ret_reg_func_t		get_abi_ret_reg;
	same_representation_func_t	same_representation;
};

struct scope;
struct vreg;
struct inline_asm_stmt;
struct gas_token;
struct decl;
struct copystruct;
struct int_memcpy_data;
struct builtinframeaddressdata;
struct putstructregs;

int	init_backend(FILE *fd, struct scope *s);
int	do_xlate(struct function *, struct icode_instr **);
struct reg	*generic_alloc_gpr(struct function *f,
	int size, struct icode_list *il, struct reg *dontwipe,
	struct reg *regset, int nregs, int *csave_map, int line);
size_t	get_sizeof_type(struct type *, struct token *);
struct vreg	*get_sizeof_vla_type(struct type *, struct icode_list *);
struct vreg	*get_sizeof_elem_vla_type(struct type *, struct icode_list *);
size_t	generic_print_init_var(FILE *out, struct decl *d, size_t segoff,
	void (*print_init_expr)(struct type *, struct expr *),
		int skip_is_space);
void	generic_print_init_list(FILE *out, struct decl *, struct initializer *,
	void (*print_init_expr)(struct type *, struct expr *));
void	new_generic_print_init_list(FILE *out, struct decl *, struct initializer *,
	void (*print_init_expr)(struct type *, struct expr *));
void	relocate_struct_regs(struct copystruct *cs, struct reg *r0,
		struct reg *r1, struct reg *r2, struct icode_list *il);
void	copy_struct_regstack(struct decl *dec);
void	store_reg_to_stack_block(struct reg *, struct stack_block *);
unsigned long	align_for_cur_auto_var(struct type *ty, unsigned long off);
char	*generic_elf_section_name(int value);
char	*generic_mach_o_section_name(int value);
struct decl	*get_next_auto_decl_in_scope(struct scope *, int);

#include <stdarg.h>

/* Various declarations (struct init, strings, etc) */

/*
 * 03/22/08: New: Support declarations for assemblers like nasm that need
 * them to e.g. call memcpy() when assigning structs by value
 */
typedef void	(*support_decls_func_t)(void);

typedef void	(*extern_decls_func_t)(void);
/*
 * 03/22/08: New: Global declarations split from extern/static variable
 * definitions
 */
typedef void	(*global_extern_decls_func_t)(struct decl **d, int ndecls);
typedef void	(*global_static_decls_func_t)(struct decl **d, int ndecls);

#if 0
typedef	void	(*static_decls_func_t)(void);
#endif
typedef void	(*static_init_vars_func_t)(struct decl *list);
typedef void	(*static_uninit_vars_func_t)(struct decl *list);
typedef void	(*static_init_thread_vars_func_t)(struct decl *list);
typedef void	(*static_uninit_thread_vars_func_t)(struct decl *list);


typedef void	(*struct_defs_func_t)(void);
typedef void	(*struct_inits_func_t)(struct init_with_name *list);

struct ty_float;
struct ty_llong;

typedef void	(*strings_func_t)(struct ty_string *str);
typedef void	(*fp_constants_func_t)(struct ty_float *fp); /* opt */
typedef void	(*llong_constants_func_t)(struct ty_llong *ll); /* opt */
typedef void	(*support_buffers_func_t)(void); /* opt, fp conv, etc */
typedef void	(*pic_support_func_t)(void); /* opt, cmdline */



typedef void	(*comment_func_t)(const char *fmt, ...);
typedef void	(*dwarf2_line_func_t)(struct token *);
typedef void	(*dwarf2_files_func_t)(void);
typedef void	(*inlineasm_func_t)(struct inline_asm_stmt *);
typedef void	(*genunimpl_func_t)(void);
typedef void	(*empty_func_t)(void);
typedef void	(*label_func_t)(const char *label, int is_func);
typedef void	(*call_func_t)(const char *name);
typedef void	(*call_indir_func_t)(struct reg *r);
typedef void	(*func_header_t)(struct function *f);
typedef void	(*func_intro_t)(struct function *f);
typedef void	(*func_outro_t)(struct function *f);
typedef void	(*define_func_t)(const char *name, const char *fmt, ...);
typedef void	(*push_func_t)(struct function *f, struct icode_instr *);
typedef void	(*allocstack_func_t)(struct function *f, size_t bytes);
typedef void	(*freestack_func_t)(struct function *f, size_t *bytes);
typedef void	(*adj_allocated_func_t)(struct function *f, int *bytes);
typedef void	(*inc_func_t)(struct icode_instr *);
typedef void	(*dec_func_t)(struct icode_instr *);
typedef void	(*load_func_t)(struct reg *r, struct vreg *vr);
typedef void	(*load_addrlabel_func_t)(struct reg *r, struct icode_instr *label);
typedef void	(*comp_goto_func_t)(struct reg *addr);
typedef void	(*store_func_t)(struct vreg *dest, struct vreg *src);
typedef void	(*neg_func_t)(struct reg **dest, struct icode_instr *src);
typedef void	(*sub_func_t)(struct reg **dest, struct icode_instr *src);
typedef void	(*add_func_t)(struct reg **dest, struct icode_instr *src);
typedef void
(*div_func_t)(struct reg **dest, struct icode_instr *src, int);
typedef void	(*mod_func_t)(struct reg **dest, struct icode_instr *src);
typedef void	(*mul_func_t)(struct reg **dest, struct icode_instr *src);
typedef void	(*shl_func_t)(struct reg **dest, struct icode_instr *src);
typedef void	(*shr_func_t)(struct reg **dest, struct icode_instr *src);
typedef void	(*or_func_t)(struct reg **dest, struct icode_instr *src);
typedef void	(*preg_or_func_t)(struct reg **dest, struct icode_instr *src);
typedef void	(*and_func_t)(struct reg **dest, struct icode_instr *src);
typedef void	(*xor_func_t)(struct reg **dest, struct icode_instr *src);
typedef void	(*not_func_t)(struct reg **dest, struct icode_instr *src);
typedef void	(*cmp_func_t)(struct reg **dest, struct icode_instr *ii);
typedef void	(*extend_sign_func_t)(struct icode_instr *ii);
typedef void	(*conv_fp_func_t)(struct icode_instr *ii);
typedef void	(*conv_to_ldouble_func_t)(struct icode_instr *ii);
typedef void	(*conv_from_ldouble_func_t)(struct icode_instr *ii);
typedef void	(*branch_func_t)(struct icode_instr *ii);

#define SECTION_STACK 1 /* never used? */
#define SECTION_UNINIT 2
#define SECTION_INIT 3
#define SECTION_TEXT 4
#define SECTION_RODATA 5

/* 02/02/08: TLS sections */
#define SECTION_INIT_THREAD	6
#define SECTION_UNINIT_THREAD	7

/* 12/25/08: PPC TOC */
#define SECTION_TOC	8


typedef void	(*setsection_func_t)(int value);
typedef void	(*alloc_func_t)(size_t nbytes);
typedef void	(*ret_func_t)(struct icode_instr *ii);
typedef void	(*mov_func_t)(struct copyreg *cr);
typedef void	(*setreg_func_t)(struct reg *dest, int *value);
typedef void	(*xchg_func_t)(struct reg *r1, struct reg *r2);
typedef void	(*addrof_func_t)(struct reg *dest, struct vreg *src,
		struct vreg *structtop);
typedef void	(*initialize_pic_func_t)(struct function *f);
typedef void	(*copyinit_func_t)(struct decl *dec);
typedef void	(*putstructregs_func_t)(struct putstructregs *ps);
typedef void	(*copystruct_func_t)(struct copystruct *cs);
typedef void	(*intrinsic_memcpy_func_t)(struct int_memcpy_data *cs);
typedef void	(*zerostack_func_t)(struct stack_block *addr, size_t nbytes);
typedef void	(*alloca_func_t)(struct allocadata *ad);
typedef void	(*dealloca_func_t)(struct stack_block *sb, struct reg *r);
typedef void	(*alloc_vla_func_t)(struct stack_block *ad);
typedef void	(*dealloc_vla_func_t)(struct stack_block *sb, struct reg *r);
typedef void	(*put_vla_size_func_t)(struct vlasizedata *data);
typedef void	(*retr_vla_size_func_t)(struct vlasizedata *data);
typedef void	(*load_vla_func_t)(struct reg *r, struct stack_block *sb);
typedef void	(*builtin_frame_address_func_t)(struct builtinframeaddressdata *);
typedef void	(*save_ret_addr_func_t)(struct function *,
		struct stack_block *);
typedef void
(*check_ret_addr_func_t)(struct function *, struct stack_block *);
typedef void	(*print_mem_operand_func_t)(struct vreg *, struct token *);
typedef void	(*finish_program_func_t)(void);

typedef void		(*stupidtrace_func_t)(struct stupidtrace_entry *f);
typedef void		(*finish_stupidtrace_func_t)(struct stupidtrace_entry *e);

struct emitter {
	/*
	 * 04/08/08: New flag to specify whether the assembler demands
	 * explicit symbol declarations when referencing external symbols,
	 * like nasm and yasm (but not gas) do
	 */
	int			need_explicit_extern_decls;
	init_func_t		init;
	strings_func_t		strings;
	fp_constants_func_t	fp_constants; /* half-opt */
	llong_constants_func_t	llong_constants; /* opt */
	support_buffers_func_t	support_buffers; /* opt */
	pic_support_func_t	pic_support; /* opt */

	support_decls_func_t	support_decls; /* opt */
	extern_decls_func_t	extern_decls;
	global_extern_decls_func_t	global_extern_decls;
	global_static_decls_func_t	global_static_decls;
#if 0
	static_decls_func_t	static_decls;
#endif
	static_init_vars_func_t			static_init_vars;
	static_uninit_vars_func_t		static_uninit_vars;
	static_init_thread_vars_func_t		static_init_thread_vars;
	static_uninit_thread_vars_func_t	static_uninit_thread_vars;

	struct_defs_func_t	struct_defs;
	comment_func_t		comment;

	/* 10 */
	dwarf2_line_func_t	dwarf2_line;
	dwarf2_files_func_t	dwarf2_files;
	inlineasm_func_t	inlineasm;
	genunimpl_func_t	genunimpl;
	empty_func_t		empty;
	label_func_t		label;
	call_func_t		call;
	call_indir_func_t	callindir;
	func_header_t		func_header;
	func_intro_t		intro;

	/* 20 */
	func_outro_t		outro;
	define_func_t		define;
	push_func_t		push;
	allocstack_func_t	allocstack;
	freestack_func_t	freestack;
	adj_allocated_func_t	adj_allocated;
	inc_func_t		inc;
	dec_func_t		dec;
	load_func_t		load;
	load_addrlabel_func_t	load_addrlabel;
	comp_goto_func_t	comp_goto;
	store_func_t		store;

	/* 30 */
	setsection_func_t	setsection;
	alloc_func_t		alloc;
	neg_func_t		neg;
	sub_func_t		sub;
	add_func_t		add;
	div_func_t		div;
	mod_func_t		mod;
	mul_func_t		mul;
	shl_func_t		shl;
	shr_func_t		shr;

	/* 40 */
	or_func_t		or;
	preg_or_func_t		preg_or;
	and_func_t		and;
	xor_func_t		xor;
	not_func_t		not;
	ret_func_t		ret;
	cmp_func_t		cmp;
	extend_sign_func_t	extend_sign;
	conv_fp_func_t		conv_fp;
	conv_from_ldouble_func_t	conv_from_ldouble;
	conv_to_ldouble_func_t		conv_to_ldouble;
	branch_func_t		branch;

	/* 50 */
	mov_func_t		mov;
	setreg_func_t		setreg;
	xchg_func_t		xchg;
	addrof_func_t		addrof;
	initialize_pic_func_t	initialize_pic;
	copyinit_func_t		copyinit;
	putstructregs_func_t	putstructregs;
	copystruct_func_t	copystruct;
	intrinsic_memcpy_func_t	intrinsic_memcpy;
	zerostack_func_t	zerostack;
	alloca_func_t		alloca_;

	/* 60 */
	dealloca_func_t		dealloca;
	alloc_vla_func_t	alloc_vla;
	dealloc_vla_func_t	dealloc_vla;
	put_vla_size_func_t	put_vla_size;
	retr_vla_size_func_t	retr_vla_size;
	load_vla_func_t		load_vla;
	builtin_frame_address_func_t	frame_address;
	struct_inits_func_t	struct_inits;
	save_ret_addr_func_t	save_ret_addr;
	check_ret_addr_func_t	check_ret_addr;

	/* 70 */
	print_mem_operand_func_t	print_mem_operand;
	finish_program_func_t	finish_program;

	stupidtrace_func_t		stupidtrace;
	finish_stupidtrace_func_t	finish_stupidtrace;
};

extern struct backend	*backend;
extern struct emitter	*emit;
extern struct reg	*tmpgpr;
extern struct reg	*tmpgpr2;
extern struct reg	*tmpfpr;
extern struct reg	*pic_reg;
extern int		host_endianness;
extern struct init_with_name	*init_list_head;
extern struct init_with_name	*init_list_tail;
extern int	backend_warn_inv;


struct allocation {
	int	unused;
};

#define ALLOC_GPR(func, size, il, dontwipe) \
	backend->alloc_gpr((func), (size), (il), (dontwipe), __LINE__) 

#include "x86_gen.h"
#include "mips_gen.h"
#include "amd64_gen.h"
#include "power_gen.h"
#include "sparc_gen.h"
#include "reg.h"
#include "stack.h"

#endif

