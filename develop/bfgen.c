#include <stdio.h>
#include <stdlib.h>


static int
randnum(int from, int to) {
	static int	init;
	if (!init) {
		srand((unsigned)time(NULL));
		init = 1;
	}

	return from + (((double)rand() + 1) / RAND_MAX) * (to - from);
}

static const char *
get_rand_type(void) {
	static const char	*types[] = {
		"char", "short", "int", "float", "double", "long long", "void *"
	};
	return types[ randnum(0, sizeof types / sizeof *types) ];
}

int
main(void) {
	int	members;
	int	mixlimit;
	int	bits[128];
	int	i;

	/* Set count of bitfield members */
	members = randnum(1, 12);

	/* Set upper bound for mixed types */
	mixlimit = randnum(1, 7);

	printf("#include <stdio.h>\n");
	printf("#include <string.h>\n");
	printf("static void dump(void *p, size_t count) {\n");
	printf("    unsigned char *ucp = p;\n");
	printf("    size_t i;\n");
	printf("    for (i = 0; i < count; ++i) {\n");
	printf("       printf(\"%%02x\", *ucp++);\n");
	printf("    }\n");
	printf("    putchar('\\n');\n");
	printf(" }\n\n");

	printf("int main(void) {\n");
	printf("    struct foo {\n");
	for (i = 0; i < members; ++i) {
		int	msize = randnum(1, 32);
		char	*type = randnum(0,1) == 0? "unsigned": "int"; /* _Bool? */

		if (randnum(0,mixlimit) == 1) {
			/* Insert "freak" type */
			printf("      %s dummy%d;\n", get_rand_type(), i);
		}

		printf("        %s m%d:%d;\n", type, i, msize); 
		bits[i] = msize; /* Save size for later use */
	}
	printf("    } f;\n");

	/*
	 * For all bitfield members: Check that setting the member does not
	 * affect other member values (they have to remain 0). This
	 * indirectly also checks for incorrect storage layout in cases
	 * where no overlap happens because the output will reveal the bit
	 * pattern
	 */
	for (i = 0; i < members; ++i) {
		unsigned	valmask;
		int		j;

		valmask = 0;
		for (j = 0; j < bits[i]; ++j) {
			valmask <<= 1;
			valmask |= 1;
		}

		printf("    memset(&f, 0, sizeof f);\n");
		printf("    f.m%d = 0x%x;\n", i, valmask);
		printf("    dump(&f, sizeof f);\n");
		printf("    printf(\"%%d\\n\", f.m%d);\n", i); 

	}

	printf("}\n");
}

