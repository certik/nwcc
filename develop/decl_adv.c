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
 * Parsing of ``advanced declarations''.
 * This module exports parse_func/struct/enum() used by the parse_decl()
 * subsystem
 */

#include "decl_adv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "type.h"
#include "backend.h"
#include "token.h"
#include "misc.h"
#include "decl.h"
#include "symlist.h"
#include "error.h"
#include "scope.h"
#include "reg.h"
#include "typemap.h"
#include "expr.h"
#include "fcatalog.h"
#include "attribute.h"
#include "inlineasm.h"
#include "debug.h"
#include "n_libc.h"

/* Parsing framework for structure and function definitions/declarations */

#define STORE_FUNC (1)
#define STORE_STRUCT (2)

/*
 * Stores src argument as structure member(s) of deststr argument
 * The integer pointed to by the ``alloc'' argument must be set to
 * zero every time a new structure is parsed, but not be touched after-
 * wards
 */
static void 
store_member_struct(struct ty_struct *deststr,
                    struct decl      **src) {
	int		nsrc;

	/* Determine number of declarations */
	for (nsrc = 0; src[nsrc] != NULL; ++nsrc)
		;
	deststr->nmemb += nsrc; 
	store_decl_scope(deststr->scope, src);
}


/* Same as store_member_struct(), but for function arguments */
static void
store_member_func(struct ty_func *destfun,
                  struct decl    **src) {
	int		nsrc;

	/* Determine number of declarations */
	for (nsrc = 0; src[nsrc] != NULL; ++nsrc)
		;

	destfun->lastarg = src[nsrc - 1];
	destfun->nargs += nsrc;
	store_decl_scope(curscope, src);
}


static int 
store_member_enum(struct token *etok,
		  struct ty_enum *dest,
                  char *name,
		  struct token *value,
		  struct type *type,
                  int *alloc) {
	struct scope	*scope;
	struct decl	*dec[2];


	/* Resize space if necessary */
	
	if (dest->nmemb >= *alloc) {
		*alloc += 8;
		dest->members = 
			n_xrealloc(dest->members,
				*alloc * sizeof *dest->members);
	}

	dec[0] = alloc_decl();
	dec[0]->dtype = type? type: n_xmemdup(make_basic_type(TY_INT), sizeof *type);
			/*vr->type;*/
	dec[0]->dtype->name = name;
	dec[0]->dtype->tenum = dest;
/*dec[0]->dtype->tenum->value = value;*/
	/*dec[0]->vreg = vr;*/

	/*
	 * 02/25/08: Set alias flag so that we can put the enum declaration
	 * into the scope without allocating any storage
	 */
	dec[0]->is_alias = dec[0];
	dec[1] = NULL;

	/* Store */
	dest->members[ dest->nmemb ].name = name;
	dest->members[ dest->nmemb ].dec = dec[0];
	/*
	 * 03/26/08: New! Don't use dec->vreg anymore
	 */
	
	dec[0]->tenum_value = value;
	/*dest->members[ dest->nmemb ].value = value;*/

	++dest->nmemb;

	/* 
	 * An enumeration constant need only appear in the symbol list,
	 * as it doesn't have to be allocated anywhere
	 */
	for (scope = curscope;
		scope->type == SCOPE_STRUCT;
		scope = scope->parent)
		;
	if (lookup_symbol(scope, dec[0]->dtype->name, 0) != NULL) {
		errorfl(etok, "Redeclaration of `%s'",
			dec[0]->dtype->name);
		return -1;
	}	
	append_symlist(scope, &scope->slist,
		&scope->slist_tail, dec[0]);
#if 0
	store_decl_scope(scope, dec);
#endif
	return 0;
}


/*
 * Function for glueing the member declarations of structures and unions
 * together. This must be called by the declaration parsing code as soon
 * as a structure *definition* (struct foo { ... }) is encountered, where
 * the curtok argument must point to the node after { 
 * Returns a pointer to the newly allocated definition on success, else 
 * a null pointer
 */

#define PS_RET_ERR(ret) do { \
	if (ret && ret->scope) { \
		/*free(ret->scope);*/ \
	} \
	free(ret); \
	close_scope(); \
	return NULL; \
} while (0)


static void
complete_bitfield_storage_unit(struct decl *cur_bf_storage_unit,
	int offset,
	struct sym_entry **bitfields) {

	int			size = offset - cur_bf_storage_unit->offset;
	int			i;
	struct type		*ty;
	static struct expr	dummy;
	static struct tyval	tv;
	int			align = backend->get_align_type(
					cross_get_nearest_integer_type(size, size, 0));

#if  1 
	long			off_diff = cur_bf_storage_unit->offset;

	if (cross_get_target_arch_properties()->endianness == ENDIAN_BIG) {
		cur_bf_storage_unit->offset &= ~(align - 1);
	}
	off_diff -= cur_bf_storage_unit->offset;
#endif

#if 0
printf("unit of %d bytes contains --\n", size);
for (i = 0; bitfields[i] != NULL; ++i) {
	printf("    %s at   %d,%d   [%s]\n", bitfields[i]->dec->dtype->name,
		bitfields[i]->dec->dtype->tbit->byte_offset,
		bitfields[i]->dec->dtype->tbit->bit_offset,
		type_to_text(bitfields[i]->dec->dtype)
		);

}
#endif
	/*
	 * Now that the size of the storage unit is known, we can save
	 * the type, which is always ``char array of N bytes''.
	 *
	 * Note that this need not really be a full ``storage unit''. We
	 * don't take non-bitfield members and padding bytes into account! 
	 * E.g. in
	 *  
	 *   struct foo { char x; int y:24; };
	 *
	 * ... here this storage unit we are recording does NOT
	 * include ``x'' (this is because we want a coherent unit to
	 * write struct initializers for bitfields, and mixing them
	 * up with non-bitfield members would complicate things)
	 */
	tv.type = make_basic_type(TY_INT);
	tv.value = n_xmalloc(16); /* XXX */
	memcpy(tv.value, &size, sizeof size); /* XXX! cross */
	dummy.const_value = &tv;

	ty = dup_type(make_basic_type(TY_UCHAR));
	append_typelist(ty, TN_ARRAY_OF, &dummy, NULL, NULL);
	cur_bf_storage_unit->dtype = ty;

	if (cross_get_target_arch_properties()->endianness == ENDIAN_BIG) {
		for (i = 0; bitfields[i] != NULL; ++i) {

			struct type	*ty = bitfields[i]->dec->dtype;
			int		unit_size = backend->get_sizeof_type(
						cross_get_nearest_integer_type(size, size, 0), NULL);
#if 0
			int		rev_byte_offset = unit_size - (ty->tbit->byte_offset + 1);
			int		rev_bit_offset = 7 - ty->tbit->bit_offset; 
#endif
			struct type	*tempty;

int norm_bit = ty->tbit->byte_offset * 8 + ty->tbit->bit_offset;
int rev_bit = unit_size * 8 - norm_bit;
rev_bit -= ty->tbit->numbits;


			ty->tbit->byte_offset=  rev_bit / 8;
			ty->tbit->bit_offset = rev_bit % 8;


#if 0 
bitfields[i]->dec->offset = cur_bf_storage_unit->offset -
	bitfields[i]->dec->offset & (unit_size - 1); 
	#endif
			ty->tbit->absolute_byte_offset = bitfields[i]->dec->offset + ty->tbit->byte_offset;

			tempty = n_xmemdup(cross_get_nearest_integer_type(size,size,
				ty->sign != TOK_KEY_UNSIGNED), sizeof *tempty);
			tempty->tbit = ty->tbit;
			tempty->name = ty->name;


			bitfields[i]->dec->dtype = tempty;

#if 0
ty->tbit->byte_offset -= off_diff; 
#endif

			cross_calc_bitfield_shiftbits(bitfields[i]->dec);

			/* This must be done after calculations or the shiftbits will be wrong */
bitfields[i]->dec->offset = cur_bf_storage_unit->offset; 

#if 0
printf("   %s at base %lu,   off %lu\n",
	ty->name,
	cur_bf_storage_unit->offset,
	ty->tbit->byte_offset);
#endif

		}
	}
}



