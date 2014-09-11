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
 * Parses gas inline assembly language statements and converts them to NASM
 * code
 */
#include "inlineasm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include "defs.h"
#include "token.h"
#include "expr.h"
#include "backend.h"
#include "error.h"
#include "x86_emit_nasm.h"
#include "x86_emit_gas.h"
#include "x86_gen.h"
#include "n_libc.h"


static int		lineno;
static struct token	*asm_tok;

/*
 * XXX the store_char() stuff bothers me, this adhoc interface should've been
 * trashed long ago
 */

static int
get_char(char **code) {
	int	ret;

	if (**code == 0) {
		return EOF;
	}
	ret = (unsigned char)**code;
	++*code;
	return ret;
}

static void
unget_char(int ch, char **code) {
	(void) ch;
	--*code;
}

#if 0
struct gas_token {
	int			type;
#define GAS_SEPARATOR	1001
#define GAS_DOTSTRING	1002
#define GAS_STRING	1003
#define GAS_DOLLAR	1004
#define GAS_OCTAL	1020
#define GAS_HEXA	1021
#define GAS_DECIMAL	1022
#define GAS_NUMBER(x) (x == GAS_OCTAL || x == GAS_HEXA || x == GAS_DECIMAL)
	int			lineno;
	int			idata;
	void			*data;
	char			*ascii;
	struct gas_token	*next;
};
#endif
#define ALLOCATED_TOKEN(type) \
	(type == GAS_DOTIDENT || type == GAS_IDENT || \
	 type == GAS_OCTAL || type == GAS_HEXA || type == GAS_DECIMAL)

static struct gas_token *
make_gas_token(int type, void *data) {
	struct gas_token	*ret;
	static struct gas_token	nulltok;

	ret = n_xmalloc(sizeof *ret);
	*ret = nulltok;
	ret->type = type;
	ret->data = data;
	ret->lineno = lineno;
	switch (ret->type) {
	case TOK_PAREN_OPEN:
		ret->ascii = "(";
		break;	
	case TOK_PAREN_CLOSE:
		ret->ascii = ")";
		break;
	case GAS_IDENT:
	case GAS_DOTIDENT:
	case GAS_OCTAL:
	case GAS_HEXA:
	case GAS_DECIMAL:	
	default:	
		ret->ascii = ret->data;
		break;
	}	

	return ret;
}


static int	gas_errors;

static void
gas_errorfl(struct gas_token *gt, const char *fmt, ...) {
	va_list	va;

	++gas_errors;
	++errors;
	fprintf(stderr, "%s:%d+%d: ", asm_tok->file, asm_tok->line, gt->lineno);
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);
	fputc('\n', stderr);
	print_source_line(asm_tok->line_ptr, asm_tok->tok_ptr, asm_tok->ascii,
		1);
}	

static struct gas_token *
get_string(char **code) {
	char	*buf = NULL;
	int	ch;

	store_char(NULL, 0);
	while ((ch = get_char(code)) != EOF) {
		if (ch == '"') {
			break;
		}
		store_char(&buf, ch);
	}	
	store_char(&buf, 0);
	return make_gas_token(GAS_STRING, buf);
}	

static struct gas_token *
get_ident(int firstch, char **code) {
	char	*buf = NULL;
	int	toktype;
	int	ch;

	store_char(NULL, 0);

	if (firstch == '.') {
		toktype = GAS_DOTIDENT;
	} else {
		toktype = GAS_IDENT;
		store_char(&buf, firstch);
	}

	while ((ch = get_char(code)) != EOF) {
		if (isalnum(ch) || ch == '_') {
			store_char(&buf, ch);
		} else {
			unget_char(ch, code);
			break;
		}
	}
	store_char(&buf, 0);
	return make_gas_token(toktype, buf);
}


static struct gas_token *
get_number(int firstch, char **code, int sign) {
	char			*buf = NULL;
	struct gas_token	*ret;
	unsigned long		*val;
	int			hexa = 0;
	int			octal = 0;
	int			ch;

	store_char(NULL, 0);
	store_char(&buf, firstch);
	if (firstch == '0') {
		/* May be hexadecimal */
		if ((ch = get_char(code)) == EOF) {
			goto out;
		}	
		if (ch == 'x') {
			store_char(&buf, 'x');
			hexa = 1;
		} else if (!isdigit(ch)) {
			store_char(&buf, '0');
			unget_char(ch, code);
			goto out;
		} else {
			octal = 1;
			store_char(&buf, ch);
		}	
	}	

	while ((ch = get_char(code)) != EOF) {
		ch = tolower(ch);
		if (isdigit(ch)
			|| (hexa && ch && strchr("abcdef", ch))) {
			store_char(&buf, ch);
#if 0
		} else if ((ch == 'f' || ch == 'b') && !hexa) {
			/*
			 * 02/28/09: This isn't really a number but a
			 * numeric label like ``1f'' or ``1b'';
			 *
			 *   jne 1f
			 *   1:
			 */
			store_char(&buf, ch);
			if (ch == 'f') {
				return make_gas_token(GAS_FORWARD_LABEL, buf);
			} else {
				return make_gas_token(GAS_BACKWARD_LABEL, buf);
			}
#endif
		} else {
			unget_char(ch, code);
			break;
		}	
	}
out:
	store_char(&buf, 0);
	val = n_xmalloc(sizeof *val);
	if (octal) {
		sscanf(buf, "%lo", val);
		ret = make_gas_token(GAS_OCTAL, buf);
	} else if (hexa) {
		sscanf(buf, "%lx", val);
		ret = make_gas_token(GAS_HEXA, buf);
	} else {
		sscanf(buf, "%ld", val);
		ret = make_gas_token(GAS_DECIMAL, buf);
	}
	ret->data2 = val;
	if (sign) {
		ret->idata = 1;
	}
	return ret;
}



