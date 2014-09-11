/*
 * Copyright (c) 2003 - 2010, Nils R. Weller
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
#ifndef ATTRIBUTE_H
#define ATTRIBUTE_H

/* Function attributes */
#define ATTRF_ALIAS			1
#define ATTRF_ALWAYS_INLINE		2
#define ATTRF_CDECL			3	
#define ATTRF_CONST			4
#define ATTRF_CONSTRUCTOR		5	
#define ATTRF_DEPRECATED		6	
#define ATTRF_DESTRUCTOR		7	
#define ATTRF_DLLIMPORT			8	
#define ATTRF_DLLEXPORT			9	
#define ATTRF_EIGHTBIT_DATA		10
#define ATTRF_FAR			11	
#define ATTRF_FORMAT			12	

#define ATTRF_FORMAT_IGNORED		0
#define ATTRF_FORMAT_PRINTF		1
#define ATTRF_FORMAT_SCANF		2

#define ATTRF_FORMAT_ARG		13	
#define ATTRF_FUNCTION_VECTOR		14	
#define ATTRF_INTERRUPT			15	
#define ATTRF_INTERRUPT_HANDLER		16	
#define ATTRF_LONGCALL			17	
#define ATTRF_MALLOC			18
#define ATTRF_MODEL			19	
#define ATTRF_NAKED			20	
#define ATTRF_NEAR			21	
#define ATTRF_NO_INSTRUMENT_FUNCTION	22	
#define ATTRF_NOINLINE			23	
#define ATTRF_NONNULL			24	
#define ATTRF_NOTHROW			25	
#define ATTRF_PURE			26	
#define ATTRF_REGPARM			27
#define ATTRF_SECTION			28	
#define ATTRF_SHORTCALL			29
#define ATTRF_SIGNAL			30	
#define ATTRF_SP_SWITCH			31	
#define ATTRF_STDCALL			32	
#define ATTRF_TINY_DATA			33	
#define ATTRF_TRAP_EXIT			34	
#define ATTRF_UNUSED			35	
#define ATTRF_USED			36	
#define ATTRF_VISIBILITY		37	
#define ATTRF_WEAK			38	
#define ATTRF_EXCEPTION_HANDLER		39
#define ATTRF_EXTERNALLY_VISIBLE	40
#define ATTRF_FASTCALL			41
#define ATTRF_FLATTEN			42
#define ATTRF_FORCE_ALIGN_ARG_POINTER	43
#define ATTRF_LONG_CALL			44
#define ATTRF_GNU_INLINE		45
#define ATTRF_KSPISUSP			46
#define ATTRF_NESTING			47
#define ATTRF_NMI_HANDLER		48
#define ATTRF_NORETURN			49
#define ATTRF_RETURNS_TWICE		50
#define ATTRF_SENTINEL			51
#define ATTRF_SHORT_CALL		52
#define ATTRF_SSEREGPARAM		53



/* Structure / union / enum attributes */
#define ATTRS_ALIGNED			60
#define ATTRS_DEPRECATED		61
#define ATTRS_MAY_ALIAS			62
#define ATTRS_PACKED			63
#define ATTRS_TRANSPARENT_UNION	64
#define ATTRS_UNUSED			65


/* Variable attributes */
#define ATTRV_ALIGNED			90
#define ATTRV_CLEANUP			91
#define ATTRV_COMMON			92
#define ATTRV_DEPRECATED		93
#define ATTRV_DLLIMPORT			94	
#define ATTRV_DLLEXPORT			95	
#define ATTRV_MODE			96

#define ATTR_MODE_DI			1
#define ATTR_MODE_SI			2

#define ATTRV_MODEL			97
#define ATTRV_NOCOMMON			98
#define ATTRV_PACKED			99
#define ATTRV_SECTION			100	
#define ATTRV_SHARED			101
#define ATTRV_TLS_MODEL			102
#define ATTRV_TRANSPARENT_UNION		103
#define ATTRV_UNUSED			104
#define ATTRV_VECTOR_SIZE		105
#define ATTRV_WEAK			106
#define ATTRV_BOUNDED			107

#define ATTR_ISFUNC(val) \
	((val) >= ATTRF_NOINLINE && (val) <= ATTRF_DLLEXPORT)

#define ATTR_ISSTRUCT(val) \
	((val) >= ATTRS_ALIGNED && (val) <= ATTRS_MAY_ALIAS)

#define ATTR_ISVAR(val) \
	((val) >= ATTRV_ALIGNED && (val) <= ATTRV_DLLEXPORT)

/*
 * Common attribute flags to quickly determine whether popular
 * attributes are set
 */
#define CATTR_WEAK		(1)
#define CATTR_PACKED		(1 << 1)
#define CATTR_ALIGNED		(1 << 2)
#define CATTR_ALIGN		(1 << 3)
#define CATTR_FORMAT		(1 << 4)
#define CATTR_MODE		(1 << 5)
#define CATTR_TRANSPARENT_UNION	(1 << 6)
#define CATTR_SECTION		(1 << 7)
#define CATTR_PURE		(1 << 7)
#define CATTR_USED		(1 << 8)
#define CATTR_UNUSED		(1 << 9)


#define ATTR_FUNC	1
#define ATTR_STRUCT	2
#define ATTR_VAR	3

struct attrib {
	int		type;	/* function, structure or object */
	int		code;	/* actual value of attribute */
	int		is_impl; /* is it implemented? */
	unsigned	fastattrflag;
	char		*parg;	/* pointer argument, if used */
	int		iarg;	/* integer argument, if used */
	int		iarg2;  /* 02/01/10: second integer argument, if used */
	int		iarg3;  /* 02/01/10: third integer argument, if used */
	struct token	*tok;
	struct attrib	*next;	/* next attribute */
};

struct token;
struct type;
struct decl;

extern int	used_attribute_alias;

void 		merge_attr_with_type(struct type *);
void 		merge_attr_with_decl(struct decl *, struct attrib *);
struct attrib	*lookup_attr(struct attrib *, int type);
struct attrib	*dup_attr_list(struct attrib *);
void		append_attribute(struct attrib **head, struct attrib *attr);
struct attrib	*get_attribute(struct type *dest, struct token **tok);
struct token	*ignore_attr(struct token *tok);
struct attrib	*lookup_attr(struct attrib *, int);
void		stupid_append_attr(struct attrib **, struct attrib *);


#endif

