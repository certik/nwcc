/*
 * Copyright (c) 2003 - 2006, Nils R. Weller
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
#ifndef ERROR_H
#define ERROR_H

/* Set file name that will reported by lexerror() and lexwarning() */
void err_setfile(char *f);

/* Set address of line counter that will be read by exerror() and lexwarning() */
void err_setline(int *l);

/* 
 * Report error message with file name and line number (file:line: Error: msg)
 */
void lexerror(char *fmt, ...);

void err_setlineptr(char *l);

/*
 * Same as error, but also lets caller specify file name and line
  * number without having to call err_setfile()/err_setline()
 */
struct token;
void errorfl(struct token *, const char *fmt, ...);

struct token;
struct token *errorfl_mk_tok(const char *, int, char *);

/*
 * Report warning with file name and line number (file:line: Warning: msg)
 */
void lexwarning(const char *fmt, ...);

/*
 * Same as error, but also lets caller speciy file name and line number
 * without having to call err_setfile()/line()
 */
void warningfl(struct token *t, const char *fmt, ...);

void
print_source_line(const char*linep, const char*tokp, const char*tok, int as);

/*
 * Number of errors encountered so far
 */
extern int errors;

/*
 * Number of warnings encountered so far
 */
extern int warnings;

#endif /* #ifndef ERROR_H */