/*
 * XXX struct alignment is done in x86_emit_nasm.c, print_init_list() and
 * x86_emit_nasm.c,  do_print_struct() ... there is some duplication of
 * efforts, should probably all be done here
 */
struct ty_struct *
parse_struct(struct token **curtok, char *tag, int type) {
	struct token		*t;
	struct token		*prevtok = NULL;
	struct decl		**dec = NULL;
	struct ty_struct	*ret;
	struct sym_entry	*se;
	unsigned long		offset = 0;
	size_t			maxalign = 0;
	size_t			align;
	int			is_bitfield = 0;
	int			bitfield_offset = 0;
	int			last_bitfield_byte = -1;
	int			last_bitfield_bit = -1;
	struct decl		*cur_bf_storage_unit = NULL;
	struct sym_entry	*be_bf_encountered[128];
	int			bf_count = 0;


	ret = alloc_ty_struct();
	ret->tag = tag;

#ifdef DEBUG2
	printf("Parsing structure definition...\n");
#endif

	ret->scope = new_scope(SCOPE_STRUCT);

	for (t = *curtok; t != NULL; t = t->next) {
		prevtok = t;
		if (t->type == TOK_COMP_CLOSE) {
			/* } reached - definition complete */
			break;
		} else if (!IS_KEYWORD(t->type) && t->type != TOK_IDENTIFIER) {
			errorfl(t, "Syntax error at ``%s'' - "
				"Expected start of structure member definition",
				t->ascii);
			PS_RET_ERR(ret);
		}
		if ((dec = parse_decl(&t, DECL_STRUCT)) == NULL) {
			PS_RET_ERR(ret);
		}


		store_member_struct(ret, dec);

		/*
		 * If any of the structure members is incomplete, the entire
		 * structure is incomplete 
		 * XXX implement incomplete check in decl.c!
		 */
#if 0 /* dtype is not yet filled! */
		if (dec->dtype->incomplete) {
			ret->incomplete = 1;
		}
#endif
	}

	if (ret->scope->slist == NULL) {
		errorfl(t, "Empty structure declaration");
		PS_RET_ERR(ret);
	}

	ret->size = 0;
	for (se = ret->scope->slist; se != NULL; se = se->next) {
		size_t		size = 0;
		struct type	*ty = se->dec->dtype;
		
		if (ty->tlist != NULL) {
			if (ty->tlist->type == TN_ARRAY_OF
#if REMOVE_ARRARG
				&& !ty->tlist->have_array_size) {	
#else
				&& ty->tlist->arrarg->const_value == NULL) {
#endif
				/* C99 flexible array member */

				/*
				 * 09/15/07: Wow this was missing alignment
				 * and member offset assignment... because
				 * we used to ``break'' here
				 */
				if (se->next != NULL) {
					errorfl(NULL /* XXX */,
						"Flexible array not "
						"last member of "
						"structure");
					continue;
				} else {
					ret->flexible = se->dec;
				/*	break;*/
				}
			/*	continue;*/
			}
		}	


		if (ret->flexible != se->dec) {
			size = backend->get_sizeof_decl(se->dec, NULL);

			if (size == 0) {
				errorfl(NULL, "Incomplete type `%s'", ty->name);
				continue;
			}	
		}

		if (se->dec->dtype->tbit != NULL
			&& se->dec->dtype->tbit->numbits == 0) {
			/*
			 * Terminate storage unit 
			 *
			 * Even on AMD64, this is still 4 bytes for this
			 * purpose (hopefully in all cases), since extended
			 * units are only used for long/long long bitfields.
			 * This may be different on MIPS/SPARC/PPC, but
			 * TY_UINT covers x86 and AMD64 for now!
			 */
			int	su_size = backend->get_sizeof_type(
				make_basic_type(/*TY_ULONG*/TY_UINT), NULL);

			if (offset & (su_size - 1) || bitfield_offset > 0) {
				/* Do it */
				do {
					++offset;
				} while (offset & (su_size - 1));
				bitfield_offset = 0;
			}

			if (cur_bf_storage_unit != NULL) {
				be_bf_encountered[bf_count] = NULL;
				complete_bitfield_storage_unit(cur_bf_storage_unit, offset,
					be_bf_encountered);
				cur_bf_storage_unit = NULL;
				bf_count = 0;
			}
		} else if (se->dec->dtype->tbit != NULL && 1) {
			/*
			 * 08/16/08: Bitfield. Combine with other bitfields
			 * if possible
			 */
			int		nbytes;
			struct type	*read_type = NULL;
			int		start_bitfield_offset;
			int		start_offset;
			int		new_bitfield_offset;
			int		extra_bytes = 0;
			int		is_signed = se->dec->dtype->sign !=
						TOK_KEY_UNSIGNED;
			int		raw_size;
			int		orig_offset;
			int		bytes_used;
			int		unit_complete = 0;

			raw_size = se->dec->dtype->tbit->numbits / 8; /* XXX 8 */
			if (raw_size * 8 < se->dec->dtype->tbit->numbits) {
				++raw_size;
			}
	
#if 0
printf("doing bitfield '%s'\n", se->dec->dtype->name);
printf("   we are at %d:%d\n", offset, bitfield_offset);
#endif

			if (is_bitfield) {
				/* Combine with previous bitfield */
				;
			} else {
				/* First bitfield (at this point) */
				is_bitfield = 1;
				bitfield_offset = 0; 
			}

			/*
			 * Now check whether the bitfield can be read in a
			 * single read operation if we place it at the current
			 * byte + possibly bit location
			 */
			nbytes = 1; /* First (possibly partial) byte */


			orig_offset = offset;
			if (bitfield_offset > 0) {
				/*
				 * If this starts a new storage unit, then it cannot start
				 * in a byte, so for that purpose the beginning is the next
				 * byte
				 */
				++orig_offset;
			}
				
			if (se->dec->dtype->tbit->numbits > 8 - bitfield_offset) {
				int		bits_left = 8 - bitfield_offset;
				int		read_type_align;
				int		got_match = 0;
				int		temp;

				bits_left = se->dec->dtype->tbit->numbits -  bits_left;

				/*
				 * Count whole bytes covered (not partial bytes
				 * at start and end)
				 */
				while (bits_left > 8) {
					bits_left -= 8;
					++nbytes;
				}
				if (bits_left > 0) {
					/* There's a partial byte at the end */
					new_bitfield_offset = bits_left;
					++nbytes;
				} else {
					new_bitfield_offset = 0;
				}


				/*
				 * Get smallest type which can be used to read
				 * the bitfield!
				 */
				read_type = cross_get_nearest_integer_type(nbytes,
						/*nbytes*/ raw_size,
						is_signed);
				if (read_type == NULL) {
					struct type	*temp;

					/*
					 * Check whether this failed because the needed
					 * size exceeds the raw size. Example:
					 *
					 *    unsigned x:31;
					 *
					 * ... takes 4 bytes, but may be placed such that
					 * it would occupy 5 bytes. That would require us
					 * to use an 8 byte type. So we check whether
					 * the actual size - 4 bytes in the example - fits,
					 * and if so, pick the corresponding type (and
					 * allow padding)
					 */
					temp = cross_get_nearest_integer_type(raw_size,
							raw_size,
							is_signed);

					if (temp != NULL) {
						/*
						 * OK, this bitfield is possible, but
						 * it needs padding because it passes
						 * some storage unit boundary
						 */
						;
					} else {
						errorfl(t, "Bitfield member too large");
						continue;
					}
				}

				/*
				 * Now that we have an ideal type, check whether
				 * it would be aligned to read from
				 */
				if (read_type != NULL) {
					read_type_align = backend->get_align_type(
							read_type);
				}
				while (!got_match) {
					if ( read_type != NULL
						&& (offset & (read_type_align - 1)) == 0) {
						/* This fits! */
						got_match = 1;
					} else {
						/*
						 * Type is not aligned. We have two
						 * options now;
						 *
						 *    - Pick a larger type which can
						 * go further back
						 *    - Add padding bytes
						 */
						int		tmp_type_align;
						struct type	*tmp_type;
	

						/*
						 * This loop walks BACK from the current offset to see whether
						 *...
						 */
						do {
		
							++extra_bytes;
							tmp_type = cross_get_nearest_integer_type(nbytes+extra_bytes,
									/*nbytes*/ raw_size,
									is_signed);
							if (tmp_type == NULL) {
								/*
								 * Too large - we have
								 * use a smaller type
								 * and pad
								 */
								break;
							}
							tmp_type_align = backend->get_align_type(tmp_type);

							/*
							 * Adjust extra byte counter (e.g. if we requested
							 * 1 extra byte for a 2-byte bitfifeld - i.e. a 3-byte
							 * item - we'll probably get a 4-byte type, i.e. 2
							 * bytes extra
							 */
#if 0
							extra_bytes = backend->get_sizeof_type(tmp_type, NULL)
								- nbytes;
#endif
							if ((unsigned)extra_bytes > offset) {
								/*
								 * We cannot go back
								 * this far because
								 * it would exceed
								 * the struct
								 */
								break;
							} else if ((signed)backend->get_sizeof_type(tmp_type, NULL)
								- extra_bytes < nbytes) {
								/*
								 * We're going so far back with this
								 * storage unit that we cannot possibly
								 * also cover the bitfield bytes that
								 * need to lie ahead of the current
								 * offset
								 */
								continue;
							}
							if ( ((offset - extra_bytes) & (tmp_type_align - 1)) == 0) {
								got_match = 1;
							}
						} while (!got_match);

						if (got_match) {
							read_type = tmp_type;
						} else {
							/*
							 * We have to pad! This also means that, if we're in
							 * the middle of a byte which already has one or more
							 * bitfields, then we cannot use the remaining storage
							 * and have to start a new byte. Thus we have to
							 * adjust the bitfield offset
							 */
							++offset;

							/*
							 * Recalculate bitfield size (maybe giving up on
							 * trying to shove this into a partial storage unit
							 * means we need a byte less)
							 */
							nbytes = se->dec->dtype->tbit->numbits / 8;
							if (nbytes * 8 < se->dec->dtype->tbit->numbits) {
								++nbytes;
							}

							/*
							 * Start bitfield at beginning of new byte
							 */
							bitfield_offset = 0;

							/*
							 * Set bit offset at which the bitfield will end
							 */
							new_bitfield_offset = bitfield_offset %
								se->dec->dtype->tbit->numbits;

							extra_bytes = 0;


							/*
							 * Now that we don't have to fool with bit offsets
							 * and storage units, calculate the first fit (padding
							 * more if necessary)
							 */
							read_type = cross_get_nearest_integer_type(nbytes,
									nbytes,
									is_signed);
							tmp_type_align = backend->get_align_type(read_type);
							while (offset & (tmp_type_align - 1)) {
								++offset;
							}
							got_match = 1;

							unit_complete = 1;
						}
					}
				}

				start_bitfield_offset = bitfield_offset;
				start_offset = offset - extra_bytes;

				temp = se->dec->dtype->tbit->numbits;
				if (start_bitfield_offset != 0) {
					++offset;
					temp -= 8 - start_bitfield_offset;
				}
				new_bitfield_offset = 0;
if (backend->arch == ARCH_X86 || backend->arch == ARCH_AMD64
	|| get_target_endianness() == ENDIAN_LITTLE) { 
	/* Please check whether the stuff below works on x86 too */
	/* 12/23/08: XXX No, it doesn't work! What to do? */
	/* 01/19/08: Disabled for AMD64 too... Why does PPC require this? */
	/* 08/01/09: Disabled for MIPSel (and potentially other little endian
	 * architetures in the future)
	 */
	;
} else {
			if (cur_bf_storage_unit != NULL) {
				/*
				 * 12/10/08: Check whether the current bitfield
				 * fits into the last storage unit, otherwise
				 * complete it and start a new one
				 */
				int	needed_bytes;

				bytes_used = offset - cur_bf_storage_unit->offset;
				needed_bytes = se->dec->dtype->tbit->numbits / 8;
				if (se->dec->dtype->tbit->numbits % 8) {
					++needed_bytes;
				}

				if (needed_bytes+bytes_used > (signed)backend->get_sizeof_type(make_basic_type(TY_ULONG), NULL)
					|| unit_complete) {
					/* Does not fit into current storage unit - complete it */
					be_bf_encountered[bf_count] = NULL;
					complete_bitfield_storage_unit(cur_bf_storage_unit, offset,
						be_bf_encountered);
					cur_bf_storage_unit = NULL;
					bf_count = 0;
				} else {
				}
			}
}
				while (temp > 0) {
					if (temp >= 8) {
						++offset;
						temp -= 8;
					} else {
						new_bitfield_offset = temp;
						break;
					}
				}
			} else {
				/* OK, store just in current byte */
				start_bitfield_offset = bitfield_offset;
				start_offset = offset;
				new_bitfield_offset = bitfield_offset + se->dec->dtype->tbit->numbits;
				if (new_bitfield_offset == 8) {
					++offset;
					new_bitfield_offset = 0;
				}
				read_type = make_basic_type(se->dec->dtype->sign == TOK_KEY_UNSIGNED?
						TY_UCHAR: TY_SCHAR);
			}


			/*
			 * If this is a bitfield which may span multiple bytes,
			 * or just a small bitfield following another small
			 * bitfield, then we have to record two offsets relative
			 * to the base storage unit offset (i.e. the offset which
			 * is set as the struct's member position, and from which
			 * an item is read and possibly masked/shifted):
			 *
			 *    - Byte offset in storage unit
			 *    - Bit offset in first used byte of storage unit
			 */
#if  0 
			printf("   member %s  (%d bits)    at     byte %d, bit %d  (off %d,%d)   "
					"to     byte %d, bit %d   [type %s]\n", 
					se->dec->dtype->name,
					(int)se->dec->dtype->tbit->numbits,
					start_offset, start_bitfield_offset,
					extra_bytes,
					bitfield_offset,
					(int)offset, (int)new_bitfield_offset,
					type_to_text(read_type));
#if 0
			printf("   ... offset within it    %d,       %d\n",
					extra_bytes,
					bitfield_offset);
#endif
#endif



			/*
			 * Set offsets of bitfield in bitfield storage unit
			 */
			se->dec->dtype->tbit->byte_offset = extra_bytes;
			se->dec->dtype->tbit->bit_offset = bitfield_offset;


			se->dec->dtype->tbit->absolute_byte_offset = 
				start_offset + se->dec->dtype->tbit->byte_offset;

			/* Set new byte offset */
			bitfield_offset = new_bitfield_offset;

			/*
			 * Replace requested bitfield type (e.g. unsigned int) with actually
			 * used type
			 */
			read_type = dup_type(read_type);
			read_type->tbit = se->dec->dtype->tbit;
			read_type->name = se->dec->dtype->name;
			/* XXX need to copy any other things from previous type?! */

			se->dec->offset = start_offset;
			se->dec->dtype = read_type;
			se->dec->size = 0; /* reset size for get_sizeof_decl() */

			last_bitfield_byte = offset;
			last_bitfield_bit = bitfield_offset;


			/*
			 * Calculate shift bits needed to decode or encode this bitfield
			 * from the surrounding storage unit
			 * 12/07/08: For big endian this can only be done after the entire
			 * storage unit has been read
			 */
			if (cross_get_target_arch_properties()->endianness != ENDIAN_BIG) {
				cross_calc_bitfield_shiftbits(se->dec);
			}


			if (cur_bf_storage_unit == NULL) {
				/*
				 * Create new storage unit handle for all bitfields
				 * starting with this one
				 */
				cur_bf_storage_unit = alloc_decl();
				cur_bf_storage_unit->offset = orig_offset; //start_offset;
			} else {
				/*
				 * There is a current storage unit
				 */
				;
			}
			se->dec->dtype->tbit->bitfield_storage_unit = cur_bf_storage_unit;
			
			if (cross_get_target_arch_properties()->endianness == ENDIAN_BIG) {
				be_bf_encountered[bf_count++] = se;
			}

			/*
			 * Check whether the current storage unit ends here
			 */


			bytes_used = offset - cur_bf_storage_unit->offset;
			if ((offset & (backend->get_sizeof_type(make_basic_type(TY_ULONG), NULL) - 1)) == 0) {
				if (bitfield_offset == 0) {
					/*
					 * This must definitely be the end because the bitfield hits
					 * the unit boundary
					 */
					be_bf_encountered[bf_count] = NULL;
					complete_bitfield_storage_unit(cur_bf_storage_unit, offset,
						be_bf_encountered);
					cur_bf_storage_unit = NULL;
					bf_count = 0;
				}
			}
		} else {
			/* Not bitfield */
			if (is_bitfield) {
				/*
				 * Finish off previous bitfields
				 */
				if (bitfield_offset > 0) {
					++offset;
					bitfield_offset = 0;
				}

				if (cur_bf_storage_unit != NULL) {
					be_bf_encountered[bf_count] = NULL;
					complete_bitfield_storage_unit(cur_bf_storage_unit, offset,
						be_bf_encountered);
					cur_bf_storage_unit = NULL;
					bf_count = 0;
				}
			}
			is_bitfield = 0;
			if (type == TY_STRUCT) {

				/* struct */
				align = get_struct_align_type(ty);
				if (/*align*/1) {
					/* Align all types > char */ 
					while (offset % align/*size*/) ++offset;
				}
				se->dec->offset = offset;
				offset += size;
			} else {
				/* union */
				size_t	align;

				align = get_struct_align_type(ty);
				if (align > maxalign) {
					maxalign = align;
				}	
				if (size > ret->size) {
					ret->size = size;
				}
			}
		}
	}


	if (bitfield_offset > 0) {
		/* Finish last byte of bitfield */
		++offset;
		bitfield_offset = 0;
	}

	if (cur_bf_storage_unit != NULL) {
		be_bf_encountered[bf_count] = NULL;
		complete_bitfield_storage_unit(cur_bf_storage_unit, offset, be_bf_encountered);
		cur_bf_storage_unit = NULL;
	}

	/*
	 * Complete bitfield storage unit if it's at the end of the struct!
	 *    struct foo { unsigned x:1; };         // takes full 4 bytes unit
	 *    struct foo { unsigned x:1; char y; }; // same as above
	 */
	if (last_bitfield_byte != -1) {
		unsigned long	isize = backend->get_sizeof_type(
					make_basic_type(TY_INT), NULL);
		unsigned long	last_multiple_of_unit = offset & ~(isize - 1);

		/*
		 * Bitfield within current possible storage unit - Complete
		 * it!
		 */
		if ((unsigned)last_bitfield_byte > last_multiple_of_unit
			|| ((unsigned)last_bitfield_byte == last_multiple_of_unit
				&& last_bitfield_bit > 0)) {
			do {
				++offset;
			} while (offset & (isize - 1));
		}
	}


	if (type == TY_UNION) {
		/* XXX this seems completely bogus ... */
		if (/*align*/1) {
			while (ret->size % maxalign) ++ret->size;
		}
	} else {
		/* XXX ?!? */
		ret->size = offset;
		se = ret->scope->slist;
#if 0
		align = backend->get_align_type(se->dec->dtype);
#endif
		{
			static struct type	ty;
			ty.code = TY_STRUCT;
			ty.tstruc =  ret;
			align = backend->get_align_type(&ty);
		}
		while (ret->size % align) {
			++ret->size;
		} 
	}	

	if (t == NULL) {
		errorfl(prevtok, "Unexpected end of file");
		PS_RET_ERR(ret);
	}

#ifdef DEBUG2
	printf("Structure def with %d members parsed successfully!\n",
		ret->nmemb);
#endif
	if (ret->nmemb == 0) {
		if (ret->tag != NULL) {
			errorfl(*curtok,
				"Empty structure declaration `%s'", ret->tag);
		} else {
			errorfl(*curtok, "Empty structure declaration");
		}
	}	
/* XXX ? */ *curtok = t;
	close_scope();
	return ret;
}


