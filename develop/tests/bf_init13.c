#include <stdio.h>

int
main() {
	/*
	 * Check whether implicit null initialization of uninitialized
	 * fields works (null padding must correctly be created or the
	 * second struct will not fit)
	 */
	struct bogus {
		unsigned	foo:7;
		unsigned	bar:11;
		unsigned	baz:3;
		int		x;
		/*
		 * Check that initializers are correctly created
		 * for bitfields in NEXT storage unit
		 */
		int		dummy:16;
		int		dummy2:16;
	} b[] = {
		{ 1 },
		{ 1, 2, 3 }
	};
	printf("%u %u %u\n", b[0].foo, b[0].bar, b[0].baz);
	printf("%u %u %u\n", b[1].foo, b[1].bar, b[1].baz);
	return 0;
}

