#include <stdio.h>
#include <string.h>

int
main() {
	struct foo {
		char		dummy[3];
		unsigned	absurd:7;  /* across storage unit boundary */
		char		bleh;
	} f;
	f.dummy[0] = 1;
	f.dummy[1] = 2;
	f.dummy[2] = 3;

	f.absurd = 0x7f;
	f.bleh = 6;

	printf("%d %d %d\n", f.dummy[0], f.dummy[1], f.dummy[2]);
	printf("%d\n", f.absurd);
	printf("%d\n", f.bleh);

	{
		printf("locations:\n");

		memset(&f, 0, sizeof f);
		f.absurd = 0x7f;
		printf("%d\n", find_loc(&f, sizeof f));
	}

	return 0;
}

int
find_loc(void *buf, int size) {
        unsigned char   *cp;

        for (cp = buf; cp < (unsigned char *)buf + size; ++cp) {
                printf("%02x ", *cp);
        }
        putchar('\n');
        for (cp = buf; cp < (unsigned char *)buf + size; ++cp) {
                if (*cp != 0) {
                        int     mask;
                        int     i = 0;

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