struct ty_enum *
parse_enum(struct token **curtok) {
	struct ty_enum	*ret;
	struct token	*t;
	struct token	*prevtok = NULL;
	struct token	*valtok;
	struct expr	*ex;
	char		*name;
	int		op;
	int		val = 0;
	int		alloc;
	int		curval = 0;

	ret = alloc_ty_enum();

	alloc = 0;
#ifdef DEBUG2
	puts("------------------------------------");
#endif
	for (t = *curtok; t != NULL; t = t->next) {
		prevtok = t;
		if (t->type == TOK_COMP_CLOSE) {
			/* End of definition reached */
			break;
		} else if (t->type != TOK_IDENTIFIER) {
			errorfl(t, "Syntax error at `%s' - Expected enum "
				"member name", t->ascii);
			free(ret);
			return NULL;
		}

		/*
		 * Ok, is identifier. Either read constant expression, if
		 * specified, or add one to previous member value
		 */
		name = t->data;

		if (next_token(&t) != 0) {
			free(ret);
			return NULL;
		}

		if (t->type == TOK_COMP_CLOSE) {
			/* } - Definition ends here */
			val = curval;
			valtok = const_from_value(&val, NULL); 
			if (store_member_enum(prevtok, ret, name, valtok,
					NULL, &alloc) != 0) {
#if 0
				errorfl(t, "Multiple definitions of `%s'",
					name);
#endif
			}
			break;
		} else if (t->type == TOK_OPERATOR) {
			/* Must be ``,'' or ``='' */
			op = *(int *)t->data;
			if (op == TOK_OP_ASSIGN) {
				if (next_token(&t) != 0) {
					return NULL;
				}
				ex = parse_expr(&t, TOK_OP_COMMA,
					TOK_COMP_CLOSE, EXPR_CONST, 1);
				if (ex == NULL) {
#ifndef NO_EXPR
					return NULL;
#endif
				}

				if (t->type == TOK_COMP_CLOSE) {
					t = t->prev;
				}	

				/* Now update current value counter */
				curval = (int)cross_to_host_size_t(
					ex->const_value);

				/*
				 * 12/19/07: This was broken in that it used
				 * const_from_value improperly, and it gave
				 * the enum constant the type of the assigned
				 * value, whereas we usually want "int", or
				 * at least a an otherwise consistent type
				 * per enum type
				 */
				valtok = const_from_value(&curval, NULL);
#if 0
				valtok = const_from_value(
						ex->const_value->value,
						ex->const_value->type);
#endif
				if (store_member_enum(prevtok, ret, name, valtok,
					NULL, &alloc) != 0) {
#if 0
					errorfl(t, "Multiple definitions of "
						"`%s'", name);
#endif
				}

				++curval;
			} else if (op == TOK_OP_COMMA) {
				/*
				 * No initializer given, so this gets the value
				 * of the previous member + 1. Note that C99
				 * permits a ``dangling'' comma at the end of
				 * an enum def., as in ``enum foo { FOO, BAR,
				 * };'', so we must also check whether the
				 * definition ends here
				 */
				val = curval++;
				valtok = const_from_value(&val, NULL);
				if (store_member_enum(prevtok, ret, name, valtok,
					NULL, &alloc) != 0) {
#if 0
					errorfl(t, "Multiple definitions of "
						"`%s'", name);
#endif
				}
				if (t->next
					&& t->next->type == TOK_COMP_CLOSE) {
					/* Definition ends */
					t = t->next;
					break;
				}
			}
		} else {
			errorfl(t,
				"Syntax error at `%s' - "
				"Expected enum initializer, comma or }",
				t->ascii);
			free(ret);
			return NULL;
		}
	}
#ifdef DEBUG2
	puts("------------------------------------");
#endif

	if (t == NULL) {
		errorfl(prevtok, "Unexpected end of file");
		free(ret);
		return NULL;
	}
	*curtok = t;
	return ret;
}


