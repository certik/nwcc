/*
 * Copyright (c) 2014, Nils R. Weller
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
 * Lexical analyzer (using ucpp)
 */
#include "lex.h"
#include "lex_ucpp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "token.h"
#include "defs.h"

#if 0
#include "numlimits.h"
#include "misc.h"
#include "debug.h"
#include "dwarf.h"
#include "n_libc.h"
#endif
#include "cc1_main.h"
#include "backend.h" /* 07/25/09: For backend->get_wchar_t() */
#include "zalloc.h"
#include "type.h"
#include "error.h"
#include "fcatalog.h"

#if !USE_UCPP

int lex_ucpp(struct input_file *in) { return 1; }

#else

#include "cpp.h"

static void
initialize_ucpp(struct lexer_state *ls, FILE *in) {
	int	i;
	/*
	 * This code is an adaption of ucpp's sample.c
	 */

	/* step 1 */
	init_cpp();

	/* step 2 */
	no_special_macros = 0;
	emit_defines = emit_assertions = 0;

	/* step 3 -- with assertions */
	init_tables(1);

	/* step 4 -- no default include path */
	init_include_path(0);

	/* step 5 -- no need to reset the two emit_* variables set in 2 */
	emit_dependencies = 0;

	/* step 6 -- we work with stdin, this is not a real filename */
	set_init_filename("[stdin]", 0);

	/* step 7 -- we make sure that assertions are on, and pragma are
	   handled */
	init_lexer_state(ls);
	init_lexer_mode(ls);
	ls->flags |= HANDLE_ASSERTIONS | HANDLE_PRAGMA | LINE_NUM;

	/* step 8 -- input is from specified FILE stream */
	ls->input = in;

	/* step 9 -- we do not have any macro to define, but we add any
	  argument as an include path */
/*	for (i = 1; i < argc; i ++) add_incpath(argv[i]);*/
	add_incpath("/usr/local/nwcc/include");  /* XXXXXXXXXXXXXXXX */
	add_incpath("/usr/include");  /* XXXXXXXXXXXXXXXX */
	for (i = 0; cpp_args[i] != NULL; ++i ){
		if (strncmp(cpp_args[i], "-I", 2) == 0) {
			add_incpath(cpp_args[i]+2);
		} else if (strncmp(cpp_args[i], "-D", 2) == 0) {
			define_macro(ls, cpp_args[i]+2);
		} else if (strncmp(cpp_args[i], "-U", 2) == 0) {
			undef_macro(ls, cpp_args[i]+2);
		}
	}

	/* step 10 -- we are a lexer and we want CONTEXT tokens */
	enter_file(ls, ls->flags);
}


