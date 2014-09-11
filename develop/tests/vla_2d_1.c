#include <stdio.h>

int
main() {
	int	i;

	for (i = 1; i < 5; ++i) {
		int	j;
		for (j = 1; j < 10; ++j) {
			char	buf[i][j];
			int	k;
			int	size;

			size = sizeof buf;

			printf("%d,%d\n", size, (int)sizeof *buf);

			/*
			 * Populate with values from 0 to i*j
			 */
			for (k = 0; k < i; ++k) {
				int	l;
				for (l = 0; l < j; ++l) {
					buf[k][l] = k*j + l;
				}
			}

			/*
			 * Test traversal with subscript operator,
			 * printing first and last value of every
			 * stored array
			 */
			for (k = 0; k < i; ++k) {
				printf("%d %d\n",
					buf[k][0], buf[k][j - 1]);
			}

			/*
			 * Test traversal with pointer to array
			 */
			{
				char	(*p)[j];

				k = 0;
				for (p = buf; p != buf+i; ++p) {
					printf("%d %d\n",
						p[k][0], p[k][j - 1]);
				}
			}

		}
	}

	return 0;
}

