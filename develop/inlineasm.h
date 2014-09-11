#ifndef INLINEASM_H
#define INLINEASM_H

struct gas_operand;

struct inline_instr {
	int			type;
#define INLINSTR_REAL	1 /* real instruction */
#define INLINSTR_ASM	2 /* assembler directive */
#define INLINSTR_LABEL	3 /* label */
	char			*name;
	struct gas_operand	*operands[3];
	int			postfix;
	struct inline_instr	*next;
};

struct inline_asm_io {
	char			*constraints;
	int			index;
	/*
	 * It may happen that the same vreg is used for both an input and
	 * an output expression, so we need a second ``struct reg *'' to
	 * record the backing register. input becomes vreg->preg, output
	 * becomes outreg
	 */
	struct reg		*outreg;
	struct reg		*inreg;
	struct expr		*expr;
	struct vreg		*vreg;
	struct inline_asm_io	*next;
};

struct reg;
struct clobbered_reg {
	struct reg		*reg;
	struct clobbered_reg	*next;
};	

struct inline_asm_stmt {
	/*char			*code;*/
	struct inline_instr	*code;
	int			extended;
	struct gas_token	*toklist;
	struct inline_asm_io	*input;
	struct inline_asm_io	*output;
	int			n_inputs;
	int			n_outputs;
	struct clobbered_reg	*clobbered;
};

struct token;
struct gas_token;

struct inline_asm_stmt	*parse_inline_asm(struct token **);
char			*parse_asm_varname(struct token **);

#include <stdio.h>

void
inline_instr_to_nasm(FILE *out, struct inline_instr *);
void
inline_instr_to_gas(FILE *out, struct inline_instr *);

#define TO_GAS 1
#define TO_NASM 2

struct gas_token {
        int                     type;
#define GAS_SEPARATOR	1001
#define GAS_DOTIDENT	1002
#define GAS_IDENT	1003
#define GAS_DOLLAR	1004
#define GAS_STRING	1005
#define GAS_OCTAL	1020
#define GAS_HEXA	1021
#define GAS_DECIMAL	1022
#define GAS_FORWARD_LABEL	1030
#define GAS_BACKWARD_LABEL	1031
#define GAS_NUMBER(x) (x == GAS_OCTAL || x == GAS_HEXA || x == GAS_DECIMAL)
        int                     lineno;
        int                     idata;
        void                    *data;
	void			*data2;
        char                    *ascii;
        struct gas_token        *next;
};


struct gas_operand {
#define ITEM_REG        1
#define ITEM_NUMBER     2
#define ITEM_VARIABLE   3
#define ITEM_OUTPUT	4
#define ITEM_INPUT	5	
#define ITEM_LABEL	6
#define ITEM_SUBREG_B	7
#define ITEM_SUBREG_H	8
#define ITEM_SUBREG_W	9
        int     addr_mode;
#define ADDR_ABSOLUTE   1
#define ADDR_INDIRECT   2
#define ADDR_SCALED     3
#define ADDR_DISPLACE   4
#define ADDR_SCALED_DISPLACE    5
        void    *items[4];
        int     item_types[4];
};

#endif

