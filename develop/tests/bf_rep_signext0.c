#include <stdio.h>
#include <string.h>

int
main() {
	struct foo {
		int	x:1;
		int	y:2;
		int	z:3;
		int	a:4;
		int	b:5;
		int	c:6;
		int	d:14;
		int	e:4;
		int	f:15;
	} f;

	f.x = -1;
	f.y = -1;
	f.z = 0x7;
	f.a = -2;
	f.b = 0x11;
	f.c = 0x3f;
	f.d = -465;
	f.e = 13;
	f.f = -333;
	printf("%d %d %d %d %d %d %d %d %d\n",
		f.x, f.y, f.z, f.a, f.b, f.c, f.d, f.e, f.f);

	{
                printf("locations:\n");

                memset(&f, 0, sizeof f);
                f.x = 1;
                printf("%d\n", find_loc(&f, sizeof f));

                memset(&f, 0, sizeof f);
                f.y = 0x3;
                printf("%d\n", find_loc(&f, sizeof f));

                memset(&f, 0, sizeof f);
                f.z = 0x7;
                printf("%d\n", find_loc(&f, sizeof f));

                memset(&f, 0, sizeof f);
                f.a = 0xf;
                printf("%d\n", find_loc(&f, sizeof f));

                memset(&f, 0, sizeof f);
                f.b = 0x1f;
                printf("%d\n", find_loc(&f, sizeof f));

                memset(&f, 0, sizeof f);
                f.c = 0x3f;
                printf("%d\n", find_loc(&f, sizeof f));

                memset(&f, 0, sizeof f);
                f.d = 0x3fff;
                printf("%d\n", find_loc(&f, sizeof f));

                memset(&f, 0, sizeof f);
                f.e = 0xf;
                printf("%d\n", find_loc(&f, sizeof f));

                memset(&f, 0, sizeof f);
                f.f = 0x7fff;
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

