#include <stdio.h>

int
main() {
	/*
	 * Case 0: Test if it works at all
	 */
	struct desig0 {
		int	x;
		int	y;
	} d0 = {
		.y = 456,
		.x = 123
	};

	/*
	 * Case 1: Test if the initializer sequeyce is correctly
	 * continued after the last explicit designation
	 */
	struct desig1 {
		int	x;
		int	y;
		int	z;
	} d1 = {
		.y = 456,
		789,
		.x = 123
	};
	/*
	 * Case 2: Test designated aggregate initializers
	 */
	struct desig2 {
		int		x;
		int		y;
		struct desig0	nested;
		int		z;
	} d2 = {
		123, /* x */
		.nested = { 456, 789 },
		1111, /* z */
		.y = 4444
	};

	/*
	 * Case 3: Test nested designated initializers and implicit zero-
	 * initialization
	 */
	struct desig3 {
		int		x;  /* 4 */
		int		y;  /* 8 */
		struct desig0	nested;  /* 16 */
		int		z;  /* 20 */
		int		a;  /* 24 */
	} d3 = {
		.nested = { .y = 555, .x = 123 },
		.z = 123,
		.x = 556
	};

	/*
	 * Case 4: Designated array initializers
	 */
	struct desig4 {
		int	x;
		int	ar[4];
		int	y;
	} d4 = {
		.ar = { [2] = 2, 3, [0] = 4 },
		62,
		.x = 11
	};
	if (d0.x == 123 && d0.y == 456) {
		puts("GOOD");
	} else {
		puts("BUG");
	}	
	if (d1.x == 123 && d1.y == 456 && d1.z == 789) {
		puts("GOOD");
	} else {
		puts("BUG");
	}	
	
	if (d2.x == 123 && d2.y == 4444 && d2.z == 1111 
		&& d2.nested.x == 456 && d2.nested.y == 789) {
		puts("GOOD");
	} else {
		puts("BUG");
	}	

	if (d3.x == 556 && d3.y == 0 && d3.z == 123 && d3.a == 0
		&& d3.nested.y == 555 && d3.nested.x == 123) {
		puts("GOOD");
	} else {
		puts("BAD");
	}	

	if (d4.x == 11 && d4.ar[0] == 4 && d4.ar[1] == 0 && d4.ar[2] == 2 && d4.ar[3] == 3
		&& d4.y == 62) {
		puts("GOOD");
	} else {
		puts("BAD");
	}		
	printf("%d, %d, %d, %d, %d, %d\n",
		d4.x, d4.ar[0], d4.ar[1], d4.ar[2], d4.ar[3], d4.y);


	return 0;
}

