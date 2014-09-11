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
 * Parsing of control structures
 * XXX This stuff is ugly 
 *
 * TODO Write LENGTHY text about how this %&#^$@@ stuff works. I am really
 * fed up with it because it is so immensely hard to debug and extend.
 
 12/04/07: Let's start with some graphical help to visualize the way C99
 for statements are represented:

        for (int temp = 0; temp < 3; ++temp)
                for (int j = 1, k = 0; j < 6; ++j, k += 2)
                        if (j % 2)
                                do
                                        printf("k=%d\n", k);
                                while (0);
                        else {
                                for (i = 0; i < 1; ++i)
                                        printf("j*k=%d\n", j*k);
                        }


... becomes (debug_print_statement()):

[COMPOUND 0x811dd98]
        [DECL   temp]
        [CONTROL]
                [COMPOUND 0x811eb80]
                        [DECL   j]
                        [DECL   k]
                        [CONTROL]
                                [CONTROL]
                                        [CONTROL]
                                                [EXPR]

 */
#include "control.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "token.h"
#include "error.h"
#include "expr.h"
#include "decl.h"
#include "functions.h"
#include "inlineasm.h"
#include "misc.h"
#include "debug.h"
#include "typemap.h"
#include "icode.h"
#include "standards.h"
#include "type.h"
#include "scope.h"
#include "zalloc.h"
#include "n_libc.h"

struct control	*curcont = NULL;
struct control	*curloop = NULL;

struct control *
alloc_control(void) {
#if USE_ZONE_ALLOCATOR
	return zalloc_buf(Z_CONTROL);
#else
	struct control	*ret = n_xmalloc(sizeof *ret);
	static struct control	nullctrl;
	*ret = nullctrl;
	return ret;
#endif
}

static struct label *
alloc_label(void) {
#if USE_ZONE_ALLOCATOR
	return zalloc_buf(Z_LABEL);
#else
	struct label	*ret = n_xmalloc(sizeof *ret);
	static struct label	nulllabel;
	*ret = nulllabel;
	return ret;
#endif
}	

void
append_label_list(struct label **head, struct label **tail, struct label *l) {
	if (l->appended) {
		abort();
	}
#if 0
	if (l == head) {
		printf("FATAL ERROR: Appending %p to itself\n", l);
		abort();
	}
#endif

	l->appended = 1;
	l->next = NULL;

	if (*head == NULL) {
		*head = *tail = l;
	} else {
		(*tail)->next = l;
		*tail = (*tail)->next;
	}
}

static void
set_parent_statement(struct control *c, struct control *parent) {
	if (c == parent) {
		printf("BUG: Control structure %d cannot be its own parent\n",
			c->type);	
		abort();
	}
	c->parent = parent;
}	

int
handle_switch_label(struct control *cont,
		struct token **tok,
		struct token *starttok,
		struct control *parent,
		int putscope) {
	struct token	*t = *tok;
	struct label		*label;
	struct statement	*stmt;

	if (cont->type == TOK_KEY_DEFAULT) {
		if (expect_token(&t, TOK_OP_AMB_COND2, 0) != 0) {
			goto fail;
		}
	} else {
		if (next_token(&t) != 0) {
			goto fail;
		}

		/*
		 * 07/18/08: Allow case ranges;
		 *
		 *    case 1 ... 5:
		 */
		cont->cond = parse_expr(&t, TOK_OP_AMB_COND2,
				TOK_ELLIPSIS, EXPR_CONST, 1);

		if (cont->cond != NULL && t->type == TOK_ELLIPSIS) {
			errorfl(t, "Switch case ranges are not "
				"implemented yet");	
		}

		if (cont->cond == NULL) {
			goto fail;
		} else {
			struct type	*tmp;

			tmp = cont->cond->const_value->type;
			if (!is_integral_type(tmp)) { 
				errorfl(*tok,
					"Switch case expression "
					"doesn't have integral "
					"type");
				goto fail;
			}
		}	
	}

	while (parent) {
		if (parent->type == TOK_KEY_SWITCH) {
			break;
		}  else {
			parent = parent->parent;
		}
	}	
	if (parent == NULL
		|| parent->type != TOK_KEY_SWITCH) {	
		errorfl(t,
			"`%s' keyword used outside of switch statement",
				starttok->ascii);
		goto fail;
	}

	if (cont->cond != NULL) {
		size_t	curval = cross_to_host_size_t(cont->cond->const_value);

		for (label = parent->labels;
			label != NULL;
			label = label->next) {
			/*
			 * 07/18/08: Finally do the comparison! Is as size_t OK?
			 */
			size_t	cmpval;
	
			if (label->value == NULL) {
				continue;
			}
			cmpval = cross_to_host_size_t(label->value->const_value);
			if (curval == cmpval) {
				errorfl(*tok, "Duplicate switch-case value %ld",
					(unsigned long)curval);
				goto fail;
			}
		}
	}
	*tok = t;

	label = alloc_label();
	label->instr = icode_make_label(NULL);
	label->value = cont->cond; 
	label->is_switch_label = 1;
	stmt = alloc_statement();
	stmt->type = ST_LABEL;
	stmt->data = label;
	cont->stmt = stmt;


	append_label_list(&parent->labels, &parent->labels_tail, label);
	if (putscope) put_ctrl_scope(cont);
	return 0;


fail:
	return -1;
}



