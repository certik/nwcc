#include <stdio.h>
#include <string.h>

int
main() {
	struct foo {
		char		dummy[3];
		int		gnu:4;
		signed		absurd:17;  /* across storage unit boundary */
		char		bleh;
		unsigned	absurd2:5;
		unsigned	hm:11; /* should continue right after absurd2 */
		char		hmc;
	} f;
	f.dummy[0] = 1;
	f.dummy[1] = 2;
	f.dummy[2] = 3;
	f.gnu = 4;
	f.absurd = 0xffff;
	f.bleh = 6;

	printf("%d %d %d\n", f.dummy[0], f.dummy[1], f.dummy[2]);
	printf("%d\n", f.gnu);
	printf("%d\n", f.absurd);
	printf("%d\n", f.bleh);

	f.absurd2 = 0x1e;
	f.hm = 0x7fe;
	printf("%d\n", f.absurd2);
	printf("%d\n", f.hm);

	return 0;
}

