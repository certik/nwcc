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
 *
 * Various debugging output functions
 */
#include "debug.h"
#include "decl.h"
#include "icode.h"
#include "type.h"
#include "defs.h"
#include "token.h"
#include "expr.h"
#include "control.h"
#include "reg.h"
#include "subexpr.h"
#include "functions.h"
#include "debug.h"
#include "scope.h"
#include "backend.h"
#include <stdlib.h>
#include <string.h>
#include "n_libc.h"

#define DOTABS(ntabs) do { \
	int	i; \
	for (i = 0; i < ntabs; ++i) putchar('\t'); \
} while (0)

void
debug_do_print_type(struct type *decty, int mode, int tabs) {
	char	*p;
	char	*name = decty->name? decty->name: "";

	DOTABS(tabs);
	printf("Read ");
	switch (mode) {
	case DECL_CAST:
		printf("cast");
		break;
	case DECL_NOINIT:
		printf("declaration of %s without initializer", name);
		break;
	case DECL_CONSTINIT:
		printf("declaration of %s with constant initializer", name);
		break;
	case DECL_VARINIT:
		printf("declaration of %s with variable initializer", name);
		break;
	case DECL_FUNCARG:
		printf("function argument");
		break;
	case DECL_STRUCT:
		printf("structure member %s", name);
		break;
	case 0:
		puts("KLUDGE XXX :->");	
		break;
	}
	printf(" of type:\n");
	p = type_to_text(decty);
	printf("\t%s\n", p);
	free(p);
}


void
debug_do_print_conv(
	struct type *old1, struct type *old2,
	int op,
	struct type *newt) {
	char		*p;
	struct operator	*operator;

	operator = &operators[LOOKUP_OP2(op)];

	printf("The resulting type of ...\n");
	p = type_to_text(old1);
	printf("<item1> (%s)\n", p);
	free(p);
	printf("%s\n", operator->name);
	p = type_to_text(old2);
	printf("<item2> (%s)\n", p);
	free(p);
	p = type_to_text(newt);
	printf("... is: %s\n", p);
	free(p);
}


/*
 * Expression parsing
 */

void
debug_do_print_icode_list(struct icode_list *list) {
	struct icode_instr	*ip;
	struct vreg		*vr;

	if (list == NULL) {
		printf("expr_to_icode() failed\n");
		return;
	}
	printf("Intermediate instruction list:\n");
	for (ip = list->head; ip != NULL; ip = ip->next) {
		putchar('\t');
		switch (ip->type) {
		case INSTR_SEQPOINT:
			printf("(Sequence point)\n");
			break;
		case INSTR_DEBUG:
			printf("DEBUG: %s\n", (char *)ip->dat);
			break;
		case INSTR_LABEL:
			printf("%s:\n", (char *)ip->dat);
			break;
		case INSTR_JUMP:
			printf("JMP\n");
			break;
		case INSTR_CMP:
			printf("CMP\n");
			break;
		case INSTR_BR_EQUAL:
			printf("BREQUAL\n");
			break;
		case INSTR_BR_NEQUAL:
			printf("BRNEQUAL\n");
			break;
		case INSTR_BR_GREATER:
			printf("BRGREATER\n");
			break;
		case INSTR_BR_SMALLER:
			printf("BRSMALLER\n");
			break;
		case INSTR_BR_GREATEREQ:
			printf("BRGREATEREQ\n");
			break;
		case INSTR_BR_SMALLEREQ:
			printf("BRSMALLEREQ\n");
			break;
		case INSTR_CALL:
			printf("CALL %s\n", (char *)ip->dat);
			break;
		case INSTR_LOAD:
			printf("LOAD %s = [%p]\n",
				ip->src_pregs[0]->name, ip->src_vreg);
			break;
		case INSTR_INC:
			printf("INC\n");
			break;
		case INSTR_ADD:
			printf("ADD\n");
			break;
		case INSTR_DIV:
			printf("DIV\n");
			break;
		case INSTR_MUL:
			printf("MUL\n");
			break;
		case INSTR_PUSH:
			printf("PUSH\n");
			break;
		case INSTR_SUB:
			printf("SUB\n");
			break;
		case INSTR_ADDROF:
			printf("ADDROF %s = [%p]\n",
				((struct reg *)ip->dat)->name, ip->src_vreg);
			break;
		case INSTR_INDIR:
			printf("[INDIR] ");
			break;
		case INSTR_FREESTACK:
			printf("ADD ESP, %d\n",
				(int)*(size_t *)ip->dat);
			break;
		case INSTR_STORE:
			if (ip->src_vreg != NULL) {
				printf("STORE [%p", ip->src_vreg);
				vr = ip->src_vreg;
				if (vr->var_backed != NULL) {
					printf(":%s",
						vr->var_backed->dtype->name);
				}
				printf("] = [%p]\n", ip->dest_vreg);
			} else {
				printf("STORE = [%p]\n", ip->dest_vreg);
			}	
			break;
		case INSTR_WRITEBACK:
			printf("WRITEBACK\n");
			break;
		case INSTR_RET:
			printf("RET\n");
			break;
		default:
			printf("Illegal instruction (core dumped) "
				"(only kidding)\n");
			printf("Code is %d\n", ip->type); 
			break;
		}
	}
}


