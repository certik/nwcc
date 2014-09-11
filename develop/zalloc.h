#ifndef ZALLOC_H
#define ZALLOC_H

#if 0
#define Z_EXPR		1
#define Z_S_EXPR	2
#define Z_FOLD_INFO	3
#define Z_INSTR		4
#define Z_TOKEN		5
#define Z_TY_STRING	6
#define Z_ASMDIR	7
#define Z_TIMES_BUF	8
#define Z_RELOC_DATA	9
#define Z_STATIC_DATA	10
#define Z_OPERAND	11
#endif

#define Z_CONTROL	1
#define Z_LABEL		2
#define Z_EXPR		3
#define Z_INITIALIZER	4
#define Z_STATEMENT	5
#define Z_FUNCTION	6
#define Z_ICODE_INSTR	7
#define Z_ICODE_LIST	8
#define Z_VREG		9
#define Z_STACK_BLOCK	11
#define Z_S_EXPR	12
#define Z_FCALL_DATA	13
#define Z_IDENTIFIER	14 /* unused right now */
#define Z_FASTSYMHASH	15
#define Z_CEXPR_BUF	16

#define Z_MAX_ZONES	17

#include <stddef.h>

void	zalloc_create(void);
void	zalloc_init(int type, int size, int needzero, int usemalloc);
void	*zalloc_buf(int type);
void	zalloc_reset(void);
void	zalloc_reset_except(int initial);

char	*zalloc_identbuf(size_t *nbytes);
void	zalloc_update_identbuf(size_t nbytes);

void	zalloc_enable_malloc_override(void);
void	zalloc_disable_malloc_override(void);

struct expr;

void	zalloc_free(void *buf, int type);
void	zalloc_free_expr(struct expr *ex);

#endif