static void
find_parent_statement(struct control *ctrl, struct token *nexttok,int compound);

static struct control	*elseok = NULL;
static struct control	*is_dowhile = 0;

struct control *
parse_ctrl(struct control *parent, struct token **tok, int putscope, int *compound0) {
	struct token		*t = *tok;
	struct token		*starttok = t;
	struct control		*cont;
	struct control		*tmpcont;
	struct control		*cont2;
	struct expr		*ex;
	char			*p;
	int			err = 0;

	if (is_dowhile) {
		cont = is_dowhile;
	} else {
		cont = alloc_control();
		cont->type = t->type;
#if 0
		cont->parent = /*curcont*/parent;
#endif
		set_parent_statement(cont, parent);

		/*
		 * Record whether the current ctrl structure should be
		 * stored in the current scope. I think this only applies
		 * to do { } while, and could perhaps be solved better
		 */
		cont->putscope = putscope;
	}
	cont->tok = *tok;

	/* 
	 * All of these control structures may only be used within a 
	 * function. We're at the top level (invalid) if func = NULL
	 */
	if (curfunc == NULL) {
		/*
		 * Error! But continue processing statement in order to prevent
		 * further syntax errors
		 */
		errorfl(*tok,
"Invalid `%s' statement at top-level. May only be used in functions!",
		(*tok)->ascii);
		err = 1; /* XXX this sux */
	}

	switch (t->type) {
	case TOK_KEY_DO:
		cont->startlabel = icode_make_label(NULL);
		cont->endlabel = icode_make_label(NULL);
		cont->do_cond = icode_make_label(NULL);
		break;
	case TOK_KEY_WHILE:
		if (expect_token(&t, TOK_PAREN_OPEN, 1) != 0) {
			goto fail;
		}
		free(t->prev);
		ex = parse_expr(&t, TOK_PAREN_CLOSE, 0, 0, 1);
		if (is_dowhile) {
			/* End of do ... while */
			if (expect_token(&t, TOK_SEMICOLON, 0) != 0) {
				goto fail;
			}
			cont->cond = ex;
			is_dowhile = NULL;

			/*
			 * Check whether we need to complete an outer
			 * do-while or if-else, or whether we have to
			 * complete an outer compound statement
			 */
			find_parent_statement(/*is_dowhile*/cont, t->next,
				cont->compound_body);
			if (cont->putscope) {
				put_ctrl_scope(cont);
			}
			*tok = t;
			return cont;
		} else {
			/* New while loop */
			cont->cond = ex;
			if (putscope) put_ctrl_scope(cont);
		}
		cont->startlabel = icode_make_label(NULL);
		cont->endlabel = icode_make_label(NULL);
		break;
	case TOK_KEY_FOR:
		if (expect_token(&t, TOK_PAREN_OPEN, 1) != 0) {
			goto fail;
		}
		free(t->prev);
		
		/*
		 * 11/28/07: C99 style extended for statements
		 */
		if (IS_TYPE(t)) {
			struct decl	**d;
			struct scope	*for_scope;
			int		i;

			if (stdflag == ISTD_C89) {
				warningfl(t, "Declarations in the first `for' clause are "
					"not allowed in C89 (declare outside of the loop, "
					"at the top of the nearest surrounding block instead!)");
			}

			/*
			 * Create surrounding scope, such that
			 *
			 *   for (int i = 0; i < 5; ++i) {}
			 *
			 * looks like
			 *
			 *   {
			 *      int i = 0;
			 *      for (; i < 5; ++i) {}
			 *   }
			 */
			for_scope = new_scope(SCOPE_CODE);
			d = parse_decl(&t, DECL_VARINIT);
			if (d == NULL
				|| next_token(&t) != 0) {
				goto fail;
			}

			for (i = 0; d[i] != NULL; ++i) {
				if (d[i]->dtype->storage != 0
					&& d[i]->dtype->storage != TOK_KEY_AUTO) {
					errorfl(d[i]->tok, "Invalid storage "
						"class for `for' declaration");	
					goto fail;
				}
			}
			cont->finit = NULL;
			cont->dfinit = d;
			cont->dfscope = for_scope;
		} else {
			cont->finit = parse_expr(&t, TOK_SEMICOLON, 0, 0, 1);
			if (cont->finit == NULL
				|| next_token(&t) != 0) {
				goto fail;
			}
			cont->dfinit = NULL;
		}

		free(t->prev);
		cont->cond = parse_expr(&t, TOK_SEMICOLON, 0, 0, 1);
		if (cont->cond == NULL
			|| next_token(&t) != 0) {
			goto fail;
		}
		free(t->prev);
		if (t->type != TOK_PAREN_CLOSE) {
			cont->fcont = parse_expr(&t, TOK_PAREN_CLOSE, 0, 0, 1);
			cont->fcont_label = icode_make_label(NULL);
			if (cont->fcont == NULL) {
				goto fail;
			}
		} else {
			cont->fcont = NULL;
		}
		cont->startlabel = icode_make_label(NULL);
		cont->endlabel = icode_make_label(NULL);
		if (putscope) {
			if (cont->dfinit != NULL) {
				/*
				 * Current scope is for declarations in
				 * initializer, so we have to put this
				 * statement into the parent scope!
				 */
				struct statement	*st;

				st = alloc_statement();
				st->type = ST_COMP;
				st->data = cont->dfscope;
				append_statement(&cont->dfscope->parent->code,
					&cont->dfscope->parent->code_tail, st);
				assert(curscope == cont->dfscope);
				put_ctrl_scope(cont);
			} else {
				put_ctrl_scope(cont);
			}
		} else if (cont->dfinit != NULL) {
			/*
			 * The parent will put our surrounding C99 scope
			 * into its body... However we still have to
			 * put the ``for'' itself into that surrounding
			 * scope
			 */
			put_ctrl_scope(cont);
		}
		break;
	case TOK_KEY_IF:
		if (expect_token(&t, TOK_PAREN_OPEN, 1) != 0) {
			goto fail;
		}
		free(t->prev);
		cont->cond = parse_expr(&t, TOK_PAREN_CLOSE, 0, 0, 1);
		cont->endlabel = icode_make_label(NULL);
		if (putscope) put_ctrl_scope(cont);
		break;
	case TOK_KEY_ELSE:
		if (is_dowhile) {
			abort();
		}	
		if (!elseok) {
			errorfl(t, "Parse error at `else'");
			goto fail;
		}
		cont->endlabel = icode_make_label(NULL);
		cont->prev = elseok;
		cont->prev->next = cont;
		elseok = NULL;
#if 0
		cont->parent = cont->prev->parent;
#endif
		set_parent_statement(cont, cont->prev->parent);
		break;
	case TOK_KEY_SWITCH:
		if (expect_token(&t, TOK_PAREN_OPEN, 1) != 0) {
			goto fail;
		}
		free(t->prev);
		cont->cond = parse_expr(&t, TOK_PAREN_CLOSE, 0, 0, 1);
		cont->endlabel = icode_make_label(NULL);
		if (putscope) put_ctrl_scope(cont);
		break;
	case TOK_KEY_BREAK:
	case TOK_KEY_CONTINUE:
		p = cont->type == TOK_KEY_BREAK? "break": "continue";
		if (expect_token(&t, TOK_SEMICOLON, 0) != 0) {
			goto fail;
		}

		for (tmpcont = cont->parent;
			tmpcont != NULL;
			tmpcont = tmpcont->parent) {
			if (tmpcont->type == TOK_KEY_DO
				|| tmpcont->type == TOK_KEY_WHILE
				|| tmpcont->type == TOK_KEY_FOR) {
				break;
			} else if (cont->type == TOK_KEY_BREAK
				&& tmpcont->type == TOK_KEY_SWITCH) {
				break;
			}
		}	
		if (tmpcont == NULL) {
			errorfl(t, "`%s' used outside of loop", p);
			goto fail;
		}	
		if (cont->type == TOK_KEY_BREAK) {
			cont->endlabel = tmpcont->endlabel;
		} else {
			/* continue */
			if (tmpcont->type == TOK_KEY_DO) {
				cont->startlabel = tmpcont->do_cond;
			} else if (tmpcont->type == TOK_KEY_FOR) {
				if (tmpcont->fcont_label != NULL) {
					cont->startlabel = tmpcont->fcont_label;
				} else {
					cont->startlabel = tmpcont->startlabel;
				}	
			} else {	
				cont->startlabel = tmpcont->startlabel;
			}	
		}	
		*tok = t;
		if (putscope) put_ctrl_scope(cont);
		return cont;
	case TOK_KEY_RETURN:
		if (next_token(&t) != 0) {
			goto fail;
		}
		if (t->type != TOK_SEMICOLON) {
			cont->cond = parse_expr(&t, TOK_SEMICOLON, 0, 0, 1);
			if (cont->cond == NULL) {
				goto fail;
			}	
		} else {
			cont->cond = NULL;
		}	
		if (putscope) put_ctrl_scope(cont);
		*tok = t;
		return cont;
	case TOK_KEY_GOTO:
		if (next_token(&t) != 0) {
			goto fail;
		}
		if (t->type != TOK_IDENTIFIER) {
			/*
			 * 07/20/08: This may be a computed goto (GNU C);
			 *
			 *    goto *ptr;
			 */
			if (t->type == TOK_OPERATOR
				&& *(int *)t->data == TOK_OP_AMB_MULTI) {
				struct expr	*gotoex;
				struct token	*firsttok = t;

				if (next_token(&t) != 0) {
					goto fail;
				}
				gotoex = parse_expr(&t, TOK_SEMICOLON, 0, 0, 1);
				if (gotoex == NULL) {
					errorfl(t,
						"Invalid argument to `goto' - must be "
						"label or computed goto expression");
					goto fail;
				} else {
					/* OK - assume computed goto */
					warningfl(firsttok, "ISO C does not support "
						"computed gotos");
					cont->cond = gotoex;
					t = t->prev;
				}
				cont->stmt = NULL;
			}
		} else {
			cont->stmt = (struct statement *)t; /* XXX change void *data */
		}

		if (expect_token(&t, TOK_SEMICOLON, 0) != 0) {
			goto fail;
		}
		*tok = t;
		if (putscope) put_ctrl_scope(cont);
		return cont;
	case TOK_KEY_CASE:
	case TOK_KEY_DEFAULT:
		if (handle_switch_label(cont, &t, starttok, parent, putscope) != 0) {
			goto fail;
		}
		*tok = t;
		return cont;
	}
	if (err) goto fail;

	/* Only loop/if/switch statements reach this point! */

	if (next_token(&t) != 0) {
		goto fail;
	}

	/*
	 * 02/05/10: Warn about relatively common mistake of writing
	 *
	 *     for (x; y; z);  <-- note empty ; body
	 *         supposed_body();
	 */
	if (t->type == TOK_SEMICOLON
		&& cont->tok->line == t->line) {
		warningfl(t, "Empty expression `;' as body for `%s' may be "
			"unintentional, suggest moving and indenting `;' "
			"on new line if it really is intended", cont->tok->ascii);
	}

	/*
	 * Now we read the statement body. Labels are allowed to be placed
	 * in front of any statement, e.g.
	 *
	 * if (foo) label: {
	 *    ...code...
	 * }
	 *
	 * In particular, the gcc code contains some such constructs
	 */
	for (;;) {
		struct label	*l = NULL;
		struct control *switch_label = NULL;
		if (t->type == TOK_IDENTIFIER) {
			if (try_label(&t, &l)) {
				/* Was label! skip ``:'' */
				t = t->next;
			} else {
				break;
			}
		} else if ( (t->type == TOK_KEY_CASE
			|| t->type == TOK_KEY_DEFAULT)
			/*&& cont->type != TOK_KEY_SWITCH*/) {
			/*
			 * 09/10/13: Handle switch cases preceding statement
			 * bodies as well, e.g.
			 *
			 *	switch (1) {
			 *	case 1:  if (0)
			 *	case 2:   puts("hello");
			 *
			 * Here the label definition of ``case 2'' was not
			 * properly merged with the if statement, so the
			 * puts() call was incorrectly always executed even
			 * if the switch was entered through ``case 1''.
			 *
			 * Thus we treat
			 *      if (0) case 2: puts("hello");
			 * like
			 *      if (0) foo: puts("hello");
			 * now.
			 *
			 * There may be problems with mixing of ordinary
			 * labels and switch labels in this construct.
			 *
			 * Switch label handling may still not be 100%
			 * correct, but now we handle the above if (0)
			 * case as found in mksh. And Duff's Device
			 * type of compound loops spread across multiple
			 * cases were already working before this change,
			 * so that should cover most if not all cases
			 * of switch label (ab-)use.
			 */
			/*
			 * 10/20/09: Don't store body case labels in the
			 * nested parse_ctrl() call! Otherwise the label
			 * is not emitted (if we disable the appending
			 * code below), or the label is appended twice
			 * (if we enable both the appending code below
			 * as well as in parse_ctrl())
			 * This (and label handling in switch in general)
			 * is not fully understood yet and we use a
			 * silly flag to work around it.
			 */
			switch_label = parse_ctrl(/*parent*/ cont, &t, 0, NULL);
			if (switch_label != NULL) {
				t = t->next; /* skip colon */
#if 1 
				l = switch_label->stmt->data;
#endif
			}
		} else {
			break;
		}	

		if (l != NULL) {
			if (cont->body_labels == NULL) {
				cont->body_labels = alloc_icode_list();
			}	
			if (switch_label == NULL) {
				/*
				 * 10/20/09: Don't append switch case
				 * label because it is already kept in
				 * the switch label list
				 */
				append_label_list(&curfunc->labels_head,
					&curfunc->labels_tail, l);
			}
			append_icode_list(cont->body_labels,
				l->instr);	
		}
	}	

	if (t->type != TOK_COMP_OPEN) {
		/*
		 * We can already complete the control
		 * structure here because it only has an
		 * expression or control structure as body,
		 * as opposed to a compound statement
		 */
		struct statement	*stmt;
		int			compound = 0;
		
		if (IS_CONTROL(t->type)) {

			/*
			 * XXX below was contr. that breaks for
			 * if (hm) { if (foo) {} else if (bar) {..  }
			 *                  ^^ parent should be hm-if
			 */
			if ((cont2 = parse_ctrl(cont, &t, 0, &compound))
				== NULL) {
				goto fail;
			}

			if (cont2->type == TOK_KEY_FOR
				&& cont2->dfinit != NULL) {	
				struct statement	*st;

				st = alloc_statement();
				st->type = ST_COMP;
				st->data = cont2->dfscope;
				cont->stmt = st;
#if 0
				append_statement(&curscope->parent->code,
					&curscope->parent->code_tail, st);
				put_ctrl_scope(cont);
#endif
			} else {	
				stmt = alloc_statement();
				stmt->type = ST_CTRL;
				stmt->data = cont2;
				cont->stmt = stmt;
			}	
		} else if (t->type == TOK_KEY_ASM) {
			struct inline_asm_stmt	*inl;

			if ((inl = parse_inline_asm(&t)) == NULL) {
				goto fail;
			}
			stmt = alloc_statement();
			stmt->type = ST_ASM;
			stmt->data = inl;
			cont->stmt = stmt;
		} else {
			struct expr	*ex;

			ex = parse_expr(&t, TOK_SEMICOLON, 0, 0, 1);
			if (ex == NULL) {
				goto fail;
			}
			/* XXX t = t->next ??? */
			stmt = alloc_statement();
			stmt->type = ST_CODE;
			stmt->data = ex;
			cont->stmt = stmt;
		}
		if (cont->type == TOK_KEY_IF) {
			if (t->next == NULL) {
				goto fail;
			}
			if (t->next->type == TOK_KEY_ELSE
				&& !compound) {
				/*
				 * If the if-body is a non-compound
				 * statement, we can already read the
				 * corresponding else here
				 */
				t = t->next;
				elseok = cont;
				if ((cont2 = parse_ctrl(cont->parent /* hmm*/,
					&t, 0, &compound)) == NULL) {
					goto fail;
				}
				cont->next = cont2;
			}
		} else if (cont->type == TOK_KEY_DO) {
			if (!compound) {
				if (next_token(&t) != 0) {
					goto fail;
				}
				if (t->type != TOK_KEY_WHILE) {
					errorfl(t, "Unterminated do-while "
						"loop");
					goto fail;
				}
				is_dowhile = cont;
				if (parse_ctrl(cont, &t, 0, NULL) == NULL) {
					goto fail;
				}
			}		
		} else if (cont->type == TOK_KEY_FOR) {
			if (!compound) {
				if (cont->dfinit != NULL) {
					/*
					 * Close scope for C99 ``for'' variable
					 * declarations
					 */
					(void) close_scope();
				}
			}
		}

		/*
		 * All of these tons and tons of immensely complex code
		 * do not suffice to make it all work. As a last resort,
		 * we now check whether a parent statement has not yet
		 * been completed, and if that's the case, prepare to
		 * complete it. If we have something like
		 *
		 * do
		 * 	if (1) for (;;) { puts("hello"); }
		 * 	else puts("hmm");
		 * while (0);
		 *
		 * ... then the compound ``for()'' causes the outer do-
		 * while not to be completed. When we're done reading
		 * the ``else'', it is also necessary to look for
		 * non-compound parent do-while statements, such as the
		 * one above, or if-else statements. Because if those
		 * are not completed, the ``while (0);'' above will be
		 * read as a while-loop with an empty statement as
		 * body
		 */
		if (!compound) {
			find_parent_statement(cont, t->next, 0);
		}

		if (compound0 != NULL) {
			*compound0 = compound;
		}	
	} else {
		/* Is compound statement - complete_ctrl() does the rest */
		curcont = cont;
		t = t->prev;
		if (compound0 != NULL) {
			*compound0 = 1;
		}	
	}

	*tok = t;
	return cont;

fail:
	*tok = t;
#if ! USE_ZONE_ALLOCATOR
	if (cont) free(cont);
#endif
	return NULL;
}