static void
append_gas_list(
	struct gas_token **dest,
	struct gas_token **dest_tail,
	struct gas_token *src) {
	if (*dest == NULL) {
		*dest = *dest_tail = src;
	} else {
		(*dest_tail)->next = src;
		*dest_tail = (*dest_tail)->next;
	}
}	

static void
free_gas_token(struct gas_token *t) {
	if (t == NULL) return;
	if (t->data != NULL) {
		if (ALLOCATED_TOKEN(t->type)) {
			free(t->data);
		}
	}	
	free(t);
}	

static void
free_gas_token_list(struct gas_token *t) {
	while (t != NULL) {
		struct gas_token	*next = t->next;
		free_gas_token(t);
		t = next;
	}	
}

static struct gas_token *
tokenize_gas_code(char *code) {
	int			ch;
	struct gas_token	*gt;
	struct gas_token	*glist = NULL;
	struct gas_token	*glist_tail = NULL;
	struct gas_token	dummytok;

	lineno = 0;

	while ((ch = get_char(&code)) != EOF) {
		gt = NULL;
		switch (ch) {
		case '\t':
		case ' ':
		case '\n':
			if (ch == '\n') {
				++lineno;
				gt = make_gas_token(GAS_SEPARATOR, NULL);
			}
			break;
		case ';':	
			gt = make_gas_token(GAS_SEPARATOR, NULL);
			break;
		case '(':	
			gt = make_gas_token(TOK_PAREN_OPEN, NULL);
			break;
		case ')':
			gt = make_gas_token(TOK_PAREN_CLOSE, NULL);
			break;
		case '-':
			while ((ch = get_char(&code)) != EOF
				&& isspace(ch))
				;
			
			if (ch == EOF || !isdigit(ch)) {
				dummytok.lineno = lineno;
				gas_errorfl(&dummytok,
					"Parse error at `%c' (%d)",
					ch == EOF? '-': ch, __LINE__);
				free_gas_token_list(glist);
				return NULL;
			}

			gt = get_number(ch, &code, 1);
			break;
		case ',':
			gt = make_gas_token(TOK_OP_COMMA, NULL);
			break;
		case '"':
			gt = get_string(&code);
			break;
		case ':':
			gt = make_gas_token(TOK_OP_AMB_COND2, ":");
			break;
		case '$':
			gt = make_gas_token(GAS_DOLLAR, "$");
			break;
		case '%':
			gt = make_gas_token(TOK_OP_MOD, "%");
			break;
		case '.':
#if 0
			if ((ch = get_char(&code)) == EOF 
				|| !isalnum((unsigned char)ch)) {
				dummytok.lineno = lineno;
				gas_errorfl(&dummytok,
					"Parse error at `%c' (%d)",
					ch, __LINE__);
				free_gas_token_list(glist);
				return NULL;
			}
#endif
			gt = get_ident(ch, &code);
			break;
		default:
			if (isdigit((unsigned char)ch)) {
				gt = get_number(ch, &code, 0);
			} else if (isalpha((unsigned char)ch) || ch == '_') {
				gt = get_ident(ch, &code);
			} else {
				dummytok.lineno = lineno;
				gas_errorfl(&dummytok,
					"Parse error at `%c' (%d)",
					ch, __LINE__);
				free_gas_token_list(glist);
				return NULL;
			}	
		}
		if (gt != NULL) {
			append_gas_list(&glist, &glist_tail, gt);
		}	
	}

#if 0
	printf("tokens:\n");
	for (gt = glist; gt != NULL; gt = gt->next) {
		printf("\t%d\n", gt->type);
	}
#endif
	return glist;
}

#if 0
struct gas_operand {
#define ITEM_REG	1
#define ITEM_NUMBER	2
#define ITEM_VARIABLE	3
#define ITEM_IO		4
	int	addr_mode;
#define ADDR_ABSOLUTE	1
#define ADDR_INDIRECT	2
#define ADDR_SCALED	3
#define ADDR_DISPLACE	4
#define ADDR_SCALED_DISPLACE	5
	void	*items[4];
	int	item_types[4];
};
#endif

static int
next_gas_token(struct gas_token **tok) {
	if ((*tok)->next == NULL) {
		struct gas_token	dummy;
		dummy.lineno = lineno;
		gas_errorfl(&dummy, "Unexpected end of assembler statement");
		return -1;
	}
	*tok = (*tok)->next;
	return 0;
}

static int
is_number(const char *str) {
	for (; isdigit((unsigned char)*str); ++str);
		;
	return *str == 0;
}

