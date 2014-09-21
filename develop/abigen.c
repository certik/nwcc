/*
 * July 25 2012:
 * Basic program generator to help track down ABI incompatibilites (originally for AMD64 struct problems)
 *
 * Writes callee and caller to stdout by default, can be made to write caller to file "y.c" instead
 * by passing "--split"
 *
 * This is missing signed types, pointers, enumerations, unions and nested struct types for now and
 * generates no variadic functions. Return values cannot be tested either 
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>


static FILE	*struct_def_file;


static int
getrand(int max) {
	return abs(rand()) % (max + 1);
}

struct type {
	enum etype {
		TY_CHAR,
		TY_SHORT,
		TY_INT,
		TY_LONG,
		TY_LONGLONG,
		TY_FLOAT,
		TY_DOUBLE,
		TY_LDOUBLE,
		TY_STRUCT,
		TY_ENUM
	} type;
	int		struct_idx;
	struct	type	*struct_members_head;
	struct	type	*struct_members_tail;
	struct type	*next;
};

struct function {
	struct type	*args_head;
	struct type	*args_tail;
	int		variadic;
};

static char *
type_to_text(struct type *ty) {
	char	*p;
	static char	buf[128];
	switch (ty->type) {
	case TY_CHAR: p = "char"; break;
	case TY_SHORT: p = "short"; break;
	case TY_INT: p = "int"; break;
	case TY_LONG: p = "long"; break;
	case TY_LONGLONG: p = "long long"; break;
	case TY_FLOAT: p = "float"; break;
	case TY_DOUBLE: p = "double"; break;
	case TY_LDOUBLE: p = "long double"; break;
	case TY_STRUCT: sprintf(buf, "struct s%d", ty->struct_idx); p = buf; break;
	case TY_ENUM: p = "enum";
	default:
		p = "?";
	}
	return p;
}

static char *
type_to_fmtstring(struct type *ty) {
	switch (ty->type) {
	case TY_CHAR:
	case TY_SHORT:
	case TY_INT:
		return "d";
	case TY_LONG:
		return "ld";
	case TY_LONGLONG:
		return "lld";
	case TY_FLOAT:
	case TY_DOUBLE:
		return "f";
	case TY_LDOUBLE:
		return "Lf";
	default:
		return "?";
	}
}


static struct type *
make_type(int disallow_struct) {
	struct type	*ty = malloc(sizeof *ty);
	enum etype	e;
	
	/* XXX disallow nested structs for now */
	do {
		e = (enum etype)getrand(TY_ENUM-1); /* -1 to avoid enum for now */
	} while (disallow_struct && e == TY_STRUCT);

	memset(ty, 0, sizeof *ty);
	ty->type = e;
	if (e == TY_STRUCT) {
		/*
		 * Struct definition - output it as we go
		 */
		int		i;
		int		members;
		static int	struct_index;

		do {
			members = getrand(5);
		} while (members == 0);

		/* Allow for multiple structs, distinguish type by ID */
		ty->struct_idx = struct_index++;

		fprintf(struct_def_file, "struct s%d {\n", ty->struct_idx);

		for (i = 0; i < members; ++i) {
			struct type	*ty2;
			
			/* XXX disallow nested structs for now */
			ty2 = make_type(1); /* XXX memory leak */

			fprintf(struct_def_file, "\t%s m%d;\n", type_to_text(ty2), i);
			if (i == 0) {
				ty->struct_members_head
					= ty->struct_members_tail
						= ty2;
			} else {
				ty->struct_members_tail->next = ty2;
				ty->struct_members_tail = ty2;
			}
		}

		fprintf(struct_def_file, "};\n\n");
	} else {
		ty->struct_members_head = NULL;
		ty->struct_members_tail = NULL;
	}
	ty->next = NULL;
	return ty;
}

int	twofiles = 0;
int	split = 0;
int	nostructs = 0;
int	maxargs = 20;

static struct function
gen_function(void) {
	int		arg_count = getrand(maxargs);
	int		i;
	int		b;
	struct function	ret = { NULL, NULL };

	b = getrand(20);
	if (b % 2 == 0) {
		do {
			ret.variadic = getrand(maxargs);
		} while (ret.variadic == 0);
	} else {
		ret.variadic = -1;
	}

	for (i = 0; i < arg_count; ++i) {
		struct type	*ty;

		ty = make_type(nostructs);
		if (i == 0) {
			ret.args_head = ret.args_tail = ty;
		} else {
			ret.args_tail->next = ty;
			ret.args_tail = ret.args_tail->next;
		}
	}
	return ret;
}

