#include "zalloc.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "token.h"
#include "expr.h"
#include "n_libc.h"

struct zone {
	void		*base;
	int		initialized;
	int		needzero;
	int		usemalloc;
	size_t		n_alloc;
	size_t		free_chunks;
	size_t		orig_free_chunks;
	size_t		chunk_size;
	void		*curptr;
	struct zone	*next;
};

/*
 * 03/10/09: zones_head points to an array of all zones for
 * all types.
 *
 * zones_free points to the current zone block of the
 * corresponding type. Initially it points to zones_head[t],
 * but after the first block of the type has filled up and
 * a new block is linked to the list, that block will be the
 * ``free'' block
 *
 * (zones_tail is the tail of the full list)
 */
static struct zone	*zones_head;
static struct zone	**zones_free;
static struct zone	**zones_tail;
static void		*free_list[Z_MAX_ZONES][128];
static int		free_list_idx[Z_MAX_ZONES];

static long	page_size;

void
zalloc_create(void) {
	int	i;

	page_size = sysconf(_SC_PAGESIZE);

	/*
	 * For now, every type gets one page to start with, or,
	 * if the page size is too large, 4096 bytes
	 */
	if (page_size < 0 || page_size > 4096) {
		page_size =4096;
	}

	zones_head = n_xmalloc(Z_MAX_ZONES * sizeof *zones_head);
	zones_free = n_xmalloc(Z_MAX_ZONES * sizeof *zones_free);
	zones_tail = n_xmalloc(Z_MAX_ZONES * sizeof *zones_tail);
	for (i = 0; i < Z_MAX_ZONES; ++i) {
		zones_head[i].initialized = 0;
		zones_tail[i] = &zones_head[i];
		zones_free[i] = &zones_head[i];
	}
}

void
zalloc_init(int type, int size, int needzero, int usemalloc) {
	assert(type >= 0 && type < Z_MAX_ZONES);
	zones_head[type].initialized = 1;
	zones_head[type].base = n_xmalloc(page_size);
	memset(zones_head[type].base, 0, page_size);
	zones_head[type].n_alloc = page_size;
	zones_head[type].free_chunks = page_size / size;
	zones_head[type].orig_free_chunks = zones_head[type].free_chunks;
	zones_head[type].chunk_size = size;
	zones_head[type].needzero = needzero;
	zones_head[type].usemalloc = usemalloc;
	zones_head[type].curptr = zones_head[type].base;
	zones_head[type].next = NULL;
}




static void
reset_zone(struct zone *z) {

	if (z->needzero) {
		size_t	count = (char *)z->curptr - (char *)z->base;

		if (count > 0) {
			memset(z->base, 0, count);
		}	
	}
	z->curptr = z->base;
	z->free_chunks = z->orig_free_chunks;
}

static char	identbuf[2048];
static char	*identp;
static size_t	identsize = sizeof identbuf;
static size_t	identbuf_curpos;

static void *
zalloc_from_freelist(int type);


char *
zalloc_identbuf(size_t *nbytes) {
	if (identsize - identbuf_curpos < 100) {
		identsize *= 2;
		identp = n_xrealloc(identp, identsize);
	}
	*nbytes = identsize - identbuf_curpos - 5;
	return identp? identp+identbuf_curpos: identbuf+identbuf_curpos; 
}

void
zalloc_update_identbuf(size_t nbytes) {
	identbuf_curpos += nbytes;
}

void
zalloc_reset_identbuf(void) {
	identbuf_curpos = 0;
}	

static void
reset_all_zones(struct zone *z) {
	for (; z != NULL; z = z->next) {
		reset_zone(z);
	}
}

static int malloc_override;

/*
 * 03/01/09: The new malloc override flag allows the caller to specify
 * that malloc() should always be used for all data structures regardless
 * of whether they are usually put into a zone. This is needed for inline
 * functions, where all data structures are not resettable but must be
 * kept around until the end of the program
 */
void
zalloc_enable_malloc_override(void) {
	malloc_override = 1;
}

void
zalloc_disable_malloc_override(void) {
	malloc_override = 0;
}


/*
 * This is a simplistic zone allocator. It is used when nwasm does
 * not perform any code analysis/optimization, as is currently
 * always the case.
 *
 * All buffers allocated here are freed and reused when the next
 * instruction is parsed and encoded
 */