static struct inline_asm_io *
get_operand_by_idx(
	struct inline_asm_stmt *st,
	struct gas_token *t,
	int idx,
	int *curitem_type) {

	struct inline_asm_io	*io;
	int			n_ios = st->n_inputs + st->n_outputs;

	if (t != NULL) {
		idx = (int)*(unsigned long *)t->data2;
	}	

	if (idx >= n_ios) {
		gas_errorfl(t, "I/O operand %d does "
			"not exist, highest is %lu "
			"(first one is %0!)", idx, n_ios);
		return NULL;
	}	
	++idx;
	if ((st->output != NULL
		&& idx > st->n_outputs)
		|| (st->output == NULL)) {
		/* Must be input */
		idx -= st->n_outputs;
		if (curitem_type != NULL) {
			*curitem_type = ITEM_INPUT;
		}
		io = st->input;
	} else {
		/* Output */
		if (curitem_type != NULL) {
			*curitem_type = ITEM_OUTPUT;
		}
		io = st->output;
	}
	while (--idx > 0) {
		io = io->next;
	}
	return io;
}

static char *
get_constraint_by_idx(struct inline_asm_stmt *data, int idx) {
	int			i;
	struct inline_asm_io	*io = data->output; 
	int			input_done = 0;

	for (i = 0; i < idx; ++i) {
		if (io == NULL) {
			if (input_done) {
				return NULL;
			} else {
				io = data->input;
			}
		}
		if (io == NULL) {
			return NULL;
		}
		io = io->next;
	}
	if (io != NULL) {
		return io->constraints;
	}
	return NULL;
}	

/*
 * Parses an operand to an instruction. This resembles a state machine,
 * which is why it is hard to read. Basically, it just always reads either
 * a parentheses or an ``item'' (a register, number or variable.) 
 * Parentheses may dictate some sort of indirect addressing mode. The items
 * and their types are stored in the items and item_types arrays in the
 * order in which they appear;
 * -0x20(%eax)  ->  -0x20=items[0], %eax=items[1]
 * 0x80(%eax,%ebx,0x4) ->   0x80=items[0], %eax=items[1], ....
 */
