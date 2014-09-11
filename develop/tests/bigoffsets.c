#include <stdio.h>

/*
 * 11/02/07: This test should have been written many months
 * or a year or more ago already!
 *
 * The problem is that RISC targets (SPARC, MIPS, PowerPC)
 * have very limited encoding capacities for offsets to
 * memory operands, such as stack offsets or pointer and
 * array displacements.
 *
 * On SPARC this stuff was handled from the beginning because
 * its TINY maximum offsets (12 bits) were immediately obvious.
 * However with PowerPC and MIPS the problem didn't become
 * visible so early, so it must be fixed just now. 
 *
 * BEWARE: This may cause temp register conflicts when a
 * seemingly simple offset operation needs one! A second
 * temp gpr used solely for offseting may be in order (on
 * SPARC there are also two temp gprs)
 */
int
main() {
	int	x;
	char	buf[1000000];
	int	y;
	char	*p;

	x = 123;
	y = x * 2; 
	buf[sizeof buf - 1] = 0xf;
	printf("%d, %d, %d\n", x, y, buf[sizeof buf - 1]);

	/*
	 * Some stress testing to expose potential register
	 * conflicts (multiple loads use the same temp gpr)
	 *
	 */
	if (x == y - x) {
		puts("good");
	} else {
		puts("bug");
	}
	p = buf;
	buf[500000] = 0xff;
	printf("%d\n", p[500000]);
	return 0;
}