static void
process_ucpp_token(struct ucpp_token *tok) {
	int           			*dummyptr       = n_xmalloc(sizeof *dummyptr);
	static struct input_file	infile;

#if 0
        NONE,           /* whitespace */
        NEWLINE,        /* newline */
        COMMENT,        /* comment */
        BUNCH,          /* non-C characters */
        PRAGMA,         /* a #pragma directive */
        CONTEXT,        /* new file or #line */


        SHARP,          /*      #       */
        DSHARP,         /*      ##      */

        OPT_NONE,       /* optional space to separate tokens in text output */

        DIGRAPH_TOKENS,                 /* there begin digraph tokens */

        /* for DIG_*, do not change order, unless checking undig() in cpp.c */
        DIG_LBRK,       /*      <:      */
        DIG_RBRK,       /*      :>      */
        DIG_LBRA,       /*      <%      */
        DIG_RBRA,       /*      %>      */
        DIG_SHARP,      /*      %:      */
        DIG_DSHARP,     /*      %:%:    */

        DIGRAPH_TOKENS_END,             /* digraph tokens end here */

        LAST_MEANINGFUL_TOKEN,          /* reserved words will go there */

        MACROARG,       /* special token for representing macro arguments */
#endif

        switch (tok->type) {
	case LPAR:           /*      (       */
        case RPAR:           /*      )       */
		store_token(&toklist, dummyptr,
			tok->type == LPAR? TOK_PAREN_OPEN :
				TOK_PAREN_CLOSE, lineno, NULL);
		break;
        case LBRA:           /*      {       */
        case RBRA:           /*      }       */
		store_token(&toklist, dummyptr,
			tok->type == LBRA? TOK_COMP_OPEN: TOK_COMP_CLOSE,
			lineno, NULL);
		break;
        case LBRK:           /*      [       */
        case RBRK:           /*      ]       */
		store_token(&toklist, dummyptr,
			tok->type == LBRK? TOK_ARRAY_OPEN : TOK_ARRAY_CLOSE,
			lineno, NULL);
		break;
	case SEMIC:          /*      ;       */
		store_token(&toklist, dummyptr, TOK_SEMICOLON, lineno, NULL);
		break;
	case STRING: {
		struct ty_string        *tmpstr;
		char			*str_value = tok->name;
		int			is_wide_char = 0;
	
		if (*str_value == 'L') {
			/* Wide character string */
			++str_value;
			is_wide_char = 1;
		}

		set_input_file_buffer(&infile, str_value+1); /* +1 to skip opening " */
		
		tmpstr = get_string_literal(&infile, is_wide_char);  /* not wide char?? */
		store_token(&toklist, tmpstr,
			TOK_STRING_LITERAL, lineno, NULL); 
		break;
		}
        case NAME: {           /* identifier */
		char	*ident;

		set_input_file_buffer(&infile, tok->name+1);
		ident = get_identifier(*tok->name, &infile);
		if (ident != NULL) {
			store_token(&toklist, ident,
				TOK_IDENTIFIER, lineno, n_xstrdup(tok->name));
		}
		break;
		}
	case CHAR: {
		int	err = 0;
		int	is_wide_char = 0;
		char	*str_value = tok->name;
		int	tmpi;

		if (*str_value == 'L') {
			/* Wide character string */
			++str_value;
			is_wide_char = 1;
		}

		set_input_file_buffer(&infile, str_value+1);
		tmpi = get_char_literal(&infile, &err); /* XXX cross-comp */
		if (!err) {
			int	char_type;

			/*
			 * Character literals are really treated
			 * like integer constants
			 */
			int	*tmpip = zalloc_buf(Z_CEXPR_BUF); /*n_xmalloc(16);*/ /* XXX */
			if (tmpip == NULL) {
				perror("malloc");
				exit(EXIT_FAILURE);
			}
			*tmpip = tmpi;

			if (is_wide_char) {
				char_type = backend->get_wchar_t()->code;

				/*
				 * The assignment above assumes int,
				 * i.e. 32bit on all supported
				 * platforms
				 */
				assert(backend->get_sizeof_type(
					backend->get_wchar_t(), NULL) == 4);
			} else {
				char_type = TY_INT;
			}
			store_token(&toklist, tmpip, char_type, lineno, n_xstrdup(tok->name));
		}
		is_wide_char = 0;
		break;
		}
	case NUMBER: {
		struct num	*n;

		set_input_file_buffer(&infile, tok->name+1);
		n = get_num_literal(*tok->name, &infile);
		if (n != NULL) {
			store_token(&toklist, n->value,
				n->type, lineno, n_xstrdup(tok->name));
		} else {
			lexerror("Couldn't read numeric literal");
		}
		break;
		}
	case MDOTS: {
		int	*tmp = n_xmalloc(sizeof *tmp);
		*tmp = 0; 
		store_token(&toklist, tmp, TOK_ELLIPSIS, lineno, NULL);
		break;
		}
	case SLASH:          /*      /       */
	case ASSLASH:        /*      /=      */
	case MINUS:          /*      -       */
	case MMINUS:         /*      --      */
	case ASMINUS:        /*      -=      */
	case ARROW:          /*      ->      */
	case PLUS:           /*      +       */
	case PPLUS:          /*      ++      */
	case ASPLUS:         /*      +=      */
	case LT:             /*      <       */
	case LEQ:            /*      <=      */
	case LSH:            /*      <<      */
	case ASLSH:          /*      <<=     */
	case GT:             /*      >       */
	case GEQ:            /*      >=      */
	case RSH:            /*      >>      */
	case ASRSH:          /*      >>=     */
	case ASGN:           /*      =       */
	case SAME:           /*      ==      */
#ifdef CAST_OP
	case CAST:           /*      =>      */
#endif
	case NOT:            /*      ~       */
	case NEQ:            /*      !=      */
	case AND:            /*      &       */
	case LAND:           /*      &&      */
	case ASAND:          /*      &=      */
	case OR:             /*      |       */
	case LOR:            /*      ||      */
	case ASOR:           /*      |=      */
	case PCT:            /*      %       */
	case ASPCT:          /*      %=      */
	case STAR:           /*      *       */
	case ASSTAR:         /*      *=      */
	case CIRC:           /*      ^       */
	case ASCIRC:         /*      ^=      */
	case LNOT:           /*      !       */
	case COMMA:          /*      ,       */
	case QUEST:          /*      ?       */
	case COLON:          /*      :       */
	case DOT:            /*      .       */
        case UPLUS: /* unary + */
        case UMINUS:          /* unary - */
		{
		int	*opval = n_xmalloc(sizeof *opval);
		char	*optext = operators_name[tok->type];

		set_input_file_buffer(&infile, optext+1);
		*opval = get_operator(*optext, &infile, &optext); /* XXX cross-comp */
		if (*opval == -1) {
			lexerror("Invalid operator `%s'", optext);
			free(opval);
		} else {
			store_token(&toklist, opval, TOK_OPERATOR, lineno, tok->name);
		}
		break;
		}
	default:
		printf("Unhandled token, type %d, value %s\n", tok->type, tok->name? tok->name: "?");
		break;
	}

	return NULL;
}