void *
zalloc_buf(int type) {
	struct zone	*z = /*zones_head*/ zones_free /*tail*/[type];
	void		*ret;

#define DUMP_STATS 0 
#if DUMP_STATS 
	static unsigned long	stats[Z_MAX_ZONES];
	static unsigned long	freelist_success[Z_MAX_ZONES];
	int			i;
	static unsigned long	callno;

#endif



	if (z->usemalloc || malloc_override) {
		void	*ret = n_xmalloc(z->chunk_size);
		memset(ret, 0, z->chunk_size);

#define DUMP_INITIALIZERS 0 
#if DUMP_INITIALIZERS
		static unsigned long	bytes;
		static unsigned long 	call;
		if (type == Z_CEXPR_BUF /*Z_INITIALIZER*/) {
			bytes += z->chunk_size;
			++call;
			if (call % 100 == 0) {
				printf("%lu\r", bytes);
				fflush(stdout);
			}
		}
#endif
		return ret;
	}	

	if ((ret = zalloc_from_freelist(type)) != NULL) {
#if DUMP_STATS
		freelist_success[type] += z->chunk_size;
#endif
		return ret;
	}

#if DUMP_STATS
	stats[type] += z->chunk_size;

	++callno;

	if (callno % 100 == 0) {
		for (i = 0; i < Z_MAX_ZONES; ++i) {
			printf("%d: %lu  (%lu)\n", i, stats[i], freelist_success[i]);
		}
	}
#endif

#if 0
	assert(z != NULL && z->initialized && type > 0 && type < Z_MAX_ZONES);
#endif
	if (z->free_chunks == 0) {
		/* There may be other free zones after this one */
		while (z->next != NULL && z->next->free_chunks == 0) {
			z = z->next;
		}

		if (z->next && z->next->free_chunks > 0) {
			/* There's another free zone after this one */
			z = z->next;

			/*
			 * 03/30/08: New... Avoid iteration between
			 * start of zone and next free zone
			 */
			zones_free[type] = z;
		} else {
			/*
			 * We have to allocate more storage! Note that it is
			 * not possible to realloc() the base block because
			 * then all previously returned pointers into it
			 * become invalid
			 */
			struct zone	*newz = n_xmalloc(sizeof *newz);
	
			newz->n_alloc = z->n_alloc; 
			newz->base = n_xmalloc(newz->n_alloc);
			memset(newz->base, 0, newz->n_alloc);
			zones_tail[type]->next = newz;
			zones_tail[type] = newz;
			newz->curptr = newz->base;
			newz->chunk_size = z->chunk_size;
			newz->free_chunks = newz->n_alloc / newz->chunk_size;
			newz->orig_free_chunks = newz->free_chunks;
			newz->initialized = 1;
			newz->next = NULL;
			
			/*
			 * 03/27/08: This was missing!!!! The needzero flag
			 * must be set for every zone structure
			 */
			newz->needzero = z->needzero;
			newz->usemalloc = z->usemalloc;
			z->next = newz;
			z = newz;
		}	
	}
	ret = z->curptr;
	z->curptr = (char *)z->curptr + z->chunk_size;
	--z->free_chunks;

	return ret;
}

void
zalloc_reset(void) {
	int	i;

	zalloc_reset_identbuf();
	for (i = 0; i < Z_MAX_ZONES; ++i) {
		if (!zones_head[i].initialized) {
			continue;
		}	
#if 0
		/* 03/30/08: Totally bogus?!!?? */
		zones_tail[i] = &zones_head[i];
#endif

#if 0
		reset_zone(&zones_head[i]);
#endif
		reset_all_zones(&zones_head[i]);
		zones_free[i] = &zones_head[i];

		/* 08/13/09: Reset freelists too */
		free_list_idx[i] = 0;
	}
}

void
zalloc_reset_except(int ex) {
	int	initialized = zones_head[ex].initialized;

	zones_head[ex].initialized = 0;
	zalloc_reset();
	zones_head[ex].initialized = initialized;
}


/*
 * 08/12/09: The zone allocator is too coarse! It is always reset on an all
 * or nothing basis, and very rarely at that.
 *
 * Ruby has a giant 100k lines initialized struct file, and other systems
 * like QT (which is however C++) do too. This is way too large to process
 * between two zone allocator resets, so we need a way to add items that
 * aren't used anymore to a freelist.
 *
 * In Ruby's case, the target is constant expressions;
 *
 *  struct foo bar = {
 *    1+2+3,  1+2+3, ...
 * };
 *
 * Here we can save the sub-expressions 1, 2 and 3 after having computed a
 * constant result expression
 */
void
zalloc_free(void *buf, int type) {
	struct zone	*z = zones_free[type];

	if (z->usemalloc) {
		(void) fprintf(stderr, "BUG: zalloc_free() called for type "
			"that is allocated using malloc()\n");
		abort();
	}
	if (malloc_override) {
		/* OK - we always use malloc(). Freelist just isn't used */
		return;
	}

	if (free_list_idx[type] >=
		(int)(sizeof free_list[type] / sizeof free_list[type][0])) {
		/* Free list ``full'' - ignore (XXX maybe change this) */
		return;
	}

#if 0
	if (type == Z_EXPR) {
		printf("freeing %p\n", buf);
	}
#endif

	free_list[type][ free_list_idx[type] ] = buf;
	++free_list_idx[type];
}

static void *
zalloc_from_freelist(int type) {
	struct zone	*z = zones_free[type];

	if (free_list_idx[type] > 0) {
		void	*ret;

		--free_list_idx[type];
		ret = free_list[type][ free_list_idx[type] ];

		memset(ret, 0, z->chunk_size);
#if 0
		if (type == Z_EXPR) {
			/* For debugging some diagnostics */
			struct expr *ex;

			ex = ret;
			printf("returning %p   (constval=%p, used=%d)\n",
					ex, ex->const_value, ex->used);
			if (ex->const_value) {
				printf("      type = %s\n", type_to_text(ex->const_value->type));
				printf("      val as int = %d\n", *(int *)ex->const_value->value);
			}
		}
#endif
		return ret;
	}
	return NULL;
}

void
zalloc_free_expr(struct expr *ex) {
	if (ex->op == 0) {
		/* Must be leaf of tree */
		zalloc_free(ex->data, Z_S_EXPR);
	}
	zalloc_free(ex, Z_EXPR);
}