void
debug_do_print_s_expr(struct s_expr *ex, int tabs) {
	(void) ex;
	DOTABS(tabs);
	printf("Sub-expression:\n");
	DOTABS(tabs);
	putchar('\n');
}

		

void
debug_do_print_expr(struct expr *ex) {
	printf("Expression %p (used=%d, const=%p):\n", ex, ex->used, ex->const_value);
	do {
		if (ex->data != NULL) {
			debug_do_print_s_expr(ex->data, 1);
		}
		if (ex->left != NULL) {
			printf("Left:\n");
			debug_do_print_expr(ex->left);
		}

		if (ex->op != 0) {
			printf(" ... connected thru %d ...\n", ex->op);
		}
	} while ((ex = ex->right) != NULL);
	printf("--- END ---\n");
}

static char	tree_map[24][80];

#define POS(x, y) (x + ((sizeof(x) / 2) + pos * 10)) 

static void
do_debug_print_tree(struct expr *ex, int line, int pos) {
	char	*p;

	if (line >= 24) {
		puts("Expression too long");
		exit(EXIT_FAILURE);
	}

	p = POS(tree_map[line], pos);
	if (ex->op) {
		p[ sprintf(p, "<%d>", ex->op) ] = ' ';
	} else {
		p[ sprintf(p, "s_expr") ] = ' ';
	}
	++line;
	if (ex->left) {
		p = POS(tree_map[line], pos) - 2;
		p[ sprintf(p, "/   ") ] = ' ';
		do_debug_print_tree(ex->left, line+1, pos - 1);
	}
	if (ex->right) {
		p = POS(tree_map[line], pos) + 2;
		p[ sprintf(p, "   \\") ] = ' ';
		do_debug_print_tree(ex->right, line+1, pos + 1);
	}
}

void
debug_do_print_tree(struct expr *ex) {
	int	i;
	for (i = 0; i < 24; ++i) {
		memset(tree_map[i], ' ', sizeof tree_map[i]);
		tree_map[i][79] = 0;
	}
	do_debug_print_tree(ex, 0, 0);
	tree_map[5][0] = '*';
	for (i = 0; i < 24; ++i) {
		printf("%s\n", tree_map[i]);
	}
}

void
debug_do_print_function(struct function *f) {
	printf("Read function `%s'\n", f->proto->dtype->name);
	debug_do_print_type(f->proto->dtype, DECL_NOINIT, 1);
}

/*
 * Note that the cast and function call operators are not handled
 * because they are ambiguous
 */
void
debug_do_print_eval_op(struct token *t) {
	char	*p = "an unknown something";

	(void) t; (void) p;
	if (t->type == TOK_ARRAY_OPEN) {
		p = "array subscript operator";
	} else if (t->type == TOK_OPERATOR) {
		switch (*(int *)t->data) {
		case TOK_OP_INCPOST:
			p = "postifx increment operator";
			break;
		case TOK_OP_DECPOST:
			p = "postfix decrement operator";
			break;
		case TOK_OP_STRUMEMB:
			p = ". operator";
			break;
		case TOK_OP_STRUPMEMB:
			p = "-> operator";
			break;
		case TOK_OP_DEREF:
			p = "dereferecing operator";
			break;
		case TOK_OP_UPLUS:
			p = "unary plus operator";
			break;
		case TOK_OP_UMINUS:
			p = "unary minus operator";
			break;
		case TOK_OP_ADDR:
			p = "address-of operator";
			break;
		case TOK_OP_INCPRE:
			p = "prefix increment operator";
			break;
		case TOK_OP_DECPRE:
			p = "prefix decrement operator";
			break;
		default:
			p = "unknown operator";
		}
	}
	printf("get_sub_expr(): evaluating %s\n", p);
}


