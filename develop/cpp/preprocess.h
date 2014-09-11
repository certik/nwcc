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
#ifndef LEX_H
#define LEX_H

#include <stdio.h>

struct token;
struct pp_directive;

extern struct token	*toklist;
extern int		g_ignore_text;

struct include_file {
	char			*name;
	size_t			namelen;
	void			*startp;
	int			has_guard;
	int			fully_guarded;
	struct token		*toklist;
	struct pp_directive	*start_dir;
	struct pp_directive	*end_dir;
	size_t			start_guard; /* only valid if has_guard set */
	size_t			end_guard;
	struct include_file	*next;
};

struct include_dir {
	char			*path;
	struct include_dir	*next;
	struct include_file	*inc_files;
	struct include_file	*inc_files_tail;
};

struct predef_macro {
	char			*name;
	char			*text;
	struct predef_macro	*next;
};

struct input_file {
	char			*path;
	int			is_header;
	int			is_cmdline;
	FILE			*fd;
	
	char			*filemap;
	char			*filep;
	char			*filemapend;
	size_t			filesize;

	int			unread_chars[10];
	int			unread_idx;

	struct input_file	*next;
};	

extern struct include_dir	*include_dirs;

int	open_input_file(struct input_file *, const char *, int silent);
int preprocess(struct input_file *file, FILE *out);
int get_next_char(struct input_file *fd);
int unget_char(int ch, struct input_file *fd);

#endif

