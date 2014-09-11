#include <stdio.h>
#include <string.h>

int
main() {
	/*
	 * Make sure a complete storage unit is allocated
	 */
	struct foo {
		int	bf:1;
		unsigned	:0;
	};
	struct bar {
		int	bf:1;
		unsigned	:0;
		char	x;
	};
	struct baz {
		int	bf:1;
		signed	:0;
		char	x;
		short	y;
	};
	struct bleh {
		int	bf:1;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		int	lolz:1;
		char	x;
		short	y;
	};
	struct bleh2 {
		int	x;
		signed	:0;  /* should have no effect */
		int	bf:1;
	};

	/*
	 * Make sure the storage unit holding bf is correctly
	 * considered complete at the end of the struct (i.e.
	 * not padded)
	 */
	struct gnu {
		int	bf:1;
		char	x;
		char	y[2];

		char 	z[4];
	};

	printf("%d\n", (int)sizeof(struct foo));
	printf("%d\n", (int)sizeof(struct bar));
	printf("%d\n", (int)sizeof(struct baz));
	printf("%d\n", (int)sizeof(struct gnu));
	printf("%d\n", (int)sizeof(struct bleh));
	printf("%d\n", (int)sizeof(struct bleh2));
	return 0;
}