static void
write_proto(struct function *f) {
	struct type	*ty;
	int		index;

	printf("\nvoid f(\n");

	for (ty = f->args_head, index = 0; ty != NULL; ty = ty->next, index++) {
		if (f->variadic != -1 && index == f->variadic) {
			printf("...");
			break;
		}
		printf("%s a%d", type_to_text(ty), index);
		if (ty->next != NULL) {
			printf(", ");
		}
		if ((index+1) % 5 == 0) {
			putchar('\n');
		}
	}

	printf("\n)\n");
}


static void
gen_program(void) {
	struct function	f;
	struct type	*ty;
	int		index;
	int		value = 0;
	struct timeval	tv;

	/* Write includes */
	printf("#include <stdio.h>\n");

	if (struct_def_file != stdout) {
		printf("#include \"h.h\"\n");
	}


	gettimeofday(&tv, NULL);
	srand(tv.tv_usec);
	f = gen_function();

	if (f.variadic != -1) {
		printf("#include <stdarg.h>\n");
	}

	/* Write function definition */
	write_proto(&f);

	printf("{\n");

	if (f.variadic != -1) {
		printf("\tva_list va;\n");
	}

	for (ty = f.args_head, index = 0; ty != NULL; ty = ty->next, index++) {
		if (f.variadic != -1 && f.variadic <= index) {
			if (f.variadic == index) {
				/* Initialize */
				printf("\tva_start(va, a%d);\n", index-1);
			}
			printf("\t{\n");
			printf("\t%s a%d = va_arg(va, ",           //%s);\n",
				type_to_text(ty), index, type_to_text(ty));
			if (ty->type == TY_FLOAT) {
				printf("double);\n");
			} else if (ty->type >= TY_CHAR && ty->type <= TY_INT) {
				printf("int);\n");
			} else {
				printf("%s);\n", type_to_text(ty));
			}
		}
		if (ty->type == TY_STRUCT) {
			struct type	*ty2;
			/* XXX no nested structs allowed for now */
			int		index2;
			for (ty2 = ty->struct_members_head, index2 = 0; ty2 != NULL; ty2 = ty2->next, index2++) {
				printf("\tprintf(\"%%%s\\n\", a%d.m%d);\n",
					type_to_fmtstring(ty2), index, index2);
			}
		} else {
			printf("\tprintf(\"%%%s\\n\", a%d);\n",
				type_to_fmtstring(ty), index);
		}
		if (f.variadic != -1 && f.variadic <= index) {
			printf("\t}\n");
		}
	}
	if (f.variadic != -1) {
		printf("\tva_end(va);\n");
	}
	printf("\n}\n");


	/* Write function invocation */

	if (split) {
		/* This goes into another file */
		if (freopen("y.c", "w", stdout) == NULL) {
			perror("freopen");
			exit(EXIT_FAILURE);
		}
		printf("#include \"h.h\"\n");
		write_proto(&f);
		printf(";\n");
	}
	printf("\nint main() {\n");
	printf("\tf(");

	value = 0;
	for (ty = f.args_head, index = 0; ty != NULL; ty = ty->next, index++) {
		if (ty->type == TY_STRUCT) {
			struct type	*ty2;
			printf("(struct s%d){", ty->struct_idx);
			/* XXX no nested structs allowed for now */
			for (ty2 = ty->struct_members_head; ty2 != NULL; ty2 = ty2->next) {
				printf("(%s)%d", type_to_text(ty2), value++);
				if (ty2->next != NULL) {
					printf(", ");
				}
			}
			printf("}");
		} else {
			printf("(%s)%d", type_to_text(ty), value++);
		}
		if (ty->next != NULL) {
			printf(", ");
		}
	}
	printf(");\n");
	printf("\treturn 0;\n");
	printf("}\n");
}


int
main(int argc, char **argv) {

	if (argc > 1) {
		int	i;
		for (i = 1; i < argc; ++i) {
			if (strcmp(argv[i], "--twofiles") == 0) {
				twofiles = 1;
			} else if (strcmp(argv[i], "--split") == 0) {
				split = 1;
			} else if (strcmp(argv[i], "--nostructs") == 0) {
				nostructs = 1;
			} else if (strcmp(argv[i], "--maxargs") == 0) {
				if (i+1 == argc) {
					puts("error: maxargs expects argument");
				} else {
					maxargs = strtol(argv[i+1], NULL, 10);
					++i;
				}
			} else {
				puts("usage: ./abigen [opt --twofiles]");
				for (i = 0; i < argc; ++i) {
					printf("   %s\n", argv[i]);
				}
				return EXIT_FAILURE;
			}
		}
	}

	if (split) {
		struct_def_file = fopen("h.h", "w");
		if (struct_def_file == NULL) {
			perror("h.h");
			return EXIT_FAILURE;
		}
	} else {
		struct_def_file = stdout;
	}

	gen_program();
	
	return 0;
}