void
debug_do_print_static_var_defs(void) {
	struct decl	*dp;

	printf("Static initialized variables:\n");
	for (dp = static_init_vars; dp != NULL; dp = dp->next) {
		printf("%s\n", dp->dtype->name);
	}
	printf("Static uninitialized variables:\n");
	for (dp = static_uninit_vars; dp != NULL; dp = dp->next) {
		printf("%s\n", dp->dtype->name);
	}
}

void
debug_print_vreg_backing(struct vreg *vr, const char *custom_indent) {
	static int	tab = ' ';
	char		*indent;

	if (custom_indent != NULL) {
		indent = (char *)custom_indent;
	} else {
		static char	buf[2];
		buf[0] = tab;
		indent = buf;
	}

	printf("%s%p->var_backed = %p  []\n", indent, vr, vr->var_backed);
	printf("%s%p->from_const = %p\n", indent, vr, vr->from_const);
	printf("%s%p->from_ptr = %p\n", indent, vr, vr->from_ptr);
	printf("%s%p->parent = %p\n", indent, vr, vr->parent);
	printf("%s%p->stack_addr = %p\n", indent, vr, vr->stack_addr);
	printf("%s%p->preg = %p ", indent, vr, vr->pregs[0]);
	if (vr->pregs[0] != NULL) {
		printf("[%s->vreg = %p] [preg used %d]",
			vr->pregs[0]->name, vr->pregs[0]->vreg, vr->pregs[0]->used);
	}	
				
	if (custom_indent == NULL) {
		if (tab == ' ') tab = '\t';
		else tab = ' ';
	}
	putchar('\n');
}	

static void
debug_do_print_vreg(struct vreg *vr) {
	printf("[vr %p: ", vr);
	if (vr->var_backed) {
		printf("%s", vr->var_backed->dtype->name);
	} else if (vr->memberdecl) {
		if (vr->memberdecl->dtype) {
			printf(". %s", vr->memberdecl->dtype->name);
		} else {
			printf(". %lu",
				(unsigned long)vr->memberdecl->offset);
		}
	} else if (vr->from_const) {
		printf("(const)");
	} else if (vr->from_ptr) {
		printf("*ptr");
	} else if (vr->pregs[0] && vr->pregs[0]->vreg == vr) {
		printf("%s", vr->pregs[0]->name);
	} else if (vr->stack_addr == NULL) {
		printf("INVALID");
	} else {
		printf("(anonstack)");
	}
	printf(",%lu bytes,type=%d] ",
		(unsigned long)vr->size, vr->type? vr->type->code: 0);
}

static void
debug_do_print_preg(struct reg *r) {
	printf("[reg %s] ", r->name);
}


static struct vreg	*tracked_vrs[10];
static int		tracked_vrs_idx;

void
debug_track_vreg(struct vreg *vr) {
#if VREG_SEQNO
	printf("======== TRACKING VR %p [seq %d]\n",
		vr, vr->seqno);
#else
	printf("======== TRACKING VR %p [seq %d]\n",
		vr, vr->seqno);
#endif
	if (tracked_vrs_idx >= 10) {
		puts("Too many tracked vregs");
		return;
	}
	tracked_vrs[tracked_vrs_idx++] = vr;
}
/* XXX implement debug_untrack_vreg() if needed */

void
do_debug_check_tracked_vregs(const char *file, int line) {
	int	i;

	if (tracked_vrs_idx > 0) {
		static int	count;
		++count;
	/*	if (count == 26) abort();*/
		printf(" %s,%d  %d === STATE OF TRACKED VREGS NOW: ===\n", file, line, count);
	}
	for (i = 0; i < tracked_vrs_idx; ++i) {
#if VREG_SEQNO
		printf("     ======= vr %p [seq %d]: ======\n",
			tracked_vrs[i], tracked_vrs[i]->seqno);
#else
		printf("     ======= vr %p: ======\n", tracked_vrs[i]);
#endif
		debug_print_vreg_backing(tracked_vrs[i],
		       "             ");
	}
}