static struct gas_operand *
parse_operand(struct gas_token **tok, struct inline_asm_stmt *stmt) {
	struct gas_token		*t;
	struct gas_operand		*ret;
	static struct gas_operand	nullop;
	void				**curitem;
	int				*curitem_type;
	int				index = 0;

	ret = n_xmalloc(sizeof *ret);
	*ret = nullop;
	curitem = &ret->items[0]; 
	curitem_type = &ret->item_types[0];

	for (t = *tok; t != NULL; t = t->next) {
		if (t->type == GAS_DOLLAR) {
			/* Should be immediate - $1234 */
			if (next_gas_token(&t) != 0) {
				return NULL;
			}
			if (t->type == GAS_IDENT) {
				*curitem_type = ITEM_VARIABLE;
				*curitem = t;
			} else if (GAS_NUMBER(t->type)) {
				*curitem_type = ITEM_NUMBER;
				*curitem = t;
			} else {
				gas_errorfl(t, "Parse error at `%s' (%d)",
					t->ascii, __LINE__);
				return NULL;
			}
		} else if (GAS_NUMBER(t->type)) {
			if (t->next
				&& t->next->type == GAS_IDENT
				&& (strcmp(t->next->data, "b") == 0
					|| strcmp(t->next->data, "f") == 0)) {
				*curitem_type = ITEM_LABEL;
				*curitem = /* XXX */
					backend->get_inlineasm_label(t->data);
				t = t->next;
			} else {	
				*curitem_type = ITEM_NUMBER;
				*curitem = t;
				if (ret->addr_mode == 0) {
					ret->addr_mode = ADDR_ABSOLUTE;
				} else {
					gas_errorfl(t,
						"Parse error at `%s' (%d)",
						t->ascii, __LINE__);
					return NULL;
				}
			}
			++index;
			curitem = &ret->items[index];
			curitem_type = &ret->item_types[index];
		} else if (t->type == GAS_IDENT) {
			/*
			 * XXX can this possibly be right, and made work with
			 * NASM?!?
			 */
			*curitem_type = ITEM_VARIABLE;
			*curitem = t;
			if (ret->addr_mode == 0) {
				ret->addr_mode = ADDR_INDIRECT;
			} else {
				gas_errorfl(t, "Parse error at `%s' (%d)",
					t->ascii, __LINE__);
				return NULL;
			}
#if 0
		} else if (t->type == GAS_FORWARD_LABEL
			|| t->type == GAS_BACKWARD_LABEL) {
			*curitem_type = ITEM_FW_BW;
			if (ret->addr_mode == 0) {
				ret->addr_mode = ADDR_
#endif
		} else if (t->type == TOK_OP_MOD) {
			if (next_gas_token(&t) != 0) {
				return NULL;
			}
			if (t->type == TOK_OP_MOD) {
				/* Should be %%reg */
				if (next_gas_token(&t) != 0) {
					return NULL;
				}
				if (t->type != GAS_IDENT) {
					gas_errorfl(t,
						"Parse error at `%s' (%d)",
						t->ascii, __LINE__);
					return NULL;
				} else if (!stmt->extended) {
					gas_errorfl(t, "Second %% in %%%%reg "
						"is only allowed for "
						"extended asm statements "
						"(use %reg instead)");
					return NULL;
				}
				*curitem_type = ITEM_REG;
				*curitem = t;
			} else if (GAS_NUMBER(t->type)) {
				struct inline_asm_io	*io;

				/* i/o operand */
				if (!stmt->extended) {
					gas_errorfl(t, "`%%number' is only "
						"allowed in extended asm "
						"statements");
					return NULL;
				}
				io = get_operand_by_idx(stmt, t, 0,
					curitem_type);

				if (io == NULL) {
					return NULL;
				}	
				*curitem = io;
				++index;
				curitem = &ret->items[index];
				curitem_type = &ret->item_types[index];
			} else if (t->type == GAS_IDENT) {
				if (stmt->extended) {
					if ((t->ascii[0] == 'h'
						|| t->ascii[0] == 'b'
						|| t->ascii[0] == 'w')
						&& is_number(t->ascii+1)) {
						char	*p;
						long	rc;
						struct inline_asm_io	*io;

						errno = 0;
						rc = strtol(t->ascii+1, &p, 0);
						if (errno
							|| rc == LONG_MIN
							|| rc == LONG_MAX
							|| *p) {
							gas_errorfl(t,
					"Parse error at `%s' (%d)",
							t->ascii, __LINE__);
							return NULL;
						}
						io = get_operand_by_idx(stmt,
							NULL, rc, curitem_type);
						if (io == NULL) {
							return NULL;
						}

						*curitem = io;
						if (t->ascii[0] == 'w') {
							*curitem_type = ITEM_SUBREG_W;
						} else {
							*curitem_type =
								t->ascii[0] == 'b'?
								ITEM_SUBREG_B
									: ITEM_SUBREG_H;
						}
					} else {	
						gas_errorfl(t, "%%reg isn't "
		"allowed in extended asm statements (use %%%%reg instead)");
						return NULL;
					}
				} else {	
					*curitem_type = ITEM_REG;
					*curitem = t;
				}
				++index;
				curitem = &ret->items[index];
				curitem_type = &ret->item_types[index];
			} else {
				gas_errorfl(t, "Parse error at `%s' (%d)",
					t->ascii, __LINE__);
				return NULL;
			}	
		} else if (t->type == TOK_PAREN_OPEN) {
			if (t == *tok) {
				/* Scaled or indirect addressing */
				ret->addr_mode = ADDR_INDIRECT;
			} else if (ret->item_types[0] != 0) {
				/*
				 * Displacement - $123(stuff)
				 */
				if (ret->item_types[0] != ITEM_NUMBER) {
					gas_errorfl(t, "Invalid "
						"displacement");	
					return NULL;
				}	
				ret->addr_mode = ADDR_DISPLACE;
				curitem = &ret->items[1];
				curitem_type = &ret->item_types[1];
				++index;
			} else {
				gas_errorfl(t, "Parse error at `%s' (%d)",
					t->ascii, __LINE__);	
				return NULL;
			}	
		} else if (t->type == TOK_PAREN_CLOSE) {
			if (ret->addr_mode != ADDR_INDIRECT
				&& ret->addr_mode != ADDR_DISPLACE
				&& ret->addr_mode != ADDR_SCALED
				&& ret->addr_mode != ADDR_SCALED_DISPLACE) {
				gas_errorfl(t, "Parse error at `%s' (%d)",
					t->ascii, __LINE__);	
				return NULL;
			}
			if (t->next) {
				if (t->next->type == TOK_OP_COMMA
					|| t->next->type == GAS_SEPARATOR) {
					t = t->next;
					break;
				} else {
					/* XXX wut to do?! */
				}	
			} else {
				t = t->next;
				break;
			}	
		} else if (t->type == TOK_OP_COMMA) {
			if (ret->addr_mode != ADDR_INDIRECT
				&& ret->addr_mode != ADDR_SCALED
				&& ret->addr_mode != ADDR_DISPLACE
				&& ret->addr_mode != ADDR_SCALED_DISPLACE) {
				/* ok, operand ends here */
				break;
			} else if (ret->addr_mode == ADDR_SCALED
				|| ret->addr_mode == ADDR_SCALED_DISPLACE) {
				int	max = ret->addr_mode == ADDR_SCALED?
					2: 3;
				if (index >= max) {
					gas_errorfl(t, "Too many operands"
						" for scaled addressing");
				} else {
					curitem = &ret->items[++index];
					curitem_type = &ret->item_types[index];
				}	
			} else {
				if (ret->addr_mode == ADDR_DISPLACE) {
					/* 0x123(foo,bar,baz) */
					ret->addr_mode = ADDR_SCALED_DISPLACE;
				} else {	
					ret->addr_mode = ADDR_SCALED;
				}	
				++index;
				curitem = &ret->items[index];
				curitem_type = &ret->item_types[index]; 
			}	
		} else if (t->type == GAS_SEPARATOR) {
			/* Instruction ends here */
			break;
		} else {
			gas_errorfl(t, "Parse error at `%s' (%d)",
				t->ascii, __LINE__);
			return NULL;
		}
	}
	*tok = t;
	return ret;
}

#if 0
static void
free_gas_operand(struct gas_operand *t) {
	(void)t;
}	
#endif


static void
store_plusminus_num(FILE *out, void *item) {
	struct gas_token	*nt;

	x_fputc(' ', out);
	nt = item;
	if (!nt->idata) {
		/* Not signed */
		x_fputc('+', out);
	} else {
		x_fputc('-', out);
	}	
	x_fputc(' ', out);

	print_item_nasm(out, item, ITEM_NUMBER, 0);
}

static void
print_operand_nasm(FILE *out, struct gas_operand *op, int postfix) {
	char	*p;
	int	i;

	if (op->addr_mode != 0 && postfix) {
		if (postfix == 'b') {
			p = "byte";
		} else if (postfix == 'w') {
			p = "word";
		} else if (postfix == 'l') {
			p = "dword";
		} else if (postfix == 'q') {
			p = "qword";
		} else if (postfix == 't') {
			p = "tword";
		} else {
			abort();
		}	
		x_fprintf(out, "%s ", p);
	}
		
	switch (op->addr_mode) {
	case 0:	
		print_item_nasm(out, op->items[0], op->item_types[0], postfix);
		break;
	case ADDR_INDIRECT:
		x_fputc('[', out);
		print_item_nasm(out, op->items[0], op->item_types[0], postfix);
		x_fputc(']', out);
		break;
	case ADDR_SCALED:
	case ADDR_SCALED_DISPLACE:	
		if (op->addr_mode == ADDR_SCALED) {
			i = 0;
		} else {
			i = 1;
		}

		x_fputc('[', out);
		print_item_nasm(out, op->items[i], op->item_types[i], postfix);
		x_fprintf(out, " + ");
		++i;
		print_item_nasm(out, op->items[i], op->item_types[i], postfix);

		++i;
		if (op->item_types[i] != 0) {
			x_fprintf(out, " * ");
			print_item_nasm(out, op->items[i],
				op->item_types[i], postfix);
		}	
		if (op->addr_mode == ADDR_SCALED_DISPLACE) {
			store_plusminus_num(out, op->items[0]);
		}	
		x_fputc(']', out);
		break;
	case ADDR_DISPLACE:	
		x_fputc('[', out);
		print_item_nasm(out, op->items[1], op->item_types[1], postfix);
		store_plusminus_num(out, op->items[0]);
		x_fputc(']', out);
		break;
	default:
		printf("UNKNOWN ADDRESSING MODE %d\n", op->addr_mode);
		abort();
	}
}

static void
print_operand_gas(FILE *out, struct gas_operand *op, int postfix) {
	int	i;

	switch (op->addr_mode) {
	case 0:	
		print_item_gas_x86(out, op->items[0], op->item_types[0], postfix);
		break;
	case ADDR_INDIRECT:
		x_fputc('(', out);
		print_item_gas_x86(out, op->items[0], op->item_types[0], postfix);
		x_fputc(')', out);
		break;
	case ADDR_SCALED:
	case ADDR_SCALED_DISPLACE:	
		if (op->addr_mode == ADDR_SCALED) {
			i = 0;
		} else {
			i = 1;
		}

		x_fputc('(', out);
		print_item_gas_x86(out, op->items[i], op->item_types[i], postfix);
		x_fprintf(out, ",");
		++i;
		print_item_gas_x86(out, op->items[i], op->item_types[i], postfix);

		++i;
		if (op->item_types[i] != 0) {
			x_fprintf(out, ",");
			print_item_gas_x86(out, op->items[i],
				op->item_types[i], postfix);
		}	
		if (op->addr_mode == ADDR_SCALED_DISPLACE) {
			store_plusminus_num(out, op->items[0]);
		}	
		x_fputc(')', out);
		break;
	case ADDR_DISPLACE:	
		store_plusminus_num(out, op->items[0]);
		x_fputc('(', out);
		print_item_gas_x86(out, op->items[1], op->item_types[1], postfix);
		x_fputc(')', out);
		break;
	default:
		printf("UNKNOWN ADDRESSING MODE %d\n", op->addr_mode);
		abort();
	}
}

static void
do_xlat_nasm(
	FILE *out,	
	/*s truct gas_token *instr_tok,*/ char *name,
	struct gas_operand *op,
	struct gas_operand *op2,
	struct gas_operand *op3,
	int postfix) {

	x_fprintf(out, "%s", name);
	if (op == NULL && postfix == 'l') {
		/* stosd, lodsd, etc */ 
		x_fputc('d', out);
	}	
	if (op != NULL) x_fputc(' ', out);
	if (op3 != NULL) {
		print_operand_nasm(out, op3, postfix);
		x_fprintf(out, ", ");
	}
	if (op2 != NULL) {
		print_operand_nasm(out, op2, postfix);
		x_fprintf(out, ", ");
	}
	if (op != NULL) {
		print_operand_nasm(out, op, postfix);
	}
}


static void
do_xlat_gas(
	FILE *out,	
	/*s truct gas_token *instr_tok,*/ char *name,
	struct gas_operand *op,
	struct gas_operand *op2,
	struct gas_operand *op3,
	int postfix) {
	
	x_fprintf(out, "%s", name);
	if (postfix) {
		x_fputc(postfix, out);
	}	
	if (op != NULL) x_fputc(' ', out);
	if (op != NULL) {
		print_operand_gas(out, op, postfix);
	}
	if (op2 != NULL) {
		x_fprintf(out, ", ");
		print_operand_gas(out, op2, postfix);
	}
	if (op3 != NULL) {
		x_fprintf(out, ", ");
		print_operand_gas(out, op3, postfix);
	}
}

/*
 * Returns instruction operand size (b for byte, w=word, l=longword,
 * q=qword, t=tword) or zero, in which case no size is specified.
 *
 * The operand size is sometimes encoded by the last character and
 * sometimes not. For example, ``int'' ends with t but doesn't take
 * a tword argument. If the return value is nonzero, the encoding
 * last character will be removed from the string.
 *
 * XXX This stuff is quite probably incorrect for a vast number of
 * instructions :-(
 */
static int
get_instr_postfix(char *instr, struct gas_operand *op) {
	char	*p;
	int	postfix = 0;

	p = strchr(instr, 0);
	--p;
	if (instr[0] == 'f') {
		/* Floating point */
		if (strchr("slt", *p) != NULL) {
			/* short (float), long (double) or tword (ldouble) */
			postfix = *p;
			*p = 0;
		}
	} else if (op != NULL) { /* XXX correct? */
		if (strchr("bwlq", *p) != NULL) {
			/* byte/word/longword(dword)/qword */
			if (strcmp(instr, "leal") == 0) {
				/*
				 * For some reason,
				 * lea eax, dword [eax]
				 * doesn't work in NASM, so let's
				 * omit the size
				 */
				;
			} else {
				postfix = *p;
			}
			*p = 0;
		}
	} else if (op == NULL && *p == 'l') {
		/*
		 * stosd, lodsd, etc are written as stosl, etc
		 */
		postfix = *p;
		*p = 0;
	}
	return postfix;
}

static void
append_instr(
	struct inline_instr **dest,
	struct inline_instr **dest_tail,
	struct inline_instr *instr) {

	if (*dest == NULL) {
		*dest = *dest_tail = instr;
	} else {
		(*dest_tail)->next = instr;
		*dest_tail = (*dest_tail)->next;
	}
}	

static struct inline_instr *
do_parse_asm(struct gas_token *tok, struct inline_asm_stmt *stmt) {
	struct gas_token		*t;
	struct gas_token		*nexttok;
	struct inline_instr		*ret = NULL;
	struct inline_instr		*ret_tail = NULL;
	struct inline_instr		*tmp;
	static struct inline_instr	nullinstr;

	for (t = tok; t != NULL;) {
		if (t->type == GAS_DOTIDENT) {
			/* Assembler directive */
			warningfl(asm_tok,
				"Ignoring assembler directive `%s'",
				t->ascii);
			do {
				t = t->next;
			} while (t && t->type != GAS_SEPARATOR);
		} else if (t->type == GAS_SEPARATOR) {
			;
		} else if ((t->type == GAS_IDENT
			|| GAS_NUMBER(t->type))
			&& t->next
			&& t->next->type == TOK_OP_AMB_COND2) {
			/* Label */
			tmp = n_xmalloc(sizeof *tmp);
			*tmp = nullinstr;
			tmp->type = INLINSTR_LABEL;
			if (t->type == GAS_IDENT) {
				tmp->name = t->data;
			} else {
				tmp->name
					= backend->get_inlineasm_label(t->data);
			}	
			t = t->next;
			append_instr(&ret, &ret_tail, tmp);
		} else if (t->type == GAS_IDENT) {
			/* Has to be instruction!? */
			struct gas_operand	*op = NULL;
			struct gas_operand	*op2 = NULL;
			struct gas_token	*t2;
			struct gas_token	*instr_tok = t; 

			t2 = t->next;
			if (t2
				&& t2->type != GAS_SEPARATOR	
				&& (op = parse_operand(&t2, stmt)) != NULL) {
				t = t2;
				if (t2 && t2->type == TOK_OP_COMMA) {
					if ((t2 = t2->next) &&
					(op2 = parse_operand(&t2, stmt))
						!= NULL) {
						t = t2;
					}
				}
			}
			if (gas_errors) {
				break;
			}	

			tmp = n_xmalloc(sizeof *tmp);
			*tmp = nullinstr;
			tmp->type = INLINSTR_REAL;
			tmp->name = instr_tok->data;
			tmp->postfix = get_instr_postfix(instr_tok->data, op);
			tmp->operands[0] = op;
			tmp->operands[1] = op2;
			tmp->operands[2] = NULL;
			append_instr(&ret, &ret_tail, tmp);
		} else {
			gas_errorfl(t, "Parse error at `%s' (%d)",
				t->ascii, __LINE__);
		}
		if (t == NULL) {
			break;
		}
		nexttok = t->next;
		/*free_gas_token(t);*/
		t = nexttok;
	}

	if (gas_errors) {
		free(ret);
		return NULL;
	}	
	return ret;
}


static void
inline_instr_to_gas_or_nasm(FILE *out, struct inline_instr *code, int to) {
	struct inline_instr	*tmp;

	for (tmp = code; tmp != NULL; tmp = tmp->next) {
		if (tmp->type == INLINSTR_REAL) {
			if (to == TO_NASM) {
				do_xlat_nasm(out, tmp->name, tmp->operands[0],
					tmp->operands[1], NULL, tmp->postfix);
			} else {
				do_xlat_gas(out, tmp->name, tmp->operands[0],
					tmp->operands[1], NULL, tmp->postfix);
			}	
		} else if (tmp->type == INLINSTR_LABEL) {
			emit->label(tmp->name, 0);
		} else if (tmp->type == INLINSTR_ASM) {
		}
		x_fputc('\n', out);
	}
}

void
inline_instr_to_nasm(FILE *out, struct inline_instr *code) {
	inline_instr_to_gas_or_nasm(out, code, TO_NASM);
}

void
inline_instr_to_gas(FILE *out, struct inline_instr *code) {
	inline_instr_to_gas_or_nasm(out, code, TO_GAS);
}

static struct inline_asm_io *
get_iolist(struct token **tok, int is_output, int *err, int *n_items,
	struct inline_asm_stmt *data) {

	struct token		*t;
	struct inline_asm_io	*ret = NULL;
	struct inline_asm_io	*ret_tail = NULL;
	struct expr		*ex;

	*n_items = 0;
	*err = 1;
	if (next_token(tok) != 0) {
		return NULL;
	}	
	for (t = *tok; t != NULL; t = t->next) {
		if (t->type == TOK_OPERATOR
			&& *(int *)t->data == TOK_OP_AMB_COND2) {
			/* Terminating : */
			break;
		} else if (t->type == TOK_PAREN_CLOSE) {
			/* Entire asm statement ends here */
			break;
		} else if (t->type == TOK_STRING_LITERAL) {
			struct inline_asm_io	*tmp;
			char			*constraints = t->ascii;

			if (is_output) {
				char	*p;

				if ((p = strchr(constraints, '=')) == NULL) {
					if (strchr(constraints, '+') == NULL) {
						errorfl(t, "Output operand "
						"constraints lack `='");
						return NULL;
					}
				} else if (constraints[0] != '=') {
					warningfl(t, "Output operand "
						"constraints don't begin "
						"with `='");
					memmove(p, p + 1, strlen(p));
				}
			}

			/*
			 * "constraints" (expr)
			 */
			if (expect_token(&t, TOK_PAREN_OPEN, 1) != 0) {
				return NULL;
			}
			ex = parse_expr(&t, TOK_PAREN_CLOSE, 0, 0, 1);
			if (ex == NULL) {
				return NULL;
			}
			tmp = n_xmalloc(sizeof *tmp);
			if (is_output && constraints[0] == '=') {
				tmp->constraints = constraints + 1;
			} else {
				tmp->constraints = constraints;
			}
			if (isdigit((unsigned char)*tmp->constraints)) {
				tmp->constraints = get_constraint_by_idx(
					data, *tmp->constraints - '0');
				if (tmp->constraints == NULL) {
					return NULL;
				}	
			}	
			tmp->expr = ex; /* XXX list order ok? */
			tmp->next = NULL;
			tmp->index = *n_items;
			if (ret == NULL) {
				ret = ret_tail = tmp;
			} else {
				ret_tail->next = tmp;
				ret_tail = tmp;
			}
			++*n_items;
			if (next_token(&t) != 0) {
				return NULL;
			}
			if (t->type == TOK_OPERATOR
				&& *(int *)t->data == TOK_OP_COMMA) {
				; 
			} else if (t->type == TOK_PAREN_CLOSE) {
				break;
			} else if (t->type == TOK_OPERATOR
				&& *(int *)t->data == TOK_OP_AMB_COND2) {
				break;
			} else {
				errorfl(t, "Parse error at `%s'", t->ascii);
				return NULL;
			}	
		} else {
			errorfl(t, "Parse error at `%s'", t->ascii);
			return NULL;
		}	
	}
	*err = 0;
	*tok = t;

	return ret;
}

static void
free_iolist(struct inline_asm_io *io) {
	struct inline_asm_io	*tmp;

	while (io != NULL) {
		tmp = io->next;
		free(io);
		io = tmp;
	}
}

static void
resolve_constraint_refs(struct inline_asm_stmt *stmt,
		struct inline_asm_io *iotmp,
		int is_output) {

	struct inline_asm_io	*mapped;

	if (iotmp->constraints[0] != '%' 
		|| !isdigit((unsigned char)iotmp->constraints[1])) {
		/* Nothing to do */
		return;
	}
	mapped = get_operand_by_idx(stmt, NULL,
		strtol(iotmp->constraints+1, NULL, 10),
		NULL);
	if (mapped == NULL) {
		errorfl(NULL, "Referenced constraint `%s' doesn't exist",
			iotmp->constraints);
		return;
	}
	iotmp->constraints = n_xmalloc(strlen(mapped->constraints + 2));
	if (is_output) {
		*iotmp->constraints = '=';
		strcpy(iotmp->constraints+1, mapped->constraints);
	} else {
		char	*p;
		char	*dest = iotmp->constraints;

		for (p = mapped->constraints; *p != 0; ++p) {
			if (*p != '=') {
				*dest++ = *p;
			}
		}
		*dest = 0;
	}
}

struct inline_asm_stmt *
parse_inline_asm(struct token **tok) {
	struct token			*t;
	struct inline_asm_stmt		*ret;
	static struct inline_asm_stmt	nullstmt;
	struct gas_token		*gt = NULL;
	static int			warned = 0;
	int				err;
	struct inline_asm_io		*iotmp;

#ifndef __FreeBSD__ /* FreeBSD uses inline asm heavily in the headers ... */
	if (!warned) {
		warningfl(*tok, "nwcc inline assembly support is new, "
			"incomplete and unproven");
		warned = 1;
	}
#endif

	gas_errors = 0;

	if ((*tok)->next && (*tok)->next->type == TOK_KEY_VOLATILE) {
		/*
		 * __asm__ __volatile__("...");
		 */
		if (next_token(tok) != 0) {
			return NULL;
		}
	}
	if (expect_token(tok, TOK_PAREN_OPEN, 1) != 0) {
		return NULL;
	}

	asm_tok = t = *tok;
	if (t->type != TOK_STRING_LITERAL) {
		errorfl(t, "Parse error at `%s' (%d)",
			t->ascii, __LINE__);
		return NULL;
	}	
	/*
	 * Syntax:
	 * "code" : output : input : clobbered
	 *        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^ optional
	 */

	ret = n_xmalloc(sizeof *ret);
	*ret = nullstmt;
	if (*t->ascii != 0) {
		if ((gt = tokenize_gas_code(t->ascii)) == NULL) {
			recover(&t, TOK_PAREN_CLOSE, 0);
			return NULL;
		}
	}
	
	if (next_token(&t) != 0) {
		return NULL;
	}
	err = 0;
	if (t->type == TOK_OPERATOR
		&& *(int *)t->data == TOK_OP_AMB_COND2) {
		int	n;

		ret->extended = 1;
		/* Output specified? */
		if ((ret->output = get_iolist(&t, 1, &err, &n, ret)) == NULL
			&& err) {
		} else if (t->type == TOK_OPERATOR
			&& *(int *)t->data == TOK_OP_AMB_COND2) {
			ret->n_outputs = n;
			/* Input specified? */
			if ((ret->input = get_iolist(&t, 0, &err, &n, ret)) == NULL
				&& err) {
				free_iolist(ret->output);
			} else {
				ret->n_inputs = n;
			}
		} else {
			ret->n_outputs = n;
		}	
		if (err == 0
			&& t->type == TOK_OPERATOR
			&& *(int *)t->data == TOK_OP_AMB_COND2) {
			/* Clobbered register list */
			for (t = t->next; t != NULL; t = t->next) {
				if (t->type == TOK_STRING_LITERAL) {
					struct clobbered_reg	*cr;

					cr = n_xmalloc(sizeof *cr);
					cr->reg = backend->
						name_to_reg(t->ascii);
					if (cr->reg == NULL
						&& strcmp(t->ascii, "memory")
						!= 0
						&& strcmp(t->ascii, "cc") != 0) {
						errorfl(t,
							"Unknown register `%s'",
							t->ascii);
						return NULL;
					}
					cr->next = ret->clobbered;
					ret->clobbered = cr;
					if ((t = t->next) == NULL) {
						break;
					} else if (t->type == TOK_OPERATOR
						&& *(int *)t->data
						== TOK_OP_COMMA) {
						; /* ok */
					} else if (t->type
						== TOK_PAREN_CLOSE) {
						break;
					}
				} else {
					errorfl(t, "Parse error at `%s'",
						t->ascii);
					return NULL;
				}
			}
			if (t == NULL) {
				errorfl(NULL, "Premature end of file");
				return NULL;
			}	
		}
	} else if (t->type == TOK_PAREN_CLOSE) {
		; /* done */

	}

	if (t->type != TOK_PAREN_CLOSE) {
		errorfl(t, "Parse error at `%s' (%d)",
			t->ascii, __LINE__);
		recover(&t, TOK_PAREN_CLOSE, 0);
		return NULL;
	}
	*tok = t;
	
	if (!err && gt != NULL) {
		ret->code = do_parse_asm(gt, ret);
	}
	/*free_gas_token_list(gt);*/
	if (err) {
		free(ret);
		return NULL;
	}	
	ret->toklist = gt;

	/*
	 * 07/11/09: Resolve references to other I/O constraints. E.g.
	 *
	 *    __asm__("..." : "=r" (x) : "%0" (y));
	 *
	 * ... will copy the %0 constraint "=r" for for the %1
	 * constraint (except it removes = because %1 is an input
	 * operand)
	 */
	for (iotmp = ret->input; iotmp != NULL; iotmp = iotmp->next) {
		resolve_constraint_refs(ret, iotmp, 0);
	}
	for (iotmp = ret->output; iotmp != NULL; iotmp = iotmp->next) {
		resolve_constraint_refs(ret, iotmp, 1);
	}

	return ret;
}


char *
parse_asm_varname(struct token **curtok) {
	char	*asmname;

	if (expect_token(curtok, TOK_PAREN_OPEN, 1) != 0) {
		return NULL;
	}	
	if ((*curtok)->type != TOK_STRING_LITERAL) {
		errorfl(*curtok, "Parse error at `%s'", (*curtok)->ascii);
		return NULL;
	}	
	asmname = (*curtok)->ascii;
	if (expect_token(curtok, TOK_PAREN_CLOSE, 1) != 0) {
		return NULL;
	}
	return asmname;
}	