static void
find_parent_statement(struct control *ctrl, struct token *nexttok,
	int compound) {

	struct control	*saved_curcont = curcont;

	if (ctrl->parent) {
		/*
		 * curcont is used to match a closing brace (}) with the
		 * opening brace of a control structure. So to match with
		 * the next control structure above, we need to find one
		 * that has a compound statement ({ ... }) as body. I
		 * really really hope this approach FINALLY correct
		 */
		curcont = ctrl->parent;
		while (!curcont->compound_body) {
			if (elseok != NULL || is_dowhile != NULL) {
				/*
				 * We have already found something that
				 * must be completed; now we're only
				 * interested in the compound parent
				 * anymore
				 */
				if ((curcont = curcont->parent) == NULL) {
					break;
				}
				continue;
			}	
			if (curcont->type == TOK_KEY_IF
				&& nexttok->type == TOK_KEY_ELSE) {
				/*
				 * We're at the end of an if-statement,
				 * so there may be an ``else'' part
				 * following it. Note that this
				 * doesn't work for do-while because the
				 * do-while parse isn't complete when
				 * this function is called
				 */
				if (elseok == NULL) {
					/*
					 * Next parse_ctrl() may read an
					 * else
					 */
				/*	elseok = ctrl;*/
					elseok = curcont;
				}	
			} else if (curcont->type == TOK_KEY_DO) {
				/*
				 * Finishing do-while with control
				 * structure containing a compound
				 * statement as body;
				 *
				 * do if (foo) { stuff; } while (0);
				 */
				is_dowhile = curcont;
			} else if (curcont->type == TOK_KEY_FOR) {
				if (curcont->dfinit != NULL && compound) {
					/*
					 * Finishing ``for'' with C99 style
					 * variable declarations
					 */
					(void) close_scope();
				}		
			}	
			if ((curcont = curcont->parent) == NULL) {
				break;
			}
		}
	} else {
		curcont = NULL;
	}

	if (!compound) {
		/*
		 * We are only checking whether we have to complete a
		 * parent do-while (by reading the while part) or an
		 * if (by reading the else part, if any.) We are only
		 * interested in direct, noncompound parents
		 */
		if (is_dowhile != NULL) {
			if (is_dowhile->compound_body) {
				is_dowhile = NULL;
			} else {
				/* Found a match */
				if (nexttok == NULL
					|| nexttok->type != TOK_KEY_WHILE) {
					errorfl(nexttok, "Unterminated do- "
						"while loop");
					is_dowhile = NULL;
				}
			}	
		} else if (elseok) {
			if (elseok->compound_body) {
				elseok = NULL;
			} else {
				/* Found a match */
			}	
		}
		curcont = saved_curcont;
	}	
}