struct ntab {
	struct nentry {
		char		*name;
		int		refcount;
		int		line;
		struct decl	*dec;
	}	*nametab;
	int	alloc;
	int	index;
};

/*
 * Delete structure members or function arguments of dest, if existent, then
 * delete dest. Intended to use for early escape from syntax errors in struct
 * and func parsing
 */

static void
drop_stuff(struct ty_func *dest, struct ntab *n) {
#if 0
	if (dest->args != NULL) {
		/* Is function */
		free(dest->args);
		dest->args = NULL;
	}
#endif
	if (dest->scope != NULL) {
		/* free(dest->scope); */
	}
	free(dest);
	if (n != NULL && n->nametab != NULL) {
		free(n->nametab);
		n->nametab = NULL;
	}
}


	
/*
 * Stores pointer ``p'' in the name entry table pointed to by ``dest''.
 * The table must be created with all members set to zero, e.g. by
 * writing: struct ntab n = { 0, 0, 0 };
 * Returns 1 if a string equivalent to the one to by ``p'' already
 * resides in the table (p is not stored if this happens), else 0 
 * n.nametab[n.index].name will always be a null pointer
 */
static int 
store_ident(struct ntab *dest, char *name, int line) {
	int	i;

	if (dest->index >= (dest->alloc - 1)) {
		dest->alloc += 8;
		dest->nametab =
			n_xrealloc(dest->nametab,
				dest->alloc * sizeof *dest->nametab);
	}

	/* Make sure we have no multiple definitions */
	for (i = 0; i < dest->index; ++i) {
		if (strcmp(dest->nametab[i].name, name) == 0) {
			/* Already exists - no need to save */
			return 1;
		}
	}
	dest->nametab[ dest->index ].name = name;
	dest->nametab[ dest->index ].line = line;
	dest->nametab[ dest->index ].refcount = 0;
	dest->nametab[ ++dest->index ].name = NULL;
	return 0;
}

