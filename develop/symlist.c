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
/*
 * This module contains a primitive function to build a linked list for
 * symbols. Because every scope has its own symbol ``table'', speeding
 * this stuff up, e.g. by using a skip list, would probably buy very
 * little, maybe even make it slower. Implementing a unified lookup
 * mechanism for all symbols might be a reasonable future goal though
 */

#include "symlist.h"
#include <string.h>
#include <stdlib.h>
#include "misc.h"
#include "decl.h"
#include "type.h"
#include "scope.h"
#include "zalloc.h"
#include "debug.h"
#include "token.h"
#include "n_libc.h"

#define HASH_SYM_ENTRY	1

static int
hash_symbol(const char *name, int tabsize) {
	int	key = 0;

	for (; *name != 0; ++name) {
		key = (33 * key + *name) & (tabsize - 1);
	}
	return key;
}	


void
new_make_hash_table(struct sym_hash_table *tab, int size) {
	int	nbytes = size * sizeof *tab->hash_slots_head;

#if FAST_SYMBOL_LOOKUP
	abort();
#endif
	tab->n_hash_slots = size;
	
	tab->hash_slots_head = n_xmalloc(nbytes);
	tab->hash_slots_tail = n_xmalloc(nbytes);
	memset(tab->hash_slots_head, 0, nbytes); 
	memset(tab->hash_slots_tail, 0, nbytes); 
	tab->used = 1;
}	


void
dump_hash_table(struct sym_hash_table *htab) {
	int	i;

	printf(" === Dumping symbol hash table %p === \n", htab);
	for (i = 0; i < htab->n_hash_slots; ++i) {
		if (htab->hash_slots_head[i] != NULL) {
			struct sym_entry	*se;

			printf("    Slot %d:\n", i);  
			for (se = htab->hash_slots_head[i];
				se != NULL;
				se = se->next) {
				printf("           %p, %p = %s (prev = %p)\n",
					se, se->dec, se->dec->dtype->name, se->prev);
			}
		}
	}
}

void
new_put_hash_table(struct sym_hash_table *htab, struct sym_entry *item) {
	int	key = hash_symbol(item->name, htab->n_hash_slots);
	
#if FAST_SYMBOL_LOOKUP
	abort();
#endif
	/*
	 * CANOFWORMS 03/27/08: This was missing the prev assignments!
	 * Seems too obvious to be missed, so maybe this breaks something?
	 * But then unlinking hadn't been used before so maybe that's why
	 */
	if (htab->hash_slots_head[key] == NULL) {
		htab->hash_slots_head[key] = item;
		htab->hash_slots_tail[key] = item;
		item->prev = NULL;
	} else {
		item->prev = htab->hash_slots_tail[key];
		htab->hash_slots_tail[key]->next = item;
		htab->hash_slots_tail[key] = item;
	}
}

