#ifndef MACROS_H
#define MACROS_H

#include <stddef.h>

struct macro_arg {
	char			*name;
	struct token		*toklist;
	struct macro_arg	*next;
};

struct macro {
	char			*name;
	size_t			namelen;
	int			empty;
	int			functionlike;
	int			dontexpand;
	void			*builtin;
	struct macro_arg	*trailing_last;
	struct macro_arg	*arglist;
	struct token		*toklist;
	struct macro		*next;
};

struct predef_macro;

struct macro	*alloc_macro();
struct predef_macro *tmp_pre_macro(const char *name, const char *text);
struct macro	*lookup_macro(const char *name, size_t len, int key);
struct macro	*put_macro(struct macro *m, int slen, int key);
int		drop_macro(const char *name, int slen, int key);
struct token	*builtin_to_tok(struct macro *mp, struct token **tail);
struct token	*skip_ws(struct token *); /* XXX token.c ... */


/*
 * N_HASHLISTS defines the number of hash table slots for macro
 * definitions. It must be a power of two to avoid having to perform
 * expensive MOD (division)!
 *
 * Instead of % N_HASHLISTS, use & N_HASHLIST_MOD
 */
#define N_HASHLISTS	1024
#define N_HASHLIST_MOD	(N_HASHLISTS - 1)

#endif