/*
 * Looks up string pointed to by ``name'' in name table pointed to by
 * ``n''. Returns a pointer to that on success, else NULL.
 */
static struct nentry *
lookup_ident(struct ntab *n, char *name) {
	int	i;

	for (i = 0; n->nametab[i].name != NULL; ++i) {
		if (strcmp(n->nametab[i].name, name) == 0) {
			++n->nametab[i].refcount;
			return &n->nametab[i];
		}
	}
	return NULL;
}


/*
 * Helper macro for parse_func() - Close scope only if it is a function
 * prototype scope, i.e. not a definition. If we are dealing with a
 * definition (prototype followed by a ``{''), this is block scope and
 * should not be closed
 */

void CLOSE_SCOPE(struct token *tok) { do { \
	if (tok) { \
		struct token	*_t2; \
		if ((_t2 = ignore_attr(tok)) != NULL) { \
			if (_t2->type != TOK_COMP_OPEN) { \
				/* Is declaration */ \
				close_scope(); \
			} \
		} \
	} \
} while (0)
; }

static void 
check_matches(struct token *t, struct ntab *n, struct decl **dec) {
	int		i;
	struct nentry	*np;

	for (i = 0; dec[i] != NULL; ++i) {
		if ((np = lookup_ident(n, dec[i]->dtype->name)) == NULL) {
			errorfl(t,
				"Declaration for non-existent parameter `%s'",
				dec[i]->dtype->name);
		} else if (np->refcount > 1) {
			errorfl(t,
				"Multiple declarations for parameter `%s'",
				dec[i]->dtype->name);
		} else {
			np->dec = dec[i];
		}
	}
}



