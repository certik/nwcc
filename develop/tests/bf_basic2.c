#include <stdio.h>
#include <string.h>

int
main() {
	struct foo {
		char		dummy[3];
		int		gnu:4;
		unsigned	absurd:7;  /* across storage unit boundary */
		char		bleh;
	} f;
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

	return 0;
}