void
debug_do_log_regstuff(struct reg *r, struct vreg *vr, int flag) {
	static int	seqno;

	printf("%d", seqno++);
	switch (flag) {
	case DEBUG_LOG_FAULTIN:	
		putchar('\t');
		debug_do_print_vreg(vr);
		printf("-> ");
		debug_do_print_preg(r);
		break;
	case DEBUG_LOG_REVIVE:
		putchar('\t');
		printf("REVIVING ");
		debug_do_print_vreg(vr);
		printf("[");
		debug_do_print_preg(r);
		printf("]");
		break;
	case DEBUG_LOG_MAP:
		putchar('\t');
		printf("MAPPING ");
		debug_do_print_vreg(vr);
		printf("[");
		debug_do_print_preg(r);
		printf("]");
		break;
	case DEBUG_LOG_ALLOCGPR:
		putchar('\t');
		printf("ALLOC %s ", r->name);
		if (r->vreg != NULL && r->vreg->pregs[0] == r) {
			printf("[was mapped ");
			debug_do_print_vreg(r->vreg);
			printf("] ");
		}	
		if (backend->debug_print_gprs != NULL) {
			putchar('\n');
			backend->debug_print_gprs();
		}	
		break;
	case DEBUG_LOG_FREEGPR:
		putchar('\t');
		printf("FREE %s ", r->name);
		if (r->vreg != NULL && r->vreg->pregs[0] == r) {
			printf("[was mapped ");
			debug_do_print_vreg(r->vreg);
			printf("] ");
		}
		break;
	case DEBUG_LOG_FAILEDALLOC:
		putchar('\t');
		printf("FAILED GPR ALLOC ATTEMPT\n");
		if (backend->debug_print_gprs != NULL) {
			putchar('\n');
			backend->debug_print_gprs();
		}	
		break;
		/*
		 * 05/31/09: Display allocatability
		 */
	case DEBUG_LOG_ALLOCATABLE:
		putchar('\t');
		printf("ALLOCATABLE %s ", r->name);
		if (r->vreg != NULL && r->vreg->pregs[0] == r) {
			printf("[was mapped ");
			debug_do_print_vreg(r->vreg);
			printf("] ");
		}
		break;
	case DEBUG_LOG_UNALLOCATABLE:
		putchar('\t');
		printf("UN-ALLOCATABLE %s ", r->name);
		if (r->vreg != NULL && r->vreg->pregs[0] == r) {
			printf("[was mapped ");
			debug_do_print_vreg(r->vreg);
			printf("] ");
		}
		break;
	default:
		printf("Bad log flag %d\n", flag);
		abort();
	}	
	putchar('\n');

	debug_check_tracked_vregs();
}

void
debug_print_statement(struct statement *stmt) {
	static int	nesting;
	int		i;

	if (nesting == 0) {
		puts("  === Statement ===");
	}

	for (; stmt != NULL; stmt = stmt->next) {
		struct decl	*d;

		for (i = 0; i < nesting; ++i) {
			putchar('\t');
		}
		switch (stmt->type) {
		case ST_DECL:
			d = stmt->data;
			printf("[DECL   %s]\n", d->dtype->name);
			break;
		case ST_CODE:
			printf("[EXPR]\n");
			break;
		case ST_COMP:
			printf("[COMPOUND %p]\n", stmt->data);
			++nesting;
			debug_print_statement(((struct scope *)stmt->data)->code);
			--nesting;
			break;
		case ST_LABEL:
			printf("[LABEL]\n");
			break;
		case ST_ASM:
			printf("[ASM]\n");
			break;
		case ST_EXPRSTMT:
			printf("[GNU EXPR-STMT]\n");
			++nesting;
			debug_print_statement(stmt->data);
			--nesting;
			break;
		case ST_CTRL:
			printf("[CONTROL]\n");
			++nesting;
			debug_print_statement(((struct control *)stmt->data)->stmt);
			--nesting;
			if (stmt->next != NULL) {
				printf("[PART2 CONTROL]\n");
			}
			break;
		default:
			puts("[???]");
		}
	}
}