int
get_kr_func_param_decls(struct ty_func *ret, struct token **tok, struct ntab *n) {
	struct token	*t = tok? *tok: NULL;
	struct decl	**dec;
	struct nentry	*np;
	int		i;

	for (; t != NULL; t = t->next) {
		if (t->type == TOK_COMP_OPEN) {
			/*
			 * Opening compound statement -
			 * end reached
			 */
			t = t->prev;
			break;
		} else if (IS_TYPE(t)) {
			dec = parse_decl(&t, DECL_FUNCARG_KR);
			if (dec == NULL) {
				return -1;
			}
			if (t && t->type != TOK_SEMICOLON) {
				errorfl(t,
				"Syntax error at `%s' - "
				"Expected semicolon", t->ascii);
				return -1;
			} else if (t == NULL) {
				break;
			}

			check_matches(t, n, dec);
#if 0
					store_member_func(ret, dec);
#endif
		} else {
			/*
			 * 07/17/08: Token for ellipsis
			 */
			if (t->type == TOK_ELLIPSIS) {
				continue;
			}

			/* Garbage */
			errorfl(t, "Syntax error at `%s' - "
				"Expected parameter "
				"declaration", t->ascii);
			return -1;
		}
	}

	/*
	 * If this point is reached, either the declaration
	 * list was valid, or the end-of-file has been
	 * encountered prematurely. Both cases are handled by
	 * the code below...
	 * We must check whether all parameters enclosed by
	 * parentheses have been resolved by declarations!
	 */
	if ((np = n->nametab) != NULL) {
		for (i = 0; n->nametab[i].name != NULL; ++i) {
			struct decl	*dummy[2];

			if (n->nametab[i].refcount < 1) {
				/* Undeclared - assume int */
				n->nametab[i].dec = alloc_decl();
				n->nametab[i].dec->dtype =
					n_xmemdup(
					make_basic_type(TY_INT),
					sizeof(struct type));
				n->nametab[i].dec->dtype->name =
					n->nametab[i].name;
					
			}
			dummy[0] = n->nametab[i].dec;
			dummy[1] = NULL;
			store_member_func(ret, dummy);
		}
	}

	if (tok != NULL) {
		*tok = t;
	}	
	return 0;
}