struct sym_entry *
new_lookup_hash(struct sym_hash_table *htab, const char *name, size_t len) {
	int			key = hash_symbol(name, htab->n_hash_slots);
	struct sym_entry	*hp;

	(void) len;

#if FAST_SYMBOL_LOOKUP
	abort();
#endif

	for (hp = htab->hash_slots_head[key]; hp != NULL; hp = hp->next) {
#if 0
		if (hp->item->namelen == len) {
#endif
		/* 04/08/08: Shadow declarations */	
		if (hp->inactive
			|| (hp->dec->invalid && !is_shadow_decl(hp->dec))) {
			continue;
		}
		if (strcmp(hp->name, name) == 0) {
			return hp;
		}
	}
	return NULL;
}

struct sym_entry *
make_sym_entry(struct decl *dec) {
	struct sym_entry	*s;
	static struct sym_entry	nullentry;

	s = n_xmalloc(sizeof *s);
	*s = nullentry;
	s->name = dec->dtype->name;
	s->inactive = 0;
	if (s->name != NULL) {
		s->namelen = strlen(dec->dtype->name);
	} else {
		s->namelen = 0;
	}	
	s->dec = dec;
	s->next = NULL;
	s->prev = NULL;
	return s;
}	


void
append_symlist(
	struct scope *scope,	
	struct sym_entry **head,
	struct sym_entry **tail,
	struct decl *dec) {

	struct sym_entry	*s;

	s = make_sym_entry(dec);
	if (scope != NULL && scope->type != SCOPE_STRUCT) {
		/* XXX this stuff does not belong here! it should go to
		 * new_scope() or something
		 */
#if ! FAST_SYMBOL_LOOKUP
		if (!scope->sym_hash.used) {
			if (scope == &global_scope) {
#if 0
		scope->sym_hash = n_xmalloc(64 * sizeof *scope->sym_hash);
		scope->n_hash_slots = 63;
		memset(scope->sym_hash, 0, 64 * sizeof *scope->sym_hash);
#endif
				new_make_hash_table(&scope->sym_hash,
					SYM_HTAB_GLOBAL_SCOPE);
#if 0
			} else if (scope->parent == NULL
				|| scope->parent->parent == NULL) {	
				new_make_hash_table(&scope->sym_hash,
					SYM_HTAB_FUNC_TOP_BLOCK);
#endif
			}
		}
#endif
	}

#if FAST_SYMBOL_LOOKUP
	if (s->name != NULL) {
		put_fast_sym_hash(scope, s, 0);
	}
#endif
	if (scope && scope->sym_hash.used) {
#if FAST_SYMBOL_LOOKUP
		abort();
#endif
		if (s->name != NULL) {
			new_put_hash_table(&scope->sym_hash, s);
		}
	} else {
		if (*head == NULL) {
			*head = *tail = s;
		} else {
			(*tail)->next = s;
			s->prev = *tail;
			*tail = s;
		}
	}
}

void
remove_symlist(struct scope *s, struct sym_entry *se) {
	if (se->next == NULL) {
		/*
		 * 03/11/09: Removing tail. This missing assignment
		 * was probably not noticed because we use a hash
		 * table for the global scope
		 */
		s->slist_tail = se->prev;
	}

	if (se->prev != NULL && !s->sym_hash.used) {
		/* Symbol hash not used */
		se->prev->next = se->next;
	} else {
		/* Is first in list! */
		if (s->sym_hash.used) {
			int			key;
			struct sym_hash_table	*htab;

			htab = &s->sym_hash; 

			/*
			 * 03/27/08: XXX VERY dangerous!!! We always have
			 * to hash with se->name instead of se->dec->dtype->name
			 * since in case of static variables, se->name may
			 * be ``foo'' whereas the other is ``_Static_foo0''. If
			 * we change this, remember that the append_symlist()
			 * takes place when se->dec->dtype->name has not been
			 * updated with that name mangling stuff yet
			 */
			key = hash_symbol(se->name, 
				htab->n_hash_slots);
			if (se->prev != NULL) {
				se->prev->next = se->next;
			} else {
				/* Was first node */
				htab->hash_slots_head[key] = se->next;
			}

			if (se->next != NULL) {
				se->next->prev = se->prev;
			} else {
				/* Was last node */
				htab->hash_slots_tail[key] = se->prev;
			}
			return;
		} else {
			s->slist = se->next;
		}
	}

	if (se->next != NULL) {
		se->next->prev = se->prev;
	}
}

#if FAST_SYMBOL_LOOKUP

#define N_GLOBAL_FAST_TAB_ENTRIES	256
#define N_LOCAL_FAST_TAB_ENTRIES	64

static struct fast_sym_hash_entry	*global_fast_sym_hash_head[N_GLOBAL_FAST_TAB_ENTRIES];
static struct fast_sym_hash_entry	*global_fast_sym_hash_tail[N_GLOBAL_FAST_TAB_ENTRIES];
static struct fast_sym_hash_entry	*local_fast_sym_hash_head[N_LOCAL_FAST_TAB_ENTRIES];
static struct fast_sym_hash_entry	*local_fast_sym_hash_tail[N_LOCAL_FAST_TAB_ENTRIES];


static unsigned
hash_fast_sym_name_base(const char *name, int *len) {
	const char	*origname = name;
	unsigned	key = 0;

	for (; *name != 0; ++name) {
		key = key * 33 + *name;
	}
	*len = name - origname;
	return key;
}



void
put_fast_sym_hash(struct scope *s, struct sym_entry *se, int is_typedef) {
	struct fast_sym_hash_entry	*ent;
	unsigned			 idx;
	int				namelen;

	if (s == &global_scope) {
		/* Global identifier - persistent */
		ent = n_xmalloc(sizeof *ent);
		idx = hash_fast_sym_name_base(se->dec->dtype->name, &namelen);
		idx &= (N_GLOBAL_FAST_TAB_ENTRIES - 1);
	} else {
		/* Local identifier - can be cleared at end of function */
		ent = zalloc_buf(Z_FASTSYMHASH);
		idx = hash_fast_sym_name_base(se->dec->dtype->name, &namelen);
		idx &= (N_LOCAL_FAST_TAB_ENTRIES - 1);
	}

	ent->se = se;
	ent->name = se->dec->dtype->name;
	ent->namelen = namelen;
	ent->scope = s;
	ent->is_typedef = is_typedef;
	ent->next = NULL;


	/*
	 * XXX Currently this will get called twice for extern symbols - once for
	 * the per-scope list, and once for the global extern list. Do we really
	 * want that? Possibly we do if either of them is invalidated due to a
	 * redeclaration so it may be better to keep both around
	 */
	if (s == &global_scope) {
		if (global_fast_sym_hash_head[idx] == NULL) {
			global_fast_sym_hash_head[idx]
				= global_fast_sym_hash_tail[idx]
				= ent;
		} else {
			global_fast_sym_hash_tail[idx]->next = ent;
			global_fast_sym_hash_tail[idx] = ent;
		}
	} else {
		if (local_fast_sym_hash_head[idx] == NULL) {
			local_fast_sym_hash_head[idx]
				= local_fast_sym_hash_tail[idx]
				= ent;
		} else {
			local_fast_sym_hash_tail[idx]->next = ent;
			local_fast_sym_hash_tail[idx] = ent;
		}
	}
}

void
reset_fast_sym_hash(void) {
	memset(local_fast_sym_hash_head, 0, sizeof local_fast_sym_hash_head);
	memset(local_fast_sym_hash_tail, 0, sizeof local_fast_sym_hash_tail);
}


static int
is_valid_match(struct sym_entry *se, int is_typedef, int want_typedef, int *err) {
	if (err != NULL) {
		*err = 0;
	}
	if (se->dec->invalid && !is_shadow_decl(se->dec)) {
		return 0;
	}
	if (se->inactive) {
		return 0;
	}

	if (want_typedef != is_typedef) {
		/*
		 * We got a typedef but didn't
		 * want it or the other way
		 * around
		 */
		if (err != NULL) {
			*err = 0;
		}
		return 0;
	}
	return 1;
}


struct sym_entry *
fast_lookup_symbol_se(struct scope *s, const char *name, int nested, int want_typedef) {
	unsigned			idx;
	unsigned			local_idx;
	unsigned			global_idx;
	int				len;
	int				err;
	struct scope			*tempscope;
	struct fast_sym_hash_entry	*ent;

	local_idx = global_idx = hash_fast_sym_name_base(name, &len);
	local_idx &= (N_LOCAL_FAST_TAB_ENTRIES - 1);
	global_idx &= (N_GLOBAL_FAST_TAB_ENTRIES - 1);

	/*
	 * Try local list first - if we are looking up in a local scope
	 */
	if (s != &global_scope && local_fast_sym_hash_head[local_idx] != NULL) {
		struct fast_sym_hash_entry	*matches[128];
		int				matchidx = 0;

		/*
		 * First do a linear search for the current scope and
		 * record all matches of the same name. If we find a
		 * match in the current scope, we are done. If we
		 * don't, we only have to traverse the recorded
		 * matches for all parent scopes in order to find the
		 * first match
		 */
		for (ent = local_fast_sym_hash_head[local_idx];
			ent != NULL;
			ent = ent->next) {
			if (ent->namelen == len
				&& strcmp(ent->name, name) == 0) {
				struct sym_entry	*se = ent->se;

				if (ent->scope == s) {
					/* Perfect match? */
					if (!is_valid_match(se, 
						ent->is_typedef,
						want_typedef,
						NULL)) {
						return NULL;
					}

					/* Yes! */
					return se;
				} else {
					/* Match in other scope */
					matches[matchidx++] = ent;
				}
			}
		}

		if (nested == 0) {
			/*
			 * We are only interested in a match for the
			 * current scope, and there's none
			 */
			return NULL;
		} else if (matchidx > 0) {
			/*
			 * There are matches for parent scopes
			 */
			int	i;

			while ((s = s->parent) != NULL /* XXX needed?!? */
				&& s != &global_scope) {
				for (i = 0; i < matchidx; ++i) {
					if (matches[i]->scope == s) {
						if (!is_valid_match(
							matches[i]->se,
							matches[i]->is_typedef,
							want_typedef,
							NULL)) {
							return NULL;
						} else {
							return matches[i]->se;
						}
					}
				}
			}
		}
	}
	if (s != &global_scope && nested == 0) {
		/*
		 * We are only interested in a match for the
		 * current scope, and there's none
		 */
		return NULL;
	}

	/* No match in local scopes - fall back to global */
	if (global_fast_sym_hash_head[global_idx] != NULL) {
		for (ent = global_fast_sym_hash_head[global_idx];
			ent != NULL;
			ent = ent->next) {
			if (ent->namelen == len
				&& strcmp(ent->name, name) == 0) {
				struct sym_entry	*se = ent->se;

				if (!is_valid_match(se,
					ent->is_typedef,
					want_typedef,
					&err)) {
					if (!err) {
						/*
						 * This may be an invalidated
						 * declaration, i.e. overridden
						 * by a subsequent tentative
						 * declaration. So continue
						 * searching
						 */
						continue;
					}
				}
				return se;
			}
		}
	}
	return NULL;
}

#endif

#if 0
struct sym_entry *
lookup_hash(struct hash_node **tab, size_t nslots,
	const char *name, size_t len) {

	struct hash_node        *hn;
	struct sym_entry        *se;

	if (len >= nslots) {
		hn = tab[nslots];
	} else {
		hn = tab[len - 1];
	}
	
	if (hn == NULL) {
		return NULL;
	}
	for (;;) {
		se = hn->item;
		if (se->name[0] != name[0]) {
			hn = hn->skip;
		} else if (strcmp(se->name, name) != 0) {
			hn = hn->next;
		} else {
			/* Found */
			return se;
		}
		if (hn == NULL) {
			break;
		}
	}
	/* NOTREACHED */
	return NULL;
}
#endif

