/*
 * Copyright (c) 2004 - 2010, Nils R. Weller
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
#ifndef DEBUG_H
#define DEBUG_H

struct type;
struct icode_list;
struct s_expr;
struct expr;
struct statement;
struct function;
struct token;
struct scope;
struct vreg;

void	debug_do_print_type(struct type *decty, int mode, int tabs);
void	debug_do_print_conv(
		struct type *old1, struct type *old2,
		int op, struct type *n);
void	debug_do_print_icode_list(struct icode_list *list);
void	debug_do_print_function(struct function *f);
void	debug_do_print_expr(struct expr *ex);
void	debug_do_print_tree(struct expr *ex);
void	debug_do_print_s_expr(struct s_expr *op, int tabs);
void	debug_do_print_eval_op(struct token *t);
void	debug_do_print_var_defs(struct scope *s);

void	debug_print_vreg_backing(struct vreg *vr, const char *custom_ident);
void	debug_track_vreg(struct vreg *vr);
void	do_debug_check_tracked_vregs(const char *, int);
#define debug_check_tracked_vregs() do_debug_check_tracked_vregs(__FILE__, __LINE__)
void	debug_print_statement(struct statement *stmt);

#ifdef DEBUG2
/* type.c */
#define debug_print_type(x, y, z) debug_do_print_type(x, y, z)
#else
#define debug_print_type(x, y, z) ((void)x, (void)y, (void)z)
#endif


#ifdef DEBUG3
#define debug_print_conv(x, y, z, a) debug_do_print_conv(x, y, z, a)
#else
#define debug_print_conv(x, y, z, a) ((void)x, (void)y, (void)z, (void)a)
#endif


#ifdef DEBUG4
/* icode.c */
#define debug_print_icode_list(x) debug_do_print_icode_list(x)
/* functions.c */
#define debug_print_function(x) debug_do_print_function(x)
/* expr.c */
#define debug_print_expr(x) debug_do_print_expr(x)
#define debug_print_tree(x) debug_do_print_tree(x)
/* subexpr.c */
#define debug_print_s_expr(x, y) debug_do_print_s_expr(x, y)
#define debug_print_eval_op(x) debug_do_print_eval_op(x)

#else

#define debug_print_icode_list(x) ((void)x)
#define debug_dump_icode_list(x) ((void)x)
#define debug_print_function(x) ((void)x)
#define debug_print_expr(x) ((void)x)
#define debug_print_tree(x) ((void)x)
#define debug_print_s_expr(x, y) ((void)x, (void)y)
#define debug_print_eval_op(x) ((void)x)
#endif

#ifdef DEBUG5
#define debug_print_static_var_defs(x) debug_do_print_var_defs(x)
#else
#define debug_print_static_var_defs(x) ((void)x)
#endif

#define DEBUG_LOG_FAULTIN 1
#define DEBUG_LOG_REVIVE 2
#define DEBUG_LOG_ALLOCGPR 3
#define DEBUG_LOG_FREEGPR 4
#define DEBUG_LOG_FAILEDALLOC 5
#define DEBUG_LOG_MAP	6
#define DEBUG_LOG_ALLOCATABLE	7
#define DEBUG_LOG_UNALLOCATABLE	8

struct reg;
struct vreg;

void	debug_do_log_regstuff(struct reg *, struct vreg *, int);

#ifdef DEBUG6
#define debug_log_regstuff(r, vr, flag) debug_do_log_regstuff(r, vr, flag)
#else
#define debug_log_regstuff(r, vr, flag) ((void)r, (void)vr, (void)flag)
#endif

#endif