static int
do_parse_kr_func(struct token **tok, struct ty_func *ret) {
	struct token	*t;
	int		errors = 0;
	char		**names = NULL;
	struct ntab	n = { 0, 0, 0 };

	ret->type = FDTYPE_KR;
	(void) names;

	for (t = *tok; t != NULL; t = t->next) {
		if (t->type == TOK_PAREN_CLOSE) {
			/*
			 * End of parenthesized part of declaration reached -
			 * read parameter declarations now!
			 */
#if 0 
			if (next_token(&t) != 0) {
				return -1;
			}

			get_kr_func_param_decls(ret, &t, &n);
#endif
			break;
		} else {
			/*
			 * This must be an identifier. Everything else is
			 * illegal!
			 */
			if (t->type != TOK_IDENTIFIER) {
				errorfl(t, "Syntax error at `%s' - " 
					"Expected identifier", t->ascii);
				break;
			}

			if (store_ident(&n, t->data, t->line) == 1) {
				errorfl(t,
					"Multiple definitions of `%s'",
					t->ascii);
				++errors;
			}

			if (next_token(&t) != 0) {
				return -1;
			}

			if (t->type == TOK_OPERATOR) {
				if (*(int *)t->data != TOK_OP_COMMA) {
	   				errorfl(t, "Syntax error at `%s' - "
						"expected comma or closing "
						"parentheses", t->ascii);
				} else {
					/*
					 * 01/26/08: Don't allow trailing comma
					 */
					if (t->next != NULL
						&& t->next->type
						== TOK_PAREN_CLOSE) {
						errorfl(t, "Trailing comma in "
							"parameter list");		
						break;
					}
					continue;
				}
			} else if (t->type != TOK_PAREN_CLOSE) {
		   		errorfl(t, "Syntax error at `%s' - "
					"expected comma or closing parentheses",
					t->ascii);
			} else {
				t = t->prev;
				continue;
			}
			return -1;
		}
	}
	if (errors) {
		return -1;
	}

	/*
	 * 04/11/08: Save name list if there are any paramters.
	 * We need this to read the parameter type list later on -
	 * This cannot be done here already because the may be other
	 * intermediate declarator stuff! E.g.
	 *
	 *     char (*foo(x))[128]
	 *                 ^ we are here, can't get to ``int x'' yet
	 *          int	x;  
	 *	{
	 */
	if (n.index > 0) {
		ret->ntab = n_xmemdup(&n, sizeof n);
	} else {
		ret->ntab = NULL;
	}

	*tok = t;
	return 0;
}

