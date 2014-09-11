#include <stdio.h>
#include "t1.h"

/*
 * Receive big struct by value, print it, and assign to it. This is
 * useful on SPARC where the caller is expected to provide a copy
 * of the struct. The original struct must be left unchanged
 */
void
foo(struct bigfoo f) {
	printf("%d\n", (int)sizeof f);
	printf("%d, %s, %d, %ld, %f, %f, %s\n",
		f.x, f.buf, f.y, f.z, f.f, f.d, f.buf2);
	f.x = 0;
	*f.buf = 0;
	f.y = 0;
	f.z = 0;
	f.f = 0.0f;
	f.d = 0.0;
	*f.buf2 = 0;
}

