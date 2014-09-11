#include <stdio.h>


void
check_alignment(void *addr, unsigned int align) {
	if ((unsigned long)addr % align) {
		puts("BUG");
	} else {
		puts("OK");
	}
}

/*
 * 07/11/13: This test came up on SPARC; Nested scopes yielded problems because
 * alignment wasn't done properly across scope boundaries!
 */
int
main() {
	int	three = 3;
	char	x = 5;
	{
		int	y = 35;
		{
			long z = 666;
			char buf[three];
			long f;
			*buf = 1;
			printf("%d, %d, %ld\n", x, y, z);
			check_alignment(&x, __alignof x);
			check_alignment(&y, __alignof y);
			check_alignment(&z, __alignof z);
			check_alignment(&f, __alignof f);
		}
	}

	{
		static int	sx = 52;
		{
			int	sy = 353;
			{
				static char buf[3];
				static long sz = 35221;
				static long f;
				printf("%d, %d, %ld\n", sx, sy, sz);
				*buf = 1;
				check_alignment(&sx, __alignof sx);
				check_alignment(&sy, __alignof sy);
				check_alignment(&sz, __alignof sz);
				check_alignment(&sz, __alignof f);
			}
		}
	}
	return 0;
}

