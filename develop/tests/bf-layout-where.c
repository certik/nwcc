#include <stdio.h>
#include <string.h>

int
main() {
	struct foo {
		char		x;
		/* First BF storage byte */
		unsigned	y:1;
		char		z;

		/* Second BF storage byte */
		unsigned	a:1;
		unsigned	:1;
		unsigned	b:1;
		unsigned	:3;

		/* At this point we have 1 bit left in the second BF
		 * storage byte. Check whether a large item will
		 * span this and the next unit, or whether it begins
		 * in the next one
		 * (It should begin at the next byte and not span
		 * both!)
		 */
		unsigned	lol:4;

		/*
		 * Since lol should begin on a new byte and is 4
		 * bits large, lol2 should also get its own byte
		 * because 4+5 doesn't fit into 8 bits
		 */
		unsigned	lol2:5;
	} f;

	memset(&f, 0, sizeof f);
	f.y = 1;
	printf("%d\n", f.y);

	memset(&f, 0, sizeof f);
	f.z = 0xff;
	printf("%d\n", f.z);

	memset(&f, 0, sizeof f);
	f.a = 1;
	printf("%d\n", f.a);

	memset(&f, 0, sizeof f);
	f.b = 1;
	printf("%d\n", f.b);

	memset(&f, 0, sizeof f);
	f.lol = 0xf;
	printf("%d\n", f.lol);

	memset(&f, 0, sizeof f);
	f.lol2 = 0x1f;
	printf("%d\n", f.lol2);
}

