#include <stdio.h>
#include <string.h>

int
main() {
	/*
	 * Make sure a complete storage unit is allocated
	 */
	struct foo {
		int	bf:1;
	};
	struct bar {
		int	bf:1;
		char	x;
	};
	struct baz {
		int	bf:1;
		char	x;
		short	y;
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
	
	return 0;
}