int
lex_ucpp(struct input_file *in) {
	int				r;
	char				*curfile = NULL;
	static struct lexer_state	ls;

	initialize_ucpp(&ls, in->fd);


        /* read tokens until end-of-input is reached -- errors (non-zero
           return values different from CPPERR_EOF) are ignored */
        while ((r = lex(&ls)) < CPPERR_EOF) {
                if (r) {
                        /* error condition -- no token was retrieved */
                        continue;
                }
                /* we print each token: its numerical value, and its
                   string content; if this is a PRAGMA token, the
                   string content is in fact a compressed token list,
                   that we uncompress and print. */
                if (ls.ctok->type == PRAGMA) {
			/* Pragmas are ignored for now */
			continue;

			/*
                        unsigned char *c = (unsigned char *)(ls.ctok->name);

                        printf("line %ld: <#pragma>\n", ls.line);
                        for (; *c; c ++) {
                                int t = *c;

                                if (STRING_TOKEN(t)) {
                                        printf("  <%2d>  ", t);
                                        for (c ++; *c != PRAGMA_TOKEN_END;
                                                c ++) putchar(*c);
                                        putchar('\n');
                                } else {
                                        printf("  <%2d>  `%s'\n", t,
                                                operators_name[t]);
                                }
                        }
			*/
                } else if (ls.ctok->type == CONTEXT) {
#if 0
                        printf("new context: file '%s', line %ld\n",
                                ls.ctok->name, ls.ctok->line);
#endif


			/*
			 * Note that curfile is still referred to by tokens -
			 * don't free
			 */
			curfile = strdup(ls.ctok->name);
			if (*curfile
				&& curfile[strlen(curfile) - 1] == 'c') {
				cur_inc = NULL;
				cur_inc_is_std = 0;
			}       
                     
			/* Processing new file */
			err_setfile(curfile);
			token_setfile(curfile);
			lineno = ls.ctok->line;

                } else if (ls.ctok->type == NEWLINE) {
#if 0
                        printf("[newline]\n");
#endif
			++lineno;
                } else {
#if 0
                        printf("line %ld: <%2d>  `%s'\n", ls.ctok->line,
                                ls.ctok->type,
                                STRING_TOKEN(ls.ctok->type) ? ls.ctok->name
                                : operators_name[ls.ctok->type]);
#endif

			process_ucpp_token(ls.ctok);
                }
        }

        /* give back memory and exit */
        wipeout();
        free_lexer_state(&ls);


#if 0
			if (ch == 'i' && strncmp(buf, "dent", 4) == 0) {
				/*
				 * 08/12/08: Accept but ignore (even correctness
				 * of) #ident directives
				 */
				continue;
			}
#endif
	/*print_token_list(toklist);*/
	return errors;
}

#endif /* USE_UCPP */

