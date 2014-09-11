#include <stdio.h>
#include <string.h>

int
main() {
	struct foo {
		char		dummy[3];
		int		gnu:4;
		int		absurd:9;  /* across storage unit boundary, but requiring padding */
		char		bleh;
	} f;
	struct foo2 {
		char		dummy[3];
		int		gnu:4;
		unsigned	absurd:9;  /* try the same but without sign-ext */
		char		bleh;
	} f2;
	f.dummy[0] = 1;
	f.dummy[1] = 2;
	f.dummy[2] = 3;
	f.gnu = 4;
	f.absurd = 0x7f;
	f.bleh = 6;

	printf("%d %d %d\n", f.dummy[0], f.dummy[1], f.dummy[2]);
	printf("%d\n", f.gnu);
	printf("%d\n", f.absurd);
	printf("%d\n", f.bleh);

	f2.absurd = 0x1ff;
	printf("%d\n", f2.absurd);

	{
		printf("locations:\n");
		memset(&f, 0, sizeof f);
		f.gnu = 0xf;
		printf("%d ", find_loc(&f, sizeof f));
		memset(&f, 0, sizeof f);
		f.absurd = 0x1ff;
		printf("%d\n", find_loc(&f, sizeof f));

		memset(&f2, 0, sizeof f2);
		f2.gnu = 0xf;
		printf("%d ", find_loc(&f2, sizeof f2));
		memset(&f2, 0, sizeof f2);
		f2.absurd = 0x1ff;
		printf("%d\n", find_loc(&f2, sizeof f2));
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

