#include <stdio.h>
#include <string.h>

int
main() {
	struct foo {
		char	dummy[3];
		int	gnu:4;
		int	empty:4;
	} f;
	f.dummy[0] = 1;
	f.dummy[1] = 2;
	f.dummy[2] = 3;

	f.empty = 4;
	f.gnu = 5;
	printf("%d %d %d\n", f.dummy[0], f.dummy[1], f.dummy[2]);
	printf("%d\n", f.empty);
	printf("%d\n", f.gnu);

	{
		struct foo	f;
		printf("locations:\n");

		memset(&f, 0, sizeof f);
		f.gnu = 0xf;
		printf("%d ", find_loc(&f, sizeof f));
		memset(&f, 0, sizeof f);
		f.empty = 0xf;
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