static int
do_parse_iso_func(struct token **tok, struct ty_func *ret) {
	struct token	*t;
	struct decl	**dec;

	ret->type = FDTYPE_ISO;
	t = *tok;
	for (t = *tok; t != NULL; t = t->next) {
		if (t->type == TOK_PAREN_CLOSE) {

			if (t->next == NULL) {
				break;
			}

			if (t->next->type != TOK_COMP_OPEN) {
				if (ret->nargs == 0) {
					/*
					 * This is a declaration and no
					 * arguments are given; -1 indicates
					 * an unknown amount of arguments
					 */
					ret->nargs = -1;
				}
				if (t->next->type == TOK_KEY_ASM) {
					t = t->next;
					ret->asmname
						= parse_asm_varname(&t);
					if (ret->asmname == NULL) {
						return -1;
					}
					t = t->prev;
				}	
			} else {
				/*
				 * 02/21/09: For function DEFINITIONS, we also
				 * want to allow calls with arguments because
				 * those are allowed. We have to warn for this
				 * case by checking for ``is_fdef && nargs == -1''
				 * instead
				 */
				if (ret->nargs == 0) {
					ret->nargs = -1;
				}
			}

			break;
		} else if (IS_TYPE(t)
			|| (doing_fcatalog
				&& t->type == TOK_IDENTIFIER
				&& fcat_get_dummy_typedef(t->data))) {
#ifdef DEBUG2
			struct	token	*tmp;
#endif

			/* int, unsigned, static, etc or typedef */
			if (ret->variadic) {
				errorfl(t, "`...' must be last argument");
			}
#ifdef DEBUG2
			tmp = t;
#endif

			if ((dec = parse_decl(&t, DECL_FUNCARG)) == NULL) {
				return -1;
			}

#ifdef DEBUG2
			printf("parsed declaration:\n");
			puts("--------------");
			while (tmp != t) {
				printf("%s\n", tmp->ascii);
				tmp = tmp->next;
			}
			puts("--------------");
#endif

			if (t && (t->type != TOK_OPERATOR || 
				*(int *)t->data != TOK_OP_COMMA)) {
				if (t->type != TOK_PAREN_CLOSE) { 
					errorfl(t,
						"Syntax error at `%s' - "
						"expected comma", t->ascii);
					return -1;
				} else {
					t = t->prev;
				}
			} else if (t && *(int *)t->data == TOK_OP_COMMA) {
				/*
				 * 01/26/08: Don't allow trailing comma
				 */
				if (t->next != NULL
					&& t->next->type ==
					TOK_PAREN_CLOSE) {
					errorfl(t, "Trailing comma in "
						"parameter list");		
					break;
				}	
			}
			store_member_func(ret, dec);
		} else {
			/* Garbage or variadic function */
#if 0
			if (t->type == TOK_OPERATOR) {
				int	i = 0;
#endif
			/*
			 * 07/17/08: Token for ellipsis
			 */
			if (t->type == TOK_ELLIPSIS) {
				if (ret->variadic) {
					errorfl(t, "Duplicate use of "
						"`...'");
				}
				ret->variadic = 1;
				if (ret->nargs == 0) {
					errorfl(t, "At least one "
						"argument before `...'"
						"required");
				}
				t = t->next;
				if (t && 
					(t->type != TOK_OPERATOR || 
					*(int *)t->data
						!= TOK_OP_COMMA)) {
					if (t->type
						!= TOK_PAREN_CLOSE) {
						errorfl(t, "Syntax "
							"error at `%s'"
							"- expected "
							"comma",
							t->ascii);
						return -1;
					} else {
						t = t->prev;
					}
				}
				continue;
			}
			errorfl(t,
"Syntax error at `%s' - Expected closing " 
"parentheses or start of parameter declaration",
			t->ascii);
			return -1;
		}
	}

	*tok = t;
	return 0;
}


void
put_func_name(const char *name) {
	size_t			size = strlen(name) + 1;
	struct decl		*d = alloc_decl();
	struct initializer	*init = alloc_initializer();
	struct ty_string	*ts;
	struct expr		*ex = alloc_expr();
	struct decl		*decls[4];

	ts = make_ty_string(name, size);
	ex->const_value = n_xmalloc(sizeof *ex->const_value);
	memset(ex->const_value, 0, sizeof *ex->const_value);
	ex->const_value->str = ts;
	ex->const_value->type = ts->ty;
#if REMOVE_ARRARG
	ts->ty->tlist->arrarg_const = backend->get_sizeof_type(ts->ty, NULL);
	ts->ty->tlist->have_array_size = 1;
#else
	ts->ty->tlist->arrarg = ex;
#endif
	init->type = INIT_EXPR;

#if XLATE_IMMEDIATELY
	init->data = dup_expr(ex);
#else
	init->data = ex;
#endif
	ts->ty->storage = TOK_KEY_STATIC;
	d->dtype = ts->ty;
	ts->ty->name = "__func__";
	
	/*
	 * 03/26/08: Set function name property so we can distinguish
	 * between this and other initialized variables
	 */
	ts->ty->flags |= FLAGS_FUNCNAME;

	d->init = init;
	decls[0] = d;
	/* XXXXXXXXXXXXXXXXXXX use alloc_decl() */
	decls[1] = n_xmemdup(d, sizeof *d);
	decls[1]->dtype = n_xmemdup(decls[1]->dtype, sizeof *decls[1]->dtype);
	decls[1]->dtype->name = "__PRETTY_FUNCTION__";

	/* 09/30/07: BitchX uses __FUNCTION__ */
	decls[2] = n_xmemdup(d, sizeof *d);
	decls[2]->dtype = n_xmemdup(decls[1]->dtype, sizeof *decls[1]->dtype);
	decls[2]->dtype->name = "__FUNCTION__";

	decls[3] = NULL;
	store_decl_scope(curscope, decls);
	decls[1]->dtype->name = decls[0]->dtype->name;
	decls[2]->dtype->name = decls[0]->dtype->name;
#if 0
	decls[1]->is_alias = 1;
#endif
	/*
	 * 070501: Setting a boolean alias flag is not correct. Instead we
	 * must have a pointer to the base declaration, so that access_
	 * symbol() can reference it. And then only define that declaration
	 * in the backend (currently only PowerPC seems to make use of
	 * is_alias at all
	 */
	decls[1]->is_alias = decls[0];
	decls[2]->is_alias = decls[0];
}

/*
 * Must be called with a curtok pointing to the beginning of the function
 * argument list
 * void f(foo)
 *       ^
 * 09/30/07: Now we pass the declaration type to avoid breakage when a
 * cast is read;
 * 
 *    (void(*)())expr;
 *
 * ... here the (kludged) search for the identifier breaks. Maybe we
 * should pass the name instead of searching it here
 */
struct ty_func *
parse_func(struct token **curtok, const char *name) {
	struct token	*t = NULL;
	struct token	*prevtok = NULL;
	struct type	*td = NULL;
	struct ty_func	*ret;
	struct ntab	n = { 0, 0, 0 };

	(void) name;
  
	ret = alloc_ty_func();

	if (next_token(curtok) != 0) {
		free(ret);
		return NULL;
	}
	t = *curtok;
	ret->scope = new_scope(SCOPE_CODE);

	if (t->type == TOK_KEY_VOID) {
		if (t->next != NULL) {
			if (t->next->type == TOK_PAREN_CLOSE) {
#ifdef DEBUG2
				printf("Function takes no arguments\n");
#endif
				*curtok = t->next;
				if (t->next->next
					&& t->next->next->type
						 == TOK_COMP_OPEN) {	
#if 0
					/* 04/09/08: Moved to parse_decl() */
					siv_checkpoint = siv_tail;
					put_func_name(name);
#endif
					CLOSE_SCOPE(t->next->next);
					return ret;
				}
				t = t->next;
#if 0
				CLOSE_SCOPE(t->next->next);
				return ret;
#endif
			}
		} else {
			errorfl(t, "Premature end of file");
			free(ret);
			return NULL;
		}
	}

	/* 
	 * There are two possible definition types:
	 * ISO style or K&R style, i.e.
	 * void f(char *foo, char bar, int x) {
	 * or
	 * void f(foo, bar, x)
	 *     char *foo, bar;
	 *     int   x; {
	 * K&R C has no prototypes, so every paramter declaration
	 * indicates a function definition. Thus, it can be assumed
	 * that the parameter declarations end when the ``{'' token
	 * is encountered.
	 *
	 * If the first node of the token list at this point is an
	 * identifier, we simply assume a K&R declaration, else ISO 
	 */
	if (t->type == TOK_IDENTIFIER
		&& (td = lookup_typedef(curscope, t->data, 1, 0)) == NULL
		&& !doing_fcatalog) {
		/* Is K&R declaration */
		if (do_parse_kr_func(&t, ret) != 0) {
			drop_stuff(ret, &n);
			recover(curtok, TOK_PAREN_CLOSE, 0);
			ret = NULL;
		}
	} else {
		/* Is ISO declaration */
		if (do_parse_iso_func(&t, ret) != 0) {
			drop_stuff(ret, &n);
			recover(curtok, TOK_PAREN_CLOSE, 0);
			ret = NULL;
		}
	}

	if (t == NULL) {
		errorfl(prevtok, "Unexpected end of file");
		drop_stuff(ret, ret->type == FDTYPE_KR?
				(void *)&n : (void *)NULL);
		return NULL;
	}


	/* 04/09/08: Moved to parse_decl() */
#if 0
	if (t->next && t->next->type == TOK_COMP_OPEN) {
		siv_checkpoint = siv_tail;
		put_func_name(name);
	}
#endif

	*curtok = t;

	/*
	 * 04/11/08: Now we always close the function prototype scope to
	 * make it more deterministic! The default assumption is thus
	 * that we are dealing with a mere function declaration.
	 *
	 * If this is a function definition, the caller just sets
	 * curscope to this scope again (ty->tfunc->scope)
	 */
	close_scope();
	return ret;
}

