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
		unsigned	x:7;
		unsigned 	y:7;
	} f;
	struct foo2 {
		unsigned	x:7;
		unsigned 	y:7;
	} f2;

	memset(&f, 0, sizeof f);
	f.x = 0x7f;
	printf("%d\n", find_loc(&f, sizeof f));

	memset(&f, 0, sizeof f);
	f.y = 0x7f;
	printf("%d\n", find_loc(&f, sizeof f));

	memset(&f2, 0, sizeof f2);
	f2.y = 0x7f;
	printf("%d\n", find_loc(&f, sizeof f));
}