void
complete_ctrl(struct token **tok, struct control *ctrl) {
	elseok = NULL, is_dowhile = 0;
	
	if ((*tok)->next == NULL) {
		errorfl(*tok, "Premature end of file");
		return;
	}	

	if (ctrl->type == TOK_KEY_IF) {
		if ((*tok)->next->type == TOK_KEY_ELSE) {
			/* Wait for next parse_ctrl() */
			elseok = ctrl;
		}
	} else if (ctrl->type == TOK_KEY_DO) {
		if ((*tok)->next->type == TOK_KEY_WHILE) {
			is_dowhile = ctrl;
		} else {
			errorfl((*tok)->next,
				"Unterminated do-while loop");
		}
	} else if (ctrl->type == TOK_KEY_FOR) {
		if (ctrl->dfinit != NULL) {
			(void) close_scope();
		}	
	}	

	/*
	 * If we're dealing with do-while, the next parse_ctrl()
	 * cannot pick up a possible ``else'' because it first has
	 * to get the ``while (...)'' part. So if we have a do-
	 * while, we call find_parent_statement() in parse_ctrl()
	 */
	if (ctrl->type != TOK_KEY_DO) {
		find_parent_statement(ctrl, (*tok)->next, 1 /* compound */);
	}	
}

int
is_label(struct token **tok) {
	struct token	*t = *tok;

	if (t->next != NULL
		&& (t->next->type == TOK_OPERATOR
			&& *(int *)t->next->data == TOK_OP_AMB_COND2)) {
		return 1;
	}
	return 0;
}

int
try_label(struct token **tok, struct label **resp) {
	struct token	*t = *tok;

	if (is_label(tok)) {
		struct icode_instr	*ii;
		struct label		*l;

		for (l = curfunc->labels_head;
			l != NULL;
			l = l->next) {
	if (l->name == NULL) continue;
			if (strcmp(l->/*instr->dat*/name, t->data) == 0) {
				errorfl(t, "Duplicate label `%s'", t->ascii);
				return 1;
			}
		}
		ii = icode_make_label(/*t->data*/NULL);
#if 0
		l = n_xmalloc(sizeof *l);
#endif
		l = alloc_label();
		l->name = t->data;
		l->next = NULL;
		l->value = NULL;
		l->instr = ii;
		if (resp) {
			*resp = l;
		} else {	
			put_label_scope(l);
		}	
		(void) next_token(tok);
		return 1;
	}
	return 0;
}

