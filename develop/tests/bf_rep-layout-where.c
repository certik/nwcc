#include <stdio.h>
#include <string.h>

int
find_loc(void *buf, int size) {
	unsigned char	*cp;

	for (cp = buf; cp < (unsigned char *)buf + size; ++cp) {
		printf("%02x ", *cp);
	}
	putchar('\n');
	for (cp = buf; cp < (unsigned char *)buf + size; ++cp) {
		if (*cp != 0) {
			int	mask;
			int	i = 0;

			for (mask = 1; mask <= 0x80; mask <<= 1) {
				if (*cp & mask) {
					break;
				}
				++i;
			}
			return (cp - (unsigned char *)buf) * 8 + i;
		}
	}
	return -1;
}


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
	printf("y %d (expected 8)\n", find_loc(&f, sizeof f));

	memset(&f, 0, sizeof f);
	f.z = 0xff;
	printf("z %d (expected 16)\n", find_loc(&f, sizeof f));

	memset(&f, 0, sizeof f);
	f.a = 1;
	printf("a %d (expected 24)\n", find_loc(&f, sizeof f));


	memset(&f, 0, sizeof f);
	f.b = 1;
	printf("b (atfter gap 1)  %d (expected 26)\n", find_loc(&f, sizeof f));

	memset(&f, 0, sizeof f);
	f.lol = 0xf;
	printf("lol (after gap 3) %d (expected 30)\n", find_loc(&f, sizeof f));

	memset(&f, 0, sizeof f);
	f.lol2 = 0x1f;
	printf("lol2 (after lol) %d (expected 34)\n", find_loc(&f, sizeof f));
}

