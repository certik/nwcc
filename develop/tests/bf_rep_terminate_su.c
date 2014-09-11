#include <stdio.h>
#include <string.h>

int
main() {
	/*
	 * Make sure a complete storage unit is allocated
	 */
	struct foo {
		int	bf:1;
		unsigned	:0;
	};
	struct bar {
		int	bf:1;
		unsigned	:0;
		char	x;
	};
	struct baz {
		int	bf:1;
		signed	:0;
		char	x;
		short	y;
	};
	struct bleh {
		int	bf:1;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		signed	:0;
		int	lolz:1;
		char	x;
		short	y;
	};
	struct bleh2 {
		int	x;
		signed	:0;  /* should have no effect */
		int	bf:1;
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
	printf("%d\n", (int)sizeof(struct bleh));
	printf("%d\n", (int)sizeof(struct bleh2));
	
	{
		struct foo	f;
		struct bar	b;
		struct baz	bz;
		struct bleh	bleh;
		printf("locations:\n");

		memset(&f, 0, sizeof f);
		f.bf = 1;
		printf("%d ", find_loc(&f, sizeof f));

		memset(&b, 0, sizeof b);
		b.bf = 1;
		printf("%d ", find_loc(&b, sizeof b));

		memset(&bz, 0, sizeof bz);
		bz.bf = 1;
		printf("%d\n", find_loc(&bz, sizeof bz));

		memset(&bleh, 0, sizeof bleh);
		bleh.lolz = 1;
		printf("%d\n", find_loc(&bleh, sizeof bleh));
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

