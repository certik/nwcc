#include <stdio.h>
#include <string.h>

int
main() {
	struct foo {
		char		dummy[3];
		int		gnu:4;
		int		absurd:9;  /* across storage unit boundary, but requiring padding */
		char		bleh;
	} f;
	struct foo2 {
		char		dummy[3];
		int		gnu:4;
		unsigned	absurd:9;  /* try the same but without sign-ext */
		char		bleh;
	} f2;
	f.dummy[0] = 1;
	f.dummy[1] = 2;
	f.dummy[2] = 3;
	f.gnu = 4;
	f.absurd = 0x7f;
	f.bleh = 6;

	printf("%d %d %d\n", f.dummy[0], f.dummy[1], f.dummy[2]);
	printf("%d\n", f.gnu);
	printf("%d\n", f.absurd);
	printf("%d\n", f.bleh);

	f2.absurd = 0x1ff;
	printf("%d\n", f2.absurd);

	return 0;
}

